#!/usr/bin/env python
"""
Servo Control UI - 6-DOF STS3215 bus servo control via scservo_sdk.

Six horizontal sliders command the target position of servos 1..6 on a
Feetech STS3215 bus (USB serial, e.g. /dev/ttyACM0). Each row shows the
commanded position (centered label), read-back position, velocity and link
status. Sliders are disabled while the bus is not connected and while an
individual servo is not responding.

Architecture: a background thread (ServoBusWorker) owns the serial port and
the SDK. The GUI thread never touches the bus: slider movements are queued
and coalesced by the worker (rate-limited writes), and feedback is pushed to
the GUI through a thread-safe queue drained by an `after()` loop. The UI
stays responsive even when a servo is missing or the bus misbehaves.

Usage:
    STServo_Python/stservo-env/bin/python STServo_Python/servo_control_ui.py
"""

from __future__ import annotations

import os
import queue
import sys
import threading
import time

# The scservo_sdk package lives in the virtualenv root; make it importable
# regardless of the current working directory.
sys.path.insert(
    0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "stservo-env")
)

import tkinter as tk
from tkinter import ttk

from scservo_sdk import (
    GroupSyncRead,
    PortHandler,
    SMS_STS_PRESENT_POSITION_L,
    SMS_STS_PRESENT_SPEED_L,
    sms_sts,
)

DEFAULT_PORT = "/dev/ttyACM0"
BAUDRATE = 1000000
SERVO_IDS = (1, 2, 3, 4, 5, 6)
POSITION_MIN = 0
POSITION_MAX = 4095
MOVE_SPEED = 2400
MOVE_ACC = 50

POLL_INTERVAL_MS = 30      # feedback readback cadence (~33 Hz)
WRITE_INTERVAL_MS = 20     # minimum gap between bus writes (~50 Hz max)
DRAIN_INTERVAL_MS = 15     # GUI queue drain cadence

COLOR_CONNECTED = "#166534"
COLOR_DISCONNECTED = "#b91c1c"


class ServoRow:
    """One horizontal slider row: Min entry | slider (label above) | Max entry | feedback."""

    def __init__(self, parent, root, servo_id, row, on_write):
        self.servo_id = servo_id
        self.on_write = on_write
        self._root = root
        self._min_value = POSITION_MIN
        self._max_value = POSITION_MAX

        self.frame = ttk.LabelFrame(parent, text="Servo %d" % servo_id, padding=(8, 4))
        self.frame.grid(row=row, column=0, sticky="ew", padx=6, pady=4)
        self.frame.columnconfigure(1, weight=1)

        # Left: minimum value input.
        ttk.Label(self.frame, text="Min").grid(row=0, column=0, sticky="w")
        self.min_var = tk.StringVar(value=str(POSITION_MIN))
        self.min_entry = ttk.Entry(self.frame, textvariable=self.min_var, width=6)
        self.min_entry.grid(row=1, column=0, sticky="ns", padx=(0, 8))
        self.min_entry.bind("<Return>", lambda e: self.apply_limits())
        self.min_entry.bind("<FocusOut>", lambda e: self.apply_limits())

        # Center: current position label over the slider.
        slider_area = ttk.Frame(self.frame)
        slider_area.grid(row=0, column=1, rowspan=2, sticky="ew", padx=4)
        slider_area.columnconfigure(0, weight=1)

        self.current_label = ttk.Label(
            slider_area, text="-", anchor="center", font=("TkDefaultFont", 10, "bold")
        )
        self.current_label.grid(row=0, column=0, sticky="ew")

        self.slider = ttk.Scale(
            slider_area,
            from_=POSITION_MIN,
            to=POSITION_MAX,
            orient="horizontal",
            command=self._on_slider,
        )
        self.slider.grid(row=1, column=0, sticky="ew")
        self.slider.set(POSITION_MIN)

        # Right: maximum value input.
        ttk.Label(self.frame, text="Max").grid(row=0, column=2, sticky="w")
        self.max_var = tk.StringVar(value=str(POSITION_MAX))
        self.max_entry = ttk.Entry(self.frame, textvariable=self.max_var, width=6)
        self.max_entry.grid(row=1, column=2, sticky="ns", padx=(8, 0))
        self.max_entry.bind("<Return>", lambda e: self.apply_limits())
        self.max_entry.bind("<FocusOut>", lambda e: self.apply_limits())

        # Far right: read-back position, velocity and link status.
        self.feedback_label = ttk.Label(
            self.frame, text="Disconnected", foreground=COLOR_DISCONNECTED
        )
        self.feedback_label.grid(row=0, column=3, rowspan=2, sticky="e", padx=(12, 0))

    def _on_slider(self, value_str):
        try:
            value = int(float(value_str))
        except (TypeError, ValueError):
            return
        self.current_label.config(text=str(value))
        # Queue the command; the worker coalesces and rate-limits writes, so
        # spamming the callback during a drag is cheap and safe.
        self.on_write(self.servo_id, value)

    def apply_limits(self):
        """Parse Min/Max entries, validate and re-range the slider."""
        try:
            lo = int(self.min_var.get())
            hi = int(self.max_var.get())
        except ValueError:
            self.min_var.set(str(self._min_value))
            self.max_var.set(str(self._max_value))
            return

        lo = max(POSITION_MIN, min(POSITION_MAX, lo))
        hi = max(POSITION_MIN, min(POSITION_MAX, hi))
        if lo >= hi:
            self.min_var.set(str(self._min_value))
            self.max_var.set(str(self._max_value))
            return

        self._min_value, self._max_value = lo, hi
        self.slider.configure(from_=lo, to=hi)

        current = int(float(self.slider.get()))
        if current < lo:
            self.slider.set(lo)
        elif current > hi:
            self.slider.set(hi)

    def set_feedback(self, position, speed, connected):
        """Update read-back labels and slider availability for this servo."""
        if connected:
            self.feedback_label.config(
                text="Pos: %d  Vel: %d  Connected" % (position, speed),
                foreground=COLOR_CONNECTED,
            )
            self.slider.state(["!disabled"])
        else:
            self.feedback_label.config(
                text="Pos: -  Vel: -  Disconnected", foreground=COLOR_DISCONNECTED
            )
            self.slider.state(["disabled"])


class ServoBusWorker(threading.Thread):
    """Owns the serial port and the SDK. Never touches Tk widgets.

    Commands arrive on an internal queue (coalesced per servo), writes are
    rate-limited, and feedback is emitted as plain tuples into `feedback_q`
    for the GUI thread to drain:
        ("status", "connected")
        ("status", "error", message)
        ("status", "lost", message)
        ("feedback", [(servo_id, position, speed, connected), ...])
    """

    def __init__(self, port_name, feedback_q):
        super().__init__(daemon=True)
        self._port_name = port_name
        self._feedback_q = feedback_q
        self._cmd_q = queue.Queue()
        self._pending = {}
        self._shutdown = threading.Event()

    # ------------------------------------------------------------- API (GUI thread)

    def write(self, servo_id, position):
        if not self._shutdown.is_set():
            self._cmd_q.put((servo_id, position))

    def stop(self):
        self._shutdown.set()

    # ------------------------------------------------------------- thread body

    def run(self):
        port = PortHandler(self._port_name)
        try:
            if not port.openPort():
                raise OSError("could not open %s" % self._port_name)
            if not port.setBaudRate(BAUDRATE):
                raise OSError("unsupported baudrate %d" % BAUDRATE)
            ph = sms_sts(port)
            group = GroupSyncRead(ph, SMS_STS_PRESENT_POSITION_L, 4)
            for servo_id in SERVO_IDS:
                group.addParam(servo_id)
        except Exception as exc:  # noqa: BLE001 - report and exit the thread
            self._feedback_q.put(("status", "error", str(exc)))
            try:
                port.closePort()
            except Exception:  # noqa: BLE001
                pass
            return

        self._feedback_q.put(("status", "connected"))

        last_write = 0.0
        next_poll = 0.0
        lost = None
        try:
            while not self._shutdown.is_set():
                now = time.monotonic()

                # Drain queued commands, keeping only the newest value per servo.
                try:
                    while True:
                        servo_id, position = self._cmd_q.get_nowait()
                        self._pending[servo_id] = position
                except queue.Empty:
                    pass

                # Rate-limited writes.
                if self._pending and (now - last_write) * 1000.0 >= WRITE_INTERVAL_MS:
                    for servo_id, position in self._pending.items():
                        try:
                            ph.WritePosEx(servo_id, position, MOVE_SPEED, MOVE_ACC)
                        except Exception:  # noqa: BLE001 - let the poll detect it
                            pass
                    self._pending.clear()
                    last_write = now

                # Feedback poll.
                if now >= next_poll:
                    self._poll_once(group, ph)
                    next_poll = now + POLL_INTERVAL_MS / 1000.0

                time.sleep(0.005)
        except Exception as exc:  # noqa: BLE001 - bus died hard
            lost = str(exc)
        finally:
            try:
                port.closePort()
            except Exception:  # noqa: BLE001
                pass

        if lost is not None:
            self._feedback_q.put(("status", "lost", lost))
        elif self._shutdown.is_set():
            self._feedback_q.put(("status", "stopped"))

    def _poll_once(self, group, ph):
        try:
            group.txRxPacket()
        except Exception as exc:  # noqa: BLE001 - propagate to main loop
            raise
        feedback = []
        for servo_id in SERVO_IDS:
            available, _error = group.isAvailable(servo_id, SMS_STS_PRESENT_POSITION_L, 2)
            if available:
                position = group.getData(servo_id, SMS_STS_PRESENT_POSITION_L, 2)
                speed_raw = group.getData(servo_id, SMS_STS_PRESENT_SPEED_L, 2)
                speed = ph.scs_tohost(speed_raw, 15)
                feedback.append((servo_id, position, speed, True))
            else:
                feedback.append((servo_id, 0, 0, False))
        self._feedback_q.put(("feedback", feedback))


class ServoControlApp:
    def __init__(self, root):
        self._root = root
        self._worker = None
        self._feedback_q = queue.Queue()
        self._connected = False
        self._drain_job = None

        root.title("STS3215 Servo Control")
        root.geometry("900x560")
        root.columnconfigure(0, weight=1)
        root.rowconfigure(1, weight=1)

        # Device / connection bar.
        top = ttk.Frame(root, padding=(8, 8, 8, 4))
        top.grid(row=0, column=0, sticky="ew")
        top.columnconfigure(4, weight=1)

        ttk.Label(top, text="Device:").grid(row=0, column=0, sticky="w")
        self.port_var = tk.StringVar(value=DEFAULT_PORT)
        self.port_entry = ttk.Entry(top, textvariable=self.port_var, width=24)
        self.port_entry.grid(row=0, column=1, sticky="w", padx=6)

        self.connect_btn = ttk.Button(top, text="Connect", command=self._connect)
        self.connect_btn.grid(row=0, column=2, padx=4)

        self.disconnect_btn = ttk.Button(
            top, text="Disconnect", command=self._disconnect, state="disabled"
        )
        self.disconnect_btn.grid(row=0, column=3, padx=4)

        self.status_label = ttk.Label(top, text="Disconnected", foreground=COLOR_DISCONNECTED)
        self.status_label.grid(row=0, column=4, sticky="e", padx=(12, 0))

        # Servo rows.
        body = ttk.Frame(root, padding=(8, 4, 8, 8))
        body.grid(row=1, column=0, sticky="nsew")
        body.columnconfigure(0, weight=1)

        self.rows = [
            ServoRow(body, root, servo_id, i, self._queue_write)
            for i, servo_id in enumerate(SERVO_IDS)
        ]
        self._row_by_id = {row.servo_id: row for row in self.rows}

        root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ------------------------------------------------------------------ connection

    def _connect(self):
        if self._connected:
            return

        port_name = self.port_var.get().strip() or DEFAULT_PORT
        self._feedback_q = queue.Queue()
        self._worker = ServoBusWorker(port_name, self._feedback_q)
        self._worker.start()

        self._connected = True
        self.connect_btn.state(["disabled"])
        self.disconnect_btn.state(["!disabled"])
        self.port_entry.state(["disabled"])
        self._set_status("Connecting...", True)
        self._drain_job = self._root.after(DRAIN_INTERVAL_MS, self._drain)

    def _disconnect(self):
        if not self._connected and self._worker is None:
            return
        self._connected = False

        if self._drain_job is not None:
            self._root.after_cancel(self._drain_job)
            self._drain_job = None

        if self._worker is not None:
            self._worker.stop()
            self._worker.join(timeout=1.0)
            self._worker = None

        self.connect_btn.state(["!disabled"])
        self.disconnect_btn.state(["disabled"])
        self.port_entry.state(["!disabled"])
        for row in self.rows:
            row.set_feedback(None, None, False)
        self._set_status("Disconnected", False)

    def _abort_connection(self, message):
        """The worker reported an error or lost the bus: tear down cleanly."""
        was_connected = self._connected
        self._connected = False

        if self._drain_job is not None:
            self._root.after_cancel(self._drain_job)
            self._drain_job = None

        if self._worker is not None:
            self._worker.stop()
            self._worker.join(timeout=1.0)
            self._worker = None

        self.connect_btn.state(["!disabled"])
        self.disconnect_btn.state(["disabled"])
        self.port_entry.state(["!disabled"])
        for row in self.rows:
            row.set_feedback(None, None, False)
        self._set_status(message, was_connected)

    # ------------------------------------------------------------------ feedback drain

    def _drain(self):
        try:
            while True:
                msg = self._feedback_q.get_nowait()
                kind = msg[0]
                if kind == "status":
                    state = msg[1]
                    if state == "connected":
                        self._set_status("Connected", True)
                    elif state == "error":
                        self._abort_connection("Connect failed: %s" % msg[2])
                        return
                    elif state == "lost":
                        self._abort_connection("Lost connection: %s" % msg[2])
                        return
                    elif state == "stopped":
                        # Worker exited on a user disconnect; nothing to show.
                        pass
                elif kind == "feedback":
                    for servo_id, position, speed, connected in msg[1]:
                        row = self._row_by_id[servo_id]
                        if connected:
                            row.set_feedback(position, speed, True)
                        else:
                            row.set_feedback(None, None, False)
        except queue.Empty:
            pass

        if self._connected:
            self._drain_job = self._root.after(DRAIN_INTERVAL_MS, self._drain)

    # ------------------------------------------------------------------ commands

    def _queue_write(self, servo_id, position):
        if self._worker is not None:
            self._worker.write(servo_id, position)

    # ------------------------------------------------------------------ ui helpers

    def _set_status(self, text, connected):
        self.status_label.config(
            text=text,
            foreground=COLOR_CONNECTED if connected else COLOR_DISCONNECTED,
        )

    def _on_close(self):
        self._disconnect()
        self._root.destroy()


def main():
    root = tk.Tk()
    ServoControlApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
