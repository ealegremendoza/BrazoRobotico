# Bitácora [R03] — Arquitectura ROS2 de la aplicación

_Inicio: 2026-09-25_

## Contexto

Definir la arquitectura ROS2 de la aplicación: paquetes, nodos, interfaces entre ellos (topics, servicios, actions) y diferencias entre simulación y robot real, tomando como base el curso de manipuladores con ROS2.

El objetivo original era solo definir los nodos; se amplió porque el inventario de nodos es una parte de la arquitectura, que también incluye cómo se comunican y qué cambia entre sim y real.

Referencia: `Section9_Build/arduinobot_ws/src` del curso (la sección más completa, con robot real).

## Hallazgo: la tarea ya estaba casi resuelta

Al comparar los dos workspaces, casi todos los paquetes del curso ya se habían portado mientras se avanzaba con `[R05]` a `[R11]`. La tarea pasó de "definir desde cero" a **cerrar el inventario**: decidir qué faltantes necesita el brazo y cuáles no.

## Inventario

| Paquete del curso | En `robotic_arm_ws` | Contenido | Decisión |
|---|---|---|---|
| `description` | ✅ `robotic_arm_description` | URDF, launch `display` y `gazebo` | Portado (`[R02]`) |
| `controller` | ✅ `robotic_arm_controller` | Plugin `ros2_control`, `slider_control`, controllers | Portado (`[R09]`) |
| `moveit` | ✅ `robotic_arm_moveit` | `move_group` | Portado (`[R05]`) |
| `remote` | ✅ `robotic_arm_remote` | `task_server` (action server, Python) | Portado (`[R07]`) |
| `firmware` | ✅ `robotic_arm_firmware` | `simple_serial_transmitter` / `receiver` | Portado (`[R10]`) |
| `msgs` | ✅ `robotic_arm_msgs` | Action `RoboticArmTask` | Portado (`[R07]`) |
| `bringup` | ✅ `robotic_arm_bringup` | Launch `simulated_robot` / `real_robot` | Creado en `[R12]` |
| `utils` | ❌ | `angle_conversion` (euler ↔ quaternion como servicio) | No por ahora |
| `remote/alexa_interface` | ❌ | Control por voz (Section 8) | Descartado |
| `cpp_examples` / `py_examples` | — | Ejemplos de aprendizaje | No forman parte del robot |

## Decisiones

### `alexa_interface`: descartado

El control por voz no forma parte del alcance del proyecto.

### `angle_conversion`: no por ahora

Convierte ángulos de Euler (roll, pitch, yaw) a quaternion y viceversa. Hace falta para goals **cartesianos** del efector ("andá a x, y, z con esta orientación"), porque ROS representa orientaciones con quaternions.

Verificado en `robotic_arm_remote/task_server.py`: todas las tareas son **ángulos de joint** (`set_joint_group_positions`), sin poses cartesianas ni quaternions. No hay quien use el servicio.

**Trabajo futuro:** si se agregan tareas del tipo "agarrá el objeto en (x, y, z)" (por ejemplo, con una cámara), vuelve a hacer falta.

### `bringup`: faltaba y se creó

Era el único faltante con valor inmediato: un solo comando que levanta todo el sistema pasando `is_sim` coherente a cada parte. Se resolvió como tarea aparte, `[R12]` (ver `doc/md/bitacora-R12-bringup.md`).

## Nodos del sistema

| Nodo | Paquete | Rol |
|---|---|---|
| `robot_state_publisher` | `controller.launch.py` (real) / `gazebo.launch.py` de `description` (sim) | Publica las TF a partir del URDF y `/joint_states` |
| `controller_manager` + plugin `RoboticArmInterface` | `controller.launch.py` (real) | Habla con el ESP32 por puerto serie (tramas `M`). En sim, el `controller_manager` lo aporta el plugin `gz_ros2_control` dentro de Gazebo |
| `joint_state_broadcaster`, `arm_controller`, `gripper_controller` | `controller` | Controllers de `ros2_control` |
| `move_group` | `moveit` | Planificación y ejecución de trayectorias |
| `task_server` | `remote` | Action server que expone tareas predefinidas sobre MoveIt2 |
| `slider_control` | `controller` | Control manual por sliders (pruebas) |
