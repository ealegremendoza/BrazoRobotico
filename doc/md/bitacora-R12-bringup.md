# Bitácora [R12] — Paquete `robotic_arm_bringup`

_Inicio: 2026-09-25_

## Contexto

Surgió al revisar `[R03]` (inventario de nodos del curso): casi todos los paquetes ya estaban portados, pero faltaba `bringup`. Hasta ahora había que levantar cada parte en una terminal distinta y pasar `is_sim` a mano en cada una, con riesgo de mezclar modos (por ejemplo, controller real con MoveIt en simulación).

Referencia: `arduinobot_bringup` en `Section9_Build/arduinobot_ws/src` del curso.

## Qué es `bringup`

No tiene nodos propios: solo launch files que **incluyen los launch de los otros paquetes** y les pasan `is_sim`. Es una única palanca que levanta todo el sistema con una configuración coherente.

| Launch | Incluye |
|---|---|
| `simulated_robot.launch.py` | `description/gazebo` + `controller`, `moveit`, `remote` con `is_sim: "True"` |
| `real_robot.launch.py` | `controller`, `moveit`, `remote` con `is_sim: "False"` (sin Gazebo) |

`gazebo.launch.py` no declara `is_sim` (siempre es simulación). En modo real, `controller.launch.py` levanta `robot_state_publisher` y `controller_manager` (nodos con `UnlessCondition(is_sim)`): cumple el papel que en simulación cumple Gazebo.

## Paquete

- **`ament_cmake`**: solo instala archivos. `install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})` alcanza; con `ament_python` harían falta `setup.py`, `setup.cfg`, el marcador en `resource/` y un módulo vacío.
- **`<exec_depend>`** para `ros2launch` y los 4 paquetes del robot: se usan solo en ejecución, no se compila nada contra ellos. `<depend>`/`<build_depend>` declararían una dependencia de compilación que no existe. Con `exec_depend`, `rosdep` detecta lo que falta antes de que el launch falle con `PackageNotFoundError`.
- Se borraron `include/` y `src/` (vacíos, generados por `ros2 pkg create`).

## `launch` no espera a que cada proceso esté listo

El orden de la lista del `LaunchDescription` es solo el orden de disparo: los procesos arrancan casi a la vez. Funciona porque **cada nodo espera a sus dependencias**:

- Los `spawner` de controllers reintentan (hasta un timeout) hasta que aparece el `controller_manager`.
- `move_group` espera el `robot_description` y los controllers.
- El `task_server` espera a `move_group`.

Si hiciera falta un orden estricto: `RegisterEventHandler` con `OnProcessStart`/`OnProcessExit`, o `TimerAction` (retardo fijo, lo más frágil).

## Bug encontrado: `use_python` en `remote_interface.launch.py`

Primera prueba del launch simulado:

```
executable 'task_server_node' not found on the libexec directory '.../install/robotic_arm_remote/lib/robotic_arm_remote'
```

`remote_interface.launch.py` venía del curso con un argumento `use_python` (default `"False"`) que elegía entre el `task_server` C++ (`task_server_node`) y el Python (`task_server`). El paquete propio solo tiene la versión Python; en `[R07]` se lanzaba siempre con `use_python:=True` y el problema no aparecía. `bringup` no pasaba el argumento → default → ejecutable inexistente.

Arreglo en la causa (no parche en `bringup`): se borraron el nodo C++ y el argumento `use_python`. Se corrigió también el comando de prueba en `bitacora-R07-moveit-api.md`. Misma lección que en `[R07]` con el `CMakeLists.txt`: revisar qué parte del código de referencia corresponde a lo que realmente se portó.

Los `WARNING: Cannot infer URDF/SRDF` de `MoveItConfigsBuilder` son inofensivos: aparecen antes de que `.robot_description(...)` / `.robot_description_semantic(...)` indiquen los archivos a mano.

## Verificación

```bash
ros2 launch robotic_arm_bringup simulated_robot.launch.py

# Otra terminal: cargar también el workspace (no solo /opt/ros/jazzy),
# si no, send_goal falla con "The passed action type is invalid"
source install/setup.zsh
ros2 action send_goal /task_server robotic_arm_msgs/action/RoboticArmTask "{task_number: 2}"
```

- Simulado: Gazebo y RViz levantan, Plan & Execute desde RViz mueve el brazo en Gazebo, y el `task_server` responde `SUCCEEDED`.
- Real: compila y `ros2 launch ... --print` resuelve los includes. **Sin probar** hasta que el ESP32 responda tramas `M` (`[E06]`). Verificar antes el puerto: el xacro tiene `/dev/ttyACM0` hardcodeado y el ESP32 puede aparecer como `/dev/ttyUSB0`.

`ros2 action list` funciona sin cargar el workspace (consulta nombres a los nodos vivos), pero `send_goal` necesita la definición del tipo, que está en `install/`.
