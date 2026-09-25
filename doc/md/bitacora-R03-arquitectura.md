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

#### Estado `ENSEÑANZA` (agregado con teach & repeat)

Cuarto estado del ESP32. Es el único estado de la aplicación (R4.1) que cambia el comportamiento del ESP32:

- Corta el torque de los joints del brazo (el usuario lo mueve a mano).
- Mantiene el torque del gripper y hace que siga al **potenciómetro**.
- Sigue informando posiciones a la PC (para capturar waypoints).

Transiciones de entrada y salida de `ENSEÑANZA`: pendientes (junto con el flujo de la botonera).

#### Dos niveles de estados

| Nivel | Dónde vive | Estados | Para qué |
|---|---|---|---|
| Seguridad / hardware | ESP32 | `DETENIDO`, `INICIALIZADO`, `OPERANDO`, `ENSEÑANZA` | Si el brazo puede moverse y cómo se comportan los servos |
| Aplicación (R4.1) | `arm_supervisor` | listo, enseñanza, grabando, guardado, reproduciendo, finalizado, error, detenido | Lo que ve el usuario y la lógica de la tarea |

El ESP32 no necesita saber si algo está "guardado" o "finalizado": esos estados viven solo en el supervisor. Solo "enseñanza" tiene su par en el ESP32, porque cambia qué hacen los servos.

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

### Nodo `arm_supervisor`

La lógica de parada, `S` y home va en un **nodo nuevo**, no en el `task_server`:

1. **El `task_server` no es el único que mueve el brazo:** RViz (Plan & Execute) y `slider_control` hablan directo con `move_group` o con los controllers. Si la parada viviera en el `task_server`, un movimiento desde RViz no se cancelaría.
2. **Responsabilidades distintas:** el `task_server` ejecuta tareas; el supervisor vigila el estado del sistema.
3. **Tiene que estar siempre vivo**, aunque el `task_server` no se lance.

| Nodo | Responsabilidad |
|---|---|
| `arm_supervisor` (nuevo) | Máquina de estados del brazo (DETENIDO / INICIALIZADO / OPERANDO). Manda `S` y va a home. Al recibir la parada, cancela la trayectoria activa **del controller** (sin importar quién la mandó). Publica el estado del sistema. Decide cuándo capturar waypoints. **Dueño del display**: publica en `/esp32/display` qué muestra el LCD (conoce el estado de la aplicación y la selección actual). |
| `task_server` | Ejecuta movimientos. Hoy: 7 tareas fijas (0–6). Crece para "ir a home" y "ejecutar una grabación" (recorrer la lista de waypoints que le pasa el supervisor). **Ejecuta, no decide ni guarda:** no lee archivos ni sabe de botones. Consulta el estado y rechaza goals si el brazo no está en OPERANDO. |
| `recording_manager` (nuevo) | Persistencia de grabaciones (waypoints + gripper): servicios `save`, `list`, `get`, `delete`. |

**No es lifecycle node:**

- No toma recursos propios (el puerto lo tiene el plugin): no hay nada que abrir en `on_configure` ni liberar en `on_cleanup`.
- Evita mezclar dos máquinas de estados: la del **brazo** (DETENIDO/INICIALIZADO/OPERANDO) y la del **nodo** (`unconfigured`/`inactive`/`active`).
- Un supervisor desactivable es un riesgo: en `inactive` dejaría de cancelar trayectorias ante una parada.
- Donde sí encaja lifecycle es en el plugin (pendiente de `[R09]`: abrir el puerto en `on_configure`). Reconsiderar si el supervisor llegara a tomar un recurso propio.

### Comunicación plugin ↔ `arm_supervisor`

El puerto serie lo tiene el plugin `RoboticArmInterface`, que no es un nodo: corre dentro del `controller_manager`. El curso no resuelve esto (el plugin del arduinobot no publica ni usa GPIO: no tiene botones ni eventos).

Opciones evaluadas (verificadas en Jazzy):

| | A: topics desde el plugin | B: GPIO + `GpioCommandController` |
|---|---|---|
| Parecido a `[R10]` | Alto (publisher/subscriber como `simple_serial_*`) | Bajo |
| Qué se toca | Solo el plugin | Plugin + xacro (`<gpio>`) + YAML de controllers |
| Formato | Libre (p. ej. `String` con el código) | Solo `double` |
| A cuidar | `RealtimePublisher`: `read()` corre en el loop de tiempo real | Lo resuelve el controller |

**Decisión: opción A** (más simple, patrón conocido, todo contenido en el plugin). B es la más idiomática de `ros2_control`; queda como alternativa si A se complica.

- **Eventos (`E` → supervisor):** en `on_init`, publisher de tiempo real (`realtime_tools::RealtimePublisher`) creado con `get_node()` (`hardware_component_interface.hpp:580` en Jazzy), en un topic tipo `/esp32/events`. `read()` publica cada `E` parseado.
- **Start (supervisor → `S`):** suscripción a un topic tipo `/esp32/start`. El callback solo levanta una **bandera atómica**; `write()` la consume, arma la trama `S` y la manda. La escritura al puerto sigue pasando solo por `write()` y nunca se cruza con una `M`.

### Pendiente

Tabla de códigos de `E`:

| Código | Evento |
|---|---|
| ? | Inicializado (respuesta a `S`) |
| ? | Parada por botón 1 (torque mantenido) |
| ? | Grip liberado (botón 1, 2.ª pulsación) |
| ? | Torque cortado (botón 2) |
| ? | Errores (servo que no responde, trama inválida, …) |

## Teach & repeat (enseñar y repetir)

### Qué piden los requisitos

De `doc/md/requisitos.md`, `tables/PLAN_CSV/REQUERIMIENTOS.csv` y `doc/img/display_y_boyonera.png`:

| Requisito | Qué implica |
|---|---|
| R3.0–R3.2 | Grabar una secuencia durante la enseñanza y reproducirla. `requisitos.md:81` define trayectoria como "secuencia ordenada de posiciones articulares y tiempos asociados". |
| R3.3 / R3.4 | Seleccionar y borrar trayectorias guardadas. |
| R3.5 | Persistencia después de apagar: se guardan en disco del lado de la PC. |
| R4.0 + imagen | LCD 20×4 (HD44780) y botonera GRABAR / SELECCIONAR / REPRODUCIR / BORRAR. La parada va aparte (R5.1). |
| R4.1 | Estados mínimos: listo, enseñanza, grabando, guardado, reproduciendo, finalizado, error, detenido. |
| R5.3 | Posición de referencia (home) antes de reproducir. |
| R6.0 | La "PC" es una **computadora integrada** en el brazo: sin computadora externa en uso normal. |

### Decisión: waypoints + MoveIt (primera etapa)

Los requisitos se escribieron antes de conocer MoveIt. En vez de grabar una trayectoria continua (posiciones + tiempos) y reproducirla directo por el `joint_trajectory_controller`, en la primera etapa se graban **waypoints** y MoveIt planifica entre ellos (programación por waypoints, como muchas teach pendants industriales).

- **Una grabación = lista ordenada de waypoints.** Cada waypoint guarda las posiciones de los joints del brazo y el estado del gripper.
- A favor: reusa MoveIt y el `task_server` (verificados en `[R07]`/`[R12]`); trayectorias suaves y dentro de límites; sin temblores de la mano ni backlash (`[R04]`) grabados; grabación simple (posicionar y apretar).
- En contra: el camino **entre** waypoints lo decide MoveIt, no el usuario. MoveIt solo evita lo modelado en la escena: un obstáculo real no modelado puede chocarse aunque el usuario lo haya esquivado al enseñar. Se resuelve agregando waypoints intermedios.
- La grabación continua queda como mejora futura.
- **Pendiente:** ajustar en `requisitos.md` la definición de trayectoria (hoy incluye "tiempos asociados") para que coincida con lo implementado.

### Gripper: potenciómetro como el slider de `servo_control_ui.py`

En la aplicación real no hay slider (botonera o consola de Linux). Se agrega un **potenciómetro** en el ESP32 que controla la **posición** del gripper, igual que el slider de `servo_control_ui.py`.

- **Al enseñar:** el gripper **mantiene torque** y sigue al potenciómetro; los joints del brazo quedan sin torque para moverlos a mano (igual que `GRIPPER_SERVO_ID` en `servo_control_ui.py:84-86`).
- **Se graba el valor pedido por el potenciómetro, no la posición leída del gripper.**

Hallazgo en `servo_control_ui.py`: `record_position()` (línea 277) devuelve `_last_position`, la posición **leída** (`set_feedback`, línea 284). En el gripper eso es el **punto de contacto** con la pieza: al reproducir, el servo va justo ahí y la fuerza es casi nula (toca sin apretar). La fuerza al enseñar viene de la diferencia entre lo pedido (más cerrado que la pieza) y el contacto; grabando lo pedido, al reproducir vuelve a apretar.

Tradeoffs aceptados:

- La fuerza depende de cuánto se cerró de más respecto de la pieza: si la pieza varía de tamaño, varía el agarre.
- **Protección de sobrecarga del STS3215** (valores por defecto): si la carga supera el **80 %** (registro 36) durante **2 s** (registro 35), el servo baja a **20 %** de torque (registro 34) y puede aflojar el agarre en pleno traslado. Limitar el recorrido útil del potenciómetro o avisar en el display.

Alternativas evaluadas (quedan como mejora futura):

- **B — estado binario + torque limitado:** grabar `abierto`/`cerrado` y cerrar con el registro *Torque limit* (48). Fuerza independiente del tamaño de la pieza. El potenciómetro podría controlar la **fuerza** en vez de la posición.
- **C — corriente:** grabar la corriente del gripper (registro 69, 6.5 mA/unidad, hasta 3250 mA) al enseñar y cerrar hasta alcanzarla al reproducir.

Registros relevantes del STS3215 (`doc/datasheets/ST3215 memory register map-EN.xls`): no hay sensor de fuerza. *Current current* (69) es la mejor estimación (torque ≈ proporcional a la corriente; el datasheet no da la constante, sirve para comparar o hay que calibrar). *Current load* (60) es el duty cycle del control, más indirecto.

### Pendiente

- Botonera: cómo distinguir "marcar un waypoint más" de "terminar y guardar la grabación".
- Transiciones de entrada y salida del estado `ENSEÑANZA` del ESP32.
- Formato y almacenamiento persistente de las grabaciones en la PC.
- **Display (decidido):** la lista de grabaciones llega al LCD por el CID `D`. Cambia su definición: hoy manda el **ID** de un mensaje prearmado en el ESP32; tiene que pasar a llevar **texto** (líneas a mostrar), porque la lista es dinámica. Como el puerto lo tiene el plugin, hace falta un tercer topic (p. ej. `/esp32/display`) que el plugin escuche y convierta en tramas `D`.
  - Payload: `msg1 FS msg2 FS ... FS msgN`, cada mensaje de hasta **15 caracteres** (en un LCD de 20 columnas deja 5 para prefijo `1: ` y cursor `>`).
  - Una pantalla completa (LCD 20×4) son 4 mensajes: payload 4×15 + 3 = 63, LEN 67. No entraba con el tope de LEN = 64 (payload máx. 60); **se subió el tope a 128** (`kMaxLen` en el plugin y en el firmware). Es un límite de cordura del parser: subirlo no rompe nada. A 115200, ~130 bytes ≈ 11 ms, y `D` solo se manda al cambiar la pantalla.
  - Mensajes en **ASCII imprimible**: no pueden contener `0x02`, `0x03` ni `0x1C` (romperían la trama), y el ROM del HD44780 no tiene `ñ` ni tildes en las posiciones estándar.
- **Quién graba (decidido):** el `arm_supervisor` decide cuándo capturar un waypoint (orquestación, ya escucha los `E`); un nodo nuevo y **aparte**, `recording_manager`, guarda/lista/lee/borra grabaciones en disco con servicios (`save`, `list`, `get`, `delete`). Alineado con R6.1 (módulos separados: grabación, gestión, reproducción…) y permite cambiar el formato de almacenamiento sin tocar la lógica de estados. Tradeoff: un nodo más para lanzar y mantener. Pendiente: formato del archivo y definición de los servicios.
- Ajustar la definición de trayectoria en `requisitos.md`.

### RViz opcional (`use_rviz`)

`moveit.launch.py` lanza RViz siempre. En modo real la computadora va integrada en el brazo (R6.0), probablemente sin monitor: RViz sobra o falla sin pantalla. Se agrega un argumento `use_rviz`:

- `simulated_robot.launch.py`: `use_rviz:=True` (en simulación siempre se quiere ver el brazo).
- `real_robot.launch.py`: `use_rviz:=False`; se activa a mano si se conecta una pantalla.
- Operación desde la consola de Linux (`ros2 action send_goal`, etc.) no necesita RViz.

Pendiente de implementar.

## Nodos del sistema

| Nodo | Paquete | Rol |
|---|---|---|
| `robot_state_publisher` | `controller.launch.py` (real) / `gazebo.launch.py` de `description` (sim) | Publica las TF a partir del URDF y `/joint_states` |
| `controller_manager` + plugin `RoboticArmInterface` | `controller.launch.py` (real) | Habla con el ESP32 por puerto serie (tramas `M`). En sim, el `controller_manager` lo aporta el plugin `gz_ros2_control` dentro de Gazebo |
| `joint_state_broadcaster`, `arm_controller`, `gripper_controller` | `controller` | Controllers de `ros2_control` |
| `move_group` | `moveit` | Planificación y ejecución de trayectorias |
| `task_server` | `remote` | Action server que expone tareas predefinidas sobre MoveIt2 |
| `slider_control` | `controller` | Control manual por sliders (pruebas) |
