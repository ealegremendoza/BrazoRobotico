#!/usr/bin/env python
"""
calibrate-arm.py - whole-arm calibration for the 6-DOF STS3215 arm.

Similar in spirit to `lerobot-calibrate` (LeRobot) but much simpler and
more direct. It calibrates all six servos of the arm in one session:

  1. HOMING: put the arm in the ZERO pose (every joint at the middle of
     its travel, gripper half open) and press ENTER. Each servo's OFS
     register is written so that pose reports `--target` (default 2048).
  2. RANGES: move every joint through its full range of motion, one at a
     time. Min/max are recorded live. The wrist_roll joint (continuous
     rotation) is forced to the full [0, 4095] range unless
     `--record-wrist-roll` is given.
  3. WRITE: offsets and limits are written to EEPROM and a calibration
     JSON (LeRobot MotorCalibration format) is saved for reuse.

OFS register semantics (ST3215 datasheet "memory register map", addr 0x1F
"Position correction"; verified on hardware model 777):
    Present_Position = (Actual_Position - OFS) mod 4096
OFS is the signed "Position correction": sign-magnitude with BIT11 as the
direction bit, valid range [-2047, 2047]. The SDK's scs_toscs/scs_tohost
encode/decode it with sign bit 11 (NOT bit 15). So to make the current
physical pose report `target`:
    OFS_new = (current_position + OFS_current - target) mod 4096
normalized to [-2047, 2047] (subtract 4096 when the result is >= 2048),
then encoded with sign bit 11 before writing.

The script leaves torque DISABLED when it finishes, so the arm stays free
to move. Power-cycle the arm (or write Torque_Enable=1) to re-enable.

Usage:
    STServo_Python/stservo-env/bin/python STServo_Python/calibrate-arm.py
    STServo_Python/stservo-env/bin/python STServo_Python/calibrate-arm.py --port /dev/ttyACM0
    STServo_Python/stservo-env/bin/python STServo_Python/calibrate-arm.py --output my_calibration.json
"""

from __future__ import annotations

import argparse
import json
import os
import select
import sys
import time

sys.path.insert(
    0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "stservo-env")
)

from scservo_sdk import (  # noqa: E402
    COMM_SUCCESS,
    PortHandler,
    SMS_STS_MIN_ANGLE_LIMIT_L,
    SMS_STS_OFS_L,
    SMS_STS_PRESENT_POSITION_L,
    SMS_STS_TORQUE_ENABLE,
    sms_sts,
)

DEFAULT_PORT = "/dev/ttyACM0"
BAUDRATE = 1000000
POSITION_MAX = 4095
OFS_SIGN_BIT = 11  # addr 0x1F "Position correction": BIT11 is the direction bit
# 1 count = 0.088 deg. A strict `verified == target` check would fail on
# encoder quantization noise and sub-count arm movement (measured ±1 count).
# The old sign bug was off by hundreds of counts, so ±2 still catches it.
HOMING_TOLERANCE = 2
DEFAULT_TARGET = 2048  # center of the 0..4095 encoder range
RANGES_POLL_S = 0.05

# Motor names and bus IDs, in the same order/layout as the SO-ARM101 arm
# (and LeRobot's so_follower): shoulder_pan, shoulder_lift, elbow_flex,
# wrist_flex, wrist_roll, gripper.
SERVOS = {
    "shoulder_pan": 1,
    "shoulder_lift": 2,
    "elbow_flex": 3,
    "wrist_flex": 4,
    "wrist_roll": 5,
    "gripper": 6,
}
FULL_ROTATION_MOTORS = {"wrist_roll"}


def open_bus(port_name):
    port = PortHandler(port_name)
    if not port.openPort():
        raise RuntimeError("could not open %s" % port_name)
    if not port.setBaudRate(BAUDRATE):
        port.closePort()
        raise RuntimeError("unsupported baudrate %d" % BAUDRATE)
    return port, sms_sts(port)


def scan_servos(ph):
    """Ping every expected servo; return the present ids and print a table."""
    found = {}
    print("Scanning servos on the bus...")
    for name, servo_id in SERVOS.items():
        model, result, error = ph.ping(servo_id)
        if result == COMM_SUCCESS:
            found[servo_id] = name
            print("  [ID:%d] %-14s OK (model %d)" % (servo_id, name, model))
        else:
            print(
                "  [ID:%d] %-14s MISSING (%s)"
                % (servo_id, name, ph.getTxRxResult(result))
            )
    missing = [sid for sid in SERVOS.values() if sid not in found]
    if missing:
        raise RuntimeError(
            "servo(s) %s did not respond; check power, wiring and ids"
            % ", ".join(str(sid) for sid in missing)
        )
    return found


def set_torque(ph, servo_id, enabled):
    result, error = ph.write1ByteTxRx(servo_id, SMS_STS_TORQUE_ENABLE, 1 if enabled else 0)
    if result != COMM_SUCCESS:
        raise RuntimeError(
            "failed to set torque on servo %d: %s"
            % (servo_id, ph.getTxRxResult(result))
        )


def read_position(ph, servo_id):
    position, result, _error = ph.ReadPos(servo_id)
    if result != COMM_SUCCESS:
        raise RuntimeError(
            "servo %d did not respond: %s" % (servo_id, ph.getTxRxResult(result))
        )
    return position


def read_offset(ph, servo_id):
    raw, result, _error = ph.read2ByteTxRx(servo_id, SMS_STS_OFS_L)
    if result != COMM_SUCCESS:
        raise RuntimeError(
            "servo %d did not respond: %s" % (servo_id, ph.getTxRxResult(result))
        )
    return ph.scs_tohost(raw, OFS_SIGN_BIT)


def write_offset(ph, servo_id, offset):
    encoded = ph.scs_toscs(offset, OFS_SIGN_BIT)
    result, _error = ph.writeTxRx(
        servo_id, SMS_STS_OFS_L, 2, [ph.scs_lobyte(encoded), ph.scs_hibyte(encoded)]
    )
    if result != COMM_SUCCESS:
        raise RuntimeError(
            "failed to write offset on servo %d: %s"
            % (servo_id, ph.getTxRxResult(result))
        )


def write_limits(ph, servo_id, min_limit, max_limit):
    result, _error = ph.writeTxRx(
        servo_id,
        SMS_STS_MIN_ANGLE_LIMIT_L,
        4,
        [
            ph.scs_lobyte(min_limit),
            ph.scs_hibyte(min_limit),
            ph.scs_lobyte(max_limit),
            ph.scs_hibyte(max_limit),
        ],
    )
    if result != COMM_SUCCESS:
        raise RuntimeError(
            "failed to write angle limits on servo %d: %s"
            % (servo_id, ph.getTxRxResult(result))
        )


def homing(ph, target):
    """Write OFS so the current physical pose reports `target` on every servo."""
    print()
    print("=== HOMING ===")
    print("Move the arm to the ZERO pose:")
    print("  - every joint at the MIDDLE of its travel,")
    print("  - gripper half open.")
    print("Press ENTER when ready (or Ctrl+C to abort).")
    input()

    print("Reading current offsets/positions and writing new OFS...")
    for name, servo_id in SERVOS.items():
        current_position = read_position(ph, servo_id)
        current_offset = read_offset(ph, servo_id)
        # Present = (Actual - OFS) mod 4096, so we need:
        #   OFS_new = (position + OFS_current - target) mod 4096
        new_offset = (current_position + current_offset - target) % (POSITION_MAX + 1)
        if new_offset > POSITION_MAX // 2:
            new_offset -= POSITION_MAX + 1  # normalize to signed [-2047, 2047]
        if new_offset == -(POSITION_MAX // 2 + 1):
            new_offset = -(POSITION_MAX // 2)  # -2048 is not representable (BIT11)
        write_offset(ph, servo_id, new_offset)
        print(
            "  %-14s pos=%4d offset=%5d -> new offset=%5d"
            % (name, current_position, current_offset, new_offset)
        )

    # Verify: after the write, the pose should now report the target.
    bad = []
    for name, servo_id in SERVOS.items():
        verified = read_position(ph, servo_id)
        delta = verified - target
        if abs(delta) > HOMING_TOLERANCE:
            bad.append((name, verified))
        print("  %-14s verified pos=%4d (delta %+d)" % (name, verified, delta))
    if bad:
        names = ", ".join("%s=%d" % (n, v) for n, v in bad)
        print("WARNING: homing did not center %s." % names)
        print("Check the arm is still in the ZERO pose and re-run.")
        print("OFS is 'Position correction' (addr 0x1F), sign-magnitude with BIT11")
        print("as direction bit: Present = (Actual - OFS) mod 4096.")
        return False
    return True


def record_ranges(ph, target, record_wrist_roll):
    """Record min/max for every joint while the user moves the arm by hand."""
    print()
    print("=== RANGES OF MOTION ===")
    if record_wrist_roll:
        print("Move EVERY joint through its full range of motion, one at a time.")
    else:
        print(
            "Move every joint through its full range of motion, one at a time "
            "(wrist_roll will be forced to [0, %d])." % POSITION_MAX
        )
    print("The live table updates as you move. Press ENTER when done.")
    print()

    ranges = {name: [POSITION_MAX, 0] for name in SERVOS}  # [min, max]
    try:
        while True:
            if select.select([sys.stdin], [], [], 0)[0]:
                input()  # consume the Enter key
                break
            parts = []
            for name, servo_id in SERVOS.items():
                pos = read_position(ph, servo_id)
                if pos < ranges[name][0]:
                    ranges[name][0] = pos
                if pos > ranges[name][1]:
                    ranges[name][1] = pos
                parts.append("%s:%d[%d,%d]" % (name, pos, ranges[name][0], ranges[name][1]))
            sys.stdout.write("\r  " + "  ".join(parts).ljust(110))
            sys.stdout.flush()
            time.sleep(RANGES_POLL_S)
    except KeyboardInterrupt:
        print("\nAborted by user. Calibration NOT written.")
        raise

    print()
    if not record_wrist_roll:
        for name in FULL_ROTATION_MOTORS & set(SERVOS):
            ranges[name] = [0, POSITION_MAX]
            print("  %-14s forced to [0, %d] (continuous rotation)" % (name, POSITION_MAX))

    # Sanity checks.
    unmoved = [name for name, (lo, hi) in ranges.items() if lo >= hi]
    if unmoved:
        raise RuntimeError(
            "no motion recorded for: %s. Move every joint and try again."
            % ", ".join(unmoved)
        )
    not_centered = [
        name
        for name, (lo, hi) in ranges.items()
        if not (lo <= target <= hi)
    ]
    for name in not_centered:
        print(
            "  WARNING: %s range [%d, %d] does not include the homed center %d"
            % (name, ranges[name][0], ranges[name][1], target)
        )

    print()
    print("Recorded ranges:")
    print("  %-14s %8s %8s" % ("motor", "min", "max"))
    for name in SERVOS:
        print("  %-14s %8d %8d" % (name, ranges[name][0], ranges[name][1]))
    return ranges


def save_json(calibration, output_path, offsets):
    """Persist in LeRobot MotorCalibration format for reuse."""
    payload = {}
    for name, servo_id in SERVOS.items():
        payload[name] = {
            "id": servo_id,
            "drive_mode": 0,  # not modified by this script (Feetech convention)
            "homing_offset": offsets[name],
            "range_min": calibration[name][0],
            "range_max": calibration[name][1],
        }
    with open(output_path, "w") as fh:
        json.dump(payload, fh, indent=2)
        fh.write("\n")
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT, help="serial device")
    parser.add_argument(
        "--target",
        type=int,
        default=DEFAULT_TARGET,
        help="position the ZERO pose should report (0-4095, default %d)" % DEFAULT_TARGET,
    )
    parser.add_argument(
        "--record-wrist-roll",
        action="store_true",
        help="record the real wrist_roll range instead of forcing [0, 4095]",
    )
    parser.add_argument(
        "--output",
        help="calibration JSON path (default: <script dir>/calibration.json)",
    )
    args = parser.parse_args()

    if not 0 <= args.target <= POSITION_MAX:
        parser.error("--target must be between 0 and %d" % POSITION_MAX)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    output_path = args.output or os.path.join(script_dir, "calibration.json")

    port = None
    try:
        port, ph = open_bus(args.port)
        print("Connected to %s @ %d baud" % (args.port, BAUDRATE))

        scan_servos(ph)

        # EEPROM writes need the lock released; torque must be off so the
        # arm can be moved by hand during homing/ranges.
        for servo_id in SERVOS.values():
            ph.unLockEprom(servo_id)
            set_torque(ph, servo_id, enabled=False)
        print("Torque disabled. EEPROM unlocked.")

        offsets = {}
        if not homing(ph, args.target):
            raise RuntimeError("homing verification failed; nothing was written")

        ranges = record_ranges(ph, args.target, args.record_wrist_roll)

        # Read back the offsets that were actually written (per servo).
        for name, servo_id in SERVOS.items():
            offsets[name] = read_offset(ph, servo_id)

        print()
        print("Writing limits to EEPROM...")
        for name, servo_id in SERVOS.items():
            write_limits(ph, servo_id, ranges[name][0], ranges[name][1])
            print(
                "  %-14s limits [%d, %d]" % (name, ranges[name][0], ranges[name][1])
            )

        payload = save_json(ranges, output_path, offsets)

        for servo_id in SERVOS.values():
            ph.LockEprom(servo_id)
        port.closePort()
        port = None

        print()
        print("=== CALIBRATION COMPLETE ===")
        print("Saved to: %s" % output_path)
        print("Torque is DISABLED. Power-cycle the arm to re-enable it.")
        print()
        print("  %-14s %8s %8s %12s" % ("motor", "min", "max", "offset"))
        for name, servo_id in SERVOS.items():
            print(
                "  %-14s %8d %8d %12d"
                % (name, ranges[name][0], ranges[name][1], offsets[name])
            )
    except KeyboardInterrupt:
        print("\nCalibration aborted. EEPROM left unchanged where possible.")
        return 130
    except Exception as exc:  # noqa: BLE001 - report and clean up
        print("ERROR: %s" % exc)
        return 1
    finally:
        if port is not None:
            try:
                for servo_id in SERVOS.values():
                    ph.LockEprom(servo_id)
            except Exception:  # noqa: BLE001
                pass
            try:
                port.closePort()
            except Exception:  # noqa: BLE001
                pass

    return 0


if __name__ == "__main__":
    sys.exit(main())
