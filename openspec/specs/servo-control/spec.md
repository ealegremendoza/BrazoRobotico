# Servo Control

## Purpose

Control a 6-DOF robotic arm built with Feetech STS3215 bus servos (IDs 1-6)
from a workstation over a USB serial port (`/dev/ttyACM0`), using a desktop
GUI for commanding target positions and observing live feedback.

## Requirements

### REQ-SERVO-001: Connection lifecycle
The application opens the bus at the user-provided device path with a
baudrate of 1000000 (the STS3215 factory default), and exposes Connect and
Disconnect actions that are mutually exclusive. While disconnected, all
servo controls are disabled.

#### Scenario: Connect to the bus
- **Given** servos 1 and 2 are powered and on the bus at `/dev/ttyACM0`
- **When** the user enters `/dev/ttyACM0` and clicks Connect
- **Then** the port opens, the baudrate is set to 1000000, and per-servo
  status is polled every 100 ms
- **And** Connect becomes disabled while Disconnect becomes enabled

#### Scenario: Disconnect
- **When** the user clicks Disconnect while connected
- **Then** the port is closed, all sliders are disabled, and all servos are
  shown as disconnected

### REQ-SERVO-002: Per-servo command with limits
Each servo has one horizontal slider and Min/Max inputs beside it. Changing
Min/Max re-ranges that slider, clamped to the servo's native 0-4095 count
range (12-bit position). A centered label above the slider shows the current
commanded position.

#### Scenario: Re-range a slider
- **Given** the slider of servo 1 ranges from 0 to 4095
- **When** the user sets Min=1000 and Max=2000
- **Then** the slider re-ranges to 1000-2000 and any out-of-range current
  value is clamped into the new range

### REQ-SERVO-003: Per-servo status from sync read
Servo connectivity and feedback are derived per servo from
`GroupSyncRead.isAvailable(id, ...)`, never from the global
`txRxPacket()` result, which reports `COMM_RX_CORRUPT` whenever any id in
the group is missing.

#### Scenario: Only two of six servos connected
- **Given** only servos 1 and 2 are on the bus
- **When** a sync read cycle completes
- **Then** servos 1 and 2 are marked Connected with live position and
  velocity feedback and enabled sliders
- **And** servos 3-6 are marked Disconnected with disabled sliders

### REQ-SERVO-004: Feedback display
To the right of each slider the UI shows the read position (raw counts), the
read velocity (signed via `scs_tohost(value, 15)`), and the
connected/disconnected state, color-coded.

### REQ-SERVO-005: Write behaviour
Slider movement writes the goal position with debounce (40 ms coalescing)
plus an immediate flush on mouse release, using `WritePosEx(id, position,
speed=2400, acc=50)`. The write is skipped for a servo whose last poll
marked it disconnected.

## Design

- **GUI**: tkinter/ttk single-window, one `ServoRow` frame per servo.
- **Bus reads**: one `GroupSyncRead(packetHandler, SMS_STS_PRESENT_POSITION_L, 4)`
  covers position (56-57) and speed (58-59) for all six ids in a single
  transaction; `scs_tohost(speed_raw, 15)` restores the sign.
- **Bus writes**: single-servo `WritePosEx` per changed slider.
- **Polling**: a 100 ms `after()` loop drives feedback; a failed serial
  operation triggers disconnect instead of crashing the UI.
- **Errors**: SDK failures are `COMM_*` return codes, never exceptions; the
  UI surfaces connect/open failures as a status label.
- **Entry point**: `STServo_Python/servo_control_ui.py` adds
  `stservo-env/` to `sys.path` so `scservo_sdk` imports from any cwd.
