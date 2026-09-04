# Bitácora — R01: URDF del proyecto SO-ARM100 para Gazebo/ROS2

## Objetivo

Averiguar si el proyecto en el que está basado este brazo (`TheRobotStudio/SO-ARM100`) tiene un URDF disponible para simular con Gazebo ROS2.

## Investigación realizada

- Repo: https://github.com/TheRobotStudio/SO-ARM100
- Se listó el árbol completo del repo vía GitHub API (`git/trees/main?recursive=1`) buscando archivos `.urdf`, `.xacro`, `.sdf`, `.world` y referencias a `ros2`/`gazebo`.
- Se leyó `Simulation/README.md` del repo para confirmar el uso previsto de los archivos.

## Hallazgos

- La carpeta `Simulation/` contiene:
  - **SO100**: `Simulation/SO100/so100.urdf` + mallas STL en `assets/`.
  - **SO101**: `Simulation/SO101/so101_new_calib.urdf` y `so101_old_calib.urdf` (dos calibraciones distintas), más mallas STL.
  - **SO101** además tiene archivos **MJCF** (MuJoCo): `scene.xml`, `joints_properties.xml`, `so101_new_calib.xml`, `so101_old_calib.xml`.
- No se encontró ningún archivo `.sdf`, `.world`, ni estructura de paquete ROS2 (`package.xml`, `launch/`, plugins `gazebo_ros2_control`).
- El README oficial de `Simulation/` solo documenta visualizar los URDF con [`rerun`](https://www.rerun.io/) (plugin URDF), no menciona Gazebo en ningún punto.

## Conclusión

**Sí existe URDF** (para SO100 y SO101, este último con dos variantes de calibración), pero es un URDF "crudo": geometría + juntas + mallas, sin nada específico de Gazebo. No hay paquete ROS2 ni plugins de control armados — eso queda para R02 (armar/generar lo que falte para levantarlo en Gazebo).

URDF a usar como base: **`Simulation/SO101/so101_new_calib.urdf`** (calibración nueva, la que corresponde al brazo armado).

## Próximos pasos

- [x] Confirmar si el repo tiene URDF
- [x] Identificar cuál URDF usar como base (SO101, calibración nueva)
- [ ] R02: armar paquete ROS2 + adaptaciones para levantar el URDF en Gazebo
