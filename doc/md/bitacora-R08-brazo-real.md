# Bitácora [R08] — `task_server` / MoveIt2 sobre el brazo real

## Objetivo

Ejecutar el `task_server` (API de MoveIt2, `[R07]`) sobre el brazo real, a través del plugin `ros2_control` de `[R09]` y el firmware del ESP32 de `[E06]`.

## Contexto

- `[R07]`: `task_server` verificado end-to-end en Gazebo (tareas 0–6). Quedó anotado como bloqueante: la calibración de los servos (`STServo_Python/calibrate-arm.py`, cero = 2048 cuentas) se hizo **sin mirar el URDF**. MoveIt piensa en ángulos del URDF; el brazo real interpreta los mismos números con **su** cero. Si no coinciden, un goal correcto en Gazebo termina en otra pose física (riesgo de choque).
- `[E06]`: vuelta completa PC → ESP32 → servos → PC verificada con el brazo (250/250 respuestas a 50 Hz, 5 joints convergen a ±1° del cero de los servos).
- `[R05]`: los límites del URDF se verificaron con medición física relativa al cero calibrado (herramienta "Medicion de limites (URDF)" en `STServo_Python/servo_control_ui.py`).

## Pendientes técnicos heredados

1. ~~**`wrist_roll` (servo 5)**~~ — **resuelto con la recalibración** (ver "Paso 2"). Problema original, ver "Prueba de convergencia" en `doc/md/bitacora-E06-comando-m.md`:
   - el cero calibrado (2048) cae detrás de un tope mecánico del antebrazo (~907 cuentas): sobrecarga (`0x20`) si se le manda el cero;
   - sin límites en la EEPROM (`calibrate-arm.py` fuerza `[0, 4095]` salvo `--record-wrist-roll`);
   - OFS = 1182: el rango físico cruza el corte de coordenadas → el servo puede ir por el camino largo y el feedback salta de +π a −π (2π para `ros2_control`).
   - En el URDF es `revolute` con límites `[-2.74385, 2.84121]` rad.
2. **`[R13]`** — puerto fijo `/dev/ttyACM0` en `robotic_arm_ros2_control.xacro`; la DevKitC WROOM aparece como `/dev/ttyUSB0`.
3. **Reset al abrir el puerto** — el plugin, al activarse, reinicia el ESP32: ~300 ms sin respuestas. `read()` no debe tratarlo como falla.

## Plan

1. **Comparar los dos ceros** (sin tocar nada): RViz con todos los joints en 0 al lado del brazo real en su pose cero. Por joint: ¿coincide la pose? ¿coincide el **sentido** de giro?
2. **Recalibrar** lo que no coincida (incluido `wrist_roll` con `--record-wrist-roll`), con la pose cero del URDF como referencia.
3. **`[R13]`** y **tolerancia al reset** en el plugin.
4. **`real_robot.launch.py`**: primero RViz (plan & execute de un movimiento chico), después el `task_server`.

## Paso 1 — Comparar los ceros (URDF vs brazo real)

RViz (`ros2 launch robotic_arm_description display.launch.py`, todos los joints en 0) vs brazo real en su cero (`send_m.py ... 0 0 0 0 -1750 0`): `doc/img/robotic-arm-rviz-zero.png`, `doc/img/robotic-arm-real-zero.jpeg`.

- `shoulder_lift`, `elbow_flex`, `wrist_flex` (y aparentemente `shoulder_pan`): coinciden.
- `wrist_roll`: con el servo en **−1750 mrad** la muñeca ya tenía la orientación del URDF en 0 → el cero del URDF estaba a ~−1.75 rad del cero calibrado.
- `gripper`: en 0 el real estaba bastante más abierto que el URDF.

**Causa de fondo:** `calibrate-arm.py` define el cero (HOMING) como *"every joint at the middle of its travel, gripper half open"* (convención de LeRobot). El cero de MoveIt es el del URDF. Coinciden donde la mitad del recorrido es la pose del URDF (brazo vertical, antebrazo horizontal, muñeca recta); no en el gripper (URDF 0 = casi cerrado, rango −0.17…1.74 rad) ni en el `wrist_roll` (casi una vuelta: la "mitad" es arbitraria y quedó detrás del tope).

## Paso 2 — Recalibración con la pose cero del URDF

`calibrate-arm.py --record-wrist-roll` (jumper del driver en B, USB). En HOMING, los 6 joints a la vez copiando la pose de RViz (antebrazo apoyado para que quede horizontal, gripper casi cerrado); en RANGES, cada joint de tope a tope, `wrist_roll` incluido. Se hizo dos veces (la segunda llevando el codo más lejos); los offsets variaron 4–83 cuentas (0.4°–7°): medición repetible.

Calibración final (`STServo_Python/calibration.json`), rangos en rad respecto del cero (2048):

| Joint | Offset | Rango (cuentas) | Rango (rad) | Límite URDF | |
|---|---|---|---|---|---|
| `shoulder_pan` | −1277 | 753 – 3467 | −1.99 … +2.18 | ±1.92 | ✅ |
| `shoulder_lift` | −798 | 870 – 3198 | −1.81 … +1.76 | ±1.75 | ✅ (~1° de margen arriba) |
| `elbow_flex` | 1415 | 944 – 3111 | −1.69 … **+1.63** | ±**1.69** | ⚠️ no llega arriba |
| `wrist_flex` | −1999 | 861 – 3206 | −1.82 … +1.78 | ±1.66 | ✅ |
| `wrist_roll` | −1833 | 97 – 3857 | −2.99 … **+2.78** | −2.74 … **+2.84** | ⚠️ no llega arriba |
| `gripper` | −1797 | 1917 – 3350 | −0.20 … +2.00 | −0.17 … +1.75 | ✅ |

- **`wrist_roll` resuelto:** rango dentro de 0..4095 sin cruzar el corte (el salto 0/4095 cae en la zona del tope, inalcanzable) → sin salto de ±π ni camino largo; **límites en la EEPROM** → ya no empuja contra el tope. El offset cambió de +1182 a −1833 (≈ −1081 cuentas mod 4096 ≈ −1.66 rad), consistente con el ~−1.75 rad estimado en el paso 1.
- **`gripper`:** cero casi cerrado (puede cerrar 0.20 rad más), como el URDF. El offset cambió −537 cuentas (≈ 47°: de medio abierto a casi cerrado).
- **`elbow_flex` y `wrist_roll`:** el URDF es más optimista que el brazo por arriba (en dos calibraciones el codo dio +1.60 y +1.63). Corregir el URDF (no recalibrar) para que MoveIt planifique dentro de lo alcanzable, con margen.

## Paso 3 — Verificación (sin mover servos)

`servo_control_ui.py`, modo Free, herramienta "Medicion de limites (URDF)": brazo puesto a mano en la pose de RViz y cada joint movido para comparar el sentido con RViz.

| Joint | En la pose de RViz | Desvío | Sentido igual que RViz |
|---|---|---|---|
| `shoulder_pan` | +0.092 rad | +5.3° | sí |
| `shoulder_lift` | −0.011 rad | −0.6° | sí |
| `elbow_flex` | +0.072 rad | +4.1° | sí |
| `wrist_flex` | −0.015 rad | −0.9° | sí |
| `wrist_roll` | −0.137 rad | −7.8° | sí |
| `gripper` | ~0.009 rad | ~0.5° | sí |

- **Los 6 sentidos coinciden:** `drive_mode` 0 en todos es correcto, no hay que invertir ninguno.
- Los desvíos mezclan el error de la calibración y el de volver a poner la pose a ojo para medir. 5–8° no generan choques: alcanza para empezar con MoveIt. Si el pick & place necesita más precisión, afinar con una referencia mejor que el ojo (plantilla, escuadra).

## Paso 4 — Arranque seguro del plugin (reset del ESP32 y pose inicial)

**Revisión del plugin:** `read()` sin bytes devuelve `OK` y conserva la última posición: los ~300 ms del reset del ESP32 al abrir el puerto no desactivan el hardware. **Pero** `on_activate()` ponía `position_commands_` y `position_states_` en cero: `write()` manda una `M` cada ciclo con esos ceros y el `joint_trajectory_controller` se activa sosteniendo el estado leído (ceros, todavía no llegó feedback). Al lanzar `real_robot.launch.py` el brazo **salía solo hacia la pose cero** desde donde estuviera (riesgo de choque).

**Solución** (versión mínima del arranque de `[R03]`: estado DETENIDO + comando `S`; el resto, en `[E09]`):

- **Firmware** — módulo puro `main/arm_state.{h,c}`:
  - arranca **DETENIDO**: las `M` solo leen los servos y responden el feedback, **no mueven**;
  - **`S` (start)** → **OPERANDO**: las `M` mueven como antes;
  - **watchdog de enlace**: en OPERANDO, más de 500 ms sin tramas → DETENIDO hasta otra `S`. Si el plugin se cae o se desenchufa el cable, el brazo nunca retoma objetivos viejos; cubre también el caso en que abrir el puerto no resetea el ESP32.
  - Log de cada `M`: `M (move)` o `M (stopped, read only)`.
  - Tests: `test_host/test_arm_state.c` (arranca detenido y las tramas solas no lo arrancan; `S` y `S` repetida; 50 Hz se mantiene; watchdog y nueva `S`; borde exacto del timeout; secuencia completa del arranque del plugin).
- **Plugin** — `on_activate()` → `syncWithArm()`: abre el puerto, espera 700 ms (arranque del ESP32 y más que el watchdog), descarta lo recibido (ROM), manda `M` cada 20 ms hasta el primer feedback (máx. 2 s; si no llega → `FAILURE` con la sugerencia de qué revisar y el hardware no se activa), inicializa estados **y** comandos con la pose real (la loguea) y manda `S`. Armado de tramas separado en `buildFrame()` / `buildMPayload()`.
- **`tools/send_m.py`**: manda `S` antes de las `M`; `--read-only` no la manda (solo lee posiciones).

### Pruebas del arranque seguro

Jumper en A, driver alimentado, ESP32 en `/dev/ttyUSB0`.

1. **Firmware sin `S`** (`send_m.py --read-only ... 0 0 0 0 0 0`): pidiéndole el cero, el brazo **no se movió**; las respuestas mostraron su pose real (reposo plegado: `+0002 -1799 +1634 -1767 -0005 -0199` mrad).
2. **Firmware con `S`** (`send_m.py ... 0 -1500 1400 -1500 0 0`): fue a la pose pedida con error ≤ 0.7° (`+0003 -1506 +1413 -1502 -0005 -0003`), 150/150 respuestas.
3. **`ros2_control` solo** (`ros2 launch robotic_arm_controller controller.launch.py is_sim:=False port:=/dev/ttyUSB0`):
   - `Arm pose at startup (rad): 0.002 -1.508 1.417 -1.502 -0.003 -0.003` ~0.72 s después de `Starting robot hardware` (700 ms de espera + primer feedback);
   - los 3 controllers cargados y activos sin errores;
   - `/joint_states` coincide joint por joint (viene en orden alfabético; los valores corresponden por nombre);
   - **el brazo no se movió**: quedó rígido en su pose.

**Pose de reposo fuera de los límites del URDF:** plegado contra los topes, el brazo queda con `shoulder_lift` −1.799, `elbow_flex` +1.634, `wrist_flex` −1.767 y `gripper` −0.199, todos un poco afuera de los límites (que están 0.05 rad antes de los topes a propósito). `ros2_control` arranca igual, pero **MoveIt rechazaría planificar** (estado inicial fuera de límites). Antes de lanzar MoveIt, llevar el brazo a una pose dentro de los límites (p. ej. `send_m.py ... 0 -1500 1400 -1500 0 0`). Más adelante, el `arm_supervisor` de `[R03]` debería ir a un "home" válido al arrancar (`[E09]`).

**`update_rate` en 10 Hz:** el log mostró `update rate is 10 Hz` (`robotic_arm_controllers.yaml`, heredado del curso). Todo el diseño asume 50 Hz (`[R09]`: una `M` por ciclo). Se subió a 50: el firmware lo aguantó en las pruebas de convergencia (250/250 a 50 Hz con logs de consola) y el ancho de banda sobra (~2.2 kB/s por sentido, < 20% de 115200). Si el ESP32 se atrasa con dos líneas de log por ciclo, pasar esos logs a `DEBUG`.

## Paso 5 — Primer movimiento con MoveIt (`real_robot.launch.py`)

`ros2 launch robotic_arm_bringup real_robot.launch.py port:=/dev/ttyUSB0` con el brazo en una pose dentro de los límites (`send_m.py ... 0 -1500 1400 -1500 0 0`). Levanta `ros2_control` (plugin), MoveIt (`move_group`), RViz y el `task_server` (queda esperando goals).

- **Velocidad:** el firmware limita los servos a 300 pasos/s ≈ 0.46 rad/s (`ARM_SERVO_MOVE_SPEED`); MoveIt planifica con ~4.7 rad/s de `joint_limits.yaml` × el velocity scaling de RViz. Si planifica más rápido de lo que el servo sigue, el brazo se atrasa y la ejecución puede fallar por tolerancia. Primera prueba con velocity/acceleration scaling **0.05** (0.1 ≈ el límite de los servos).
- **Prueba:** grupo `arm`, start state `<current>`, objetivo arrastrando el marcador interactivo (IK de posición, `kdl_kinematics_plugin`), Plan → Execute.

Del log de `move_group`:

- plan con OMPL (`RRTConnect`) en 0.024 s, `Motion plan was computed successfully`;
- `Validating trajectory with allowed_start_tolerance 0.01` pasó: **el estado inicial de MoveIt coincidía con el brazo real a menos de 0.01 rad** (calibración + feedback + arranque seguro cerrando);
- `arm_controller: Goal reached, success!` y `Completed trajectory execution with status SUCCEEDED` (~5 s de ejecución).

**Primer movimiento del brazo real comandado por MoveIt:** RViz → `move_group` → `arm_controller` → plugin (`M` por USB) → ESP32 (`SYNC_WRITE`) → servos, y el feedback (`SYNC_READ` → `M` → `read()` → `/joint_states`) de vuelta hasta RViz. Junta todo lo de `[E06]`, `[R09]` y este `[R08]`.

## Paso 6 — `task_server` sobre el brazo real

**Velocidad del `task_server`:** `planning_python_api.yaml` tenía `max_velocity_scaling_factor` / `max_acceleration_scaling_factor` en **1.0** → planificaba a 4.24 rad/s (`joint_limits.yaml`), 10 veces lo que el firmware deja a los servos (0.46 rad/s). Físicamente no es peligroso (el firmware limita), pero MoveIt cree que el movimiento termina antes de que el brazo llegue y la tarea siguiente arranca fuera del `allowed_start_tolerance` (0.01): la secuencia se rompe. Pasados a **0.1** (0.42 rad/s, como el default de RViz en `joint_limits.yaml`). En Gazebo las tareas también van más lentas, coherentes con el real.

**Prueba:** `real_robot.launch.py port:=/dev/ttyUSB0` (brazo en pose válida, espacio revisado: las tareas 2/3/5/6 giran la base ±1.36 rad y la 3 baja hacia la mesa) y las tareas de a una con `ros2 action send_goal /task_server robotic_arm_msgs/action/RoboticArmTask "{task_number: N}"`, N = 0…6.

**Resultado: las 7 tareas terminaron `SUCCEEDED`** (pose cero, posición inicial, girar y abrir, agarrar, trasladar al centro, girar al otro lado, soltar). **`[R08]` cumplida.**

**Pendientes que pasan al backlog** (no bloquean `[R08]`): `[R14]` límites duplicados en `robotic_arm_ros2_control.xacro`, `[R15]` recalibración más precisa, `[R16]` regla udev; el "home" automático al arrancar se agregó a `[E09]`.

## Pendientes

- [x] Comparar cero y sentido de cada joint: URDF (RViz) vs brazo real
- [x] Recalibrar con la pose cero del URDF (`--record-wrist-roll`): 6 sentidos correctos, ceros a ≤8°
- [x] Límites del URDF dentro del rango físico con 0.05 rad de margen (`robotic_arm.urdf.xacro`): `elbow_flex` −1.64…+1.58 (era ±1.69), `wrist_roll` upper +2.72 (era +2.841), `shoulder_lift` upper +1.71 (era +1.745). El resto ya cumplía; el `gripper` lower queda con 0.03 (cerrar del todo no tiene riesgo). Las tareas del `task_server` siguen dentro (máx. `elbow_flex` +1.434, `wrist_roll` −1.416). Verificado con `xacro` + `check_urdf`
- [x] `servo_control_ui.py`: botón `Home (cero)` manda a `ZERO_COUNTS` (2048) en vez del 50% del rango (ya no es el cero: el gripper queda ~9% de su slider). `calibrate-arm.py`: docstring y mensaje del HOMING → pose cero del URDF, los 6 joints a la vez. Probado: Home lleva el brazo a la pose de RViz; el `wrist_roll` queda ~8° torcido (el −7.8° de la verificación)
- [ ] → `[R15]` **Recalibración completa más precisa** (más adelante): el `wrist_roll` quedó ~8° corrido y `shoulder_pan` ~5°, por acomodar la pose a ojo. Hacerla con una referencia mejor (plantilla, escuadra, marcas en las piezas). Si solo hace falta un joint, `calibrate_servo_offset.py --id N --target 2048 --min .. --max ..` sirve, pero los límites de la EEPROM están en coordenadas del cero viejo: hay que correrlos lo mismo que el cero, en el mismo comando, y actualizar `calibration.json'
- [x] `[R13]`: puerto serie configurable con argumento del launch `port` (default `/dev/ttyUSB0`, la DevKitC WROOM): `real_robot.launch.py port:=...` → `controller.launch.py` → `xacro ... port:=...` → `<param name="port">` → plugin (el C++ ya lo leía de `hardware_parameters`). Verificado: `xacro` con y sin `port:=`, y `--show-args` en los dos launch. La regla udev (`/dev/robotic_arm`, por número de serie USB: el CP2102 del adaptador a comprar tiene el mismo VID:PID que la WROOM) queda para la computadora integrada
- [ ] → `[R14]` `robotic_arm_ros2_control.xacro` repite los límites de cada joint (`<param name="min">`/`max`) con los valores viejos (p. ej. `elbow_flex` ±1.69): alinearlos con el URDF
- [x] Arranque seguro (sin movimiento al activar `ros2_control`) — ver "Paso 4"
- [x] Probar el arranque seguro con el brazo (ver "Pruebas del arranque seguro")
- [x] `update_rate` de `controller_manager` de 10 a 50 Hz (`robotic_arm_controllers.yaml`)
- [x] `real_robot.launch.py`: plan & execute desde RViz (ver "Paso 5")
- [x] `task_server` sobre el brazo real: las 7 tareas `SUCCEEDED` (ver "Paso 6")
