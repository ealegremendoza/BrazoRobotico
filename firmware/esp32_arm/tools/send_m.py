#!/usr/bin/env python3
"""Send M (motors) frames to the ESP32 like the ros2_control plugin's write().

Frame format (doc/md/bitacora-R09-ros2-control.md):
    STX | LEN | CID | FS | payload | ETX | LRC

Usage:
    send_m.py [--port /dev/ttyACM0] [--count N] [--period-ms MS] [--boot-wait-ms MS] j1 j2 j3 j4 j5 j6

Joint values are integers in milliradians (e.g. 1571 -1571 0 0 0 0).
The ESP32 logs what it decoded on its console (UART1, GPIO14).

Opening the port resets the ESP32 (Linux asserts DTR/RTS on open, before pyserial
can clear them; on ESP32 dev boards those lines drive EN/GPIO0). Frames sent while
it boots (~300 ms until app_main) are lost, so the script waits --boot-wait-ms first.
"""
import argparse
import time

import serial

STX = 0x02
ETX = 0x03
FS = 0x1C
BAUD = 115200
MRAD_MAX = 3142


def build_m_frame(mrad):
    fields = ["%+05d" % max(-MRAD_MAX, min(MRAD_MAX, v)) for v in mrad]
    body = b"M" + bytes([FS]) + bytes([FS]).join(f.encode("ascii") for f in fields)
    length = "%04d" % (len(body) + 2)  # + ETX + LRC
    covered = length.encode("ascii") + body + bytes([ETX])
    lrc = 0
    for b in covered:
        lrc ^= b
    return bytes([STX]) + covered + bytes([lrc])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--count", type=int, default=1, help="frames to send (default 1)")
    parser.add_argument("--period-ms", type=int, default=20, help="gap between frames (default 20, like the 50 Hz loop)")
    parser.add_argument("--boot-wait-ms", type=int, default=500,
                        help="wait after opening the port, while the ESP32 reboots (default 500)")
    parser.add_argument("mrad", type=int, nargs=6)
    args = parser.parse_args()

    frame = build_m_frame(args.mrad)
    print("Frame (%d bytes): %s" % (len(frame), frame.hex(" ")))

    # Keep DTR/RTS deasserted: on ESP32 dev boards they drive EN and GPIO0
    # (auto-reset circuit). This avoids holding the board in reset or download
    # mode, but the kernel still pulses them on open, which resets it once.
    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = BAUD
    ser.dtr = False
    ser.rts = False
    ser.open()
    try:
        time.sleep(args.boot_wait_ms / 1000.0)
        for _ in range(args.count):
            ser.write(frame)
            time.sleep(args.period_ms / 1000.0)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
