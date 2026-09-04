# Bitácora — R02: Levantar el URDF de SO101 en Gazebo (ROS2)

## Objetivo

Armar el paquete/workspace ROS2 necesario para simular el brazo SO101 en Gazebo, partiendo del URDF vendorizado en `ros2/urdf/so101/` (ver [R01](bitacora-R01-urdf.md)).

## Contexto

El usuario está haciendo esta tarea por su cuenta (modo aprendizaje); esta bitácora registra los pasos y comandos ROS2 que va ejecutando, a medida que los va pasando.

## Configuración del entorno

- Guía seguida para configurar ROS2 en la computadora: [`Configure the Development Environment in Ubuntu 24.04.pdf`](../datasheets/Configure+the+Development+Environment+in+Ubuntu+24.04.pdf)

## Activación del entorno ROS2

Para activar el entorno de ROS2 en la terminal se usa el alias `ros_jazzy`, definido en `~/.zshrc`:

```bash
alias ros_jazzy="source /opt/ros/jazzy/setup.zsh"
```

Este alias ejecuta el script `setup.zsh` que instala ROS2 Jazzy en `/opt/ros/jazzy/`. Ese script no "arranca" nada, sino que exporta en la sesión actual de la terminal las variables de entorno que las herramientas de ROS2 necesitan para funcionar: `ROS_DISTRO`, `AMENT_PREFIX_PATH` (dónde buscar paquetes instalados), `PYTHONPATH` (para poder importar los módulos Python de ROS2/`rclpy`) y agrega los binarios (`ros2`, `colcon`, etc.) al `PATH`. Por eso hay que correrlo en cada terminal nueva antes de usar comandos `ros2` o `colcon` — es exclusivo de esa sesión de shell, no queda activado a nivel sistema.

## Comandos ejecutados

```bash
mkdir -p robotic_arm_ws/src
```
Creación del workspace ROS2 (`robotic_arm_ws`), con la carpeta `src/` donde van los paquetes.

```bash
cd robotic_arm_ws/src
ros_jazzy
colcon build
```
Resultado: `Summary: 0 packages finished [0.44s]`.

**Error detectado:** `colcon build` se corrió parado adentro de `src/`, no en la raíz del workspace. Consecuencia: `build/`, `install/` y `log/` se generaron dentro de `src/` en vez de al lado (rompe la estructura estándar de un workspace ROS2, que es `<ws>/{src,build,install,log}`). Se corrigió borrando esos tres directorios (nada trackeado en git, sin pérdida) y volviendo a correr `colcon build` desde `robotic_arm_ws/` (un nivel arriba de `src/`).

```bash
cd ..
colcon build
```
Resultado: `Summary: 0 packages finished [0.35s]`, ahora con `build install log src` correctamente a la misma altura. Sigue en 0 porque todavía no hay ningún paquete creado dentro de `src/` — eso es lo que sigue.

```bash
cd src
ros2 pkg create --build-type ament_cmake robotic_arm_description
```
Creación del paquete `robotic_arm_description` (build type `ament_cmake`), convención estándar en ROS2 para paquetes que solo contienen URDF/meshes/launch (sin nodos). Emite un warning de licencia (`TODO: License declaration` no es una licencia válida) — cosmético, no bloquea el build.

```bash
cd ..
colcon build
```
Resultado: `Summary: 1 package finished [2.52s]` — compila `robotic_arm_description` correctamente, parado en la raíz del workspace.

```bash
cd robotic_arm_description
mkdir urdf
touch urdf/robotic_arm.urdf.xacro
mkdir meshes
```
Estructura del paquete `robotic_arm_description`: carpeta `urdf/` con un archivo placeholder `robotic_arm.urdf.xacro` (vacío, a completar) y carpeta `meshes/` para las mallas STL.

## Construcción de `robotic_arm.urdf.xacro`

Traducción del URDF plano (`ros2/urdf/so101/so101_new_calib.urdf`, de R01) al xacro del paquete, siguiendo el formato del proyecto de referencia del curso Udemy (ver [[repo-de-referencia-del-curso-udemy-para-r02]]).

- **`base_link`** (el primero, hecho paso a paso para aprender el patrón): copiado el inertial, las 4 partes visual/collision y sus orígenes desde el URDF original. Errores encontrados y corregidos en el camino:
  - `scale="0.01 0.01 0.01"` de más en los `<mesh>` (copiado sin pensar del proyecto de referencia — ver detalle técnico abajo).
  - Un `<mesh>` sin auto-cerrar (`<mesh .../>` faltaba la `/`) que rompía el parseo XML — detectado corriendo `xacro archivo.urdf.xacro` (exit code y error de línea exacta).
  - Faltaban las definiciones `<material name="...">` con el color (`3d_printed`, `sts3215`) — solo estaban referenciadas por nombre, nunca definidas.
- **Los 6 links restantes** (`shoulder_link`, `upper_arm_link`, `lower_arm_link`, `wrist_link`, `gripper_link`, `gripper_frame_link`, `moving_jaw_so101_v1_link`) los completé yo (a pedido del usuario, para ganar velocidad una vez entendido el patrón del primero), mismo criterio: rutas `package://robotic_arm_description/meshes/...`, sin `scale`, materiales referenciados.
- **Joints**: se decidió (razonamiento del usuario, correcto) usar propiedades xacro `${effort}` y `${velocity}` (valor `10`, idéntico en los 6 joints revolute del URDF original) pero **hardcodear** los límites `lower`/`upper` de cada joint, porque son datos de calibración real específicos de cada motor, no un valor genérico. El primer joint (`shoulder_pan`, base→shoulder) se armó paso a paso; los 5 restantes (`shoulder_lift`, `elbow_flex`, `wrist_flex`, `wrist_roll`, `gripper`) más el `gripper_frame_joint` (fijo) y un `virtual_joint` nuevo (`world`→`base_link`, necesario para tener una única raíz) los completé yo.
- **No se portaron los bloques `<transmission>`** del URDF original: son sintaxis vieja de `ros_control` (ROS1), que `ros2_control` (lo que se va a usar en Gazebo) no lee. Además todos tenían `mechanicalReduction="1"` (sin reducción real que modelar). Cuando se adapte el URDF para Gazebo va a hacer falta un bloque `<ros2_control>` en su lugar, no esto.
- **Verificación:** cada cambio se validó corriendo `xacro robotic_arm.urdf.xacro` (chequea que el XML resultante sea válido) y, tras completar todos los joints, `check_urdf` sobre el resultado — confirma un único root (`world`) y el árbol cinemático completo: `world → base_link → shoulder_link → upper_arm_link → lower_arm_link → wrist_link → gripper_link → {moving_jaw_so101_v1_link, gripper_frame_link}`.

## Decisiones y hallazgos técnicos

- **No usar `scale="0.01 0.01 0.01"` en los `<mesh>` del xacro.** Ese factor viene copiado del proyecto de referencia del curso (`arduinobot`), pero ahí corrige una malla modelada en otra unidad. Las mallas de SO101 (exportadas con `onshape-to-robot`) ya vienen en metros: verificado midiendo el bounding box de `base_motor_holder_so101_v1.stl` (~-0.02 a 0.07 en cada eje, consistente con una pieza de 5-9cm). Aplicar ese scale las achicaría 100x.
- **`scale` no afecta el rendimiento** (no cambia la cantidad de triángulos, solo el tamaño geométrico). El total real de la malla del brazo es **322.564 triángulos** (13 STL, el más pesado `wrist_roll_pitch_so101_v2.stl` con ~54.000). El riesgo real de performance en Gazebo es que el URDF reusa la misma malla de alto detalle para `<visual>` y `<collision>` — si hay lag/inestabilidad de física más adelante, la solución es simplificar la geometría de colisión (cajas/cilindros o convex hull), no tocar el scale.

```bash
cd robotic_arm_ws/src/robotic_arm_description
mkdir urdf meshes
# ... escritura de robotic_arm.urdf.xacro y CMakeLists.txt (ver arriba) ...
colcon build
```
**Error repetido:** de nuevo se corrió `colcon build` parado adentro del paquete (`robotic_arm_ws/src/robotic_arm_description/`) en vez de la raíz del workspace — generó `build/`, `install/`, `log/` mezclados con el código fuente del paquete. Se corrigió borrando esos tres directorios (nada trackeado, sin pérdida) y volviendo a correr `colcon build` parado en `robotic_arm_ws/`.

```bash
cd robotic_arm_ws
colcon build
```
Resultado: `Summary: 1 package finished [1.25s]` — correcto, parado en la raíz del workspace. Con el `install(DIRECTORY meshes urdf ...)` agregado al `CMakeLists.txt`, esta vez `colcon build` también copia las mallas y el xacro a `install/share/robotic_arm_description/`.

```bash
sudo apt install ros-jazzy-urdf-tutorial
```
Instalación del paquete `urdf_tutorial` (trae `display.launch.py`: levanta `robot_state_publisher` + `joint_state_publisher_gui` + RViz para visualizar y mover el URDF interactivamente). Paso previo a Gazebo, para validar visualmente el xacro armado.

```bash
ros2 launch urdf_tutorial display.launch.py model:=/home/ezequiel/proyecto-final/BrazoRobotico/robotic_arm_ws/src/robotic_arm_description/urdf/robotic_arm.urdf.xacro
```
Primer chequeo visual del xacro completo en RViz (`robot_state_publisher` + `joint_state_publisher_gui`). Resultado: **el brazo se ve completo y correcto**, todas las piezas presentes, sin errores de mallas faltantes ni de parseo. Capturas de pantalla tomadas con la herramienta de GNOME (`PrtScn`/`Shift+PrtScn`).

Configuración de RViz guardada en `robotic_arm_ws/src/robotic_arm_description/rviz/display.rviz` (carpeta `rviz/` nueva dentro del paquete, convención estándar de ROS2 para reutilizar la vista desde un launch file más adelante).

### Launch file propio para RViz

Creado `robotic_arm_ws/src/robotic_arm_description/launch/display.launch.py` (basado en el patrón del proyecto de referencia): declara el argumento `model` (default: xacro instalado del paquete), corre `xacro` sobre él vía `Command(...)` para publicar `robot_description`, y levanta `robot_state_publisher` + `joint_state_publisher_gui` + `rviz2` (con `-d` apuntando a `rviz/display.rviz`). Se agregó `install(DIRECTORY meshes urdf launch rviz ...)` al `CMakeLists.txt` y los `exec_depend` correspondientes (`robot_state_publisher`, `urdf`, `joint_state_publisher_gui`, `rviz2`, `xacro`, `ros2launch`) al `package.xml`.

**Bug encontrado y corregido (dos idas y vueltas):** al renombrar la variable `arduinobot_description_dir` → `robotic_arm_description_dir` (nombre pegado del proyecto de referencia), quedó una referencia sin actualizar en la línea del nodo de RViz (`arguments=["-d", os.path.join(arduinobot_description_dir, ...)]`) — hubiera tirado `NameError` al ejecutar `generate_launch_description()`. Detectado leyendo el archivo línea por línea (un primer intento de verificación con `ros2 launch ... --show-args` dio falso negativo por correr contra la copia vieja en `install/`, no contra el `src/` editado). Corregido y confirmado con:

```bash
colcon build
ros2 launch robotic_arm_description display.launch.py
```
Resultado: brazo visible en RViz, correcto.

### Launch file para Gazebo

Creado `robotic_arm_ws/src/robotic_arm_description/launch/gazebo.launch.py`: `robot_state_publisher` (con `use_sim_time: True`), include del launch de `ros_gz_sim` (`gz_sim.launch.py`, Gazebo Harmonic/Sim — no Gazebo clásico) con mundo vacío, `SetEnvironmentVariable` de `GZ_SIM_RESOURCE_PATH` (padre del share dir, para que Gazebo resuelva las mallas `package://`), spawn del robot vía `ros_gz_sim create` leyendo el tópico `robot_description`, y bridge de `/clock` (`ros_gz_bridge`). Agregados `ros_gz_sim`/`ros_gz_bridge` como `exec_depend` en `package.xml` (ya instalados en el sistema: `ros-jazzy-ros-gz-sim`, `ros-jazzy-ros-gz-bridge`, etc.). Esta vez sin el bug de variable a medio renombrar de la vez pasada — consistente en todo el archivo. Se sacó la variable `ros_distro` sin uso (código muerto).

```bash
colcon build
ros2 launch robotic_arm_description gazebo.launch.py
```
Resultado: **el brazo se spawnea y se ve bien en Gazebo** (mundo vacío, mallas correctas, sin errores).

## `ros2_control` + control de joints en Gazebo

Concepto: `ros2_control` abstrae el hardware (real o simulado) detrás de una interfaz uniforme — un `controller_manager` corre controladores que comandan/leen joints a través de `command_interface`/`state_interface`, sin saber si atrás hay un motor real o Gazebo. En Gazebo, esa "hardware" es el plugin `gz_ros2_control` (Gazebo Sim/Harmonic — no el `gazebo_ros2_control` de Gazebo clásico).

Estructura elegida, siguiendo el patrón del proyecto de referencia (separar en archivos xacro incluidos desde `robotic_arm.urdf.xacro` en vez de todo en un solo archivo):
- `robotic_arm_gazebo.xacro`: tag `<gazebo>` con el `<plugin>` `gz_ros2_control::GazeboSimROS2ControlPlugin`, apuntando a los parámetros del controller_manager.
- `robotic_arm_ros2_control.xacro`: tag `<ros2_control>` con `<hardware><plugin>gz_ros2_control/GazeboSimSystem</plugin></hardware>` y, por joint, `<command_interface>`/`<state_interface>`.
- Paquete nuevo **`robotic_arm_controller`** (mirando al `arduinobot_controller` de referencia): `config/robotic_arm_controllers.yaml` con la config del `controller_manager`.

**Errores encontrados en el camino:**
- Paquete `robotic_arm_controller` creado por error **fuera de `src/`** (directo en `robotic_arm_ws/`) — corregido moviéndolo a `robotic_arm_ws/src/robotic_arm_controller`.
- Nombre de archivo `robotic_arm_ros2_controll.xacro` (doble `l`) no coincidía con el `filename` del `xacro:include` (`robotic_arm_ros2_control.xacro`, una sola `l`) — hubiera fallado al procesar. Corregido renombrando el archivo.
- Como el `include` usa `$(find robotic_arm_description)/...`, xacro necesita el paquete resuelto vía `ament_index` — hace falta tener el workspace *instalado* (`colcon build` + sourcear `install/setup.zsh`), no alcanza con tener el archivo en `src/`.

**`robotic_arm_controllers.yaml`** (completado): `joint_state_broadcaster` (publica `/joint_states`), `arm_controller` (`JointTrajectoryController`, los 5 joints del brazo: `shoulder_pan`, `shoulder_lift`, `elbow_flex`, `wrist_flex`, `wrist_roll`) y `gripper_controller` (`JointTrajectoryController`, joint `gripper`).

**Decisión: `JointTrajectoryController` para el gripper, no `ForwardCommandController`.** El curso de referencia deja comentada la opción `ForwardCommandController` (más simple: aplica un valor directo, sin interpolar, sin action interface) y usa `JointTrajectoryController` para el gripper también — misma interfaz (action `FollowJointTrajectory` con feedback) para brazo y gripper. Ventaja: uniformidad si más adelante se integra MoveIt2 o se necesita esperar/coordinar el cierre del gripper con el resto del movimiento. `ForwardCommandController` hubiera sido más liviano para un gripper simple abrir/cerrar, pero se prioriza la interfaz uniforme.

**Pendiente:** agregar `install(DIRECTORY config DESTINATION share/${PROJECT_NAME})` al `CMakeLists.txt` de `robotic_arm_controller` (todavía no lo tiene) para que `$(find robotic_arm_controller)/config/robotic_arm_controllers.yaml` resuelva tras el build.

**Bug del `<xacro>` suelto:** apareció en los dos archivos nuevos (resto de sacar el `xacro:if`/`xacro:unless` del `is_ignition`). Corregido en `robotic_arm_gazebo.xacro` restaurando el patrón completo `xacro:if value="$(arg is_ignition)"` / `xacro:unless` (con `<xacro:arg name="is_ignition" default="false"/>` agregado al xacro principal — por defecto usa `gz_ros2_control`, la rama que corresponde a Jazzy) y, con el mismo criterio, en `robotic_arm_ros2_control.xacro` para el bloque `<hardware>`.

**Joints de `robotic_arm_ros2_control.xacro` completados** (a pedido del usuario, patrón ya entendido): los 6 joints revolute (`shoulder_pan`, `shoulder_lift`, `elbow_flex`, `wrist_flex`, `wrist_roll`, `gripper`), cada uno con `<command_interface name="position">` (min/max = límites reales de calibración, los mismos del URDF principal) y `<state_interface name="position"/>`. Se sacó la propiedad `PI` que había quedado sin usar. Verificado con `colcon build` + `xacro` (`exit: 0`, el `<gazebo>` y el `<ros2_control>` completo aparecen en el URDF final) + `check_urdf` (árbol cinemático intacto).

### Primer arranque con `ros2_control` completo

```bash
colcon build
ros2 launch robotic_arm_description gazebo.launch.py
```
Log revisado línea por línea. **Éxito:** `gz_ros_control` carga `controller_manager`, se suscribe a `/robot_description`, registra los 6 joints (`shoulder_pan`, `shoulder_lift`, `elbow_flex`, `wrist_flex`, `wrist_roll`, `gripper`) con `State: position` / `Command: position` (coincide con `robotic_arm_ros2_control.xacro`), inicializa y **activa** el hardware `RobotSystem` sin errores. Warnings esperados/benignos: joint fijo `virtual_joint` se salta (correcto, no es actuable), warning de "Executor not available"/"statistics" (normales en este plugin), warning de `update_rate` 10Hz vs paso de física 1000Hz (esperado, el control corre más lento que la física a propósito).

**Hallazgo real (no de `ros2_control`, del motor de física `dartsim`):** en el log aparece repetido para *todos* los links: `Mesh construction from an SDF has not been implemented yet for dartsim` + `The geometry element of collision [<link>_collision] couldn't be created`. `dartsim` no puede construir la geometría de `<collision>` directo desde malla STL — **el brazo ahora mismo no tiene colisión física en ningún link** (se ve bien porque el `<visual>` sí renderiza, pero no va a interactuar físicamente con nada). Esto confirma/adelanta el pendiente "Futuro" ya anotado (simplificar `<collision>` a formas primitivas) — pasa de ser hipotético a ser un problema real y actual.

**Chequeo cruzado con el proyecto de referencia:** se corrió `ros2 launch arduinobot_description gazebo.launch.py` sobre el `arduinobot_ws` del curso (Section5_Control) para confirmar si el problema de colisión con dartsim era general o específico de nuestro URDF. Mismo resultado exacto: `Mesh construction... has not been implemented yet for dartsim` + `The geometry element of collision [...] couldn't be created` para todos sus links. **Confirma que es una limitación conocida de `dartsim`, no un error de nuestro proyecto.** Bonus: el log de referencia también muestra que dartsim no soporta joints "mimic" (`[Err] ... physics engine does not support mimic constraints`) — no nos afecta, nuestro diseño no usa mimic joints.

### Launch de los controllers (`spawner`)

Creado `robotic_arm_controller/launch/controller.launch.py` (patrón del curso adaptado): arg `is_sim` (default `True`) que en simulación evita levantar `robot_state_publisher`/`ros2_control_node` propios (ya los provee el plugin `gz_ros2_control` dentro de Gazebo), más 3 `Node` de `controller_manager`/`spawner` (`joint_state_broadcaster`, `arm_controller`, `gripper_controller`). Agregado `launch` al `install(DIRECTORY ...)` del `CMakeLists.txt` y los `exec_depend` (`robotic_arm_description`, `robot_state_publisher`, `controller_manager`, `xacro`, `ros2launch`) al `package.xml`.

```bash
# Terminal 1
ros2 launch robotic_arm_description gazebo.launch.py
# Terminal 2
ros2 launch robotic_arm_controller controller.launch.py
```
**Resultado: éxito total.** Los 3 controladores se cargaron y activaron sin errores contra el `controller_manager` de Gazebo: `Loaded` → `Configured and activated` para `gripper_controller`, `joint_state_broadcaster` y `arm_controller`. Cada `spawner` termina limpio después de activar su controlador (comportamiento esperado, es un script de un solo uso). **El brazo queda completamente controlable desde ROS2** — objetivo central de R02 cumplido.

### Primer movimiento real comandado

```bash
ros2 action send_goal /arm_controller/follow_joint_trajectory control_msgs/action/FollowJointTrajectory "{trajectory: {joint_names: [shoulder_pan, shoulder_lift, elbow_flex, wrist_flex, wrist_roll], points: [{positions: [0.3, 0.0, 0.0, 0.0, 0.0], time_from_start: {sec: 3}}]}}"
```
Trayectoria mandada al action `FollowJointTrajectory` de `arm_controller` (solo `shoulder_pan` a 0.3 rad, resto en 0, en 3s), con Gazebo + `gazebo.launch.py` + `controller.launch.py` corriendo. **Resultado: `Goal successfully reached!`, status `SUCCEEDED`.** El brazo se movió en Gazebo tal cual lo pedido. Cadena completa validada: URDF (xacro) → spawn en Gazebo → `ros2_control` activado → controllers spawneados → movimiento real comandado desde ROS2.

## Próximos pasos

- [x] Crear workspace ROS2 (`robotic_arm_ws/src`)
- [x] Crear paquete ROS2 para el brazo (`robotic_arm_description`)
- [x] Escribir `robotic_arm.urdf.xacro` completo (9 links + 8 joints), validado con `xacro` + `check_urdf`
- [x] Chequeo visual del URDF en RViz (`urdf_tutorial display.launch.py`) — brazo completo, sin errores
- [x] Levantar el modelo en Gazebo (`gazebo.launch.py`, `ros_gz_sim` + spawn + bridge de `/clock`) — se ve bien
- [x] Escribir `robotic_arm_controllers.yaml` (joint_state_broadcaster + arm_controller + gripper_controller)
- [x] Completar `robotic_arm_ros2_control.xacro` (6 joints con command/state interface) y `robotic_arm_gazebo.xacro` (plugin bien formado, sin el wrapper `<xacro>` suelto)
- [x] Agregar `install(DIRECTORY config ...)` al `CMakeLists.txt` de `robotic_arm_controller`
- [x] Confirmar que `ros2_control` levanta bien en Gazebo (`controller_manager` activo, hardware `RobotSystem` inicializado, 6 joints registrados) — revisado log completo
- [x] Crear el launch que levante los controllers (`controller.launch.py`) — los 3 controladores se activan sin errores
- [x] Probar mover el brazo mandando una trayectoria real — `Goal successfully reached!`, se movió correctamente en Gazebo
- [ ] **Confirmado, no solo hipotético:** reemplazar la geometría de `<collision>` (mallas STL de detalle completo) por formas primitivas (cajas/cilindros) — `dartsim` no puede construir colisión desde STL, el brazo no tiene colisión física en ningún link ahora mismo
