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

> **Revisada en el diseño (DS2):** sin lectura periódica. Lo que sigue queda como registro de la decisión original.

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

## Diseño

Mapa del código de partida (`esp32_arm`, `pot_nav_lcd_idf`, plugin), con dos hallazgos:

- El watchdog de enlace solo se revisa cuando llega una trama (`arm_state.c:29-38`, sin timer): si el plugin se cae, el ESP32 no vuelve a `DETENIDO` hasta la próxima trama. Con D1, HOME quedaría en "TAREA EN CURSO" y CTL bloqueado para siempre: hay que revisarlo periódicamente.
- El plugin no usa `info_.limits`; ante una respuesta a la `S`, hoy toma la `M` como feedback y la `E` solo la loguea.

### DS1 — Una sola tarea dueña del bus de servos (UART2)

- Hoy `pc_rx` es dueña de UART0 y UART2 (lee la trama y hace el `SYNC_WRITE`/`SYNC_READ` en línea). Con CTL hay dos productores de comandos (las `M` de la PC y la UI) y en `DETENIDO` la PC puede seguir mandando `M` mientras se usa CTL: si dos tareas usan UART2, se mezclan las respuestas.
- Una tarea dueña del bus recibe los pedidos por cola (`pc_rx` → `M`, `ui_task` → objetivos de CTL) y ejecuta una **única transacción** "escribir objetivos (si se puede mover) + `SYNC_READ`", la misma para `M` y para CTL. Publica el feedback para el LCD y responde la `M` a la PC.
- Descartado: mutex sobre UART2 (`arm_state` compartido entre tareas, lógica del bus repartida, una `M` esperando a `ui_task` de prioridad 2).

### DS2 — Lecturas solo con cada comando (revisa D6)

- Los servos se leen únicamente en la transacción de DS1: con cada `M` de la PC (50 Hz, haya sesión o no) y con cada comando de CTL. Sin lectura periódica ni lecturas de asentamiento.
- Con PC conectada, NAV muestra la pose viva. En `DETENIDO` sin PC, NAV muestra el último valor leído; al arrancar sin PC no hay valor hasta el primer comando de CTL.
- Aceptado: al soltar el potenciómetro la última lectura puede quedar a mitad de camino (se toma ~1 ms después del comando, con el servo en viaje), unos grados distinta de la pose final.
- Descartados: lectura periódica sin `M` (D6 original) y lecturas de asentamiento (~1 s después del último comando de CTL).

### DS3 — La tarea del bus es dueña de `arm_state` y revisa el watchdog

- `pc_rx` queda como parser + reenvío: manda las `M` y las `S` a la cola de la tarea del bus. La `S` también necesita el bus (D3: responde `M` con la posición), y ahí mismo se resuelven D2 (rechazo si hay CTL) y D5 (límites).
- Watchdog: la tarea del bus espera su cola con timeout (del orden de 100 ms) y en cada vuelta revisa el enlace, haya llegado algo o no.
- Descartado: `arm_state` en `pc_rx` + `esp_timer` para el watchdog (estado compartido entre tres contextos con mutex, y la lógica de la `S` partida en dos lugares).

### DS4 — Dos dueños; la tarea del bus es la autoridad para CTL

- Dos máquinas de estados, como los "dos niveles" de `[R03]`: la tarea del bus es dueña de `arm_state` (seguridad) y `ui_task` de `app_state` (pantallas, NAV/CTL). El camino crítico (`M` a 50 Hz, watchdog y, en `[E09]`, la parada por botón) no depende de `ui_task` (prioridad 2, hasta ~12 ms dibujando por I2C).
- Para que D2 no tenga carrera (la UI entra a CTL justo cuando se acepta una `S`), la UI **pide** entrar a CTL y la tarea del bus lo **concede o niega** según `arm_state`; también se le avisa la salida de CTL. Mientras CTL esté concedido, la tarea del bus rechaza la `S`; sin CTL concedido, ignora los comandos de CTL.
- Descartada: tarea orquestadora dueña de las dos máquinas. Resuelve D2 sin carrera, pero un problema de la UI (bug, ráfaga de eventos) frenaría el camino de seguridad, una sola cola mezclaría las `M` a 50 Hz con teclas y pote, y se perdería la separación seguridad/UI de `[R03]`.

### DS5 — Canales tarea del bus → UI: eventos + buzón

- Se reusan los dos canales de `pot_nav_lcd_idf`:
  - **Cola de eventos** de `app_state` (16 lugares): dos eventos nuevos, el cambio de estado (`DETENIDO`/`OPERANDO`) y la respuesta al pedido de CTL (DS4). Son raros y no se pueden perder; `app_state` los procesa y se testean como cualquier tecla.
  - **Buzón de 1 elemento** (sobrescritura): las posiciones leídas, que hoy produce `joint_sim_task` (desaparece). `ui_task` lo lee como hoy (`screens_draw_joint_values`). Conversión mrad → grados en el borde.
- Descartados: todo por el buzón (hay que detectar cambios en `ui_task` para generar los eventos igual) y todo por la cola (50 posiciones/s saturan 16 lugares; `app_event_t` lleva un solo `value`).

### DS6 — Los límites viajan en el buzón de posiciones

- La struct del buzón lleva las 6 posiciones leídas **y** los límites vigentes (los por defecto o los de la última `S`, D5). La UI siempre tiene los actuales sin otro canal; el costo son 12 valores más por cada `M` (despreciable).
- Al pasar los límites a grados enteros se redondea **hacia adentro** (p. ej. −1.64 rad = −93.97° → −93°) para no salirse del URDF.
- Descartados: un buzón aparte para los límites (un canal más, leído en el momento justo) y que la UI mande porcentajes (rehacer `app_state` y `screens`; el jog dejaría de ser en grados).

### DS7 — Si la `S` es rechazada, falla la activación del plugin

- `syncWithArm()` espera la respuesta a la `S` (D3); si llega un `E` de rechazo, `on_activate()` devuelve `FAILURE` con un mensaje claro (p. ej. "ESP32 rejected start: panel in CTL mode, exit CTL and relaunch"). Hay que relanzar el sistema.
- El rechazo es raro y deliberado (alguien está usando el panel): relanzar es un precio aceptable a cambio de no dejar el plugin en un estado intermedio.
- Descartado: reintentar la `S` hasta que la acepten (más lógica, activación bloqueada y `controller_manager` sin responder mientras espera, `M` de mantenimiento para el watchdog).

### DS8 — Códigos de `E`: numéricos de dos dígitos, por rangos

- Payload del `E`: dos dígitos decimales. La tabla vive en `[R03]` y se traduce a texto en el plugin.
- Rangos: `0x` respuestas y estados, `1x` paradas (`[E09]`), `2x` errores.
- `[E10]` define solo los que necesita:

| Código | Evento |
|---|---|
| `01` | `S` rechazada: el panel está en CTL (D2) |
| `20` | Los servos no responden (al leer la posición para responder la `S`, D3) |
| `21` | Límites inválidos en el `param_init` de la `S` (DS11) |

- El resto de la tabla de `[R03]` (inicializado ya no hace falta por D3; paradas por botón, otros errores) queda para `[E09]`.
- Descartado: tokens de texto (se leen solos en los logs, pero comparaciones de strings y errores de tipeo que no detecta el compilador).

### DS9 — La lógica de decisión de la tarea del bus extiende `arm_state`

- `arm_state` (C puro, tests en `test_host/test_arm_state.c`) suma a `DETENIDO`/`OPERANDO` y el watchdog: el permiso de CTL (DS4) y la decisión sobre la `S` (aceptar → responder `M`, rechazar → `E` con código, DS8). El permiso de CTL es parte del estado de seguridad (D4, DS4).
- Los límites vigentes (D5) van en un struct simple aparte, sin lógica.
- Descartado: un módulo puro nuevo encima de `arm_state` (un archivo y un test más; seguir una decisión obliga a mirar dos módulos).

### DS10 — Formato de los límites en el `param_init` de la `S`

- Dos bloques con el formato de la `M`: `min1 FS … FS min6 FS max1 FS … FS max6`, mrad `%+05ld` (35 + 1 + 35 = 71 bytes, tope 128).
- Se reusan `m_payload_parse` (ESP32) y `buildMPayload` (plugin) tal cual, una vez por bloque.
- Descartado: pares min/max por joint (parser y armador nuevos).

### DS11 — `S` sin límites o con límites inválidos

- **Sin `param_init`** (p. ej. `tools/send_m.py`): se acepta y se conservan los límites vigentes (por defecto o de una `S` anterior), con un log de advertencia.
- **Inválidos** (bloque mal formado, min ≥ max, fuera de ±3142 mrad): se rechaza con `E` **`21`** "límites inválidos"; el plugin falla la activación con un mensaje claro (DS7). Un bug del plugin se ve enseguida.
- Descartados: aceptar siempre y conservar los vigentes (un error del plugin pasa desapercibido) y rechazar también la `S` vacía (obliga a cambiar `send_m.py`).

### DS12 — JOINTS de solo lectura: indicador fijo + aviso al apretar

- Mientras CTL no esté permitido (sesión activa, D1, o pedido negado, DS4), el modo de la fila 0 muestra `LEC` en lugar de `NAV` (`J1 +045° 1/6 LEC`).
- Si se aprieta el pulsador (o Enter) en ese modo, la fila 1 muestra ~1.5 s el motivo (p. ej. `TAREA EN CURSO`) en lugar de la barra, y después vuelve a la barra. Temporizador en `app_state` con `APP_EV_TICK`.
- Descartados: ignorar la pulsación sin aviso (parece que el pulsador anda mal), solo el indicador (3 letras no se entienden la primera vez) y solo el aviso temporal (no se ve antes de apretar).

### DS13 — Sin salto al entrar a CTL: potenciómetro absoluto con enganche

- Hoy `joint_toggle_mode` (`pot_nav_lcd_idf`) manda de inmediato el ángulo del potenciómetro al entrar a CTL: con el brazo real, el joint saltaría a esa posición.
- *Soft takeover*: al entrar a CTL no se manda nada hasta que el potenciómetro **pasa por** el ángulo actual del joint; desde ahí lo sigue en absoluto (0..100 % = min..max, DS6). Mientras no engancha, la fila 1 indica hacia dónde girar (p. ej. `>>` / `<<`).
- El jog de `w`/`s` (±5°) parte del ángulo actual y desengancha el potenciómetro, que tiene que volver a pasar por el ángulo nuevo (si no, el siguiente movimiento del potenciómetro saltaría la diferencia).
- Requiere conocer el ángulo actual del joint al entrar a CTL (ver la decisión sobre la lectura antes de escribir).
- Descartados: potenciómetro relativo con ancla (recorrido finito: cerca de un extremo no se puede mover hacia ese lado; se pierde el mapeo absoluto) y absoluto inmediato pero lento (el joint igual se mueve solo al entrar a CTL).

### DS14 — CTL escribe solo el joint seleccionado

- `servo_bus_build_sync_write_pos(…, targets, count)` acepta cualquier subconjunto de servos (`servo_bus.h:74`): la transacción de DS1 recibe qué servos escribir (los 6 para la `M`, uno para CTL). La lectura (`SYNC_READ`) sigue siendo de los 6.
- Al conceder CTL (DS4), la tarea del bus hace primero un `SYNC_READ` y concede solo si respondió el servo seleccionado: esa lectura es el ángulo actual para el enganche (DS13).
- Un servo que nunca recibe un comando no se puede mover por error.
- Descartado: escribir los 6 con la posición medida en los 5 quietos (una lectura errónea los movería).

### DS15 — Velocidad y aceleración propias para CTL

- Kconfig nuevos `ARM_CTL_MOVE_SPEED` y `ARM_CTL_MOVE_ACC`, con los mismos valores por defecto que los de las `M` (`ARM_SERVO_MOVE_SPEED` = 300 ≈ 26°/s, `ARM_SERVO_MOVE_ACC` = 10 ≈ 88°/s²). Cada fuente se ajusta por separado: subir la velocidad de las `M` para MoveIt no acelera el panel.
- Descartado: usar los de las `M` (quedan atados).

### DS16 — HOME: solo cambia la fila 1

- Sin sesión: `Enter: menu` (como hoy). Con sesión (`OPERANDO`): `TAREA EN CURSO`. El dibujo del brazo y `BRAZO ROBOTICO` no cambian; el menú sigue funcionando con Enter durante la sesión (D1).
- Descartados: mostrar siempre el estado (`DETENIDO`/`TAREA EN CURSO`, se pierde la pista siempre) y alternar los dos textos con sesión (un temporizador más y difícil de leer).

### DS17 — Arranque del plugin: sondas, pausa y `S` con respuesta

- `syncWithArm()` conserva las `M` de sondeo hasta el primer feedback (ESP32 vivo tras el reset). Después: ~50 ms sin mandar nada y vaciar el buffer de entrada (que no quede la respuesta tardía de una sonda), mandar la `S` (con los límites, DS10) y esperar su respuesta, `M` o `E`, con un timeout menor que el watchdog (~200 ms; el ESP32 responde en ~5 ms). Timeout o `E` → `FAILURE` (DS7).
- Con la `M` de respuesta se cargan estados y comandos (D3), antes de que `write()` mande la primera `M`.
- Descartado: solo la `S` reintentada (una `S` en `OPERANDO` tendría que volver a responder `M` sin cambiar el estado: cambia la semántica de `arm_state`).

### DS18 — Detalles de implementación (aprobados en bloque)

| # | Tema | Decisión |
|---|---|---|
| 1 | Tareas | `bus` prio 11, stack 4096 (nueva, por encima de `pc_rx` para responder rápido); `pc_rx` prio 10 (parser + reenvío); `input` prio 5 (UART1); `panel` prio 5; `ui` prio 2. Desaparece `joint_sim`. Los callbacks del pulsador siguen en `esp_timer` |
| 2 | Cola de la tarea del bus | 8 lugares, `struct` con tipo (`M`, `S`, pedido CTL, salida CTL, comando CTL) y sus datos. `pc_rx` y `ui_task` mandan con espera 0: si está llena se descarta y se loguea (la PC reintenta en 20 ms; el potenciómetro reenvía en su próximo cambio) |
| 3 | Watchdog | Espera de la cola del bus con timeout de 100 ms; se revisa el enlace en cada vuelta. Timeout del enlace: 500 ms (sin cambios) |
| 4 | Componentes | `components/lcd` y `components/lcd_ui` copiados byte a byte de `pot_nav_lcd_idf` en `firmware/esp32_arm/components/`, sin editarlos |
| 5 | Módulos de la UI | Estructura de `pot_nav_lcd_idf` en `main/`: `app/`, `nav/`, `button/`, `pot/`, `sensor/`. Se eliminan `servo/servo_port.h` y `servo_mock.c`: CTL manda a la cola del bus |
| 6 | Kconfig / sdkconfig | Menú `NAV_*` en `Kconfig.projbuild`, más `ARM_CTL_MOVE_SPEED/ACC` (DS15). `CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y` en `sdkconfig.defaults` (ISR del pulsador) |
| 7 | Tests de host | `test_arm_state.c` ampliado (CTL, `S` aceptada/rechazada, límites, DS11), `test_app_state.c` adaptado (`LEC`, enganche, eventos de estado, HOME), copias de los tests de `pot_nav`, `button_core`, `pot_core`, `ui_core`, `lcd_core`, y test del parseo del `param_init` |
| 8 | Prueba con el brazo | (1) Sin PC: entrar a CTL, enganchar y mover un joint; los demás no se mueven. (2) Con `real_robot.launch.py`: HOME `TAREA EN CURSO`, JOINTS `LEC` con valores vivos. (3) Lanzar en CTL: falla con el mensaje de DS7. (4) Matar el plugin: HOME vuelve a `Enter: menu` en ~0.6 s |
| 9 | Documentación | Actualizar `[R03]`: la `S` responde `M`, `param_init` con límites, tabla de `E` (01, 20, 21) |
| 10 | Commits | Por unidad de trabajo: `arm_state` + tests → tarea del bus → UI → plugin → docs |

Diseño cerrado: se puede pasar a la implementación.
