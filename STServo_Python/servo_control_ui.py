#!/usr/bin/env python
"""
Servo Control UI - 6-DOF STS3215 bus servo control via scservo_sdk.

Six horizontal sliders command the target position of servos 1..6 on a
Feetech STS3215 bus (USB serial, e.g. /dev/ttyACM0), displayed 6..1 from
top to bottom so the layout mirrors the physical arm (gripper on top, base
on the bottom row). Each slider moves
between 0 and 100 percent of the TOTAL calibrated range of its servo
(loaded from calibration.json, written by calibrate-arm.py): 0 maps to
range_min, 100 maps to range_max, linearly in encoder counts. Each row
shows the commanded % (and the mapped encoder position it sends), the
read-back position as counts + %, velocity and link status. Sliders are
disabled while the bus is not connected and while an individual servo is
not responding. When a servo (re)connects, its slider snaps to the servo's
actual read-back position. A Home button in the toolbar sends every servo
back to 50% of its calibrated range, the calibrated zero pose. Record saves
the current read-back pose (counts per servo, keyed by joint name) to
pose.json; Apply commands the servos back to that saved pose, mirroring it
on the sliders. If calibration.json is missing, the full raw 0-4095 range
is used.

Architecture: a background thread (ServoBusWorker) owns the serial port and
the SDK. The GUI thread never touches the bus: slider movements are queued
and coalesced by the worker (rate-limited writes), and feedback is pushed to
the GUI through a thread-safe queue drained by an `after()` loop. The UI
stays responsive even when a servo is missing or the bus misbehaves.

Usage:
    STServo_Python/stservo-env/bin/python STServo_Python/servo_control_ui.py
"""

from __future__ import annotations

import json
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
SERVO_NAMES = {
    1: "shoulder_pan",
    2: "shoulder_lift",
    3: "elbow_flex",
    4: "wrist_flex",
    5: "wrist_roll",
    6: "gripper",
}
POSITION_MIN = 0
POSITION_MAX = 4095
CALIBRATION_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "calibration.json"
)
POSE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pose.json")
MOVE_SPEED = 300
MOVE_ACC = 10
# Movement profile units (STS3215 datasheet): speed in steps/s (max 3400 ~=
# 50 RPM no-load), acceleration in units of 100 steps/s^2, and 4096 steps =
# one revolution. Defaults are deliberately slow and gentle: 300 step/s ~=
# 26 deg/s with a soft 1000 steps/s^2 ramp (~88 deg/s^2).
SPEED_MIN = 50
SPEED_MAX = 3400
ACC_MIN = 1
ACC_MAX = 50
STEPS_PER_REV = 4096

POLL_INTERVAL_MS = 30      # feedback readback cadence (~33 Hz)
WRITE_INTERVAL_MS = 20     # minimum gap between bus writes (~50 Hz max)
DRAIN_INTERVAL_MS = 15     # GUI queue drain cadence

COLOR_CONNECTED = "#166534"
COLOR_DISCONNECTED = "#b91c1c"


def _load_calibrated_ranges():
    """Load {servo_id: (range_min, range_max)} from calibration.json.

    Entries that are missing, malformed or out of order fall back to the full
    raw 0-4095 range for that servo. Returns {} when the file is absent or
    unreadable, so the UI falls back to the raw range everywhere.
    """
    try:
        with open(CALIBRATION_PATH, "r", encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        return {}

    ranges = {}
    for item in data.values():
        servo_id = item.get("id")
        rmin = item.get("range_min")
        rmax = item.get("range_max")
        if (
            isinstance(servo_id, int)
            and isinstance(rmin, int)
            and isinstance(rmax, int)
            and POSITION_MIN <= rmin < rmax <= POSITION_MAX
        ):
            ranges[servo_id] = (rmin, rmax)
    return ranges


class ServoRow:
    """One slider row: commanded % (and mapped counts) | slider | feedback."""

    def __init__(self, parent, root, servo_id, row, range_min, range_max, on_write):
        self.servo_id = servo_id
        self.on_write = on_write
        self._root = root
        self._range_min = range_min
        self._range_max = range_max
        # Snap the slider to the actual read-back position on (re)connect.
        self._sync_pending = True
        # Suppresses the slider command callback while we move the knob
        # programmatically (ttk.Scale.set() does fire the -command here).
        self._syncing = False
        # Last read-back position in counts; None while disconnected.
        self._last_position = None

        name = SERVO_NAMES.get(servo_id, "")
        title = "Servo %d  (%s)" % (servo_id, name) if name else "Servo %d" % servo_id
        self.frame = ttk.LabelFrame(parent, text=title, padding=(8, 4))
        self.frame.grid(row=row, column=0, sticky="ew", padx=6, pady=4)
        self.frame.columnconfigure(0, weight=1)

        # Center: commanded value label over the slider.
        self.current_label = ttk.Label(
            self.frame, text="-", anchor="center", font=("TkDefaultFont", 10, "bold")
        )
        self.current_label.grid(row=0, column=0, sticky="ew")

        # Slider moves 0..100 percent of the servo's calibrated total range:
        # 0 maps to range_min, 100 maps to range_max, linearly in counts.
        self.slider = ttk.Scale(
            self.frame,
            from_=0,
            to=100,
            orient="horizontal",
            command=self._on_slider,
        )
        self.slider.grid(row=1, column=0, sticky="ew")
        self.slider.set(0)

        # Right: read-back position (counts + %), velocity and link status.
        self.feedback_label = ttk.Label(
            self.frame, text="Disconnected", foreground=COLOR_DISCONNECTED
        )
        self.feedback_label.grid(row=0, column=1, rowspan=2, sticky="e", padx=(12, 0))

    def percent_to_counts(self, percent):
        return round(
            self._range_min + percent * (self._range_max - self._range_min) / 100.0
        )

    def counts_to_percent(self, counts):
        span = self._range_max - self._range_min
        if span <= 0:
            return 0
        return max(0, min(100, round((counts - self._range_min) * 100.0 / span)))

    def _on_slider(self, value_str):
        try:
            percent = int(float(value_str))
        except (TypeError, ValueError):
            return
        percent = max(0, min(100, percent))
        counts = self.percent_to_counts(percent)
        self.current_label.config(text="%d%%  (%d)" % (percent, counts))
        if self._syncing:
            return
        # Queue the command; the worker coalesces and rate-limits writes, so
        # spamming the callback during a drag is cheap and safe.
        self.on_write(self.servo_id, counts)

    def home(self):
        """Command this servo back to the calibrated zero pose (50%)."""
        self.command_counts(self.percent_to_counts(50))

    def command_counts(self, counts):
        """Command an absolute encoder position, mirroring it on the slider."""
        counts = max(POSITION_MIN, min(POSITION_MAX, counts))
        percent = self.counts_to_percent(counts)
        self._syncing = True
        try:
            self.slider.set(percent)
        finally:
            self._syncing = False
        self.current_label.config(text="%d%%  (%d)" % (percent, counts))
        self.on_write(self.servo_id, counts)

    def record_position(self):
        """Return the last read-back position in counts, or None if unknown."""
        return self._last_position

    def set_feedback(self, position, speed, connected):
        """Update read-back labels and slider availability for this servo."""
        if connected:
            self._last_position = position
            percent = self.counts_to_percent(position)
            self.feedback_label.config(
                text="Pos: %d (%d%%)  Vel: %d  Connected"
                % (position, percent, speed),
                foreground=COLOR_CONNECTED,
            )
            self.slider.state(["!disabled"])
            # On the first feedback after (re)connect, snap the slider to the
            # servo's actual pose without queueing a write.
            if self._sync_pending:
                self._syncing = True
                try:
                    self.slider.set(percent)
                finally:
                    self._syncing = False
                self.current_label.config(text="%d%%  (%d)" % (percent, position))
                self._sync_pending = False
        else:
            self._last_position = None
            self._sync_pending = True
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
        self._params_lock = threading.Lock()
        self._move_speed = MOVE_SPEED
        self._move_acc = MOVE_ACC

    # ------------------------------------------------------------- API (GUI thread)

    def write(self, servo_id, position):
        if not self._shutdown.is_set():
            self._cmd_q.put((servo_id, position))

    def stop(self):
        self._shutdown.set()

    def set_move_params(self, speed, acc):
        """Update the speed/accel applied to every position write."""
        with self._params_lock:
            self._move_speed = speed
            self._move_acc = acc

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
                    with self._params_lock:
                        speed = self._move_speed
                        acc = self._move_acc
                    for servo_id, position in self._pending.items():
                        try:
                            ph.WritePosEx(servo_id, position, speed, acc)
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
        self._status_token = 0

        root.title("STS3215 Servo Control")
        root.geometry("900x560")
        root.columnconfigure(0, weight=1)
        root.rowconfigure(2, weight=1)

        self._calibrated = _load_calibrated_ranges()
        if self._calibrated:
            root.title("STS3215 Servo Control (calibrated ranges)")
        else:
            root.title("STS3215 Servo Control (raw 0-4095)")

        # Device / connection bar.
        top = ttk.Frame(root, padding=(8, 8, 8, 4))
        top.grid(row=0, column=0, sticky="ew")
        top.columnconfigure(7, weight=1)

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

        self.home_btn = ttk.Button(
            top, text="Home (50%)", command=self._home_all, state="disabled"
        )
        self.home_btn.grid(row=0, column=4, padx=4)

        self.record_btn = ttk.Button(
            top, text="Record", command=self._record_pose, state="disabled"
        )
        self.record_btn.grid(row=0, column=5, padx=4)

        self.apply_btn = ttk.Button(
            top, text="Apply", command=self._apply_pose, state="disabled"
        )
        self.apply_btn.grid(row=0, column=6, padx=4)

        self.status_label = ttk.Label(top, text="Disconnected", foreground=COLOR_DISCONNECTED)
        self.status_label.grid(row=0, column=7, sticky="e", padx=(12, 0))

        # Movement parameters, live-tunable: applied to every bus write.
        params = ttk.Frame(root, padding=(8, 0, 8, 4))
        params.grid(row=1, column=0, sticky="ew")
        params.columnconfigure(5, weight=1)

        ttk.Label(params, text="Speed:").grid(row=0, column=0, sticky="w")
        self.speed_var = tk.IntVar(value=MOVE_SPEED)
        self.speed_spin = ttk.Spinbox(
            params,
            from_=SPEED_MIN,
            to=SPEED_MAX,
            increment=50,
            textvariable=self.speed_var,
            width=6,
            command=self._apply_move_params,
        )
        self.speed_spin.grid(row=0, column=1, sticky="w", padx=6)
        self.speed_hint_label = ttk.Label(
            params,
            text="%.0f°/s" % (MOVE_SPEED * 360.0 / STEPS_PER_REV),
            foreground="#6b7280",
        )
        self.speed_hint_label.grid(row=0, column=2, sticky="w", padx=(0, 16))
        self.speed_spin.bind("<Return>", lambda e: self._apply_move_params())
        self.speed_spin.bind("<FocusOut>", lambda e: self._apply_move_params())

        ttk.Label(params, text="Accel:").grid(row=0, column=3, sticky="w")
        self.acc_var = tk.IntVar(value=MOVE_ACC)
        self.acc_spin = ttk.Spinbox(
            params,
            from_=ACC_MIN,
            to=ACC_MAX,
            increment=1,
            textvariable=self.acc_var,
            width=6,
            command=self._apply_move_params,
        )
        self.acc_spin.grid(row=0, column=4, sticky="w", padx=6)
        ttk.Label(params, text="x100 step/s^2").grid(row=0, column=5, sticky="w")
        self.acc_spin.bind("<Return>", lambda e: self._apply_move_params())
        self.acc_spin.bind("<FocusOut>", lambda e: self._apply_move_params())

        # Servo rows.
        body = ttk.Frame(root, padding=(8, 4, 8, 8))
        body.grid(row=2, column=0, sticky="nsew")
        body.columnconfigure(0, weight=1)

        self.rows = []
        # Display order is reversed (6..1, top to bottom) so the layout
        # mirrors the physical arm: gripper at the top row, base at the bottom.
        for i, servo_id in enumerate(reversed(SERVO_IDS)):
            rmin, rmax = self._calibrated.get(servo_id, (POSITION_MIN, POSITION_MAX))
            self.rows.append(
                ServoRow(body, root, servo_id, i, rmin, rmax, self._queue_write)
            )
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
        self.home_btn.state(["!disabled"])
        self.record_btn.state(["!disabled"])
        self.apply_btn.state(["!disabled"])
        self.port_entry.state(["disabled"])
        self._apply_move_params()
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
        self.home_btn.state(["disabled"])
        self.record_btn.state(["disabled"])
        self.apply_btn.state(["disabled"])
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
        self.home_btn.state(["disabled"])
        self.record_btn.state(["disabled"])
        self.apply_btn.state(["disabled"])
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

    def _apply_move_params(self):
        """Push the Speed/Accel spinbox values to the worker (clamped)."""
        try:
            speed = int(self.speed_var.get())
            acc = int(self.acc_var.get())
        except (ValueError, tk.TclError):
            return
        speed = max(SPEED_MIN, min(SPEED_MAX, speed))
        acc = max(ACC_MIN, min(ACC_MAX, acc))
        self.speed_var.set(speed)
        self.acc_var.set(acc)
        self.speed_hint_label.config(
            text="%.0f°/s" % (speed * 360.0 / STEPS_PER_REV)
        )
        if self._worker is not None:
            self._worker.set_move_params(speed, acc)

    def _home_all(self):
        """Send every servo back to the calibrated zero pose (50%)."""
        if not self._connected or self._worker is None:
            return
        for row in self.rows:
            row.home()

    def _record_pose(self):
        """Save the current read-back position of every servo to pose.json."""
        if not self._connected or self._worker is None:
            return
        pose = {}
        for row in self.rows:
            pos = row.record_position()
            if pos is not None:
                name = SERVO_NAMES.get(row.servo_id, "servo_%d" % row.servo_id)
                pose[name] = pos
        if not pose:
            self._flash_status("Record: no servo position available", False)
            return
        payload = {
            "recorded_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "servos": pose,
        }
        try:
            with open(POSE_PATH, "w", encoding="utf-8") as fh:
                json.dump(payload, fh, indent=2)
        except OSError as exc:
            self._flash_status("Record failed: %s" % exc, False)
            return
        self._flash_status("Pose recorded: %d servos -> %s" % (len(pose), POSE_PATH), True)

    def _apply_pose(self):
        """Command the servos to the positions saved in pose.json."""
        if not self._connected or self._worker is None:
            return
        try:
            with open(POSE_PATH, "r", encoding="utf-8") as fh:
                data = json.load(fh)
        except (OSError, ValueError) as exc:
            self._flash_status("Apply: no valid pose file (%s)" % exc, False)
            return
        servos = data.get("servos") if isinstance(data, dict) else None
        if not isinstance(servos, dict):
            self._flash_status("Apply: pose file has no 'servos' object", False)
            return
        applied = 0
        for row in self.rows:
            name = SERVO_NAMES.get(row.servo_id)
            if name is None:
                continue
            counts = servos.get(name)
            if not isinstance(counts, int):
                continue
            row.command_counts(counts)
            applied += 1
        if applied:
            self._flash_status("Applied pose: %d servos -> %s" % (applied, POSE_PATH), True)
        else:
            self._flash_status("Apply: no matching servos in pose file", False)

    # ------------------------------------------------------------------ ui helpers

    def _set_status(self, text, connected):
        self.status_label.config(
            text=text,
            foreground=COLOR_CONNECTED if connected else COLOR_DISCONNECTED,
        )

    def _flash_status(self, text, ok):
        """Show a transient status message, restoring 'Connected' after 3s."""
        self._status_token += 1
        token = self._status_token
        self.status_label.config(
            text=text,
            foreground=COLOR_CONNECTED if ok else COLOR_DISCONNECTED,
        )
        if self._connected:
            self._root.after(3000, lambda: self._restore_status(token))

    def _restore_status(self, token):
        if token == self._status_token and self._connected:
            self._set_status("Connected", True)

    def _on_close(self):
        self._disconnect()
        self._root.destroy()


def main():
    root = tk.Tk()
    ServoControlApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
