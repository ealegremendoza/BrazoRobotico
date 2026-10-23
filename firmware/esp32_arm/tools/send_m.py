#!/usr/bin/env python3
"""Send M (motors) frames to the ESP32 like the ros2_control plugin's write().

Frame format (doc/md/bitacora-R09-ros2-control.md):
    STX | LEN | CID | FS | payload | ETX | LRC

Usage:
    send_m.py [--port /dev/ttyACM0] [--count N] [--period-ms MS] [--boot-wait-ms MS]
              [--countdown S] [--reply-wait-ms MS] [--read-only] j1 j2 j3 j4 j5 j6

Joint values are integers in milliradians (e.g. 1571 -1571 0 0 0 0).
The ESP32 logs what it decoded on its console (UART1, GPIO14) and answers each M
with an M feedback frame (present joint angles), which this script decodes and prints.

Opening the port resets the ESP32 (Linux asserts DTR/RTS on open, before pyserial
can clear them; on ESP32 dev boards those lines drive EN/GPIO0). Frames sent while
it boots (~300 ms until app_main) are lost, so the script waits --boot-wait-ms first.

Every M frame moves the servos: a --countdown (default 3 s) runs before anything is
sent, to get a hand on the power supply switch. Use --countdown 0 to skip it.

After the last frame it keeps listening --reply-wait-ms (default 200) so late
replies are not lost.

The ESP32 boots STOPPED: M frames only read the servos until it gets an S (start).
The script sends S before the M frames, unless --read-only is given (then the arm
never moves and the replies show where it is). The S goes without param_init, so the
ESP32 keeps its default joint limits; it answers it with an M (the pose, accepted) or
an E with a code (rejected, e.g. 01 when the local panel is in CTL mode).
"""
import argparse
import time

import serial

STX = 0x02
ETX = 0x03
FS = 0x1C
BAUD = 115200
MRAD_MAX = 3142
# E codes (doc/md/bitacora-R03-arquitectura.md)
E_CODES = {
    "01": "S rejected: the local panel is in CTL mode",
    "20": "the servos did not answer",
    "21": "invalid joint limits in S",
}


def build_frame(cid, payload=b""):
    """STX | LEN | CID | FS | payload | ETX | LRC (doc/md/bitacora-R09-ros2-control.md)."""
    body = cid.encode("ascii") + bytes([FS]) + payload
    length = "%04d" % (len(body) + 2)  # + ETX + LRC
    covered = length.encode("ascii") + body + bytes([ETX])
    lrc = 0
    for b in covered:
        lrc ^= b
    return bytes([STX]) + covered + bytes([lrc])


def build_m_frame(mrad):
    fields = ["%+05d" % max(-MRAD_MAX, min(MRAD_MAX, v)) for v in mrad]
    return build_frame("M", bytes([FS]).join(f.encode("ascii") for f in fields))


def parse_frames(buf):
    """Extract valid frames from buf (same rules as the C parser). Returns (frames, rest)."""
    frames = []
    while True:
        start = buf.find(bytes([STX]))
        if start < 0:
            return frames, b""
        buf = buf[start:]
        if len(buf) < 5:
            return frames, buf
        digits = buf[1:5]
        if not digits.isdigit() or not 4 <= int(digits) <= 128:
            buf = buf[1:]
            continue
        size = 5 + int(digits)
        if len(buf) < size:
            return frames, buf
        lrc = 0
        for b in buf[1:size - 1]:
            lrc ^= b
        if buf[6] != FS or buf[size - 2] != ETX or lrc != buf[size - 1]:
            buf = buf[1:]
            continue
        frames.append((chr(buf[5]), buf[7:size - 2]))
        buf = buf[size:]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--count", type=int, default=1, help="frames to send (default 1)")
    parser.add_argument("--period-ms", type=int, default=20, help="gap between frames (default 20, like the 50 Hz loop)")
    parser.add_argument("--boot-wait-ms", type=int, default=500,
                        help="wait after opening the port, while the ESP32 reboots (default 500)")
    parser.add_argument("--countdown", type=int, default=3,
                        help="seconds to wait before sending, to reach the power switch (default 3, 0 = none)")
    parser.add_argument("--reply-wait-ms", type=int, default=200,
                        help="keep listening for replies after the last frame (default 200)")
    parser.add_argument("--read-only", action="store_true",
                        help="do not send S: the ESP32 stays STOPPED and only reports positions")
    parser.add_argument("mrad", type=int, nargs=6)
    args = parser.parse_args()

    frame = build_m_frame(args.mrad)
    print("Frame (%d bytes): %s" % (len(frame), frame.hex(" ")))

    # Before opening the port: opening it already resets the ESP32.
    if args.countdown > 0:
        print("Target: %s mrad. Hand on the power switch!" % " ".join(str(v) for v in args.mrad))
        for remaining in range(args.countdown, 0, -1):
            print("Sending in %d..." % remaining, flush=True)
            time.sleep(1)

    # Keep DTR/RTS deasserted: on ESP32 dev boards they drive EN and GPIO0
    # (auto-reset circuit). This avoids holding the board in reset or download
    # mode, but the kernel still pulses them on open, which resets it once.
    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = BAUD
    ser.dtr = False
    ser.rts = False
    ser.open()
    rx = b""
    replies = 0

    def drain():
        nonlocal rx, replies
        rx += ser.read(ser.in_waiting)
        frames, rx = parse_frames(rx)
        for cid, payload in frames:
            replies += 1
            if cid == "E":
                # S rejected (or another event): two-digit code, see E_CODES
                code = payload.decode("ascii", "replace")
                print("Reply E %s: %s" % (code, E_CODES.get(code, "unknown code")))
                continue
            fields = payload.split(bytes([FS]))
            print("Reply %s: %s mrad" % (cid, " ".join(f.decode("ascii", "replace") for f in fields)))

    try:
        time.sleep(args.boot_wait_ms / 1000.0)
        ser.reset_input_buffer()  # drop the ROM boot messages printed on UART0
        if not args.read_only:
            ser.write(build_frame("S"))  # start: M frames move the servos from now on
        for _ in range(args.count):
            ser.write(frame)
            time.sleep(args.period_ms / 1000.0)
            drain()
        # Replies take a few ms more than period_ms: keep listening a bit.
        deadline = time.monotonic() + args.reply_wait_ms / 1000.0
        while time.monotonic() < deadline:
            time.sleep(0.01)
            drain()
    finally:
        ser.close()
    print("Sent %d M frame(s), got %d reply frame(s)" % (args.count, replies))


if __name__ == "__main__":
    main()
