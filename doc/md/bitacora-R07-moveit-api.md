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

### Tabla de límites por joint (verificado contra `robotic_arm.urdf.xacro`)

| Joint | Group | Límite inf. (rad) | Límite sup. (rad) | Límite inf. (°) | Límite sup. (°) |
|---|---|---|---|---|---|
| `shoulder_pan` | arm | -1.91986 | 1.91986 | -110.0° | 110.0° |
| `shoulder_lift` | arm | -1.74533 | 1.74533 | -100.0° | 100.0° |
| `elbow_flex` | arm | -1.69 | 1.69 | -96.9° | 96.9° |
| `wrist_flex` | arm | -1.65806 | 1.65806 | -95.0° | 95.0° |
| `wrist_roll` | arm | -2.74385 | 2.84121 | -157.3° | 162.8° |
| `gripper` | gripper | -0.174533 | 1.74533 | -10.0° | 100.0° |

`gripper` es el único asimétrico y el más angosto de todos (el que rompía `task_number == 0`). `wrist_roll` también es asimétrico, a diferencia de los otros 4 joints de `arm`.

## Bug heredado — `goal_handle.succeed()` incondicional (CORREGIDO)

Igual que en el `task_server.py` de referencia: `goal_handle.succeed()` se llamaba siempre, incluso si `arm_plan_result`/`gripper_plan_result` fallaban (el `else` solo logueaba, no cortaba el flujo). Corregido moviendo `result = RoboticArmTask.Result()` antes del `if`, seteando `result.success = True/False` en cada rama, y llamando `goal_handle.succeed()` dentro del `if` y `goal_handle.abort()` dentro del `else`. Ahora el estado del Action y el contenido del `Result` quedan consistentes en los dos casos.

## Segundo bug encontrado en la corrección de valores — `elif` duplicados

Al reescribir los `task_number` con los valores medidos en Gazebo, quedaron **tres `elif` distintos comparando contra el mismo número** (`== 4` repetido tres veces para las tareas "levanto y traslado", "roto a posición b" y "suelto en posición b"). En un `if/elif`, solo se ejecuta el primero que matchea — las otras dos ramas quedaban como código muerto, sin dar ningún error. Corregido renumerando a `4`, `5`, `6`.

## Bloqueadores de lanzamiento encontrados y corregidos

1. **Mismatch de nombre de ejecutable**: `CMakeLists.txt` instala el script con `RENAME task_server` (sin `.py`), pero `remote_interface.launch.py` inicialmente pedía `executable="task_server.py"` (con extensión) — `ros2 launch` fallaba con "executable not found". Corregido a `executable="task_server"`.
2. **Faltaba `robotic_arm_moveit/config/planning_python_api.yaml`**: referenciado por `.moveit_cpp(file_path="config/planning_python_api.yaml")` en el launch, no existía. Creado con `pipeline_names: ["ompl"]` y `plan_request_params` (mismo contenido que el de referencia).

## R07 completada — verificado end-to-end en Gazebo

Secuencia de prueba, en 3 terminales + 1 para mandar goals:

```bash
# Terminal 1 — Gazebo + robot
ros2 launch robotic_arm_description gazebo.launch.py

# Terminal 2 — controladores ros2_control (joint_state_broadcaster, arm_controller, gripper_controller)
ros2 launch robotic_arm_controller controller.launch.py is_sim:=True

# Terminal 3 — task_server (MoveItPy)
ros2 launch robotic_arm_remote remote_interface.launch.py is_sim:=True

# Terminal 4 — mandar goals (CLI de Actions, visto en R06)
ros2 action list                                                          # confirmar que aparece /task_server
ros2 action send_goal /task_server robotic_arm_msgs/action/RoboticArmTask "{task_number: 0}"
```

Las 7 tareas (`task_number` 0 a 6) probadas y funcionando en Gazebo: pose zero, posición inicial, rotar+abrir gripper, agarrar objeto, trasladar a centro, rotar a posición B sosteniendo, soltar en posición B.

## Próximos pasos (queda para [R08])

- Ejecutar contra el brazo real — bloqueado por el desacople de calibración entre el "0" del modelo y el "0" físico de los servos (ver sección más arriba).
- Evaluar si conviene migrar más adelante a la arquitectura desacoplada (action client Python propio contra `move_action` de `move_group`, en vez de `MoveItPy`), aprovechando lo aprendido en [[project_r06_actions_progress]].
