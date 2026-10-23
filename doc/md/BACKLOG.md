# Backlog de Tareas

_Última actualización: 2026-10-23_

## Cómo usar este documento
- Agregar tareas nuevas bajo la categoría correspondiente (o crear una nueva sección si hace falta).
- Marcar como hecha cambiando `[ ]` por `[x]`.
- Mover tareas completadas a la sección "Hecho" si querés mantener el backlog activo limpio.

---

## 🔧 ESP32 / Embedded Systems

- [x] **[E01]** Programar ESP32 para poder enviar y recibir mensajes por puerto serie a través de los pines TX-RX
- [x] **[E02]** Medir niveles de las señales que salen por TX y RX del ESP32 usando el analizador lógico
- [x] **[E03]** Averiguar si es necesario un adaptador lógico entre el ESP32 y el driver de los servomotores; si se necesita, conseguir uno — ver `doc/md/bitacora-E03-adaptador-logico.md` (no hace falta, ambos son 3.3V)
- [x] **[E04]** Averiguar cómo comandar el driver de motores vía TX-RX — ver `doc/md/bitacora-E04-protocolo-driver.md` (protocolo Feetech/Dynamixel-like, header 0xFF 0xFF + ID + length + instrucción + parámetros + checksum)
- [x] **[E05]** Probar el código del ESP32 para manejar TX-RX con un Arduino Nano; programar el Arduino Nano para que pueda enviar y recibir mensajes por los pines TX-RX
- [x] **[E06]** (Depende de E01) Hacer un código en el ESP32 que traduzca los comandos recibidos de la computadora principal en los comandos que precisa el driver de los motores; estudiar el código Python de ejemplo. Alcance: manejo del comando `M` — parsear las tramas de la PC (protocolo de `[R09]`), convertir mrad → cuentas con la calibración, enviar `SYNC_WRITE` al bus de servos, leer las posiciones reales y responder con una `M` de feedback — ver `doc/md/bitacora-E06-comando-m.md`. Probado con el brazo real: vuelta completa PC → ESP32 → servos → PC, 250/250 respuestas a 50 Hz, 5 joints convergen a ±1° (el `wrist_roll` queda para `[R08]`: calibración)
- [x] **[E07]** Evaluar si hace falta realmente desacoplar la gestión del driver de motores hacia el ESP32, o si está bien como está — ver `doc/md/bitacora-R09-ros2-control.md` (sí: protocolo propio PC↔ESP32 por display/botones, parada de emergencia latcheada y calibración en el ESP32)
- [x] ~~**[E08]** (Depende de E07) Hacer un nuevo script de Python para delegar la parte de gestión del driver de motores al ESP32~~ — **cancelada**: la delegación al ESP32 ya la hace el plugin C++ de `[R09]` (tramas `M`); lo que falta del lado ESP32 es `[E06]`
- [ ] **[E09]** (Depende de E06) Completar el firmware del ESP32 según la arquitectura de `[R03]`: máquina de estados (incluido `ENSEÑANZA`), parada de emergencia latcheada, comandos `E`/`S`/`D`, LCD 20x4, botones y potenciómetro del gripper — ver `doc/md/bitacora-R03-arquitectura.md`. Ya hecho en `[R08]`: estado DETENIDO/OPERANDO mínimo, `S` (sin la respuesta `E`) y watchdog de enlace (`main/arm_state.{h,c}`). Falta también: ir a un "home" válido al arrancar (en reposo el brazo queda fuera de los límites del URDF y MoveIt no planifica desde ahí)
- [x] **[E10]** (Depende de E06, R08) Incorporar a `firmware/esp32_arm` el panel de `pot_nav_lcd_idf` (LCD 1602 I2C + potenciómetro + pulsador): la pantalla JOINTS mueve el brazo real como otra fuente de control (NAV elige el joint, CTL lo mueve con el potenciómetro). Con una sesión de ROS activa (`OPERANDO`) el menú sigue habilitado, HOME muestra "TAREA EN CURSO" y JOINTS es de solo lectura — ver `doc/md/bitacora-E10-panel-lcd.md`. Probado con el brazo real: CTL sin PC en los 6 joints, plan & execute con "TAREA EN CURSO" y JOINTS en LEC, límites de `S` iguales al URDF, arranque rechazado con `E 01` si el panel está en CTL y vuelta a HOME al cortar el plugin. Pendiente en `[E11]`
- [ ] **[E11]** (Depende de E10) Evitar el salto al entrar a CTL: si un joint está en reposo fuera de los límites del URDF, al entrar a CTL salta de golpe hacia adentro (3°–12° en las pruebas), contra DS13 (entrar a CTL no mueve el brazo). Evaluar junto con el "home" válido al arrancar de `[E09]` — ver `doc/md/bitacora-E10-panel-lcd.md`

## 🦾 Brazo Robótico

- [x] **[R01]** Averiguar si el proyecto armso-101 (en el cual está basado mi proyecto) tiene un URDF para simular con Gazebo ROS2 — ver `doc/md/bitacora-R01-urdf.md`
- [x] **[R02]** (Depende de R01) Si tiene URDF: tratar de levantar el modelo en Gazebo. Si no tiene: ver cómo generarlo usando los STL disponibles — ver `doc/md/bitacora-R02-gazebo.md`. Pendiente no bloqueante: geometría de `<collision>` (STL → primitivas, `dartsim` no soporta colisión desde malla)
- [ ] **[R03]** Definir la arquitectura ROS2 de la aplicación: paquetes, nodos, interfaces entre ellos (topics, servicios, actions) y diferencias entre simulación y robot real, tomando como base el curso de manipuladores con ROS2
- [ ] **[R04]** Investigar/implementar mitigación de backlash mecánico de los servos STS3215 (compensación por software al invertir dirección, ya que el encoder está en el eje del motor y no en el eslabón de salida)
- [x] **[R05]** Implementar MoveIt2 para planificación de movimiento del brazo — referencia: `/home/ezequiel/cursos/Robotics-and-ROS2-Manipulators/Robotics-and-ROS-2-Learn-by-Doing-Manipulators/Section6_Kinematics/arduinobot_ws` — ver `doc/md/bitacora-R05-moveit2.md`. Plan & Execute verificado end-to-end en Gazebo (grupos `arm` y `gripper`)
- [x] **[R06]** Aprender el manejo de Actions en ROS2 (goal/result/feedback, action server y client, patrón Future) — referencia: ejemplo Fibonacci en `/home/ezequiel/cursos/Robotics-and-ROS2-Manipulators/Robotics-and-ROS-2-Learn-by-Doing-Manipulators/Section7_Application/arduinobot_ws` — ver `doc/md/bitacora-R06-actions.md`
- [x] **[R07]** (Depende de R05, R06) Implementar API para manejar MoveIt2 (interfaz programática para enviar goals de planificación/ejecución al brazo, en vez de operarlo solo desde RViz) — ver `doc/md/bitacora-R07-moveit-api.md`. `task_server` verificado end-to-end en Gazebo (tareas 0-6)
- [x] **[R08]** (Depende de R07) Ejecutar el `task_server`/API de MoveIt2 sobre el brazo real — recalibrar o verificar que el "0" físico de los servos coincida con el "0" del modelo URDF antes de correr goals planificados en hardware — ver `doc/md/bitacora-R08-brazo-real.md`. Hecho: brazo recalibrado contra el cero del URDF, arranque seguro (estado DETENIDO + `S`), plan & execute desde RViz y las 7 tareas del `task_server` con `SUCCEEDED` sobre el brazo real
- [ ] **[R09]** (Depende de R05) Implementar en C++ el plugin `ros2_control` (`hardware_interface::SystemInterface`) para el brazo real — comunicación directa por puerto serie reusando la lógica de `STServo_Python`, dejando la interfaz reemplazable más adelante por una variante con ESP32 (ver `[E07]`)
- [x] **[R10]** Crear los nodos `simple_serial_transmitter` y `simple_serial_receiver` en Python (topic ↔ puerto serie) igual que en el curso — referencia: `/home/ezequiel/cursos/Robotics-and-ROS2-Manipulators/Robotics-and-ROS-2-Learn-by-Doing-Manipulators/Section9_Build/arduinobot_ws/src/arduinobot_firmware/arduinobot_firmware` — ver `doc/md/bitacora-R10-serial-nodes.md`
- [x] **[R11]** Estudiar cómo funciona un lifecycle node (managed node) en ROS2 — ver `doc/md/bitacora-R11-lifecycle-node.md`
- [x] **[R12]** Crear el paquete `robotic_arm_bringup` con `simulated_robot.launch.py` y `real_robot.launch.py`, que incluyan los launch de description (solo sim), controller, moveit y remote pasándoles `is_sim` — referencia: `/home/ezequiel/cursos/Robotics-and-ROS2-Manipulators/Robotics-and-ROS-2-Learn-by-Doing-Manipulators/Section9_Build/arduinobot_ws/src/arduinobot_bringup` — ver `doc/md/bitacora-R12-bringup.md`. Simulado verificado end-to-end (RViz + `task_server`); `real_robot.launch.py` sin probar hasta `[E06]`
- [x] **[R13]** (Depende de R09) Hacer configurable el puerto serie del plugin: hoy `/dev/ttyACM0` está fijo en `robotic_arm_ros2_control.xacro`, pero el nombre depende del chip USB de la placa (CH9102X → `/dev/ttyACM0`, CP2102 → `/dev/ttyUSB0`) y del orden de conexión. Opciones: argumento del launch o regla udev con nombre fijo (p. ej. `/dev/robotic_arm`) — ver `doc/md/bitacora-E06-comando-m.md`. Hecho con argumento del launch `port` (default `/dev/ttyUSB0`) — ver `doc/md/bitacora-R08-brazo-real.md`; la regla udev queda para la computadora integrada
- [ ] **[R14]** (Depende de R08) Alinear los límites de `robotic_arm_ros2_control.xacro` (`<param name="min">`/`max` de cada joint) con los del URDF corregidos en `[R08]` (hoy repiten los valores viejos, p. ej. `elbow_flex` ±1.69) — ver `doc/md/bitacora-R08-brazo-real.md`
- [ ] **[R15]** (Depende de R08) Recalibración más precisa del brazo: con la pose a ojo quedaron `wrist_roll` ~8° y `shoulder_pan` ~5° corridos del cero del URDF. Usar una referencia mejor (plantilla, escuadra, marcas en las piezas); si es un solo joint, `calibrate_servo_offset.py` corriendo también los límites de la EEPROM — ver `doc/md/bitacora-R08-brazo-real.md`
- [ ] **[R16]** (Depende de R13) Regla udev para el ESP32 (`/dev/robotic_arm`, por número de serie USB: el adaptador CP2102 tiene el mismo VID:PID que la DevKitC WROOM) y default del argumento `port` a ese nombre, para la computadora integrada

## 📌 Otros

- [ ] (agregar tareas aquí)

---

## ✅ Hecho

- [x] (ejemplo de tarea completada)
