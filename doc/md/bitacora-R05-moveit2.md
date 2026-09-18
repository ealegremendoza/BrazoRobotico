# Bitácora — R05: Implementar MoveIt2 para planificación de movimiento del brazo

## Objetivo

Integrar MoveIt2 al workspace ROS2 (`robotic_arm_ws`) para poder planificar y ejecutar movimientos del brazo, en vez de comandar joints a mano. Referencia de curso: `arduinobot_ws` (Section6_Kinematics) en `/home/ezequiel/cursos/Robotics-and-ROS2-Manipulators/Robotics-and-ROS-2-Learn-by-Doing-Manipulators/`.

## Contexto

El usuario hace esta tarea en modo aprendizaje (mentor): escribe/ejecuta él, el asistente pregunta el porqué y corrige hasta que la explicación es correcta, sin escribir código de la tarea por él.

Punto de partida: workspace ya tiene `robotic_arm_description` (URDF/xacro, ver [R02](bitacora-R02-gazebo.md)) y `robotic_arm_controller`, pero ningún paquete de MoveIt2 todavía.

## Conceptos — comunicación ROS2 (tópicos, servicios, acciones)

Antes de tocar MoveIt2, se repasaron los tres mecanismos de comunicación de ROS2, arrancando de dos ejemplos del proyecto de referencia (`simple_service_server.py` / `simple_service_client.py`, patrón `AddTwoInts`).

| | Tópicos | Servicios | Acciones |
|---|---|---|---|
| Patrón | Pub/Sub (many-to-many) | Request/Response (1 a 1) | Goal/Feedback/Result (1 a 1) |
| Duración | Continuo, streaming | Instantáneo | Larga, con progreso |
| ¿Bloquea al cliente? | No aplica | Opcional (`call()` sync o `call_async()`) | No — siempre async por diseño |
| ¿Feedback intermedio? | N/A (es el feedback en sí) | No | Sí, mientras ejecuta |
| ¿Cancelable? | No aplica | No | Sí (`cancel_goal`) |
| Archivo de definición | `.msg` | `.srv` (Request / Response) | `.action` (Goal / Result / Feedback) |
| Ejemplo en MoveIt2 | `/joint_states` | `/compute_ik`, `/compute_fk` | ejecutar trayectoria (`FollowJointTrajectory`) |
| Cuándo usarlo | Datos que cambian constantemente, nadie "pide" nada puntual | Cálculo o consulta rápida, sin necesidad de progreso | Tarea que tarda y donde importa el progreso (o poder frenarla) |

**Por qué MoveIt2 no ejecuta trayectorias con un servicio**: un servicio solo puede devolver una única respuesta, al final. No hay forma estándar de reportar progreso durante el movimiento ni de cancelarlo a mitad de camino. Una acción sí: el servidor manda `Feedback` periódico mientras ejecuta y el cliente puede mandar `cancel_goal` en cualquier momento.

**Ciclo de vida de una acción**:
1. El cliente manda el `Goal`.
2. El servidor lo acepta/rechaza; mientras ejecuta, publica `Feedback` (progreso, posición actual, etc.).
3. Al terminar (éxito, error o cancelación), el servidor manda el `Result` una sola vez. El cliente nunca manda feedback ni result, solo `Goal` y opcionalmente `cancel_goal`.

**Por qué IK/FK sí son servicios (`/compute_ik`, `/compute_fk`)**: calcular la cinemática es instantáneo (no hay movimiento físico de por medio), así que no necesita feedback progresivo — encaja en el modelo request/response.

## Referencias

- Documentación oficial de MoveIt2: https://moveit.picknik.ai/main/index.html

## Comandos ejecutados

Siguiendo el video del curso (`arduinobot_moveit` como referencia), se creó el paquete `robotic_arm_moveit` con el mismo build-type que usa el curso:

```bash
ros2 pkg create --build-type ament_cmake robotic_arm_moveit
```

**Error detectado:** el comando se corrió parado en la raíz de `robotic_arm_ws`, no adentro de `src/` (mismo tipo de error que en [R02](bitacora-R02-gazebo.md), pero al revés — ahí `colcon build` se corrió desde `src/` en vez de la raíz). Resultado: `robotic_arm_moveit` quedó como carpeta hermana de `build/install/log/src`, en vez de paquete dentro de `src/`.

Se verificó que `colcon build` nunca llegó a correr después de crear el paquete mal ubicado (ni `build/` ni `install/` tenían rastro de `robotic_arm_moveit`, y el último log de build era anterior a la creación del paquete) — un intento posterior de correr `colcon build` se pegoteó con el comando anterior en la terminal (`...robotic_arm_moveitcolcon build`) y falló en el parser de `ros2` antes de invocar `colcon`. Conclusión: `build/`, `install/` y `log/` estaban intactos, no hacía falta borrarlos — alcanzaba con el fix mínimo.

**Fix aplicado:** se borró solo la carpeta `robotic_arm_moveit` mal ubicada, se recreó el paquete parado dentro de `src/`, y se corrió `colcon build` desde la raíz del workspace. Resultado verificado: `robotic_arm_moveit` aparece correctamente en `src/`, `build/` e `install/`, junto a `robotic_arm_controller` y `robotic_arm_description`.

## SRDF (`config/robotic_arm.srdf`)

**Primer borrador con error grave:** el primer guardado del archivo resultó ser el SRDF de `arduinobot` casi sin adaptar (se ve que no se había guardado el archivo real cuando se pidió revisión). Evidencia encontrada al comparar contra el URDF real:
- `<robot name="arduinobot">` en vez de `robotic_arm`.
- Grupo `gripper` con joints `joint_4`/`joint_5`, que no existen en el robot (el joint real es `gripper`, único).
- Los `group_state name="home"` referenciaban `joint_1`/`joint_2`/`joint_3`/`joint_4`, inexistentes.
- Todo el bloque `disable_collisions` apuntaba a links de `arduinobot` (`base_plate`, `forward_drive_arm`, `claw_support`, `gripper_left`, `gripper_right`, `horizontal_arm`), ninguno presente en este robot.

Se corrigió (tras guardar el archivo real): `robot name="robotic_arm"` ✓, grupo `gripper` con el joint real `gripper` ✓, `group_state` con los joints reales de cada grupo ✓.

**Grupos definidos:**
- `arm`: `virtual_joint`, `shoulder_pan`, `shoulder_lift`, `elbow_flex`, `wrist_flex`, `wrist_roll`.
- `gripper`: `gripper`.

**Colisiones (`disable_collisions`)** — cadena cinemática real (de `parent`/`child` en el xacro): `base_link → shoulder_link → upper_arm_link → lower_arm_link → wrist_link → gripper_link → {moving_jaw_so101_v1_link, gripper_frame_link}`. `gripper_frame_link` no tiene `<collision>` en el URDF, así que nunca participa en chequeos.

- 6 pares `Adjacent` (padre-hijo directo): `base_link`–`shoulder_link`, `shoulder_link`–`upper_arm_link`, `upper_arm_link`–`lower_arm_link`, `lower_arm_link`–`wrist_link`, `wrist_link`–`gripper_link`, `gripper_link`–`moving_jaw_so101_v1_link`.
- 9 pares `Never` (geométricamente no llegan a tocarse): los 5 que salen de `base_link` contra links lejanos, más `upper_arm_link`–`wrist_link`, `lower_arm_link`–`gripper_link`, `lower_arm_link`–`moving_jaw_so101_v1_link`, `wrist_link`–`moving_jaw_so101_v1_link`.
- 6 pares dejados **sin marcar a propósito** (quedan habilitados/chequeados): `shoulder_link` contra `lower_arm_link`/`wrist_link`/`gripper_link`/`moving_jaw_so101_v1_link`, y `upper_arm_link` contra `gripper_link`/`moving_jaw_so101_v1_link`. Validado físicamente en el brazo real (impreso en PLA, armado) que sí colisionan en algún punto del recorrido combinado de varios joints — no es un caso de un solo joint pasándose de rango (como el de abajo), sino de una combinación de joints plegados. No hay un `<limit>` único que lo prevenga sin sacrificar rango útil, así que se deja que el planner de MoveIt2 los descarte en tiempo real.

**Discusión — `disable_collisions` vs. límites de joint:** se identificó que la matriz de colisiones (ACM) es estática y binaria por par de links: no varía según el ángulo del joint. Para el caso `base_link`–`shoulder_link`, que además de tocarse en el pivote (zona `Adjacent` normal) puede chocar de verdad si `shoulder_pan` gira a un ángulo peligroso, se decidió **ajustar el `<limit>` del joint** para excluir ese ángulo del rango planificable, en vez de dejar el par habilitado — así `Adjacent` queda seguro porque el ángulo peligroso nunca es alcanzable. Pendiente: medir y ajustar el `upper`/`lower` real de `shoulder_pan` en el xacro.

## Archivos de configuración adicionales

Agregados en `config/`: `initial_positions.yaml`, `joint_limits.yaml`, `kinematics.yaml`, `moveit_controllers.yaml`, `pilz_cartesian_limits.yaml`.

**Typos de nombre de archivo corregidos:** `joints_limits.yaml` → `joint_limits.yaml` (singular, nombre que MoveIt2 busca por convención), `pliz_cartesian_limits.yaml` → `pilz_cartesian_limits.yaml` (planner Pilz).

**`joint_limits.yaml` — velocidades muy por encima del hardware real.** Config original: `max_velocity: 10.0` (rad/s) para todos los joints. Cálculo de conversión steps/s → rad/s: `rad/s = steps/s × (2π / 4096)` (4096 steps = 1 vuelta del servo STS3215).

Se detectó y corrigió un error propio: inicialmente se citó "techo de hardware ~50 RPM (~5.22 rad/s)" tomado de un comentario en `STServo_Python/servo_control_ui.py` que decía "(STS3215 datasheet)" sin haber verificado la fuente real — el repo no tiene el datasheet en PDF, solo un mapa de registros XLS. Se buscó el dato real en la wiki oficial de Waveshare (mismo fabricante que el Bus Servo Adapter usado en el proyecto, ver [E03](bitacora-E03-adaptador-logico.md)/[E04](bitacora-E04-protocolo-driver.md)):

> No-load Speed: 0.222 sec/60° (**45 RPM @ 12V**) — https://www.waveshare.com/wiki/ST3215_Servo

El voltaje real de la fuente del proyecto es 12.3V (medido con multímetro, ver `assembly.md`), así que el dato de 12V aplica directamente. Corroborado por medición independiente de terceros (~46 RPM máx., ~45.6 RPM promedio al 100%) en https://servodatabase.com/servo/feetech/sts3215.

Techo real: `45 RPM × 2π/60 ≈ 4.71 rad/s`. Comparación:
- `max_velocity: 10.0` configurado ≈ **2.1×** el techo real del servo — MoveIt2 podría planear velocidades que el hardware no puede alcanzar (error de seguimiento de trayectoria, o estrés mecánico en piezas PLA dado el backlash ya investigado en [R04](BACKLOG.md)).
- Contra el perfil "gentle" ya validado en el brazo real (`MOVE_SPEED = 300` steps/s ≈ 0.46 rad/s): ≈ **21.7×** más rápido.
- `max_acceleration: 5.0` (rad/s²) configurado queda por debajo del techo real (`ACC_MAX=50` unidades × 100 steps/s² × 2π/4096 ≈ 7.67 rad/s²) — más discutible, no tan urgente como la velocidad.

**Valores finales elegidos:** margen del 10% por debajo del techo real del servo, en vez de usar el techo absoluto o el perfil gentle directamente (compensa con `default_velocity_scaling_factor: 0.1`/`default_acceleration_scaling_factor: 0.1`, que ya están en el archivo, para la velocidad efectiva de uso normal).

- `max_velocity`: `45 RPM × 0.9 = 40.5 RPM` → `40.5 × (2π/60) ≈ 4.24 rad/s`.
- `max_acceleration`: techo real `ACC_MAX = 50` unidades × `100 steps/s²`/unidad `= 5000 steps/s²` → `× (2π/4096) ≈ 7.67 rad/s²`. Con el mismo margen del 10%: `7.67 × 0.9 ≈ 6.90 rad/s²`.

Ambos valores aplicados a los 6 joints (`shoulder_pan`, `shoulder_lift`, `elbow_flex`, `wrist_flex`, `wrist_roll`, `gripper`) en `joint_limits.yaml`. Se evaluó usar YAML anchors/aliases (`&`/`*`) para no repetir el bloque 6 veces, pero se decidió mantenerlo explícito por ahora, para entender bien cada bloque mientras se aprende.

**Resto de archivos, revisados sin hallazgos:** `kinematics.yaml` (KDL, `position_only_ik: true` — correcto, el brazo tiene 5 joints revolutos = 5 DOF, no alcanza para pose completa de 6 DOF). `moveit_controllers.yaml` (nombres de controller `arm_controller`/`gripper_controller` coinciden con los reales, usados también en `slider_control.py`). `initial_positions.yaml` (joints con nombres reales, todos en 0).

**`pilz_cartesian_limits.yaml` — dejado con los defaults del curso, sin adaptar.** A diferencia de `joint_limits.yaml`, acá no hay una conversión limpia de una sola variable (la velocidad cartesiana en la punta depende de la pose y de qué joints se mueven a la vez, no de un solo servo). Estimación gruesa con los offsets reales del xacro: brazo de palanca desde `shoulder_lift` hasta la punta ≈ suma de `upper_arm_link` (0.116 m) + `lower_arm_link` (0.135 m) + `wrist_link` (0.064 m) + `gripper_link` (0.036 m) ≈ **0.35 m**. A `max_velocity` (4.24 rad/s), el peor caso en la punta sería `4.24 × 0.35 ≈ 1.48 m/s` — por encima del `max_trans_vel: 1.0` configurado, así que el límite cartesiano actual no es un valor inofensivo/permisivo, actúa como tope real. Se decidió dejarlo así por ahora: no bloqueante (no hay `launch/` todavía que use el planner Pilz), y la validación precisa requeriría simulación, no cálculo a mano.

## DDS y cambio a Cyclone DDS

El curso indica que la documentación de MoveIt2 reporta problemas con la implementación default de DDS de ROS2, y pide cambiar a Cyclone DDS.

**Qué es DDS:** protocolo de middleware (estándar OMG) que ROS2 usa por debajo para toda la comunicación (tópicos, servicios, acciones — ver tabla más arriba). A diferencia de ROS1 (que necesitaba un `roscore` central), ROS2 no tiene nodo maestro: cada nodo se descubre con los demás vía DDS (multicast UDP), de forma descentralizada. ROS2 no implementa DDS desde cero — delega en una implementación externa a través de la interfaz **RMW** (ROS MiddleWare), intercambiable con la variable de entorno `RMW_IMPLEMENTATION`. Implementaciones disponibles: Fast DDS (eProsima, default histórico), Cyclone DDS (Eclipse), Connext DDS (RTI, comercial).

**Instalación:**
```bash
sudo apt install ros-jazzy-rmw-cyclonedds-cpp
```

**Activación**, agregada al final de `~/.zshrc` (no `.bashrc`, porque el shell del usuario es zsh — mismo criterio que el alias `ros_jazzy`):
```bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
```

Es solo una variable de entorno — no depende de haber corrido `ros_jazzy` antes o después; se exporta automáticamente al abrir cualquier terminal nueva (por estar en `.zshrc`), y recién se usa cuando un programa ROS2 arranca y la lee para decidir qué librería de RMW cargar.

**Validación:** `echo $RMW_IMPLEMENTATION` en una terminal nueva — debe devolver `rmw_cyclonedds_cpp`.

## Launch de MoveIt2 (`launch/moveit.launch.py`) y prueba end-to-end

Creado `launch/moveit.launch.py`, usando `MoveItConfigsBuilder("robotic_arm", package_name="robotic_arm_moveit")` para armar la config (URDF, SRDF, `moveit_controllers.yaml`), y dos nodos: `move_group` (paquete `moveit_ros_move_group`) y `rviz2` con los parámetros de MoveIt cargados.

**Hallazgos en `package.xml`:**
- Typo: `<exec_depend>moveit_config_utils</exec_depend>` — falta la "s", el paquete real es `moveit_configs_utils` (coincide con el import real en `moveit.launch.py` línea 3, que sí está bien escrito).
- Falta declarar `moveit_ros_move_group` como `exec_depend` (es el paquete que provee el ejecutable `move_group` que el launch arranca). El proyecto de referencia (`arduinobot_moveit`) sí lo declara.
- Ninguno de los dos bloqueó el build porque `colcon build` no valida existencia de paquetes en `exec_depend` (eso lo haría `rosdep`) — pendiente de corregir para que el `package.xml` documente correctamente las dependencias reales.

**`config/moveit.rviz` no existe:** el launch le pide a RViz cargar `config/moveit.rviz` (línea 49), que no está creado. No bloqueó nada — RViz abrió con layout vacío, y se configuró a mano (Fixed Frame `world` + display "MotionPlanning").

**Prueba end-to-end, exitosa:**

Secuencia de terminales:
```bash
# Terminal 1
colcon build
. install/setup.zsh
ros2 launch robotic_arm_description gazebo.launch.py

# Terminal 2
ros2 launch robotic_arm_controller controller.launch.py
ros2 launch robotic_arm_moveit moveit.launch.py
```

En RViz, dentro del display "MotionPlanning": en la pestaña **Context** se verificó que el planner sea OMPL (`ros-jazzy-moveit-planners-ompl`, ya estaba instalado); en la pestaña **Planning** se marcó **"Approx IK Solutions"** (relevante porque el robot tiene `position_only_ik: true` — 5 DOF, no siempre hay solución exacta de IK). Con el planning group `arm` seleccionado, se arrastró la esfera interactiva (goal) desde la pose actual (modelo rojo) y se presionó **Plan & Execute**.

**Resultado:** el brazo llegó al goal correctamente — tanto en RViz como en el modelo simulado de **Gazebo**, confirmando el flujo completo: MoveIt2 planifica → manda la trayectoria por la acción `FollowJointTrajectory` → `arm_controller` de `ros2_control` la ejecuta sobre el modelo simulado. Cierra el objetivo central de R05.

**Prueba del grupo `gripper`:** mismo procedimiento, con una diferencia — al ser un solo joint revoluto (no una cadena que necesite IK), no aparece el marcador interactivo de pose. El goal se generó con la opción **"Random valid"** del Query Goal State en el panel de MotionPlanning. Confirmado OMPL en la pestaña Context. Plan & Execute: el gripper llegó al goal correctamente.

Con `arm` y `gripper` planificando y ejecutando bien en simulación, el objetivo central de R05 queda cumplido.

**Pendiente:** corregir el `package.xml` (typo + dependencia faltante), decidir si crear `moveit.rviz`.

## Medición física del límite de `shoulder_pan`

Para resolver el pendiente de la sección SRDF (¿el `<limit>` heredado del URDF original de SO-ARM100 cubre el choque real de este ensamblaje?), se agregó una herramienta de medición a `STServo_Python/servo_control_ui.py` (fuera del modo mentor de R05 — tooling de apoyo, escrita directamente).

**Herramienta agregada:** sección "Medicion de limites (URDF)" en la UI — combobox para elegir servo, lectura en vivo de counts + radianes relativos al cero calibrado (`ZERO_COUNTS = 2048`, mismo valor que `DEFAULT_TARGET` en `calibrate-arm.py`; el `homing_offset` ya está aplicado en la EEPROM del servo, así que el counts crudo ya viene relativo a ese cero), botones "Marcar minimo"/"Marcar maximo" que capturan la posición actual, y una línea `<limit lower=".." upper=".." .../>` calculada automáticamente (ordena lower/upper por si se marcan al revés). Todo se loggea por stdout para poder pegar el log.

**Medición real de `shoulder_pan`** (torque off, movido a mano hasta el punto de contacto):
```
[MEDICION] shoulder_pan (id=1) -> <limit lower="-2.02332" upper="2.09388" .../>
```
(-115.93° / 119.97°)

**Comparación contra el límite actual del xacro** (`lower="-1.91986" upper="1.91986"`, ±110.0°, heredado del URDF original de SO-ARM100 sin verificar hasta ahora):
- Lado negativo: choque real a -115.93°, límite actual en -110° → margen de 5.9°.
- Lado positivo: choque real a 119.97°, límite actual en 110° → margen de 9.97°.

**Conclusión:** el límite heredado, pese a no haber sido derivado de este ensamblaje físico, sí cubre el choque real con margen en ambos lados — **no hace falta modificar el `<limit>` del xacro**. Confirmado con medición real, no con una suposición. (Asunción usada en el cálculo: el signo de los counts crudos coincide con el signo del eje del joint sin inversión, válido porque `drive_mode: 0` en `calibration.json` para este servo.)

Con esto, el pendiente de `shoulder_pan` marcado en la sección SRDF queda resuelto: el `disable_collisions reason="Adjacent"` de `base_link`–`shoulder_link` es seguro tal como está.
