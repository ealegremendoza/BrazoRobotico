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

## Pendientes

- [x] Comparar cero y sentido de cada joint: URDF (RViz) vs brazo real
- [x] Recalibrar con la pose cero del URDF (`--record-wrist-roll`): 6 sentidos correctos, ceros a ≤8°
- [x] Límites del URDF dentro del rango físico con 0.05 rad de margen (`robotic_arm.urdf.xacro`): `elbow_flex` −1.64…+1.58 (era ±1.69), `wrist_roll` upper +2.72 (era +2.841), `shoulder_lift` upper +1.71 (era +1.745). El resto ya cumplía; el `gripper` lower queda con 0.03 (cerrar del todo no tiene riesgo). Las tareas del `task_server` siguen dentro (máx. `elbow_flex` +1.434, `wrist_roll` −1.416). Verificado con `xacro` + `check_urdf`
- [ ] `servo_control_ui.py`: botón Home a 2048 (hoy manda al 50% del rango, que ya no es el cero); `calibrate-arm.py`: texto del HOMING → pose cero del URDF
- [ ] `[R13]`: puerto serie configurable
- [ ] Plugin: tolerar el reset del ESP32 al abrir el puerto
- [ ] `real_robot.launch.py`: plan & execute desde RViz
- [ ] `task_server` sobre el brazo real
