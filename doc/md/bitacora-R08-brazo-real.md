# Bitácora [R08] — `task_server` / MoveIt2 sobre el brazo real

## Objetivo

Ejecutar el `task_server` (API de MoveIt2, `[R07]`) sobre el brazo real, a través del plugin `ros2_control` de `[R09]` y el firmware del ESP32 de `[E06]`.

## Contexto

- `[R07]`: `task_server` verificado end-to-end en Gazebo (tareas 0–6). Quedó anotado como bloqueante: la calibración de los servos (`STServo_Python/calibrate-arm.py`, cero = 2048 cuentas) se hizo **sin mirar el URDF**. MoveIt piensa en ángulos del URDF; el brazo real interpreta los mismos números con **su** cero. Si no coinciden, un goal correcto en Gazebo termina en otra pose física (riesgo de choque).
- `[E06]`: vuelta completa PC → ESP32 → servos → PC verificada con el brazo (250/250 respuestas a 50 Hz, 5 joints convergen a ±1° del cero de los servos).
- `[R05]`: los límites del URDF se verificaron con medición física relativa al cero calibrado (herramienta "Medicion de limites (URDF)" en `STServo_Python/servo_control_ui.py`).

## Pendientes técnicos heredados

1. **`wrist_roll` (servo 5)** — ver "Prueba de convergencia" en `doc/md/bitacora-E06-comando-m.md`:
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

## Pendientes

- [ ] Comparar cero y sentido de cada joint: URDF (RViz) vs brazo real
- [ ] Recalibrar los joints que no coincidan (`wrist_roll` incluido)
- [ ] `[R13]`: puerto serie configurable
- [ ] Plugin: tolerar el reset del ESP32 al abrir el puerto
- [ ] `real_robot.launch.py`: plan & execute desde RViz
- [ ] `task_server` sobre el brazo real
