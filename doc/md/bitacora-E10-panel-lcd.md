# Bitácora [E10] — Panel local (LCD + potenciómetro + pulsador) como fuente de control del brazo

## Objetivo

Incorporar a `firmware/esp32_arm` (D8; el pedido original era un firmware nuevo en `firmware/`) lo de `/home/ezequiel/esp-ws/potenciometro/pot_nav_lcd_idf` (LCD 1602 I2C + potenciómetro B1K + pulsador). La pantalla JOINTS de `pot_nav_lcd_idf` (modo NAV elige el joint, modo CTL el potenciómetro mueve el joint) pasa a mover el **brazo real** en lugar del servo MOCK: otra fuente de control además de ROS.

Pedido original:

- En modo CTL se ignoran los comandos `M` que llegan de ROS.
- Mientras el brazo ejecuta una trayectoria ordenada por ROS, el menú queda deshabilitado y HOME muestra "TAREA EN CURSO" (**modificado por D1**).

## Contexto (verificado)

- `ros2_control` manda una `M` cada ciclo a 50 Hz, haya tarea o no (`robotic_arm_controllers.yaml`): "llegan `M`" no significa "hay tarea en curso".
- El comando `S` es *start* (`DETENIDO` → `INICIALIZADO`, ver "Comando S" en `doc/md/bitacora-R03-arquitectura.md`): marca el inicio de la **sesión**, no el de cada trayectoria. Ningún CID del protocolo (`M`/`E`/`S`/`D`) marca hoy el inicio o el fin de una trayectoria. `D` está pensado para que el `arm_supervisor` (todavía no existe) mande texto al display por `/esp32/display`.
- Implementado hoy (`[R08]`): el plugin, en `on_activate()` → `syncWithArm()`, manda `M` (en `DETENIDO` solo leen los servos) hasta el primer feedback, carga estados y comandos con la pose real y recién ahí manda la `S` (`robotic_arm_interface.cpp:262`), una sola vez y sin esperar respuesta. En el ESP32, `main/arm_state.{h,c}`: `STOPPED` → `OPERATING` con `S`, vuelta a `STOPPED` por el watchdog de enlace (500 ms sin tramas).
- Pines de `esp32_arm`: UART0 GPIO1/3 enlace con la PC, consola UART1 GPIO14/27, servos UART2 GPIO17 TX / GPIO16 RX. Los de `pot_nav_lcd_idf` no chocan: LCD GPIO25/26 (SDA/SCL, por el HW-221), potenciómetro GPIO34 (ADC1 canal 6), pulsador GPIO33.
- J6 es el gripper: `m_payload.h:20` ordena los joints `shoulder_pan .. gripper`.
- Los límites de `pot_nav_lcd_idf` son los de la simulación (MOCK). Los reales medidos están en "Calibración final" de `doc/md/bitacora-R08-brazo-real.md`.
- A favor del objetivo: `doc/md/requisitos.md:446` pide como deseable mover cada servo desde la interfaz local ("útil para pruebas").

## Decisiones

### D1 — "Tarea en curso" = sesión de ROS activa (`OPERANDO`)

Alternativas evaluadas: inferirlo de las `M` en el ESP32 (heurística), CID nuevo de inicio/fin, reusar `D`, que lo infiera el plugin, o usar el estado de la sesión. **Elegida: el estado de la sesión.**

- El menú **no** se deshabilita: HOME muestra "TAREA EN CURSO" y las pantallas JOINTS pasan a ser de **solo lectura** (NAV para ver; no se entra a CTL).
- Consecuencia: CTL solo existe en `DETENIDO`, donde las `M` ya solo leen, así que "en CTL se ignoran las `M`" se cumple solo.

### D2 — `S` durante CTL: gana el LCD

Si llega una `S` mientras el usuario está en CTL, se **rechaza**: el ESP32 sigue en `DETENIDO` y no hay salto. Requiere implementar la respuesta `E` y que el plugin la maneje (hoy no espera respuesta: creería que arrancó y el error quedaría oculto).

### D3 — Respuesta a `S`: `M` con la posición actual

- Todo bien → responde **`M` con la posición actual** de los servos. La lectura es atómica con la transición a `OPERANDO`: desaparece la ventana entre el `M` de `syncWithArm()` y la `S` en la que el brazo podía moverse desde el LCD. La PC usa esa pose para planificar (por ejemplo, el viaje a home).
- Falta iniciar los motores o hay un error → responde **`E`**.
- Alternativa descartada: `E` "inicializado" con las posiciones en el payload.

### D4 — CTL según la causa del `DETENIDO`

- CTL habilitado en el `DETENIDO` de arranque o del watchdog de enlace.
- CTL **bloqueado** (JOINTS de solo lectura) después de una parada por botón 1 o 2 (`[R03]`, "Parada de emergencia y estados del ESP32"), hasta una nueva `S`: respeta la parada enclavada y evita devolverle el torque a un solo servo después del botón 2.
- Hoy esos botones no existen (el pulsador de GPIO33 es solo el Enter de la UI): el único `DETENIDO` es el de arranque/watchdog, así que en `[E10]` CTL siempre está habilitado en `DETENIDO`. La distinción se implementa en `[E09]`.

### D5 — Límites de CTL: los del URDF, enviados en la `S`

- El potenciómetro (0..100 %) se mapea a los límites del **URDF corregido** (`robotic_arm.urdf.xacro`): cualquier pose a la que se lleve el brazo desde el LCD es válida para MoveIt. Descartados: la calibración de `[R08]` (más amplia: el brazo podría quedar fuera del URDF y MoveIt no planifica desde ahí; además la EEPROM ya los aplica) y el xacro de `ros2_control` (desactualizado, `[R14]`).
- La PC manda los límites en el `param_init` de la `S` (`[R03]`, "Comando S", hasta hoy "contenido a definir"): 6 pares min/max en mrad (~70 bytes, tope 128). El plugin los toma de `hardware_interface::HardwareInfo::limits` (límites del URDF parseados, `hardware_info.hpp:421-423` en Jazzy). El URDF queda como única fuente de verdad.
- Antes de la primera `S` el firmware usa límites **por defecto** (copia del URDF actual); cada `S` los reemplaza (solo en RAM). Descartados: guardarlos en NVS (más complejo) y deshabilitar CTL hasta la primera `S` (sin PC el panel no movería el brazo).

| Joint | URDF corregido |
|---|---|
| J1 `shoulder_pan` | −110° … +110° (±1.91986 rad) |
| J2 `shoulder_lift` | −100° … +98° (−1.74533 … +1.71 rad) |
| J3 `elbow_flex` | −94° … +90.5° (−1.64 … +1.58 rad) |
| J4 `wrist_flex` | −95° … +95° (±1.65806 rad) |
| J5 `wrist_roll` | −157° … +155.8° (−2.74385 … +2.72 rad) |
| J6 `gripper` | −10° … +100° (−0.174533 … +1.74533 rad) |

### D6 — JOINTS muestra el feedback real de los servos

- Hoy los servos solo se leen como respuesta a una `M` (`handle_m_frame`, `esp32_arm.c:192`, única tarea `pc_rx_task`): sin PC nadie lee el bus.
- Con la PC conectada se reusa el feedback de cada `M` (50 Hz, sin tráfico extra). Sin `M`, el ESP32 hace una lectura periódica propia (`SYNC_READ`, del orden de 5 Hz).
- Consecuencia: el bus UART2 pasa a tener dos usuarios; hay que serializarlos (una tarea dueña del bus o un mutex) para que no se mezclen las respuestas. Frecuencia y mecanismo: a definir en el diseño.
- Descartados (D6): feedback solo con `M` (el CTL no recibe posición por mandar un comando: `SYNC_WRITE` broadcast no responde y `WRITE` a un ID responde solo el error; con `SYNC_WRITE` + `SYNC_READ` por comando la lectura queda congelada a mitad de camino al dejar quieto el pote) y la simulación (no muestra el brazo).

### D7 — Teclado por la consola UART1

- `pot_nav_lcd_idf` lee las teclas de UART0 (`INPUT_UART UART_NUM_0`, `main.c:67`), que en `esp32_arm` es el enlace binario con la PC: no pueden convivir. El `input_task` pasa a leer la consola **UART1** (RX GPIO27, ya configurado en `sdkconfig.defaults`, 115200).
- Se reusan `app_decode_byte()` y sus tests. Requiere el adaptador USB-serie (CP2102) en GPIO14/27; en esa terminal se mezclan logs y teclas (igual que en `pot_nav_lcd_idf`).
- Descartados: sin teclado (pierde la forma cómoda de depurar la UI) y teclas desde la PC con un CID nuevo (mucho trabajo para algo de depuración).

### D8 — Se evoluciona `firmware/esp32_arm` en el lugar

- Un solo firmware, sobre el que también sigue `[E09]`. Antes de empezar se marca con un tag la versión probada en `[R08]` (p. ej. `r08-arm-ok`) para poder volver a ella.
- Descartado: carpeta nueva (dos firmwares que comparten parser, `servo_bus`, `arm_state`, unidades y tests; los arreglos habría que hacerlos dos veces).

## Consecuencias para otras tareas

- `[R03]`: actualizar la regla "todo cambio de estado se informa con `E`" y la definición del comando `S`.
- Plugin (`syncWithArm()`): mandar `S`, esperar la respuesta con timeout y cargar esa pose en estados y comandos **antes** de que `write()` mande la primera `M` (si no, arrastra un objetivo viejo). Manejar el `E` de rechazo.
- `[R03]`: definir el contenido de `param_init` de la `S` (límites de los joints, D5).
- Plugin: armar el `param_init` con `info_.limits` (D5).
- Al cambiar el URDF (`[R14]`, `[R15]`): actualizar también los límites por defecto del firmware (D5).
- `[E09]`: guardar la causa de la parada y bloquear CTL después de una parada por botón 1 o 2 (D4).
- Conflictos con `[R03]`/`[E09]` a resolver: el dueño del display es el `arm_supervisor` (vía `D`), el LCD previsto es 20×4 (acá 1602) y el potenciómetro estaba pensado para el gripper en `ENSEÑANZA`.

## Pendientes

1. ~~Cómo se entera el ESP32 de que hay una tarea en curso~~ → D1.
2. ~~El salto del brazo al pasar del LCD a ROS~~ → D2 y D3.
3. ~~CTL frente al estado `DETENIDO`~~ → D4.
4. ~~Límites de los joints~~ → D5.
5. ~~JOINTS: feedback real o simulación~~ → D6.
6. ~~Navegación por teclado~~ → D7.
7. ~~Nombre de la carpeta del firmware nuevo~~ → D8.

Todas las definiciones cerradas: se puede pasar al diseño.
