# LeRobot — Brazo SO-101 Follower: control y calibración

Documento de profundización técnica sobre el subsistema *follower* del brazo SO-101 en
LeRobot (HuggingFace), enfocado en su **control** y su **calibración**. Todo el contenido
fue verificado contra el código fuente del repositorio local de LeRobot (rutas listadas en
[Referencias](#7-referencias)); cuando un dato no pudo confirmarse se indica explícitamente.

---

## 1. Resumen y contexto

LeRobot es el framework de código abierto de HuggingFace para *robot learning*: unifica
grabación de demostraciones, entrenamiento de políticas y teleoperación sobre hardware real.
Su ecosistema de brazos de bajo costo (diseñados por TheRobotStudio, familias SO-ARM100 /
SO-ARM101) incluye el brazo **SO-101**, usado como brazos *leader* (guía) y *follower*
(ejecutor).

El brazo **SO-101 follower** tiene 5 grados de libertad más una pinza (gripper). Los 6 ejes
usan servos **Feetech STS3215** con reductora 1/345 y resolución de encoder de 12 bits
(4096 cuentas por vuelta). El *leader* del SO-101 usa en cambio motorizaciones mixtas
(1/191, 1/345, 1/147 según el eje) para poder sostenerse y moverse a mano.

Para el proyecto **BrazoRobotico** (ROS 2 + grabación/reproducción de trayectorias sobre
servos Feetech ST) este subsistema es la referencia de cómo LeRobot resuelve los problemas
reales de un brazo de bajo costo:

- **Calibración**: encontrar el cero físico (`homing_offset`), el rango mecánico
  (`range_min`/`range_max`) y persistirlo de forma consistente.
- **Control**: un bucle de teleoperación pasivo que lee posiciones, normaliza valores y
  escribe objetivos de posición con protecciones de seguridad (clip de acción, límites de
  torque en la pinza).

---

## 2. Arquitectura del subsistema

La clase base es `SOFollower(Robot)` en
`src/lerobot/robots/so_follower/so_follower.py` (242 líneas):

```python
class SOFollower(Robot):
    config_class = SOFollowerRobotConfig
    name = "so_follower"
```

- **Alias `SO100Follower` y `SO101Follower`**: ambos son la misma clase
  (`SO100Follower = SOFollower` y `SO101Follower = SOFollower` al final del archivo). La
  distinción entre SO-100 y SO-101 es solo cosmética desde el punto de vista del código.
- **Registro de configuración**: `SOFollowerRobotConfig` se registra con dos claves mediante
  `@RobotConfig.register_subclass`:
  - `"so101_follower"`
  - `"so100_follower"`
  - El atributo `name` de la instancia queda en `"so_follower"` (sin versión), lo cual
    condiciona la ruta del archivo de calibración (ver [Ruta del JSON](#42-ruta-del-json-y-estructura)).
- **Factory**: `make_robot_from_config(config)` en `src/lerobot/robots/utils.py` despacha
  `"so100_follower"` → `SO100Follower` y `"so101_follower"` → `SO101Follower`. En
  `__init__` el bus se arma con 6 motores `sts3215`:

  | Nombre | ID | Modo de normalización |
  | ------ | -- | --------------------- |
  | `shoulder_pan` | 1 | `DEGREES` (o `RANGE_M100_100` si `use_degrees=False`) |
  | `shoulder_lift` | 2 | ídem |
  | `elbow_flex` | 3 | ídem |
  | `wrist_flex` | 4 | ídem |
  | `wrist_roll` | 5 | ídem |
  | `gripper` | 6 | `RANGE_0_100` (siempre) |

- **Bimanual**: `BiSOFollower` (`src/lerobot/robots/bi_so_follower/bi_so_follower.py`)
  compone dos instancias de `SOFollower` (`left_arm`/`right_arm`), registrado como
  `"bi_so_follower"`. Cada brazo mantiene su propio bus, puerto y calibración.

---

## 3. Control del brazo follower

### 3.1 Ciclo de vida

El flujo típico (usado por los scripts y por el contexto manager `with robot:`):

```
connect()  →  calibrate() (si aplica)  →  configure()
→  get_observation() / send_action()  (bucle)
→  disconnect()
```

Detalles de cada etapa:

- `connect(calibrate=True)` (so_follower.py:92):
  1. `bus.connect()` → abre el puerto serie y hace el *handshake* (ver [Protocolo Feetech](#38-protocolo-feetech)).
  2. Si `not is_calibrated and calibrate`: invoca `calibrate()` (recalibración interactiva o
     reescritura desde archivo, ver [Calibración](#4-calibración-en-profundidad)).
  3. Conecta las cámaras.
  4. Llama `configure()`.
  - El docstring advierte: se asume que en el momento de conectar **el brazo está en
    reposo** (posición de reposo), porque el torque puede deshabilitarse de forma segura
    para correr la calibración.
- `get_observation()` → `sync_read` de `Present_Position` + captura de cámaras.
- `send_action(action)` → escribe `Goal_Position` por `sync_write` con clip de seguridad.
- `disconnect()` → `bus.disconnect(disable_torque_on_disconnect)` + desconexión de cámaras.
  Si `disable_torque_on_disconnect=True` (valor por defecto), se deshabilita el torque antes
  de cerrar el puerto para evitar que los motores queden aplicando torque de resistencia.

`Robot` (`src/lerobot/robots/robot.py`) aporta la infraestructura base: `_load_calibration`
/ `_save_calibration`, `calibration_dir`, `calibration_fpath` y el context manager
(`__enter__` conecta, `__exit__` desconecta incluso con error). También define las
propiedades abstractas `observation_features` y `action_features` que describen la estructura
plana de observaciones/acciones: claves `"<motor>.pos"` y, para las cámaras, la clave de
cámara (más `"<cam>_depth"` si usa profundidad).

### 3.2 Configuración clave

`SOFollowerConfig` (`config_so_follower.py`, 64 líneas) — hereda de `RobotConfig` (que
aporta `id`, `calibration_dir`):

| Campo | Valor por defecto | Significado |
| ----- | ----------------- | ----------- |
| `port` | — (obligatorio) | Puerto serie del bus (p. ej. `/dev/ttyACM0`) |
| `disable_torque_on_disconnect` | `True` | Deshabilita torque al desconectar |
| `max_relative_target` | `None` | Límite máximo de la acción relativa; `float` (todos los motores) o `dict[str, float]` por motor |
| `cameras` | `{}` | Configuración de cámaras (`CameraConfig`) |
| `use_degrees` | `True` | `True` → normalización `DEGREES`; `False` → `RANGE_M100_100` |
| `position_p_coefficient` | `16` | Ganancia P del PID de posición |
| `position_i_coefficient` | `0` | Ganancia I |
| `position_d_coefficient` | `32` | Ganancia D |
| `num_read_retries` | `2` | Reintentos adicionales de `sync_read` ante paquetes corruptos |

`SOFollowerRobotConfig` es la unión de `RobotConfig` + `SOFollowerConfig` (sin campos
adicionales), con los alias `SO100FollowerConfig`/`SO101FollowerConfig`.

### 3.3 Normalización de valores

El bus normaliza automáticamente los registros `Goal_Position` y `Present_Position`
(única lista `NORMALIZED_DATA = ["Goal_Position", "Present_Position"]` en feetech.py:47).
La normalización exige calibración cargada: sin `calibration`, `_normalize`/`_unnormalize`
lanzan `RuntimeError`.

El modo de normalización se define por motor en el `MotorNormMode`
(`src/lerobot/motors/motors_bus.py:169`):

- **`DEGREES`** (cuerpo, cuando `use_degrees=True`). Lectura normalizada:

  ```
  mid   = (range_min + range_max) / 2
  max_res = resolution - 1          # = 4095 para STS3215
  norm  = (raw - mid) * 360 / max_res
  ```

  Es decir, la posición central (`mid`) reporta `0°` y el rango mecánico completo abarca
  ±180°. La escritura inversa es `raw = int(val * max_res / 360 + mid)`.

- **`RANGE_0_100`** (pinza). Normaliza a porcentaje:

  ```
  norm = ((raw - range_min) / (range_max - range_min)) * 100
  ```

  Para la pinza, **`100` = abierta** (en `range_max`) y `0` = cerrada (en `range_min`). Con
  `drive_mode=1` se invierte (`norm = 100 - norm`).

- **`RANGE_M100_100`** (cuerpo cuando `use_degrees=False`): normaliza al rango [-100, 100]
  con `norm = ((raw - min) / (max - min)) * 200 - 100`, negando el signo si `drive_mode=1`.

  > Detalle: `DEGREES` **ignora** `drive_mode`; los modos `RANGE_*` sí lo aplican. En la
  > calibración del follower siempre se escribe `drive_mode=0`, por lo que la inversión
  > solo es relevante si un JSON trae `drive_mode=1` (caso típico de brazos *leader* con
  > ejes montados invertidos).

Los valores crudos leídos/escritos se convierten siempre al espacio normalizado cuando el
registro está en `NORMALIZED_DATA`, tanto en `sync_read`/`sync_write` como en `read`/`write`
(con el flag `normalize`).

### 3.4 `configure()`

`SOFollower.configure()` (so_follower.py:159) se ejecuta bajo el context manager
`bus.torque_disabled()` (garantiza re-habilitar torque al final):

1. `bus.configure_motors()` (feetech.py:209), para cada motor:
   - `Return_Delay_Time = 0` (reduce el retardo de respuesta de los 500 µs por defecto al
     mínimo de 2 µs).
   - `Maximum_Acceleration = 254` y `Acceleration = 254` (solo protocolo 0 para el primero)
     para acelerar el arranque y frenado.
   - **Limpieza del bit 4 (0x10) del registro `Phase` (0x12)** en los `sts3215`: fuerza el
     modo de realimentación de ángulo a 0, manteniendo las lecturas en `[0, resolution-1]`
     y evitando valores negativos o desbordados.
2. Por motor:
   - `Operating_Mode = OperatingMode.POSITION.value` (= 0, modo servo de posición).
   - `P_Coefficient = 16`, `I_Coefficient = 0`, `D_Coefficient = 32` (desde config).
3. **Límites de seguridad de la pinza** (`gripper`), para evitar quemar el motor:
   - `Max_Torque_Limit = 500` (50 % del torque máximo).
   - `Protection_Current = 250` (50 % de la corriente máxima).
   - `Overload_Torque = 25` (25 % de torque al detectar sobrecarga).

### 3.5 `send_action()`

`SOFollower.send_action(action)` (so_follower.py:204) devuelve **la acción realmente
enviada** (posiblemente recortada), que puede diferir de la original:

1. Extrae de `action` las claves que terminan en `.pos` como `goal_pos` por motor.
2. **Clip por `max_relative_target`**: si está configurado, primero hace un
   `sync_read("Present_Position")` adicional y luego
   `ensure_safe_goal_position(goal_present_pos, max_relative_target)`
   (`src/lerobot/robots/utils.py:91`), que recorta el delta `goal - present` al rango
   `[±max_relative_target]` por motor. El propio código advierte: `Slower fps expected due
   to reading from the follower.` — ese `sync_read` extra encarece el bucle.
3. Escribe `Goal_Position` en todos los motores con un único `sync_write`.
4. Devuelve el dict `{f"{motor}.pos": val}` con los valores efectivamente escritos.

### 3.6 `get_observation()`

`SOFollower.get_observation()` (so_follower.py:179):

1. `bus.sync_read("Present_Position", num_retry=config.num_read_retries)` → un
   `GroupSyncRead` de los 6 motores (valores normalizados). Se convierte a claves
   `"<motor>.pos"`.
2. Por cada cámara: `read_latest()` (RGB) y opcionalmente `read_latest_depth()` (clave
   `"<cam>_depth"`).

### 3.7 El follower es pasivo: el bucle vive en el script

`SOFollower` **no tiene** `teleop_step` ni un bucle propio: es un dispositivo pasivo
que solo responde a llamadas. El bucle de control está en
`src/lerobot/scripts/lerobot_teleoperate.py` (`teleop_loop`, fps por defecto **60**):

```python
obs = robot.get_observation()                       # 1. observación
raw_action = teleop.get_action()                    # 2. acción del teleoperador
teleop_action = teleop_action_processor((raw_action, obs))
robot_action_to_send = robot_action_processor((teleop_action, obs))
_ = robot.send_action(robot_action_to_send)         # 3. enviar al follower
```

El mismo patrón de `get_observation` → `get_action` → procesadores → `send_action` es el
que usa el pipeline de grabación de datasets. `lerobot_find_joint_limits.py` usa un bucle
idéntico (ver [CLI](#45-cli)).

### 3.8 Protocolo Feetech

Detalles del bus `FeetechMotorsBus` (`src/lerobot/motors/feetech/feetech.py`, 459 líneas) y
sus tablas (`src/lerobot/motors/feetech/tables.py`, 257 líneas):

- **Comunicación**: puerto serie USB + SDK `feetech-servo-sdk` (`scservo_sdk`, basado en el
  SDK Dynamixel), protocolo **v0**, baudrate por defecto **1 Mbaud**
  (`DEFAULT_BAUDRATE = 1_000_000`), timeout de paquete 1000 ms. En la tabla de baudrates,
  1 Mbaud corresponde al valor `0` del registro `Baud_Rate`.
- **Handshake** (`_handshake`): dos verificaciones en `connect()`:
  1. `_assert_motors_exist()`: `ping` a cada ID esperado y comprobación del número de
     modelo (`sts3215` → 777). Si falta un ID o el modelo no coincide, lanza `RuntimeError`
     con la lista de motores esperados/encontrados.
  2. `_assert_same_firmware()`: lee `Firmware_Major_Version` (addr 0) y `Firmware_Minor_Version`
     (addr 1) de todos los motores; si no son iguales, lanza `RuntimeError` indicando que hay
     que actualizar el firmware con el software de Feetech.
- **Monkeypatch `setPacketTimeout`**: el SDK publicado en PyPI tiene un bug en el cálculo
  del timeout de paquete (issue IBY2S6 de Gitee, ya corregido en el repo oficial de Feetech
  pero no publicado). LeRobot lo parchea en runtime:
  `packet_timeout = (tx_time_per_byte * packet_length) + (tx_time_per_byte * 3.0) + 50`.
- **Registro `Phase` (0x12), bit 4 (0x10)**: se limpia en `configure_motors()` para los
  `sts3215` (ver [3.4](#34-configure)); solo se sabe necesario para ese modelo.
- **Lectura/escritura en grupo**: `GroupSyncRead`/`GroupSyncWrite` del SDK para
  `sync_read`/`sync_write`; `broadcast_ping` para escaneo. Nota: `sync_write` no espera
  paquete de estado (puede perder paquetes) pero es rápido; `write` sí espera respuesta
  (más lento, para configuración).
- **Paquetes corruptos**: los buses Feetech devuelven ocasionalmente un paquete de estado
  corrupto (`"Incorrect status packet!"`), sobre todo cuando varios ejes se mueven a la vez.
  Por eso `num_read_retries` (por defecto 2) reintenta de inmediato (sin `sleep`) solo ante
  fallo; el costo de lectura en estado estable no cambia.
- **Torque**: `disable_torque()` escribe `Torque_Enable = 0` **y** `Lock = 0`;
  `enable_torque()` escribe `Torque_Enable = 1` **y** `Lock = 1`. El registro `Lock`
  (addr 55) desbloquea/bloquea el EEPROM, condición necesaria para escribir la
  calibración (ver [Calibración](#4-calibración-en-profundidad)).
- **Encoding sign-magnitude, bit 15**: los registros de posición/velocidad/carga usan
  *sign-magnitude* (bit de signo = dirección). Para STS, `Goal_Position`, `Present_Position`,
  `Goal_Velocity`, `Present_Velocity`, `Goal_Speed`, `Present_Speed` y `Present_Load` usan el
  bit de signo 15; `Homing_Offset` usa el bit 11. `encode_sign_magnitude` pone el bit de
  signo en 1 si el valor es negativo: `value = (dir << sign_bit) | magnitude`, con magnitud
  máxima `(1 << sign_bit) - 1`.
- **Resolución**: 4096 ticks por vuelta (12 bits) para `sts3215`; `max_res = 4095`.

---

## 4. Calibración en profundidad

### 4.1 Qué es la calibración

Para cada motor, `MotorCalibration` (`motors_bus.py:175`) define 5 campos:

| Campo | Descripción |
| ----- | ----------- |
| `id` | ID en el bus (1..6) |
| `drive_mode` | Dirección/inversión del motor (0 = normal). En el follower siempre 0 |
| `homing_offset` | Desplazamiento de cero físico. Convención Feetech: `Present_Position = Actual_Position - Homing_Offset` |
| `range_min` | Límite inferior del rango mecánico (cuentas de encoder) |
| `range_max` | Límite superior del rango mecánico |

La calibración existe porque el STS3215 **no tiene un "reset" de posición**: la posición
reportada (0-4095) es una lectura de encoder y el cero físico depende de cómo se montó el
horn del servo. Es la misma motivación del script propio
`STServo_Python/calibrate_servo_offset.py` (ver [Relevancia](#5-relevancia-para-el-proyecto-brazorobotico)).

### 4.2 Doble persistencia

La calibración vive en **dos lugares** que deben coincidir:

1. **EEPROM del motor** (memoria no volátil del servo), vía `write_calibration`
   (feetech.py:268). Con protocolo 0 escribe:
   - `Homing_Offset` (addr 31, 2 bytes)
   - `Min_Position_Limit` (addr 9, 2 bytes)
   - `Max_Position_Limit` (addr 11, 2 bytes)
2. **Archivo JSON** en el host, vía `_save_calibration` (`Robot` en robot.py:162).

`is_calibrated` (feetech.py:227) compara ambos: `read_calibration()` lee de los motores
`Min_Position_Limit`, `Max_Position_Limit` y (protocolo 0) `Homing_Offset`, y verifica que
el conjunto de motores y los rangos (y offsets, en protocolo 0) coincidan con
`self.calibration` (cargado del JSON). Con protocolo 1 los offsets no se comparan.

### 4.3 Ruta del JSON y estructura

Ruta (robot.py:49-56):

```python
self.calibration_dir  = config.calibration_dir or HF_LEROBOT_CALIBRATION / ROBOTS / self.name
self.calibration_fpath = self.calibration_dir / f"{self.id}.json"
```

- `HF_LEROBOT_CALIBRATION` = variable de entorno `HF_LEROBOT_CALIBRATION`, o por defecto
  `~/.cache/huggingface/lerobot/calibration` (deriva de `HF_LEROBOT_HOME`, no de
  `LEROBOT_HOME`, que está deprecado).
- `ROBOTS = "robots"` (constantes.py:45).
- **OJO**: la carpeta usa `self.name = "so_follower"`, **no** `so101`. La ruta completa
  efectiva es:

  ```
  ~/.cache/huggingface/lerobot/calibration/robots/so_follower/<id>.json
  ```

  (o `$HF_LEROBOT_CALIBRATION/robots/so_follower/<id>.json` si se define la variable).
- El JSON se carga/guarda con `draccus` (`config_type("json")`). Estructura efectiva de
  `dict[str, MotorCalibration]`:

  ```json
  {
    "shoulder_pan": {
      "id": 1,
      "drive_mode": 0,
      "homing_offset": -1947,
      "range_min": 91,
      "range_max": 4031
    },
    "shoulder_lift": { "id": 2, "drive_mode": 0, "homing_offset": -16, "range_min": 27, "range_max": 3981 },
    "elbow_flex":   { "id": 3, "drive_mode": 0, "homing_offset": 291, "range_min": 128, "range_max": 3798 },
    "wrist_flex":   { "id": 4, "drive_mode": 0, "homing_offset": 1932, "range_min": 50, "range_max": 3985 },
    "wrist_roll":   { "id": 5, "drive_mode": 0, "homing_offset": 250, "range_min": 0, "range_max": 4095 },
    "gripper":      { "id": 6, "drive_mode": 0, "homing_offset": -281, "range_min": 156, "range_max": 4043 }
  }
  ```

  (los valores numéricos son ilustrativos; solo los nombres de clave, IDs y campos son
  canónicos del formato).

### 4.4 Flujo de `SOFollower.calibrate()`

`SOFollower.calibrate()` (so_follower.py:115):

1. **¿Existe archivo de calibración?** Si `self.calibration` ya está cargado (el JSON del
   id existe), pregunta al usuario:

   ```
   Press ENTER to use provided calibration file associated with the id <id>, or type 'c' and press ENTER to run calibration:
   ```

   - `ENTER` → escribe la calibración del archivo a los motores
     (`bus.write_calibration(self.calibration)`) y termina.
   - `'c'` → continúa con calibración nueva.

2. **Preparación**: `bus.disable_torque()` (desbloquea EEPROM vía `Lock=0`) y escribe
   `Operating_Mode = POSITION` en todos los motores.

3. **Homing (cero físico)**: pide al usuario

   ```
   Move <robot> to the middle of its range of motion and press ENTER....
   ```

   y ejecuta `set_half_turn_homings()` (motors_bus.py:775), que:
   - `reset_calibration()`: escribe `Homing_Offset = 0`, `Min_Position_Limit = 0` y
     `Max_Position_Limit = max_res` (= 4095).
   - Lee `Present_Position` crudo de cada motor.
   - Calcula el offset de medio giro: `homing_offset = pos - int(max_res / 2)`
     (= `pos - 2047` en STS3215), de modo que la posición actual pase a reportar
     exactamente 2047 (el punto medio del encoder).
   - Escribe `Homing_Offset` por motor.

4. **Registro de rangos de movimiento** (`record_ranges_of_motion`, motors_bus.py:803),
   con excepción de `wrist_roll`:

   ```
   Move all joints except 'wrist_roll' sequentially through their entire ranges of motion.
   Recording positions. Press ENTER to stop...
   ```

   Con el torque deshabilitado, el método lee `Present_Position` en bucle, actualiza
   min/max de cada motor y muestra una tabla `NAME | MIN | POS | MAX` en vivo, hasta que se
   presiona Enter. Lanza `ValueError` si algún motor quedó con min == max (no se movió).
   Tras la grabación, `wrist_roll` se fuerza a `range_min = 0`, `range_max = 4095`
   (es un eje de giro continuo de vuelta completa).

5. **Persistencia**: construye el dict `MotorCalibration` con `drive_mode=0`, y:
   - `bus.write_calibration(self.calibration)` → escribe `Homing_Offset`,
     `Min_Position_Limit`, `Max_Position_Limit` en el EEPROM de cada motor.
   - `self._save_calibration()` → guarda el JSON en `calibration_fpath`.
   - Imprime `Calibration saved to <ruta>`.

### 4.5 CLI

**`lerobot-calibrate`** (`src/lerobot/scripts/lerobot_calibrate.py`, 110 líneas):

```bash
lerobot-calibrate \
    --robot.type=so101_follower \
    --robot.port=/dev/ttyACM0 \
    --robot.id=my_follower
```

- Requiere exactamente uno de `--teleop.*` o `--robot.*` (el `__post_init__` lanza
  `ValueError` si ambos o ninguno).
- Elige dispositivo con `make_robot_from_config` / `make_teleoperator_from_config`,
  conecta con `connect(calibrate=False)` (sin auto-calibración), ejecuta
  `device.calibrate()` y desconecta en `finally`.

**`lerobot-find-joint-limits`** (`lerobot_find_joint_limits.py`, 227 líneas): con
teleoperación, ejecuta un bucle de control (teleop → `send_action` → `get_observation`) y
aplica **cinemática directa** desde un URDF para medir los límites de las articulaciones y
los límites del efector final:

```bash
lerobot-find-joint-limits \
  --robot.type=so100_follower --robot.port=<port> --robot.id=black \
  --teleop.type=so100_leader --teleop.port=<port> --teleop.id=blue \
  --urdf_path=<ruta>/so101_new_calib.urdf \
  --target_frame_name=gripper \
  --teleop_time_s=30 --warmup_time_s=5 --control_loop_fps=30
```

- Fases: *warmup* (5 s sin registrar) y *recording* (30 s moviendo cada articulación a sus
  topes); imprime `max_ee`/`min_ee` (x, y, z) y `max_pos`/`min_pos` (radianes).
- El URDF recomendado es `Simulation/SO101/so101_new_calib.urdf` del repo
  TheRobotStudio/SO-ARM100.

**`lerobot-setup-motors`** (asociado, documentado en so101.mdx): asigna ID y baudrate a
cada motor en el EEPROM, conectando el controlador a **un solo motor a la vez**, fuera del
daisy-chain (ver [Particularidades](#47-particularidades)).

### 4.6 `is_calibrated` y desajuste en `connect()`

Al conectar, si no hay archivo de calibración o el archivo no coincide con los motores
(`is_calibrated` falso), `connect()` llama automáticamente a `calibrate()`, que despliega
la elección interactiva ENTER / 'c' descrita arriba. Esto permite:

- **Primera vez**: sin JSON → calibración completa (homing + rangos).
- **Cambio de firmware/reset de motores**: el desajuste de rangos/offsets dispara el
  diálogo; se puede restaurar desde el archivo (ENTER) o recalibrar ('c').

### 4.7 Particularidades

- **`wrist_roll` (id 5)**: rango forzado `[0, 4095]` en la calibración (giro completo).
- **Gripper (id 6)**: normalización `RANGE_0_100` y límites de seguridad especiales en
  `configure()` (ver [3.4](#34-configure)); el calibrado de su rango es igual al resto
  (homing + rangos), pero al ser una pinza el rango registrado corresponde a la apertura
  física, y `100` = abierta.
- **Espejo 1:1 entre leader y follower**: la calibración debe coincidir entre ambos brazos
  para que, en la misma posición física, reporten los mismos valores (esto es lo que permite
  que una política entrenada en un brazo funcione en otro, y que la teleoperación sea un
  mapeo directo).
- **`setup_motors()`** (so_follower.py:173): itera los motores en **orden inverso**
  (empieza por el `gripper`, id 6). Por cada uno pide conectar el controlador **solo a ese
  motor** y ejecuta `bus.setup_motor(motor)`, que escanea baudrate/ID (protocolo 0:
  `broadcast_ping`), deshabilita torque, escribe el ID destino y el `Baud_Rate` de 1 Mbaud
  en el EEPROM, y reconfigura el bus. Los motores nuevos vienen con ID 1; esta etapa es
  obligatoria una vez antes de armar el brazo.
- **Cámaras y observaciones**: la calibración de motores es independiente de las cámaras;
  el JSON solo contiene los 6 `MotorCalibration`.

### 4.8 Errores comunes

| Error / síntoma | Causa y tratamiento |
| --------------- | ------------------- |
| `Some Motors use different firmware versions` | Firmware no uniforme entre los 6 motores (`_assert_same_firmware`). Actualizar con el software de Feetech. |
| `Missing motor IDs` / `Motors with incorrect model numbers` | El `ping` del handshake no encontró un ID o el modelo no es `sts3215` (777). Revisar cableado, daisy-chain, alimentación y `--port`. |
| `Could not connect on port ...` (`ConnectionError`) | Puerto incorrecto o permisos; probar `lerobot-find-port` y, en Linux, `sudo chmod 666 /dev/ttyACM0`. |
| `Incorrect status packet!` | Paquete de estado corrupto en el `sync_read` (típico con varios ejes en movimiento). Mitigado por `num_read_retries` (2 por defecto). |
| Lecturas fuera de `[0, 4095]` o negativas | Bit 4 del registro `Phase` (0x12) activo; se limpia en `configure_motors()` para `sts3215`. |
| `connect()` calibra de forma inesperada | `connect()` asume el brazo en reposo y auto-calibra si `is_calibrated` es falso; usar `connect(calibrate=False)` para controlar el momento. |

---

## 5. Relevancia para el proyecto BrazoRobotico

El proyecto propio opera sobre el mismo hardware (bus Feetech STS3215, `scservo_sdk`) y ya
tiene dos herramientas en `STServo_Python/`:

- **`calibrate_servo_offset.py`** (194 líneas): calibra el cero (`OFS`, registro 31-32) y los
  límites de recorrido de **un servo a la vez**, por CLI. Detalles de implementación:
  desbloquea el EEPROM con `unLockEprom`, escribe offset (sign-magnitude con **bit de signo 11**,
  rango [−2047, 2047] según el datasheet `ST3215 memory register map`, addr 0x1F "Position
  correction") con `--target`/`--reset`, escribe límites con `--min`/`--max`, re-bloquea con
  `LockEprom` y verifica. Convención unificada con LeRobot:
  `Present_Position = Actual_Position - Homing_Offset (mod 4096)`. OJO: el signo y el bit son
  los de Feetech (bit 11); usar el bit 15 (convención de `Goal_Position`) rompe el homing
  (fue el bug detectado en la primera corrida real de `calibrate-arm.py`).
- **`servo_control_ui.py`** (449 líneas): GUI Tkinter con 6 sliders para comandar posición
  de los servos 1..6 sobre el bus (1 Mbaud, `/dev/ttyACM0`), con lectura de
  `Present_Position`/`Present_Speed` por `GroupSyncRead` en un hilo de fondo
  (`ServoBusWorker`), escrituras limitadas en tasa y cola thread-safe.

**Conceptos de LeRobot directamente aplicables al brazo propio:**

1. **Homing offset y límites** — la misma idea de `calibrate_servo_offset.py`; LeRobot
   agrega la calibración *por brazo completo* y persistida en JSON, que es la base para
   reproducibilidad entre sesiones.
2. **Normalización** — los modos `DEGREES`/`RANGE_0_100`/`RANGE_M100_100` son el puente
   entre cuentas de encoder crudas y unidades físicas (grados / porcentaje de apertura).
   Aplicar esta capa evita que la lógica de control dependa del montaje mecánico.
3. **PID y límites de seguridad** — ganancias P=16 / I=0 / D=32 y, sobre todo, los topes de
   torque/corriente/sobrecarga de la pinza son un patrón de seguridad que el proyecto propio
   puede replicar en el control de trayectorias (ROS 2).
4. **Bucle pasivo** — el follower de LeRobot es un dispositivo sin bucle propio; el control
   (frecuencia, procesado, clipping) vive fuera. Es una separación limpia que encaja con el
   diseño de un nodo de control en ROS 2.
5. **`max_relative_target`** — recorte de la acción relativa por seguridad; equivalente a
   una limitación de velocidad en el espacio de articulaciones.

---

## 6. Tabla de referencia rápida

**IDs y normalización de motores** (`SOFollower.__init__`):

| Motor | ID | Modelo | Norm mode |
| ----- | -- | ------ | --------- |
| `shoulder_pan` | 1 | `sts3215` | `DEGREES` (o `RANGE_M100_100`) |
| `shoulder_lift` | 2 | `sts3215` | ídem |
| `elbow_flex` | 3 | `sts3215` | ídem |
| `wrist_flex` | 4 | `sts3215` | ídem |
| `wrist_roll` | 5 | `sts3215` | ídem |
| `gripper` | 6 | `sts3215` | `RANGE_0_100` |

**Registros clave de la tabla de control STS3215** (`STS_SMS_SERIES_CONTROL_TABLE`):

| Registro | Dirección | Tamaño | Nota |
| -------- | --------- | ------ | ---- |
| `Firmware_Major_Version` | 0 | 1 | solo lectura |
| `Firmware_Minor_Version` | 1 | 1 | solo lectura |
| `Model_Number` | 3 | 2 | solo lectura; STS3215 = 777 |
| `ID` | 5 | 1 | EEPROM |
| `Baud_Rate` | 6 | 1 | 1 Mbaud = 0 |
| `Min_Position_Limit` | 9 | 2 | EEPROM |
| `Max_Position_Limit` | 11 | 2 | EEPROM |
| `Max_Torque_Limit` | 16 | 2 | EEPROM (gripper = 500) |
| `Phase` | 18 | 1 | bit 4 (0x10) se limpia |
| `P_Coefficient` | 21 | 1 | EEPROM (16) |
| `D_Coefficient` | 22 | 1 | EEPROM (32) |
| `I_Coefficient` | 23 | 1 | EEPROM (0) |
| `Protection_Current` | 28 | 2 | gripper = 250 |
| `Homing_Offset` | 31 | 2 | EEPROM; sign-magnitude bit 11 |
| `Operating_Mode` | 33 | 1 | POSITION = 0 |
| `Overload_Torque` | 36 | 1 | gripper = 25 |
| `Torque_Enable` | 40 | 1 | SRAM |
| `Acceleration` | 41 | 1 | SRAM |
| `Goal_Position` | 42 | 2 | SRAM; sign-magnitude bit 15 |
| `Lock` | 55 | 1 | 0 = desbloquea EEPROM |
| `Present_Position` | 56 | 2 | solo lectura; sign-magnitude bit 15 |
| `Maximum_Acceleration` | 85 | 1 | fábrica (254) |

**Comandos CLI**:

| Comando | Uso |
| ------- | --- |
| `lerobot-calibrate --robot.type=so101_follower --robot.port=<port> --robot.id=<id>` | Calibración del follower |
| `lerobot-find-joint-limits ... --urdf_path=<urdf> --target_frame_name=gripper` | Límites de articulación y efector |
| `lerobot-setup-motors --robot.type=so101_follower --robot.port=<port>` | IDs y baudrate (un motor a la vez) |
| `lerobot-teleoperate --robot.type=so101_follower ... --teleop.type=so101_leader ...` | Bucle de teleoperación |

**Rutas**:

| Elemento | Ruta |
| -------- | ---- |
| JSON de calibración | `~/.cache/huggingface/lerobot/calibration/robots/so_follower/<id>.json` (o `$HF_LEROBOT_CALIBRATION/...`) |
| Clase del follower | `lerobot/src/lerobot/robots/so_follower/so_follower.py` |
| Config del follower | `lerobot/src/lerobot/robots/so_follower/config_so_follower.py` |
| Clase base `Robot` | `lerobot/src/lerobot/robots/robot.py` |
| Bus de motores (normalización, calibración a nivel bus) | `lerobot/src/lerobot/motors/motors_bus.py` |
| Driver Feetech | `lerobot/src/lerobot/motors/feetech/feetech.py` |
| Tablas de control STS/SCS | `lerobot/src/lerobot/motors/feetech/tables.py` |
| CLI de calibración | `lerobot/src/lerobot/scripts/lerobot_calibrate.py` |
| CLI de límites | `lerobot/src/lerobot/scripts/lerobot_find_joint_limits.py` |
| Guía oficial SO-101 | `lerobot/docs/source/so101.mdx` |

---

## 7. Referencias

Repositorio LeRobot local (`/home/ezequiel/proyecto-final/lerobot`):

- `src/lerobot/robots/so_follower/so_follower.py` — `SOFollower`, aliases, ciclo de vida,
  `calibrate()`, `configure()`, `get_observation()`, `send_action()`.
- `src/lerobot/robots/so_follower/config_so_follower.py` — `SOFollowerConfig`,
  `SOFollowerRobotConfig`, registro `so101_follower`/`so100_follower`.
- `src/lerobot/robots/robot.py` — clase base `Robot`, persistencia de calibración.
- `src/lerobot/robots/utils.py` — `make_robot_from_config`, `ensure_safe_goal_position`.
- `src/lerobot/robots/bi_so_follower/bi_so_follower.py` — `BiSOFollower`.
- `src/lerobot/motors/motors_bus.py` — `MotorCalibration`, `MotorNormMode`, `_normalize`/
  `_unnormalize`, `set_half_turn_homings`, `record_ranges_of_motion`, `sync_read`/`sync_write`,
  torque.
- `src/lerobot/motors/feetech/feetech.py` — `FeetechMotorsBus`, handshake, monkeypatch,
  calibración a nivel bus, torque.
- `src/lerobot/motors/feetech/tables.py` — tablas de control, baudrates, resolución,
  encoding.
- `src/lerobot/motors/encoding_utils.py` — `encode/decode_sign_magnitude`.
- `src/lerobot/utils/constants.py` — `HF_LEROBOT_CALIBRATION`, `ROBOTS`.
- `src/lerobot/scripts/lerobot_calibrate.py`, `lerobot_find_joint_limits.py`,
  `lerobot_teleoperate.py`.
- `docs/source/so101.mdx` — guía oficial de armado, setup de motores y calibración.

Proyecto BrazoRobotico (`/home/ezequiel/proyecto-final/BrazoRobotico`):

- `STServo_Python/calibrate_servo_offset.py` — calibración de offset/límites por servo.
- `STServo_Python/servo_control_ui.py` — GUI de control de posición de 6 servos.
