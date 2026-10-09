# Bitácora [E06] — Manejo del comando `M` en el ESP32

## Objetivo

Firmware del ESP32 que maneja el comando `M` de la PC: parsear las tramas (protocolo de `[R09]`), convertir mrad → cuentas con la calibración, enviar `SYNC_WRITE` al bus de servos, leer las posiciones reales y responder con una `M` de feedback.

El resto del firmware (máquina de estados, parada de emergencia, comandos `E`/`S`/`D`, LCD, botones, potenciómetro) queda para `[E09]`.

## Proyecto

- **Ubicación:** `firmware/esp32_arm/` — no dentro de `robotic_arm_ws/src`, porque colcon detectaría el `CMakeLists.txt` como paquete cmake plano.
- **Creación por CLI** (desde la raíz del repo; `-C` define el directorio donde se crea el proyecto):
  ```
  . ~/esp/esp-idf/export.sh
  idf.py -C firmware create-project esp32_arm
  cd firmware/esp32_arm
  idf.py set-target esp32
  cp ../esp32_bridge/.gitignore .
  ```
- **`sdkconfig` no se versiona** (lo ignora el `.gitignore`, igual que en `esp32_bridge`): lo configurado con `menuconfig` no viaja con el repo. Lo esencial (target + consola) va en `sdkconfig.defaults`, versionado.
- **`sdkconfig.defaults`:** mismo formato que `sdkconfig` (`CONFIG_X=valor`, `#` comentarios); solo las opciones elegidas a mano, no las derivadas (`CONFIG_ESP_CONSOLE_UART=y`, `..._UART_NUM=1`) ni los alias deprecados (`CONFIG_CONSOLE_UART_*`). Verificado generando un `sdkconfig` desde cero solo con este archivo.
- **Prioridad por opción:** `sdkconfig` > `sdkconfig.defaults` > default del Kconfig. Si `sdkconfig` ya existe, cambiar el `.defaults` **no lo modifica** (hay que borrar `sdkconfig` o cambiarlo también en `menuconfig`). Al cambiar algo en `menuconfig` que se quiera conservar, copiarlo al `.defaults`.
  - **Caso real:** se agregó `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` al `.defaults` con un `sdkconfig` local ya existente (con 2MB). La verificación en una carpeta limpia dio 4MB, pero el firmware grabado arrancó con `SPI Flash Size : 2MB` y el warning `Detected size(4096k) larger than the size in the binary image header(2048k)`. Arreglo: borrar el `sdkconfig` local (se regenera desde el `.defaults`, que tiene todo lo configurado) o cambiarlo en `menuconfig` (`Serial flasher config → Flash size`).
- `idf.py save-defconfig` genera el `.defaults` con todo lo que difiere del default, pero **sobrescribe** el archivo y borra los comentarios: usarlo solo para ver diferencias.

## Placa

La DevKitC de 38 pines (WROOM) está ocupada en otra cosa; E06 se desarrolla sobre una placa genérica de 30 pines (tipo "DevKit V1"). Fotos: `doc/img/esp32.jpeg`, `doc/img/esp32-wroom-32.jpeg`, `doc/img/esp32-vs-esp32wroom.jpeg`.

Pinout de referencia de la placa de 30 pines (DevKit V1): https://lastminuteengineers.com/esp32-pinout-reference/ — imagen en `doc/datasheets/esp32_30pin_pinout.png`

Comparación (`esptool.py --port <puerto> flash_id` + serigrafía):

| | Placa nueva (30 pines) | DevKitC WROOM (38 pines) |
|---|---|---|
| Módulo | "ESP-32" genérico | ESP32-WROOM-32 |
| Chip | ESP32-D0WD-V3 rev v3.1 | ESP32-D0WD-V3 rev v3.1 |
| Flash | 4MB a 3.3V (fabricante `0x5e`) | 4MB a 3.3V (fabricante `0x68`) |
| Chip USB | CH9102X → `/dev/ttyACM0` (`cdc_acm`) | CP2102 → `/dev/ttyUSB0` (`cp210x`) |
| GPIO 16/17/21/23, ADC1 (32-39) | ✅ | ✅ |

- **Mismo silicio:** el binario corre en las dos sin cambios; la marca del chip de flash es transparente para ESP-IDF.
- **Sin PSRAM:** `flash_id` no detecta PSRAM externa, pero el blindaje del módulo tiene el mismo tamaño que el WROOM (un WROVER es más largo) y la placa expone `TX2`/`RX2` en GPIO17/16, que en un WROVER estarían ocupados.
- La de 30 pines no expone GPIO6-11 (flash) ni GPIO0 (solo en el botón BOOT): no se pierde nada útil.
- Los números de GPIO son los mismos en las dos; cambia la posición física: **cablear guiándose por la serigrafía de la placa en uso.**

## Arquitectura de UARTs

```
PC (ROS2) <--USB--> ESP32 <--UART TX/RX--> Driver de servos
```

| UART | Uso | Pines | Baudrate |
|---|---|---|---|
| UART0 | Protocolo con la PC (USB de la placa) | TX=1, RX=3 (fijos: cableados al chip USB) | 115200 |
| UART1 | Consola de logs | TX=14, RX=27 | 115200 |
| UART2 | Bus de servos | TX=17, RX=16 (ver `[E01]`) | 1000000 |

Mapa de pines completo:

| Uso | Pines |
|---|---|
| UART0 — PC | 1, 3 |
| UART1 — consola | TX 14, RX 27 |
| UART2 — servos | TX 17, RX 16 |
| ADC1 — potenciómetros (hasta 6) | 32, 33, 34, 35, 36 (VP), 39 (VN) |
| Reservados: I2C futuro (p. ej. acelerómetro) | 21 (SDA), 22 (SCL) |
| Reservados: SPI/SD futura (VSPI nativo) | 23 (MOSI), 19 (MISO), 18 (SCK), 5 (CS) |

Puertos y pines: cada bloque UART (`UART_NUM_0/1/2`) es un solo canal con un solo dueño; los pines se eligen aparte por la matriz GPIO (`uart_set_pin()`). El puerto de los servos es fijo en el código (UART2 es el único bloque libre); hacerlo configurable solo agrega la forma de pisar la consola.

### La consola sale de UART0

Por defecto la consola de ESP-IDF (`ESP_LOGx`, `printf`) sale por UART0, que es el mismo canal del protocolo:

- Texto suelto **entre** tramas: el parser de `read()` en `[R09]` lo descarta (busca `STX`, y el texto ASCII imprimible no contiene `0x02`).
- Un log escrito **en medio** de una trama la corrompe: falla el LRC y la trama se descarta. El ciclo de feedback se pierde sin aviso visible desde la PC.

**Regla: UART0 tiene un solo dueño, el protocolo.** La consola se mueve a UART1 (`menuconfig` → `Component config → ESP System Settings → Channel for console output → Custom UART`).

La consola se configura con opciones **de ESP-IDF** (`ESP_CONSOLE_UART_CUSTOM`, `ESP_CONSOLE_UART_CUSTOM_NUM_1`, `ESP_CONSOLE_UART_TX_GPIO`, `ESP_CONSOLE_UART_RX_GPIO`, `ESP_CONSOLE_UART_BAUDRATE`), no en `board_pins.h` ni en el Kconfig propio: las lee el bootloader y el arranque, antes de `app_main`. Un `#define` propio no mueve la consola.

Los mensajes del bootloader (ROM) siguen saliendo por UART0 al arrancar; el parser los descarta y no hay tramas en vuelo en ese momento.

**Efecto colateral en UART0:** al mover la consola, el bootloader desconecta GPIO1/GPIO3 de UART0 (`bootloader_console.c`: "Change default UART pins back to GPIOs"). El firmware tiene que reconectarlos con `uart_set_pin(PC_UART_PORT, 1, 3, ...)`, o la PC no recibe nada. Por eso los pines de UART0 van como constantes en `board_pins.h`.

En la versión final se puede apagar la consola (`CONFIG_ESP_CONSOLE_NONE`).

### Pines de la consola: TX GPIO14, RX GPIO27

Los pines por defecto de UART1 (GPIO9/10) van a la flash; la señal se rutea a otros pines por la matriz GPIO. La consola custom configura **TX y RX**: si `ESP_CONSOLE_UART_RX_GPIO` queda en su default, usa **GPIO3**, el RX de UART0 (las tramas de la PC llegarían también a UART1, porque un pin de entrada puede alimentar a dos periféricos).

Criterios:

- **Potenciómetros solo en ADC1 (32-39).** El ADC2 lo usa el driver de WiFi: con WiFi activo, `adc_oneshot_read()` sobre ADC2 falla (`ESP_ERR_TIMEOUT`). Los pines de ADC2 siguen funcionando como **digitales**, así que sirven para UART aunque haya WiFi.
- Evitar strapping (0, 2, 5, 12, 15), flash (6-11), I2C convencional (21/22) y VSPI nativo (23, 19, 18, 5).

Descartes:

| Candidato | Motivo |
|---|---|
| GPIO21 (TX), primera elección | Es el SDA del I2C convencional (21/22): se deja libre para un sensor futuro. En ESP-IDF el I2C se rutea a cualquier pin, pero tutoriales y librerías asumen 21/22. |
| GPIO23 | MOSI nativo de VSPI (y no es SCL: el I2C es 21/22). |
| Pines de solo entrada (34-39) como RX | Los ocupa el ADC1 si hay 6 potenciómetros, y **no tienen pull-up interno**: el bootloader intenta activarlo (`esp_rom_gpio_pad_pullup_only`) y el RX quedaría flotando. |
| GPIO12 como RX | **Strapping:** define la tensión de la flash (bajo = 3.3V, alto = 1.8V). Una UART en reposo mantiene su TX en alto: con el adaptador conectado, al arrancar GPIO12 queda en alto, la flash recibe 1.8V y el ESP32 no bootea (sin ningún log). `esptool` lo muestra: "Flash voltage set by a strapping pin to 3.3V". |
| GPIO14 como RX | Según [Random Nerd Tutorials](https://randomnerdtutorials.com/esp32-pinout-reference-gpios/) saca PWM al arrancar (lastminuteengineers no lo menciona; las fuentes no coinciden). Como RX, el PWM del ESP32 pelearía con el TX del adaptador (dos salidas en el mismo cable). |

**Decisión: TX = GPIO14, RX = GPIO27.** Con los pines invertidos el diseño es seguro sea cierto o no lo del PWM: si existe, sale por TX hacia la entrada del adaptador y como mucho aparecen caracteres basura en la consola al arrancar. **Medido:** con el analizador en GPIO14 durante un reset no aparece ningún PWM antes del log del bootloader. En esta placa lo de Random Nerd Tutorials no se cumple; la elección igual queda como TX por ser segura en cualquier placa.

### Cómo leer la consola

- **Ahora:** con el Logic Analyzer (Logic2, decodificador "Async Serial") sobre el TX de UART1 + GND — ver `[E01]`.
- **Después:** con un adaptador USB-serie (pendiente de compra). El firmware no cambia.

### Logs por WiFi (descartado para E06)

Posible con `esp_log_set_vprintf()` + UDP, pero:

- no hay logs antes de que conecte el WiFi, y un panic/backtrace sale por la UART de consola, no por WiFi;
- el stack WiFi consume RAM y CPU y mete jitter en el lazo de control;
- requiere SSID/contraseña, reconexión y receptor en la PC: es un proyecto aparte.

### Logs en tarjeta SD (descartado para E06)

Hay disponible un módulo lector micro-SD (SPI, FAT16, alimentación 5V o 3.3V), pero no sirve como consola de debug:

- no es en vivo: hay que sacar la tarjeta y leerla en la PC;
- ante un crash se pierde lo que estaba en el buffer de FAT, justo lo más relevante;
- la escritura tarda un tiempo variable y ocupa 4 pines SPI + el stack FAT: mete jitter en el lazo de control.

Uso posible a futuro (`[E09]` o tarea propia): **caja negra** con el brazo funcionando sin PC de debug, para registrar eventos (paradas de emergencia, errores del bus, resets).

Antes de cablearlo: estos módulos suelen traer un AMS1117-3.3 y un buffer de nivel; alimentado con 3.3V el regulador puede quedar sin margen. Verificar contra la placa real (como en `[E03]`).

### Adaptador USB-serie

- Tiene que ser **TTL** (no RS232 de ±12V ni DB9) y con lógica a **3.3V**.
- **CP2102:** lógica fija a 3.3V (sin jumper que dejar mal puesto), hasta 1.5 Mbps, driver `cp210x` en el kernel (mismo chip que la DevKitC). Su RX tolera 5V.
- **CH340G / FT232 con jumper:** más versátiles con micros de 5V (TX a 5V con margen completo), pero el jumper en 5V conectado al ESP32 quema el pin. Con un micro de 5V, el CP2102 funciona pero con margen justo (ATmega328 a 5V: V_IH = 3.0V).
- Los pines `5V`/`3.3V` del módulo son **de alimentación**, no de nivel lógico.
- Conexión para consola: **solo GND–GND y RXD del adaptador → TX de consola del ESP32**. No conectar VCC (el ESP32 ya se alimenta por su USB).
- Descartar publicaciones con chip no identificable o mal nombrado (p. ej. "FT2323LL").

## Calibración

**La calibración ya vive en la EEPROM de cada servo**, grabada por `STServo_Python/calibrate-arm.py`:

- **Cero:** offset OFS (registro `0x1F`); en la pose cero todos reportan 2048.
- **Límites de ángulo** min/max (`write_limits()`): el servo recorta por hardware cualquier posición fuera de rango. Es la capa de seguridad más baja, independiente del ESP32 y de ROS.
- **Sentido:** `drive_mode` es 0 en los 6 servos de `calibration.json` (el script no lo modifica): no hay inversiones que aplicar.

El ESP32 no necesita guardar calibración: no hace falta SD ni que la PC la envíe. Si en el futuro necesita parámetros propios (inversiones, límites más estrictos que los del servo), van en la **NVS** (clave-valor en la flash interna, incluida en ESP-IDF), no en la SD.

## Conversión mrad → cuentas

- `counts = 2048 + delta`, con `delta = round(mrad * 4096 / 6283)`.
- `int32_t` alcanza (±3142 mrad).
- La división en C trunca hacia cero, así que `(x + den/2) / den` está mal para negativos (−1571 → −1023 en vez de −1024): redondear sobre el valor absoluto y restaurar el signo.
- Nunca castear a `unsigned`.

Implementación en `firmware/esp32_arm/main/servo_units.c`:

```c
int32_t delta = (abs(mrad) * COUNTS_PER_REV + MRAD_PER_REV / 2) / MRAD_PER_REV;
//              (abs(mrad) * 4096           + 3141            ) / 6283
if (mrad < 0) {
    delta = -delta;
}
```

`delta` es cuántas cuentas se aleja el servo del cero (2048). Tres pasos:

1. **Regla de tres: `abs(mrad) * 4096 / 6283`.** Se multiplica primero y se divide después: dividiendo primero (`mrad / 6283`) el resultado entero daría casi siempre 0.
2. **Redondeo: `+ 3141` antes de dividir.** La división entera descarta los decimales (`1023.9` → `1023`). Sumar la mitad del divisor (6283 / 2 = 3141) equivale a sumar 0.5 al cociente: si la parte decimal era ≥ 0.5 pasa al entero siguiente (redondea para arriba); si era < 0.5 no llega (trunca para abajo).
3. **`abs()` y signo al final.** El truco del paso 2 solo sirve para positivos: con un negativo la división trunca hacia cero (para *arriba*) y sumar 3141 lo acerca todavía más a cero, redondeando para el lado equivocado.

Ejemplo con −1571 mrad (valor exacto −1571 × 4096 / 6283 = −1024.2):

| Paso | Cuenta | Resultado |
|---|---|---|
| `abs(-1571)` | | 1571 |
| `× 4096` | 1571 × 4096 | 6434816 |
| `+ 3141` | | 6437957 |
| `/ 6283` | 1024.66 → trunca | 1024 |
| signo | `mrad < 0` | −1024 |
| `counts` | 2048 + (−1024) | **1024** |

Con `(mrad * 4096 + 3141) / 6283` directo daba −1023 → 1025.

Además:

- **Clamp de la entrada a ±3142** antes de multiplicar: evita el overflow de `mrad * 4096` en `int32_t` (pasa con |mrad| > 524287) y garantiza que la salida nunca sea negativa.
- **+π → 4096**, una más que la última cuenta válida: se recorta a 4095. Sin el recorte, 4096 (`0x1000`) en 12 bits se lee como 0, el extremo opuesto (con `wrist_roll`, límites 0..4095, daría casi una vuelta al revés).
- Verificado con un test en la PC (`gcc`) contra `2048 + round()` en punto flotante para cada mrad de −3500 a 3500: 0 diferencias.

## Parser de tramas (`main/frame_parser.{h,c}`)

Mismo formato y mismo algoritmo de resincronización que el `read()` del plugin (ver `doc/md/bitacora-R09-ros2-control.md`), para que los dos lados se comporten igual.

Decisiones:

- **Separado de la UART:** el parser recibe bytes sin saber de dónde vienen. Se testea en la PC con cualquier secuencia (tramas partidas, basura, LEN corrupto), casos difíciles de provocar con hardware. La tarea que lee UART0 solo le pasa bytes; lo que hace cada trama (convertir con `servo_units`, `SYNC_WRITE`) es otra capa.
- **Estado en un struct (`frame_parser_t`) pasado por puntero:** el equivalente C del `rx_buffer_` miembro de clase del plugin. Sin `static` globales: cada test arranca con `frame_parser_init()` limpio, y otra línea serie tendría su propio struct.
- **Buffer lineal de tamaño fijo, no circular ni `malloc`:**
  - Fijo: en un firmware que corre semanas, la memoria dinámica fragmenta el heap.
  - Lineal con `memmove` al consumir: la trama candidata siempre empieza en `buf[0]` y está contigua (el LRC y el payload se leen directo). Un circular parte tramas en el borde (índices `% SIZE`, payload no contiguo, bugs que solo aparecen en el borde). Copiar ≤133 bytes ~50 veces/s es despreciable.
  - El buffer circular ya existe una capa abajo: el ring buffer de 1024 de `uart_driver_install()`.
  - Tamaño `FRAME_MAX_SIZE = FRAME_STX_SIZE + FRAME_LEN_DIGITS + FRAME_MAX_LEN` = 1 + 4 + 128 = **133** (calculado: si sube el tope de LEN, el buffer acompaña). `SIZE` = trama completa, `LEN` = el campo del protocolo.
- **`feed()` acepta bloques de cualquier tamaño:** el tope de 133 vale para una trama *esperando*, no para "lo que había + lo que llegó" (25 pendientes + un bloque de 200 = 225). `feed()` copia solo lo que entra, parsea (libera espacio) y repite: nunca desborda, sin depender del que llama.
- **Entrega por callback**, no byte a byte: `frame_handler_t(cid, payload, payload_len, ctx)`.
  - El algoritmo de R09 no es una máquina de estados clásica sino **buffer + reescaneo**: ante un error descarta 1 byte y vuelve a buscar en lo que ya tenía. Una máquina de estados (WAIT_STX → LEN → BODY) descartaría la trama entera y perdería una trama buena que empezaba adentro.
  - Con reescaneo, un solo byte nuevo puede completar **dos** tramas; un `feed()` byte a byte solo podría devolver una y la otra quedaría atrapada hasta que llegue otro byte.
  - El callback recibe el CID separado (el que maneja hace `switch (cid)`); `payload` apunta al buffer del parser y solo vale durante la llamada; `ctx` es el estado del que maneja (en el test, la lista de tramas recibidas).
- Además de ETX y LRC, valida que el **FS** esté en su posición.

**LEN corrupto pero en rango:** con un STX espurio seguido de `0020`, el parser espera 25 bytes que no son de esa trama. Las tramas buenas que vienen detrás **no se pierden, se demoran**: cuando llegan bytes suficientes, la candidata falsa falla ETX/LRC, se descarta de a 1 byte y aparecen. La espera está acotada por `FRAME_MAX_LEN` (un `9039` se rechaza enseguida por fuera de rango).

## Payload de `M` (`main/m_payload.{h,c}`)

`m_payload_parse(payload, payload_len, mrad[6])` convierte `+1571 FS -0785 FS ... FS +0001` en 6 `int32_t` en mrad.

- **Tamaño exacto:** `M_PAYLOAD_SIZE` = 6 campos × 5 + 5 FS = **35** (LEN `0039` = 35 + 4, como en `[R09]`). Es lo primero que se valida.
- **Cada campo:** signo `+`/`-` y 4 dígitos; cada campo salvo el último va seguido de FS.
- **Todo o nada:** decodifica en un array local y copia a `mrad` solo si los 6 campos son válidos. Si falla devuelve `false` y no toca la salida: nunca se mueve un servo con una trama a medias (igual que `position_states_` en el plugin).
- **Valida formato, no rango:** `+9999` es un campo bien formado y se acepta; el recorte a ±3142 lo hace `mrad_to_counts()`. Una responsabilidad por función.
- **Módulo propio**, no dentro de `frame_parser`: el parser no sabe qué significa cada CID. Acá va a vivir también el armado del payload de la `M` de feedback.

## Recepción desde la PC (`pc_rx_task` en `esp32_arm.c`)

- `pc_rx_task` lee bloques de hasta 64 bytes de UART0 (`uart_read_bytes`, timeout 20 ms) y se los pasa a `frame_parser_feed()`. El `frame_parser_t` es `static` dentro de la tarea (133 bytes fuera del stack). Stack 4096, prioridad 10.
- `on_pc_frame` (callback del parser): `switch (cid)`; `M` → `handle_m_frame`; otros CID (D, S, ... llegan con `[E09]`) → warning.
- `handle_m_frame`: `m_payload_parse()` → `mrad_to_counts()` por joint → log. `TODO(E06)`: `SYNC_WRITE` y `M` de feedback.

### `tools/send_m.py`

Arma la trama `M` igual que el `write()` del plugin y la manda por `/dev/ttyACM0`:

```
firmware/esp32_arm/tools/send_m.py 1571 -785 0 -3142 3142 -1
firmware/esp32_arm/tools/send_m.py --count 100 0 0 0 0 0 0   # 2 s a 50 Hz, como ros2_control
```

### Prueba de punta a punta (PC → ESP32)

Con `--count 100` (100 tramas a 50 Hz) y el analizador en GPIO14:

```
I (279) main_task: Returned from app_main()
I (309) ESP32-ARM: M: mrad [1571 -785 0 -3142 3142 -1] -> counts [3072 1536 2048 0 4095 2047]
I (339) ESP32-ARM: M: mrad [1571 -785 0 -3142 3142 -1] -> counts [3072 1536 2048 0 4095 2047]
...  (una cada ~20 ms hasta I (2049))
```

- `UART0 → frame_parser → m_payload → mrad_to_counts` da exactamente los valores calculados en la PC.
- Ritmo estable de ~20 ms (50 Hz) sin acumular atraso; los saltos ocasionales de 30 ms son del `sleep` de Python.
- ~88 de 100 tramas: las ~12 que llegaron durante el arranque se perdieron.

### Abrir el puerto resetea el ESP32

- **Síntoma:** con una sola trama (sin `--count`) aparecía un log de arranque y ninguna `M:`.
- **Causa:** en Linux el kernel activa DTR/RTS al abrir el puerto, antes de que pyserial aplique `dtr = False` / `rts = False`. En las placas ESP32 esas líneas manejan EN y GPIO0 (circuito de auto-reset): la placa se reinicia y la trama llega mientras arranca (~300 ms hasta `app_main`).
- **Arreglo en el script:** `--boot-wait-ms` (default 500) espera después de abrir el puerto. Poner `dtr`/`rts` en `False` antes de abrir se mantiene: evita dejar la placa en reset o en modo descarga, aunque no evita el pulso del `open`.
- **Para el plugin de `[R09]`:** al activarse, `ros2_control` también va a resetear el ESP32. Las primeras `M` se pierden, pero el plugin manda una en cada ciclo, así que se recupera solo en ~300 ms. Las respuestas que no llegan en ese lapso no deben tratarse como falla de comunicación.

## Tests en la PC (`test_host/`)

Funciones puras (sin hardware): se compilan con `gcc`, sin ESP-IDF. Desde `firmware/esp32_arm`:

```
gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
    test_host/test_frame_parser.c main/frame_parser.c -o /tmp/test_frame_parser && /tmp/test_frame_parser

gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
    test_host/test_servo_units.c main/servo_units.c -lm -o /tmp/test_servo_units && /tmp/test_servo_units

gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
    test_host/test_m_payload.c main/m_payload.c -o /tmp/test_m_payload && /tmp/test_m_payload
```

- `test_frame_parser.c` (12 casos): trama `M` de 44 bytes con LEN `0039`, byte a byte, incompleta que espera, 50 tramas en un solo bloque (más grande que el buffer), basura/log antes, 1000 bytes sin STX, LRC/ETX/FS malos, trama buena dentro de una corrupta, LEN corrupto que demora, LEN inválidos (`9999`, `0129`, `0003`, `00a9`) y límites (LEN 4 y 128), dos tramas cortadas en cada punto posible.
- `test_m_payload.c`: payload válido (signos, ceros, ±3142), fuera de rango bien formado (se acepta), 10 malformados (vacío, 5 o 7 joints, sin signo, espacio como signo, letra, último campo malo, coma en vez de FS, campo corto + largo, FS al final): todos devuelven `false` y dejan la salida intacta.
- `test_servo_units.c`: valores conocidos, contra `round()` en punto flotante en todo el rango, extremos sin overflow (`INT32_MIN/MAX`), ida y vuelta.
- ASan/UBSan detectan accesos fuera del buffer y overflow de enteros que un test "pasa/falla" no vería.

## Pendientes

- [x] Crear `firmware/esp32_arm` (`set-target esp32`, `.gitignore`)
- [x] Elegir los pines de la consola (UART1): TX 14, RX 27
- [x] `board_pins.h` con puertos, pines y baudrates de UART0 (PC) y UART2 (servos)
- [x] Agregar a `board_pins.h` los pines de UART0 (TX 1, RX 3), con comentario de por qué son fijos
- [x] Reconectar los pines de UART0 con `uart_set_pin()` en el código: `init_uarts()` en `esp32_arm.c` levanta UART0 (PC) y UART2 (servos) con `uart_driver_install` → `uart_param_config` → `uart_set_pin`, todo desde `board_pins.h`. RX buffer: PC 1024 (varias tramas `M`/`D`), servos 256; ambos > FIFO de 128 (mínimo de `uart_driver_install`). Sin buffer de TX (`uart_write_bytes()` bloquea hasta el FIFO). **Probado en la placa (30 pines):** el log de arranque sale completo por GPIO14 (analizador, Async Serial 115200) y `init_uarts()` corre sin que aborte ningún `ESP_ERROR_CHECK`: `ESP32-ARM: PC link on UART0 @ 115200 baud, servo bus on UART2 @ 1000000 baud (TX=17, RX=16)` (firmware `c5334b1`)
- [x] Configurar la consola en `menuconfig` (UART1, TX 14, RX 27)
- [x] Verificar con el analizador si GPIO14 saca PWM durante un reset: no saca PWM
- [x] Regenerar el `sdkconfig` local para que tome la flash de 4MB: `rm sdkconfig` + `idf.py build flash`. El log muestra `SPI Flash Size : 4MB` sin warning, y la consola (GPIO14) e `init_uarts()` siguen igual (firmware `548b2a1`): el `sdkconfig.defaults` está completo
- [ ] Puerto serie configurable en el plugin (fuera de E06, pasó al backlog como `[R13]`): hoy `/dev/ttyACM0` está fijo en `robotic_arm_ros2_control.xacro`; con la DevKitC (CP2102) es `/dev/ttyUSB0` y no conectaría. Opciones: argumento del launch o regla udev con nombre fijo (p. ej. `/dev/robotic_arm`)
- [x] Migrar `board_pins.h` a `Kconfig.projbuild` (menú "Robotic Arm Configuration"): `ARM_PC_UART_BAUD_RATE` (115200), `ARM_SERVO_UART_BAUD_RATE` (1000000), `ARM_SERVO_UART_TX_GPIO` (17), `ARM_SERVO_UART_RX_GPIO` (16). Puertos y pines de UART0 quedan fijos en `board_pins.h`, que ahora mapea sus nombres a `CONFIG_ARM_*` (los `.c` no cambian). Baudrates con rango 9600..5000000 (el `1200..115200` de `esp32_bridge` rechazaba 1000000); GPIO con rango `0..SOC_GPIO_OUT_RANGE_MAX` (TX, excluye los de solo entrada 34-39) y `0..SOC_GPIO_IN_RANGE_MAX` (RX). Los defaults del Kconfig ya son los valores elegidos: no van en `sdkconfig.defaults`
- [x] `sdkconfig.defaults` versionado (target + consola). `Kconfig.projbuild` **crea** opciones; `sdkconfig.defaults` **elige valores** (propios y de ESP-IDF); `sdkconfig` es el resultado local
- [x] `mrad_to_counts(int32_t mrad)` con redondeo simétrico (`main/servo_units.c`)
- [x] `counts_to_mrad(int32_t counts)`: inversa, mismo redondeo, clamp de entrada a 0..4095, salida en [−3142, 3140] mrad. Test en la PC: 0 diferencias contra `round()` en punto flotante e ida y vuelta exacta para las 4096 cuentas (error máximo mrad → cuentas → mrad: 2 mrad, solo en +π por el recorte a 4095)
- [x] Flash de 4MB en `sdkconfig.defaults` (`CONFIG_ESPTOOLPY_FLASHSIZE_4MB`): las dos placas tienen 4MB según `esptool`, pero ESP-IDF asume 2MB por defecto (dejaba la mitad sin usar)
- [x] Parser de tramas del protocolo de `[R09]` (`frame_parser.{h,c}`), con tests en la PC
- [x] Tests de `servo_units` guardados en `test_host/` (antes eran archivos sueltos en la carpeta temporal)
- [x] Parsear el payload de `M` (6 joints de 5 chars con signo, separados por FS): `m_payload_parse()`, con tests en la PC
- [x] Tarea que lee UART0 y alimenta el parser (`pc_rx_task`): probada de punta a punta con `tools/send_m.py`
- [ ] Plugin de `[R09]`: tolerar el reset del ESP32 al abrir el puerto (sin respuestas los primeros ~300 ms)
- [ ] Enviar `SYNC_WRITE` al bus de servos
- [ ] Leer las posiciones reales y responder con una `M` de feedback
- [ ] Comprar adaptador USB-serie TTL 3.3V
