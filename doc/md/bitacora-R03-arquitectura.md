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

### Nodos de aprendizaje fuera del sistema final

- **`simple_serial_transmitter` / `simple_serial_receiver`** (`[R10]`): no van en el sistema final. Abren el mismo puerto serie que el plugin `RoboticArmInterface`. En Linux dos procesos pueden abrir el mismo `/dev/tty*` sin error: cada byte lo recibe uno solo de los lectores (las tramas `M` llegan partidas) y las escrituras se intercalan. Falla silenciosa ("a veces no responde"). Solo sirven para depurar el ESP32 con el plugin apagado.
- **`simple_lifecycle_node`** (`[R11]`): eliminado de `robotic_arm_remote` (paquete del sistema real). Queda en el commit `370bab1` y documentado en la bitácora de `[R11]`.

## Parada de emergencia y estados del ESP32

### Quién detiene el brazo

La parada la ejecuta el **ESP32**, lo más cerca posible de los servos y sin depender de la PC. Si la parada pasara por ROS (`E` → `read()` → topic → nodo → cancelar → `write()`), la latencia se suma en cada salto, y si algo de la PC está colgado el brazo no para. Después de detener, el ESP32 **informa** a la PC con un evento `E`.

### Botones

| Botón | Acción | Efecto |
|---|---|---|
| 1 — parar/soltar | 1.ª pulsación | Frena y **mantiene torque en todos los servos**: el brazo no cae y no suelta la carga |
| 1 — parar/soltar | 2.ª pulsación | Abre el gripper (liberar la carga a mano) |
| 2 — cortar torque | Pulsación | Corta el torque de los joints del brazo para moverlo a mano (liberar algo atrapado entre eslabones) |

- Botón 1 es una **parada controlada con retención**, no un corte de energía: prioriza no soltar la carga.
- Botón 2 hace caer el brazo por gravedad (con la carga, si la sostiene). Es aceptable para liberar a alguien atrapado, por eso es un botón **dedicado**, no una combinación fácil de apretar sin querer.
- Definición de hardware de los botones: pendiente.

### Máquina de estados

```
DETENIDO ──S──▶ INICIALIZADO ──(home + trabajo con M)──▶ OPERANDO
    ▲                                                        │
    └────────────────── botón 1 / botón 2 ◀──────────────────┘
```

- **Parada enclavada (latched):** en `DETENIDO` el ESP32 **ignora las tramas `M`**. `write()` manda `M` cada ciclo (50 Hz) con el objetivo de la trayectoria en curso; si el ESP32 las obedeciera, el brazo retomaría el movimiento 20 ms después de frenar.
- Solo sale de `DETENIDO` con el comando **`S` (start)**: rearmar tiene que ser una acción deliberada.
- Todas las transiciones de estado se informan a la PC con un `E`.

### Comando `S`

```
STX | LEN | S | FS | param_init (opcional) | ETX | LRC
```

- `param_init`: configuración adicional opcional (contenido a definir).
- **Se responde con `E`** (código "inicializado"), no con `S`: la `S` no pide datos, pide un cambio de estado, y todo cambio de estado se informa con `E`. La PC espera ese código con timeout; si no llega, no empieza a mandar `M`.

### Secuencia de operación

1. La PC manda `S` (con `param_init` opcional).
2. El ESP32 responde `E` "inicializado".
3. La PC manda el brazo a **home** antes de empezar el trabajo: el brazo arranca siempre desde una posición conocida (después del botón 2 queda donde lo dejaron a mano).
4. Trabajo normal: `M` ↔ `M`.
5. Botón → el ESP32 frena y manda `E`. La PC cambia de estado y decide qué hacer.
6. Vuelve a esperar `S`.

### Qué hace la PC al recibir el `E` de parada

**Cancelar la trayectoria en curso de inmediato.** Mientras el brazo está detenido, el `joint_trajectory_controller` sigue avanzando y `write()` sigue mandando esos objetivos (el ESP32 los ignora). Al cancelar, el controller sostiene la posición **medida**, que es real porque `read()` usa el feedback de los servos (`[R09]`). Así, después de la `S`, el plan a home parte de donde el brazo realmente quedó y no hay salto.

### Pendiente

Tabla de códigos de `E`:

| Código | Evento |
|---|---|
| ? | Inicializado (respuesta a `S`) |
| ? | Parada por botón 1 (torque mantenido) |
| ? | Grip liberado (botón 1, 2.ª pulsación) |
| ? | Torque cortado (botón 2) |
| ? | Errores (servo que no responde, trama inválida, …) |

## Nodos del sistema

| Nodo | Paquete | Rol |
|---|---|---|
| `robot_state_publisher` | `controller.launch.py` (real) / `gazebo.launch.py` de `description` (sim) | Publica las TF a partir del URDF y `/joint_states` |
| `controller_manager` + plugin `RoboticArmInterface` | `controller.launch.py` (real) | Habla con el ESP32 por puerto serie (tramas `M`). En sim, el `controller_manager` lo aporta el plugin `gz_ros2_control` dentro de Gazebo |
| `joint_state_broadcaster`, `arm_controller`, `gripper_controller` | `controller` | Controllers de `ros2_control` |
| `move_group` | `moveit` | Planificación y ejecución de trayectorias |
| `task_server` | `remote` | Action server que expone tareas predefinidas sobre MoveIt2 |
| `slider_control` | `controller` | Control manual por sliders (pruebas) |
