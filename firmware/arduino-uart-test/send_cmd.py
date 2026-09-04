#!/usr/bin/env python3
import sys
import time
import serial

STX = 0x02
ETX = 0x03
PORT = "/dev/ttyUSB0"  # ajustar al puerto del Nano
BAUD = 9600

def build_frame(payload: str) -> bytes:
    data = payload.encode("ascii")
    lrc = 0
    for b in data:
        lrc ^= b
    lrc ^= ETX
    return bytes([STX]) + data + bytes([ETX, lrc])

def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ("ON", "OFF"):
        print("Uso: send_cmd.py ON|OFF")
        sys.exit(1)

    frame = build_frame(sys.argv[1])
    print("Frame:", frame.hex(" "))

    with serial.Serial(PORT, BAUD, timeout=1) as ser:
        time.sleep(2)  # esperar a que el Nano termine el reset (DTR) y arranque loop()
        while ser.in_waiting:
            print("Nano:", ser.readline().decode(errors="replace").strip())

        ser.write(frame)

        time.sleep(0.5)
        while ser.in_waiting:
            print("Nano:", ser.readline().decode(errors="replace").strip())

if __name__ == "__main__":
    main()
