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

## Próximos pasos

- [x] Crear workspace ROS2 (`robotic_arm_ws/src`)
- [x] Crear paquete ROS2 para el brazo (`robotic_arm_description`)
- [x] Escribir `robotic_arm.urdf.xacro` completo (9 links + 8 joints), validado con `xacro` + `check_urdf`
- [x] Chequeo visual del URDF en RViz (`urdf_tutorial display.launch.py`) — brazo completo, sin errores
- [ ] Adaptar el URDF para Gazebo (tags `<gazebo>`, `<ros2_control>`, plugin `gazebo_ros2_control`)
- [ ] Levantar el modelo en Gazebo
- [ ] (Futuro, si hay problemas de performance/física) Simplificar geometría de `<collision>` en vez de usar las mallas de detalle completo
