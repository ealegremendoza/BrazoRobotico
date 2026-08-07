#!/usr/bin/env python
"""
Calibrate STS3215 servo zero offset and angle limits.

The STS3215 has no position "reset": the reported position (0-4095 counts,
12-bit) is an encoder reading, and the physical zero depends on how the
servo horn was mounted. To align them you write the OFS "Position
correction" register (31-32, addr 0x1F), sign-magnitude with BIT11 as the
direction bit, valid range [-2047, 2047] (datasheet "ST3215 memory
register map"). The reported position follows (mod 4096):
    reported_position = actual_position - OFS
This script:

  1. unlocks the EEPROM,
  2. optionally writes a new zero offset so the CURRENT physical position
     reports a target value (--target),
  3. optionally restores the factory offset with --reset,
  4. optionally writes angular travel limits (--min/--max) so the arm cannot
     drive into mechanical stops,
  5. locks the EEPROM again and verifies.

Usage:
    STServo_Python/stservo-env/bin/python STServo_Python/calibrate_servo_offset.py --id 1 --target 2048
    STServo_Python/stservo-env/bin/python STServo_Python/calibrate_servo_offset.py --id 2 --reset
    STServo_Python/stservo-env/bin/python STServo_Python/calibrate_servo_offset.py --id 3 --min 512 --max 3584
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(
    0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "stservo-env")
)

from scservo_sdk import (  # noqa: E402
    COMM_SUCCESS,
    PortHandler,
    SMS_STS_MAX_ANGLE_LIMIT_L,
    SMS_STS_MIN_ANGLE_LIMIT_L,
    SMS_STS_OFS_L,
    SMS_STS_PRESENT_POSITION_L,
    sms_sts,
)

DEFAULT_PORT = "/dev/ttyACM0"
BAUDRATE = 1000000
POSITION_MAX = 4095
OFS_SIGN_BIT = 11  # addr 0x1F "Position correction": BIT11 is the direction bit
# 1 count = 0.088 deg; ±2 absorbs encoder quantization / sub-count arm drift.
HOMING_TOLERANCE = 2


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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT, help="serial device")
    parser.add_argument("--id", type=int, required=True, help="servo id to calibrate")
    parser.add_argument(
        "--target",
        type=int,
        help="position that the CURRENT physical position should report (0-4095)",
    )
    parser.add_argument(
        "--reset", action="store_true", help="restore factory offset (0) instead"
    )
    parser.add_argument("--min", type=int, help="min angle limit (0-4095)")
    parser.add_argument("--max", type=int, help="max angle limit (0-4095)")
    args = parser.parse_args()

    if not args.reset and args.target is None and args.min is None and args.max is None:
        parser.error("provide --target, --reset, --min/--max, or a combination")
    if args.reset and args.target is not None:
        parser.error("--reset and --target are mutually exclusive")

    port = PortHandler(args.port)
    if not port.openPort():
        print("ERROR: could not open %s" % args.port)
        return 1
    if not port.setBaudRate(BAUDRATE):
        print("ERROR: unsupported baudrate %d" % BAUDRATE)
        return 1
    ph = sms_sts(port)
    servo_id = args.id

    try:
        # EEPROM registers require the lock to be released first.
        ph.unLockEprom(servo_id)

        current_offset = read_offset(ph, servo_id)
        current_position = read_position(ph, servo_id)
        print(
            "servo %d: current position=%d offset=%d"
            % (servo_id, current_position, current_offset)
        )

        if args.reset:
            write_offset(ph, servo_id, 0)
            print("servo %d: factory offset restored (0)" % servo_id)
        elif args.target is not None:
            if not 0 <= args.target <= POSITION_MAX:
                parser.error("--target must be between 0 and %d" % POSITION_MAX)
            # reported = actual - OFS, so we need:
            #   OFS_new = (position + OFS_current - target) mod 4096
            new_offset = (current_position + current_offset - args.target) % (
                POSITION_MAX + 1
            )
            if new_offset > POSITION_MAX // 2:
                new_offset -= POSITION_MAX + 1  # normalize to signed [-2047, 2047]
            if new_offset == -(POSITION_MAX // 2 + 1):
                new_offset = -(POSITION_MAX // 2)  # -2048 is not representable (BIT11)
            write_offset(ph, servo_id, new_offset)
            print(
                "servo %d: wrote offset %d so current position reports %d"
                % (servo_id, new_offset, args.target)
            )

        if args.min is not None or args.max is not None:
            min_limit = args.min if args.min is not None else 0
            max_limit = args.max if args.max is not None else POSITION_MAX
            if not 0 <= min_limit < max_limit <= POSITION_MAX:
                parser.error("limits must satisfy 0 <= min < max <= %d" % POSITION_MAX)
            write_limits(ph, servo_id, min_limit, max_limit)
            print(
                "servo %d: angle limits set to [%d, %d]" % (servo_id, min_limit, max_limit)
            )

        # Re-lock so EEPROM is protected, then verify.
        ph.LockEprom(servo_id)

        verified = read_position(ph, servo_id)
        verified_offset = read_offset(ph, servo_id)
        print(
            "servo %d: VERIFIED position=%d offset=%d"
            % (servo_id, verified, verified_offset)
        )
        if args.target is not None and abs(verified - args.target) > HOMING_TOLERANCE:
            print(
                "WARNING: verification does not match target %d; check the servo "
                "did not move and re-run. OFS is 'Position correction' (addr 0x1F), "
                "sign-magnitude with BIT11 as direction bit: "
                "reported = actual - OFS (mod 4096)." % args.target
            )
        elif args.target is not None:
            print("  (within +/-%d counts: encoder quantization noise)" % HOMING_TOLERANCE)
    except Exception as exc:  # noqa: BLE001
        print("ERROR: %s" % exc)
        try:
            ph.LockEprom(servo_id)
        except Exception:  # noqa: BLE001
            pass
        return 1
    finally:
        port.closePort()

    return 0


if __name__ == "__main__":
    sys.exit(main())
