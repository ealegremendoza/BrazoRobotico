# Bitácora [R07] — Implementar API para manejar MoveIt2

_Inicio: 2026-09-18_

## Contexto

Implementación de un `task_server` (Action Server propio) que expone MoveIt2 como una API programática, en vez de operar el brazo solo desde RViz. Basado en el ejemplo del curso Udemy en `Section7_Application/arduinobot_ws` (`arduinobot_remote/task_server.py`), que combina:
- El patrón de Actions estudiado en [[project_r06_actions_progress]] (propia interfaz `.action`, no la de MoveGroup directamente).
- `MoveItPy` para planificar/ejecutar (visto en `simple_moveit_interface.py`).

Depende de [R05] (MoveIt2 configurado) y [R06] (Actions).

## MoveGroupInterface (C++) vs MoveItPy (Python)

Verificado contra la documentación oficial de MoveIt2 (picknik.ai) y el código fuente del curso (`task_server.cpp` vs `task_server.py`):

- **`MoveGroupInterface`** (C++): cliente que habla con el nodo `move_group` (separado, corriendo aparte) vía Actions/Services. Desacoplado.
- **`MoveItPy`** (Python): envuelve `MoveItCpp`, que carga el pipeline de planificación completo **dentro del propio proceso**, saltándose la capa de Action/Service — más rápido pero acopla el nodo con toda la config de MoveIt.

Motivo del split en el curso: `moveit_commander` (el equivalente Python clásico de `MoveGroupInterface`) está deprecado en ROS2; `MoveItPy` es el reemplazo oficial, pero no es un reemplazo 1:1.

**Decisión para este proyecto:** implementar primero calcando el ejemplo del curso con `MoveItPy` (para validar que funciona end-to-end), documentado acá. Alternativa desacoplada (action client propio contra `move_action` de `move_group`) queda pendiente como posible refactor futuro.

**Decisión de lenguaje:** Python, por consistencia con el resto del stack del proyecto (`STServo_Python`, control de servos, scripts ESP32) — no hay necesidad de un loop de control duro en tiempo real que justifique C++.

## Paquetes nuevos creados

### `robotic_arm_msgs` (interfaces)
`ros2 pkg create --build-type ament_cmake robotic_arm_msgs`, con `action/RoboticArmTask.action`:
```
# Goal
int32 task_number
---
# Result
bool success
---
# Feedback
int32 percentage
```
`CMakeLists.txt` necesita `find_package(rosidl_default_generators REQUIRED)` + `rosidl_generate_interfaces(${PROJECT_NAME} "action/RoboticArmTask.action")`. `package.xml` necesita `build_depend rosidl_default_generators`, `depend action_msgs`, `exec_depend rosidl_default_runtime`, `member_of_group rosidl_interface_packages`.

### `robotic_arm_remote` (nodo)
`ros2 pkg create --build-type ament_cmake robotic_arm_remote` (elegido `ament_cmake` + `ament_cmake_python`, no `ament_python` puro, para consistencia con `robotic_arm_controller`).

Estructura: `robotic_arm_remote/robotic_arm_remote/{__init__.py, task_server.py}` + `launch/`.

## Errores de build y causa raíz (para no repetirlos)

1. **`file INSTALL cannot find ".../launch"`** — el `CMakeLists.txt` tenía `install(DIRECTORY launch ...)` pero la carpeta no existía todavía. Crearla antes de buildear si el `CMakeLists.txt` ya la referencia.

2. **`Could not find a package configuration file provided by "robotic_arm_msgs"`** — `colcon` arma el orden de build leyendo las dependencias declaradas en `package.xml` de cada paquete. Si `robotic_arm_remote/package.xml` no tiene `<depend>robotic_arm_msgs</depend>` (o `exec_depend`), colcon no sabe que tiene que esperar a que ese paquete termine, y los buildea en paralelo/desorden.

3. **Copiar CMakeLists de un paquete de referencia mixto (C+++Python) trae basura.** `arduinobot_remote` (el paquete del curso) mezcla `task_server.cpp` y `task_server.py` en el mismo `CMakeLists.txt`. Copiarlo literal para un paquete 100% Python arrastra `find_package(moveit_ros_planning_interface REQUIRED)` (la lib C++ de `MoveGroupInterface`, no usada en Python) y un `install(TARGETS ...)` sin ningún target real (instalaba la librería `task_server` que compilaba `add_library(...)` en el original — acá no existe). Lección: revisar línea por línea qué corresponde a la parte que realmente se está portando, no asumir que "es lo mismo que el de referencia".

## `CMakeLists.txt` / `package.xml` finales de `robotic_arm_remote`

`package.xml`: `buildtool_depend` de `ament_cmake` + `ament_cmake_python`; `exec_depend` de `robotic_arm_msgs`, `action_msgs`, `rclpy`, `moveit_py`. Nota: `moveit_py` no se busca con `find_package` en CMake (no se compila nada contra él) — solo aparece en `package.xml` como `exec_depend`, porque es un paquete Python puro.

`CMakeLists.txt`: `find_package` de `ament_cmake`, `ament_cmake_python`, `robotic_arm_msgs`, `rclpy` — sin `moveit_ros_planning_interface`. `ament_python_install_package(${PROJECT_NAME})` + `install(PROGRAMS ${PROJECT_NAME}/task_server.py DESTINATION lib/${PROJECT_NAME} RENAME task_server)`.

## Cómo se cargan los goals en MoveItPy (joint-space)

Flujo de `goalCallback`:
1. `RobotState(get_robot_model())` — una "foto" del estado completo del robot (modelo cinemático de URDF+SRDF).
2. `set_joint_group_positions("arm", arm_joint_goal)` — rellena esa foto con los ángulos deseados. `"arm"`/`"gripper"` son los planning groups del SRDF; el array de ángulos va **en el mismo orden en que el SRDF lista los joints del group** (`shoulder_pan, shoulder_lift, elbow_flex, wrist_flex, wrist_roll` para `arm`; `gripper` para `gripper`).
3. `set_start_state_to_current_state()` — el plan arranca desde la posición real actual del robot (leída de `joint_states`).
4. `set_goal_state(robot_state=...)` — la foto rellenada en el paso 2 pasa a ser el destino del plan.
5. `plan()` — calcula la trayectoria con el planner configurado (OMPL).

**Los valores son ángulos de joint en radianes, uno por joint (joint-space), NO coordenadas cartesianas ni roll/pitch/yaw.** Cada joint del brazo es `type="revolute"` con `axis xyz="0 0 1"` — una bisagra de 1 solo grado de libertad, como un codo: un ángulo describe su estado completo. Roll/pitch/yaw (3 números) hace falta para describir la orientación de un cuerpo libre en el espacio (ej. la pose cartesiana del end-effector), no la de un joint individual — por eso `set_joint_group_positions` solo pide 1 valor por joint. Importante: esto usa radianes del modelo cinemático (cero = pose "zero" del URDF), un sistema de unidades distinto al de los *counts* crudos (0-4095) que usa `STServo_Python/servo_control_ui.py` — la traducción entre ambos la hace `ros2_control`/el hardware interface, no algo a mano en el `task_server`.

## Pendiente bloqueante — el "0" del modelo no coincide con el "0" físico calibrado

El `group_state name="home"` del SRDF (todos los joints en 0) define el "0" de MoveIt como la pose que resulta de los `<origin>` de cada `<joint>` en el URDF — una elección de modelado, visible cargando el robot en RViz sin comandar nada. Pero la calibración física de los servos (`ZERO_COUNTS=2048` en `STServo_Python/calibrate-arm.py`) se hizo **de forma independiente, sin mirar el modelo** (confirmado con el usuario 2026-09-18). Es decir, el "0 rad" del modelo cinemático y el "0" físico calibrado de los servos **no están garantizados a coincidir**.

**Consecuencia:** un goal planificado con MoveIt (ej. `task_server`, o directamente desde RViz) que en simulación se ve correcto y dentro de límites, puede terminar en una pose física distinta al ejecutarse en el brazo real — con riesgo de forzar un joint contra un tope mecánico aunque el modelo diga que está dentro de rango.

**Mitigación actual:** probar todo en Gazebo/simulación (R02/R05) hasta recalibrar. **No ejecutar `task_server` contra el brazo físico hasta resolver este desacople** — ya sea recalibrando los servos para que coincidan con la pose "home" del URDF, o verificando/ajustando el mapeo en el hardware interface de `ros2_control`.

## Bug encontrado — gripper fuera de límites (no corregido todavía)

`task_number == 0` en el `task_server.py` propio manda `gripper_joint_goal = np.array([-0.7])`, copiado del ejemplo del curso. El joint `gripper` del URDF real (`robotic_arm.urdf.xacro`, línea 405) tiene `<limit lower="-0.174533" upper="1.74533"/>` — `-0.7` está ~4 veces más allá del límite inferior. Viene de copiar el valor de un gripper de 2 joints simétrico del `arduinobot` del curso; el SO-101 tiene 1 solo joint (`moving_jaw`) con rango angosto y asimétrico. **Pendiente: corregir antes de ejecutar en hardware real.**

`task_number == 1` y `2` sí verificados dentro de límites de los 5 joints de `arm`.

## Bug heredado sin corregir — `goal_handle.succeed()` incondicional

Igual que en el `task_server.py` de referencia: `goal_handle.succeed()` (línea 68) se llama siempre, incluso si `arm_plan_result`/`gripper_plan_result` fallan (el `else` solo loguea, no corta el flujo). El cliente se entera de "éxito" aunque el brazo no se haya movido. **Pendiente: corregir para llamar `goal_handle.abort()` y devolver `success=False` en el caso de falla.**

## Próximos pasos

- Corregir el valor de `gripper_joint_goal` en `task_number == 0` (dentro de `[-0.174533, 1.74533]`).
- Corregir el flujo condicional de `goal_handle.succeed()`/`abort()`.
- Crear el launch file (`MoveItConfigsBuilder` + `.moveit_cpp(file_path="config/planning_python_api.yaml")` en `robotic_arm_moveit/config/`, siguiendo el patrón de `simple_moveit_interface.launch.py`) y probar `ros2 launch`.
- Evaluar si conviene migrar más adelante a la arquitectura desacoplada (action client Python propio contra `move_action` de `move_group`, en vez de `MoveItPy`), aprovechando lo aprendido en [[project_r06_actions_progress]].
