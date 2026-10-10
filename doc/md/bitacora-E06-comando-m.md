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

### Cómo funciona `frame_parser_feed()`

Puerta de entrada: recibe un bloque de cualquier tamaño y lo mete en el buffer de 133 **de a pedazos que entren**, parseando entre pedazo y pedazo.

```c
while (size > 0) {
    size_t space = sizeof(parser->buf) - parser->len;   // lugar libre
    size_t n = size < space ? size : space;             // meto lo que entre
    memcpy(parser->buf + parser->len, data, n);         // al final de lo pendiente
    parser->len += n; data += n; size -= n;
    parse_buffer(parser);                               // saca tramas: libera lugar
    if (parser->len == sizeof(parser->buf)) drop(parser, 1);  // red de seguridad
}
```

- Los bytes se agregan **al final** de lo pendiente: una trama partida entre dos lecturas queda contigua.
- `parse_buffer()` entrega las tramas completas y las saca: eso libera lugar para la vuelta siguiente. Lo incompleto queda esperando en `buf[0]`.
- **Red de seguridad:** un buffer lleno (133) empieza con STX y LEN ≤ 128, así que siempre hay una candidata completa que se acepta o descarta. Si alguien cambia las constantes y rompe esa garantía, el loop se trabaría (sin lugar ni progreso): descartar 1 byte asegura que siempre avance.

Ejemplo: 3 tramas `M` (132 bytes) juntas, con 20 bytes de una trama anterior esperando:

| Vuelta | Lugar libre | Copia | Buffer | `parse_buffer` | Queda |
|---|---|---|---|---|---|
| 1 | 133 − 20 = 113 | 113 | 133 | entrega 2 tramas (la de antes + 1) | 45 |
| 2 | 133 − 45 = 88 | 19 | 64 | entrega 1 trama | 20 (incompleta) |

### Cómo funciona `parse_buffer()`

La trama candidata siempre empieza en `buf[0]`, así que cada campo tiene posición fija:

```
índice:  0     1  2  3  4    5     6     7 ...        size-2  size-1
        STX | L  E  N  ... | CID | FS | payload ... | ETX  | LRC
             LEN_OFFSET    CID_   FS_   PAYLOAD_
                           OFFSET OFFSET OFFSET
        └── HEADER_SIZE = 5 ──┘└──────── LEN bytes ────────┘
```

`HEADER_SIZE` (STX + 4 dígitos) son los bytes que LEN no cuenta: tamaño total = `HEADER_SIZE + LEN`.

Cada vuelta del loop termina de una de **tres** formas:

| Salida | Cuándo | Significado |
|---|---|---|
| `return` | faltan bytes | No se puede decidir todavía: esperar la próxima lectura |
| `drop(1)` + `continue` | algo inválido | Ese STX era falso: saltearlo y buscar el siguiente |
| callback + `drop(frame_size)` | trama válida | Entregarla, sacarla y seguir por si hay otra |

Pasos:

1. **Buscar el STX** (`memchr`). Sin STX → todo es basura, vaciar. Con STX → descartar lo anterior (log, restos) y dejarlo en `buf[0]`.
2. **¿Llegaron los 4 dígitos de LEN?** Menos de 5 bytes → `return`.
3. **¿LEN es válido?** 4 dígitos ASCII, entre 4 (sin payload) y 128. Si no → STX falso: `drop(1)` y volver al paso 1. Evita que un `9039` deje esperando miles de bytes.
4. **¿Llegó la trama completa** (`5 + LEN`)? No → `return`.
5. **¿FS, ETX y LRC bien?** (`frame_is_valid`): FS en la posición 6, ETX en `size-2`, XOR desde el primer dígito de LEN hasta ETX igual al último byte. Si no → `drop(1)`, no la trama entera.
6. **Trama válida:** callback con CID y payload (`LEN - 4` bytes). El payload apunta **adentro del buffer**, por eso el `drop` va **después** del callback (antes, el `memmove` pisaría el payload mientras se lee).

**Por qué descartar 1 byte y no la trama entera** (pasos 3 y 5), caso de `test_valid_frame_inside_corrupted_one`:

```
02 30 30 30 38 | 02 30 30 30 36 44 1C 30 37 03 xx
└ STX falso, LEN=8 ┘└──────── trama buena ────────┘
```

El STX falso dice "mido 13 bytes" y abarca parte de la trama buena; falla el ETX (en la posición 11 hay `1C`). Descartando los 13 bytes se perdería el comienzo de la trama buena; descartando 1, el paso 1 encuentra el segundo STX y la recupera.

### Por qué buffer + reescaneo y no una FSM

**Por el protocolo: el STX no está escapado.** Nada impide que `0x02` aparezca dentro de una trama (el LRC es un XOR y puede valer cualquier cosa). Un `0x02` no garantiza un inicio de trama: solo se sabe después de validar todo.

- **FSM clásica** (`WAIT_STX → READ_LEN → READ_BODY → validar → WAIT_STX`): procesa cada byte una vez y lo olvida. Si el LRC falla, vuelve a `WAIT_STX` desde el byte siguiente: una trama buena que empezaba dentro de la corrupta ya pasó y se pierde. No puede volver atrás.
- **Buffer + reescaneo:** guarda los bytes; si la candidata falla, descarta 1 y vuelve a buscar en lo que ya tenía, y recupera la trama buena.

En realidad sigue siendo una máquina de estados, **implícita**: el estado lo da cuántos bytes hay (sin STX → esperando STX; < 5 → esperando LEN; < `5 + LEN` → esperando cuerpo). Una FSM explícita que guarde y "reproduzca" bytes tras un error termina siendo esto mismo con más código.

| | FSM clásica | Buffer + reescaneo |
|---|---|---|
| Recupera tramas dentro de una corrupta | ❌ | ✅ |
| Trabajo por byte | O(1), cada byte una vez | Peor caso O(n²) con basura patológica |
| Memoria | Igual (el payload hay que guardarlo) | Igual |
| Igual al parser del plugin (`[R09]`) | ❌ | ✅ |

El peor caso cuadrático no importa: n ≤ 133 y a 115200 baudios sobra tiempo. Usar el mismo algoritmo que el `read()` del plugin hace que los dos lados reaccionen igual a los mismos errores, lo que facilita el debug.

**Cuándo convendría una FSM:** si el protocolo escapara el STX (SLIP, HDLC: `0x02` nunca aparece dentro de una trama, un STX siempre es un inicio y resincronizar desde el próximo es seguro), o con tramas muy largas o muy poca RAM para guardar la trama completa. Ninguno aplica acá.

## Payload de `M` (`main/m_payload.{h,c}`)

`m_payload_parse(payload, payload_len, mrad[6])` convierte `+1571 FS -0785 FS ... FS +0001` en 6 `int32_t` en mrad.

- **Tamaño exacto:** `M_PAYLOAD_SIZE` = 6 campos × 5 + 5 FS = **35** (LEN `0039` = 35 + 4, como en `[R09]`). Es lo primero que se valida.
- **Cada campo:** signo `+`/`-` y 4 dígitos; cada campo salvo el último va seguido de FS.
- **Todo o nada:** decodifica en un array local y copia a `mrad` solo si los 6 campos son válidos. Si falla devuelve `false` y no toca la salida: nunca se mueve un servo con una trama a medias (igual que `position_states_` en el plugin).
- **Valida formato, no rango:** `+9999` es un campo bien formado y se acepta; el recorte a ±3142 lo hace `mrad_to_counts()`. Una responsabilidad por función.
- **Módulo propio**, no dentro de `frame_parser`: el parser no sabe qué significa cada CID. Acá va a vivir también el armado del payload de la `M` de feedback.

### Cómo funciona `m_payload_parse()`

Cada campo tiene posición fija (5 bytes + FS): el joint `j` empieza en `j × 6`.

```
índice: 0    5  6    11 12   17 18   23 24   29 30   34
        +1571 FS -0785 FS +0000 FS -3142 FS +3142 FS -0001
        └ j0 ┘   └ j1 ┘   └ j2 ┘   └ j3 ┘   └ j4 ┘   └ j5 ┘
```

1. **Tamaño exacto primero** (`payload_len != M_PAYLOAD_SIZE` → `false`). No es solo formato: es lo que hace seguros los accesos por posición fija que siguen (`payload[34]`, etc.) sin chequear límites en cada uno. Con un payload de 20 bytes sin este chequeo se leería memoria fuera del buffer.
2. **Recorrer los 6 campos:** `field = payload + j * (M_PAYLOAD_FIELD_SIZE + 1)` apunta al signo del joint `j`. `field[-1]` es el byte anterior, el separador: en C un índice negativo sobre un puntero es válido (`field[-1]` = `*(field - 1)`). Para `j = 0` no hay separador antes, de ahí el `j > 0`.
3. **`parse_field()` decodifica cada campo:** signo `+`/`-` obligatorio, después 4 dígitos. `field[i] - '0'` da el valor del dígito porque en ASCII son consecutivos (`'0'` = 48 … `'9'` = 57); `magnitude * 10 + dígito` va corriendo los dígitos a la izquierda. El signo se aplica al final (`"-0000"` → 0).

   | Dígito | Cuenta | `magnitude` |
   |---|---|---|
   | `'1'` | 0 × 10 + 1 | 1 |
   | `'5'` | 1 × 10 + 5 | 15 |
   | `'7'` | 15 × 10 + 7 | 157 |
   | `'1'` | 157 × 10 + 1 | 1571 |

4. **Todo o nada:** los valores van a un array local (`values`) y se copian a `mrad` solo si los 6 son válidos. Escribiendo directo en `mrad`, una trama con el último campo roto dejaría 5 joints nuevos y 1 viejo: el brazo iría a una pose que nadie pidió.

**Por qué no `sscanf` / `strtol`:**

- El payload **no termina en `\0`** (apunta dentro del buffer del parser) y esas funciones asumen strings terminados.
- Son **más permisivas** que el formato: `strtol` acepta espacios al principio, signo opcional y cualquier cantidad de dígitos. El protocolo exige exactamente signo + 4 dígitos (el `%+05ld` del plugin).
- `sscanf` arrastra todo el motor de formatos: pesado para un micro.

## Recepción desde la PC (`pc_rx_task` en `esp32_arm.c`)

- `pc_rx_task` lee bloques de hasta 64 bytes de UART0 (`uart_read_bytes`, timeout 20 ms) y se los pasa a `frame_parser_feed()`. El `frame_parser_t` es `static` dentro de la tarea (133 bytes fuera del stack). Stack 4096, prioridad 10.
- `on_pc_frame` (callback del parser): `switch (cid)`; `M` → `handle_m_frame`; otros CID (D, S, ... llegan con `[E09]`) → warning.
- `handle_m_frame`: `m_payload_parse()` → `mrad_to_counts()` por joint → `SYNC_WRITE` por UART2 → log. `TODO(E06)`: `M` de feedback (ver "Movimiento de los servos").

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

## Movimiento de los servos (`main/servo_bus.{h,c}`)

Cada trama `M` válida se traduce en **un** paquete `SYNC_WRITE` broadcast al bus (protocolo de `[E04]`, ver `doc/md/bitacora-E04-protocolo-driver.md`):

```
FF FF | FE | Length | 83 | 29 | 07 | [ID acc posL posH 0 0 spdL spdH] × 6 | checksum
```

- `servo_bus_build_sync_write_pos(out, out_size, targets, count)` solo arma el paquete (no sabe nada de la UART, como el parser). `servo_target_t` = ID, posición (cuentas), velocidad, aceleración.
- Bloque de 7 registros desde `ACC` (41): acc, posición (little-endian), time (0, no se usa: manda la velocidad), velocidad. Mismo bloque que `SyncWritePosEx` del SDK.
- Checksum = `~(ID + Length + ... + último parámetro) & 0xFF` (todo menos `FF FF`).
- Con 6 servos: 8 + 6 × 8 = **56 bytes**, Length `0x34`. A 1 Mbaud son ~0.56 ms.
- Broadcast: los servos **no responden** (fire and forget). Devuelve 0 si el buffer no alcanza o si el Length (1 byte) se desborda (más de 31 servos).
- **Orden de joints:** el joint j del payload va al servo ID j + 1 (`shoulder_pan` = 1 … `gripper` = 6), el mismo orden de `robotic_arm_ros2_control.xacro` y de `STServo_Python`.
- **Velocidad y aceleración en Kconfig:** `ARM_SERVO_MOVE_SPEED` (default 300 pasos/s ≈ 26°/s, máx. 3400) y `ARM_SERVO_MOVE_ACC` (default 10 = 1000 pasos/s²). Mismos defaults que `servo_control_ui.py`: lentos y seguros para la puesta en marcha. Muy bajos hacen que el brazo se atrase respecto de las trayectorias de MoveIt; ajustar después de probar.
- Compila y el paquete coincide byte a byte con el ejemplo de `[E04]` en el test. **Probado con el brazo:** ver "Resultados de la prueba con el brazo".

### Cómo funciona `servo_bus_build_sync_write_pos()`

Arma en un buffer **un solo paquete** que mueve varios servos; no toca la UART. Frente a 6 `WRITE` separados: menos bytes en el bus (un encabezado en vez de seis) y los servos **arrancan juntos** (cada uno lee su parte del mismo paquete), sin movimiento "en cascada".

1. **Tamaño y validación primero:** `SERVO_BUS_SYNC_WRITE_POS_SIZE(count)` = 8 + count × 8 (56 con 6 servos). Length = "bytes que vienen después de mí" (instrucción … checksum) = total − 4 (`FF FF ID Length`) = 52 (`0x34`). Si el buffer no alcanza o Length no entra en 1 byte (> 31 servos) devuelve 0 **sin escribir nada** (misma idea que el chequeo de tamaño de `m_payload_parse`).
2. **Encabezado:** `FF FF` (inicio) · `FE` (broadcast) · `34` (Length) · `83` (`SYNC_WRITE`) · `29` (registro 41 = `ACC`, primero a escribir) · `07` (bytes por servo).
3. **Un bloque por servo:** `ID | acc | posL posH | timeL timeH | speedL speedH`, que cada servo escribe en sus registros 41–47.
   - **Little-endian:** byte bajo primero. `position & 0xFF` = 8 bits de abajo; `position >> 8` = 8 bits de arriba. 2048 = `0x0800` → `00 08`.
   - **`time` = 0:** el STS3215 puede moverse "en X ms" o "a X velocidad"; con 0 manda la velocidad.
4. **Checksum:** suma desde el ID (sin `FF FF`) en un `uint8_t`, que se desborda solo (suma módulo 256, el "solo el byte bajo" del protocolo), y `~` invierte los bits (complemento a 1). El receptor suma todo desde el ID **incluido el checksum** y tiene que dar `0xFF` (un valor más su complemento = todos unos): eso verifica `test_six_servos`.

**Sin respuesta:** al broadcast (`FE`) ningún servo contesta; si contestaran todos a la vez en un bus half-duplex chocarían. Para la `M` de feedback hacen falta lecturas aparte.

Protocolo validado contra el manual oficial y el mapa de registros del ST3215: ver "Validación contra la documentación oficial" en `doc/md/bitacora-E04-protocolo-driver.md`.

### Prueba con el brazo

⚠️ Desde este firmware cada `M` mueve los motores. Los valores de las pruebas anteriores (`1571 -785 0 -3142 3142 -1`) mandan joints a los extremos: no usarlos.

**Cableado** (todo apagado; verificado con la wiki de Waveshare, el esquemático y la serigrafía, ver "Cableado ESP32 ↔ driver" en `doc/md/bitacora-E03-adaptador-logico.md`):

| ESP32 (placa de 30 pines) | Conecta a |
|---|---|
| `TX2` (GPIO17) | `TX` del conector `UART` del driver (**sin cruzar**) |
| `RX2` (GPIO16) | `RX` del driver |
| `GND` | `GND` del driver (obligatorio: masa común) |
| `D14` (GPIO14) | Analizador canal 0 (consola, 115200) |
| `D17` (GPIO17) | Analizador canal 1 (bus de servos, 1000000) |
| `GND` | GND del analizador |

- **Jumper amarillo del driver en A** (`UART – SERVO`). Con `STServo_Python` estaba en B (USB).
- No unir 3.3V ni 5V del ESP32 con el driver: el ESP32 se alimenta por su USB; el driver, por su fuente de 9~12.6V.

#### Con la DevKitC WROOM (38 pines) en la placa base

En la protoboard la DevKitC tapa una hilera entera; para la prueba se usa la **DevKitC WROOM** sobre una placa base "ESP32S 38P/V4/Goouuu Expansion board" (fotos: `doc/img/esp32-38pines-base-1.jpeg`, `doc/img/esp32-38pines-base-2.jpeg`).

- **Firmware sin cambios:** mismo chip y flash que la de 30 pines (ver "Placa"); `IO14`, `IO16`, `IO17` e `IO27` están en los conectores de la DevKitC V4 (esquemático). Hay que grabarla (hoy tiene otro firmware).
- **Puerto `/dev/ttyUSB0`** (CP2102), no `ttyACM0`: `idf.py -p /dev/ttyUSB0 build flash` y `./tools/send_m.py --port /dev/ttyUSB0 ...`. Es el caso de `[R13]`: con el plugin de ROS no conectaría.
- **Orden de pines verificado:** columna derecha de la base `GND, P23, P22, TX, RX, P21, GND, P19, P18, P5, P17, P16, ...` = J3 de la DevKitC (`P17` en la posición 11, como `IO17` en `[E01]`).

| Base (serigrafía) | Contacto | Va a |
|---|---|---|
| `P17` | amarillo (S) | `TX` del driver **y** canal 1 del analizador |
| `P16` | amarillo (S) | `RX` del driver |
| `P14` | amarillo (S) | canal 0 del analizador |
| cualquier `G` | negro | `GND` del driver **y** GND del analizador |

Trampas:

- ⚠️ **Cada pin tiene tres contactos S/V/G:** amarillo = señal (GPIO), **rojo = V** (tensión elegida con el jumper `JUMP`: 5V o 3.3V), negro = GND. Las señales van solo al amarillo. **Jumper `JUMP` en 3.3V:** si un cable cae por error en el rojo, el peor caso es 3.3V y no 5V en una entrada del driver.
- ⚠️ **El conector `TX RX VCC GND` de abajo a la derecha es UART0** (GPIO1/3, el USB de la PC): el driver **no** va ahí. Misma trampa que `TXD`/`RXD` en la serigrafía de la DevKitC (en la WROOM, el bus de servos es `G17`/`G16`). El conector `D22 D21 VCC GND` es el I2C (reservado para un sensor futuro).
- **Ancho:** dos tiras de zócalos a la izquierda (24.00 mm y 25.50 mm). La DevKitC mide ~25.4 mm entre hileras: va en la de 25.5 mm, sin forzar. Antena del lado de `3V3`/`GND`, USB del lado de `5V`/`CLK`.
- **No usar las entradas de alimentación de la base** (jack 6.5–16V, USB-C, micro-USB): el ESP32 se alimenta por su USB, que además es el canal con la PC. Alimentar por los dos lados puede meter tensión en el USB de la PC.

**Pasos** (desde `firmware/esp32_arm`, con `. ~/esp/esp-idf/export.sh`):

1. **Driver sin alimentar:** `idf.py -p /dev/ttyACM0 build flash` y `./tools/send_m.py 0 0 0 0 0 0`. En el canal 1: `SYNC_WRITE` de 56 bytes (`FF FF FE 34 83 29 07 01 0A 00 08 00 00 2C 01 ...`) seguido del `SYNC_READ` de 14 (`FF FF FE 0A 82 38 02 01 02 03 04 05 06 ..`).
2. **Driver alimentado**, brazo despejado y la mano cerca del interruptor (va a la pose cero a ~26°/s desde donde esté): `./tools/send_m.py 0 0 0 0 0 0`. Esperado: el brazo va a la pose de calibración; `Reply M: ...` cerca de 0 y `got 1 reply frame(s)`.
3. **Feedback en movimiento:** `./tools/send_m.py --count 150 200 0 0 0 0 0` (3 s a 50 Hz, `shoulder_pan` +200 mrad ≈ 11°). El primer valor de las respuestas tiene que subir de 0 a ~200; los demás, quedar cerca de 0.
4. **Volver:** `./tools/send_m.py 0 0 0 0 0 0`.

Con el driver **sin alimentar** ningún servo responde: la consola muestra `SYNC_READ: not every servo answered` por cada trama y `send_m.py` dice `got 0 reply frame(s)`. Es lo esperado en el paso 1. Con el driver alimentado, `send_m.py` imprime cada `M` de feedback (`Reply M: ... mrad`).

| Síntoma | Causa probable |
|---|---|
| Paso 1 sin bytes en GPIO17 | Firmware sin grabar o canal mal conectado |
| Brazo quieto y `0 reply frames` | TX/RX cruzados, falta GND, jumper del driver en B, o driver sin alimentar |
| Brazo quieto pero **con** respuestas | Torque deshabilitado (p. ej. la UI de Python quedó en "Free"): apagar y prender la fuente |
| Se mueve otro joint | Orden de IDs: anotar cuál se movió y revisar |

## `M` de feedback (lectura de posiciones)

Ciclo completo por cada `M` de la PC (modelo request/response de `[R09]`):

```
M de la PC → SYNC_WRITE (mover) → SYNC_READ (¿dónde están?) → M de feedback a la PC
```

- **`SYNC_READ`** (`servo_bus_build_sync_read_pos()`): `FF FF FE 0A 82 38 02 01..06 chk` — lee 2 bytes desde el registro 56 (posición actual) de los 6 servos. Length = N + 4 (sección SYNC READ del manual). Cada servo responde con su propio paquete de estado (`FF FF ID 04 ERROR lo hi chk`, 8 bytes), en el orden de los IDs pedidos.
- **`servo_bus_parse_positions()`**: busca la respuesta de cada servo en orden y saltea todo lo demás de a 1 byte (misma idea que el parser de tramas): ruido, checksums malos o un **eco** de nuestros propios paquetes. No se sabe si el adaptador devuelve lo que transmite (el SDK de Python también ignora paquetes con otro ID, así que tolera eco): el código funciona en los dos casos. Aplica el bit de signo (bit 15) y junta los bytes de ERROR con OR. Todo o nada.
- **`read_servo_positions()`** (`esp32_arm.c`): `uart_flush_input()` antes del `SYNC_WRITE` (solo cuentan las respuestas de este ciclo), manda el `SYNC_READ` y junta respuestas hasta que estén todas o pasen `SERVO_READ_TIMEOUT_MS` (10 ms). Primero pide los 48 bytes esperados y después de a 8: con el tick de FreeRTOS a 100 Hz, `uart_read_bytes()` espera hasta llenar lo pedido o el timeout, así que pedir de a poco hace que vuelva apenas llega cada respuesta.
- **Sin respuesta de todos los servos → no se manda feedback:** la PC detecta la trama faltante (diseño de `[R09]`). Warning en consola.
- **Errores de servo** (byte ERROR ≠ 0: sobrecarga, temperatura, etc.): se loguean solo cuando cambian, para no inundar la consola a 50 Hz.
- **`send_m_feedback()`**: `counts_to_mrad()` → `m_payload_format()` → `frame_build()` → UART0.
- **`m_payload_format()`**: inversa de `m_payload_parse()`, mismo formato que el plugin (`%+05ld`, FS). Recorta a ±9999 para que cada campo siga siendo de 5 chars.
- **`frame_build()`** (en `frame_parser`): arma `STX | LEN | CID | FS | payload | ETX | LRC`.
- **`tools/send_m.py`** lee y decodifica las respuestas (mismas reglas que el parser en C) y al final dice cuántas llegaron. Descarta lo recibido durante el arranque (mensajes de la ROM por UART0).
- Tiempos a 1 Mbaud: `SYNC_WRITE` 56 bytes + `SYNC_READ` 14 + 6 respuestas × 8 = 118 bytes ≈ 1.2 ms (el registro 7, "Return delay", es 0 por defecto). Muy por debajo del ciclo de 20 ms.
- **`send_m_feedback()` loguea la posición real** (`Feedback: counts [...] -> mrad [...]`): con una `M` aparecen dos líneas, el objetivo y lo medido. A 50 Hz conviene pasar ambos logs a `DEBUG` más adelante.

### Latencia de `pc_rx_task` (corregida)

Primera prueba con el brazo: el log del ESP32 indicaba que los 6 servos respondieron (sin warning de `SYNC_READ`), pero `send_m.py` decía `got 0 reply frame(s)`.

- **Causa:** `uart_read_bytes(PC_UART_PORT, chunk, 64, pdMS_TO_TICKS(20))` vuelve cuando junta los 64 bytes pedidos **o** vence el timeout. Una `M` mide 44: nunca llena los 64 y siempre esperaba el timeout (20–30 ms con el tick de 100 Hz). Latencia total: ~3.8 ms (llegada) + 20–30 ms (espera) + ~1.5 ms (bus) + ~3.8 ms (respuesta) ≈ **30–40 ms**. El script leía una sola vez a los 20 ms y cerraba: la respuesta llegaba después.
- **Por qué importa más allá del script:** el plugin de `[R09]` corre a 50 Hz (20 ms). Con 30–40 ms la respuesta al ciclo N llegaría en el N+2; el diseño pide un ciclo.
- **Arreglo:** bloquear esperando **1 byte** (`portMAX_DELAY`, la tarea no tiene otra cosa que hacer) y tomar lo que ya está en el buffer sin esperar (`uart_get_buffered_data_len()` + `uart_read_bytes(..., 0)`). Si la trama sigue llegando, el parser guarda lo incompleto. Latencia ≈ 10 ms.
- **`send_m.py`:** `--reply-wait-ms` (default 200) sigue escuchando después de la última trama, y `--countdown` (default 3 s) avisa antes de mandar para tener la mano en la fuente.

### Resultados de la prueba con el brazo

Montaje: DevKitC WROOM en la placa base, `P17` → `TX` del driver, `P16` → `RX`, GND común, jumper en A; analizador CH0 en `P14` (consola, 115200) y CH1 en `P16` (respuestas del bus, 1000000). Fotos: `doc/img/esp32-driver-analyzer.jpeg`, `doc/img/esp32-driver-arm-analyzer.jpeg`, `doc/img/esp32-driver-arm-analyzer-2.jpeg`.

1. **Driver sin alimentar** (`doc/img/analyzer-gpio14-test1-sin-brazo.png`): `M: mrad [0 0 0 0 0 0] -> counts [2048 ...]` y `SYNC_READ: not every servo answered (0 bytes received)`. Esperado.
2. **Driver alimentado, firmware con la latencia vieja** (`doc/img/analyzer-gpio14-test1-con-brazo.png`): el brazo se movió a la pose cero y no apareció el warning (los 6 servos respondieron), pero el script no llegó a leer la respuesta.
3. **Con la latencia corregida y el log de feedback** (`doc/img/analyzer-gpio14-test1-con-brazo-2.png`, CH1 decodificado en hex):

   | Servo | Paquete en el bus | ERROR | Posición |
   |---|---|---|---|
   | 1 | `FF FF 01 04 00 FE 07 F5` | 0 | `0x07FE` = 2046 |
   | 2 | `FF FF 02 04 00 5A 03 9C` | 0 | `0x035A` = 858 |
   | 3 | `FF FF 03 04 00 09 0C E3` | 0 | `0x0C09` = 3081 |
   | 4 | `FF FF 04 04 00 87 03 6D` | 0 | `0x0387` = 903 |
   | 5 | `FF FF 05 04 00 44 03 AF` | 0 | `0x0344` = 836 |
   | 6 | `FF FF 06 04 00 95 05 5B` | 0 | `0x0595` = 1429 |

   Consola: `Feedback: counts [2046 858 3081 903 836 1429] -> mrad [-3 -1825 1585 -1756 -1859 -950]`. PC: `Reply M: -0003 -1825 +1585 -1756 -1859 -0950 mrad` y `got 1 reply frame(s)`.

**Las tres vistas coinciden byte a byte:** los servos responden en orden de ID y sin errores, el ESP32 los interpreta bien y la PC recibe la `M` de feedback dentro de la ventana. Primera vuelta completa PC → ESP32 → servos → ESP32 → PC.

- **Por qué el feedback no da 0 si se mandó la pose cero:** se lee ~1 ms después del `SYNC_WRITE`, cuando los servos recién arrancan a 26°/s: refleja dónde estaba el brazo antes de moverse. Para ver la convergencia: `./tools/send_m.py --port /dev/ttyUSB0 --count 250 0 0 0 0 0 0` (5 s a 50 Hz).
- **Warning en la WROOM:** `spi_flash: boya detected but related driver is not linked` — su flash es Boya (fabricante `0x68` en `esptool`). ESP-IDF trae `SPI_FLASH_SUPPORT_BOYA_CHIP` desactivado en el ESP32 "para ahorrar IRAM" y usa el driver genérico, que funciona. Se silencia con `CONFIG_SPI_FLASH_SUPPORT_BOYA_CHIP=y` en `sdkconfig.defaults`.

### Prueba de convergencia (`--count 250`)

Misma `M` 250 veces a 50 Hz (5 s, como `ros2_control`) para ver que el feedback acompaña el movimiento.

**Primera corrida** (`0 0 0 0 0 0`): `got 215 reply frame(s)`.

- Joints 1, 2, 3, 4 y 6 **convergen**: bajan ~9 mrad por respuesta (300 pasos/s × 20 ms = 6 cuentas ≈ 9.2 mrad) y terminan en `-3 +5 +18 +8 · -3` (±1° del cero; el +18 del codo, probablemente el peso del brazo).
- **35 respuestas faltantes**, de a ~4 seguidas cada ~0.5 s. La consola: `SYNC_READ: not every servo answered (0 bytes received)`.
- **`wrist_roll` (servo 5) trabado** en ~906 cuentas y `Servo status error flags: 0x20` a los ~2.6 s.

**Bug del timeout del `SYNC_READ` (corregido):**

- `uart_read_bytes(..., pdMS_TO_TICKS(10))` con `CONFIG_FREERTOS_HZ=100` es **1 tick**, y un timeout de 1 tick termina en la próxima interrupción del tick: **entre 0 y 10 ms**. Las respuestas tardan ~1–1.5 ms: si el tick caía antes, volvía con 0 bytes y el código abandonaba. ~1.5 ms / 10 ms ≈ 14% = 35/250. Los grupos salen del desfase entre el período de la PC (20 ms) y el tick.
- **Arreglo** (`read_servo_positions()`): cada espera suma un tick (`pdMS_TO_TICKS(restante redondeado) + 1`), así dura al menos lo que falta; el plazo se mide con `esp_timer` y una espera sin datos no abandona, vuelve a chequear el plazo. Costo: si los servos realmente no responden, la espera llega a ~20 ms (un ciclo). Alternativa de fondo: tick a 1000 Hz.

**`wrist_roll` contra el tope (protección de sobrecarga):**

- `0x20` = bit 5 del registro 65 ("Overload", mapa de registros del ST3215). La protección se dispara con carga > 80% del torque durante 2 s (registros 36 y 35) y baja el torque al 20% (registro 34): el servo 5 se trabó en ~908 a los ~1.1 s y el flag apareció ~2 s después.
- Lo frena un **tope de la pieza del antebrazo**: el cero calibrado (2048) cae detrás del tope (faltan ~1140 cuentas, ~100°).
- Además el `wrist_roll` **no tiene límites en la EEPROM**: `calibrate-arm.py` lo trata como giro continuo y fuerza `[0, 4095]` sin medirlo (no se usó `--record-wrist-roll`). Los demás servos recortan solos objetivos fuera de rango; el 5 va contra el tope.

**Segunda corrida, con el arreglo** (`0 0 0 0 -1750 0`: al `wrist_roll` se le manda su posición actual para no forzarlo): **`got 250 reply frame(s)`**, sin saltos; los otros 5 joints terminan en `-3 +5 +17 -2 · -2`, prácticamente igual que la primera vez (repetible).

**`wrist_roll` por el camino largo y salto de ±π:**

- Arrancó en +1336 mrad (2919 cuentas; se había movido a mano al revisar el tope) con objetivo −1750 (907). En vez de bajar, **subió**: pasó por +3131, saltó a −3142 y siguió hacia −1750 (más de media vuelta).
- **Causa, el offset de calibración:** el servo reporta `(encoder − OFS) mod 4096` con OFS = 1182 (`homing_offset` en `calibration.json`), pero se mueve en coordenadas del **encoder**, que no cruzan su propio 0/4095:

  | | Reportado (cuentas) | Encoder (+1182 mod 4096) |
  |---|---|---|
  | Inicio | 2919 | 5 |
  | Objetivo | 907 | 2089 |

  En el encoder es subir en línea recta de 5 a 2089; el salto de +π a −π existe solo en nuestras coordenadas, porque el OFS corre el corte.
- **Para ROS:** el feedback del `wrist_roll` mostraría saltos de 2π en medio de un movimiento normal; `ros2_control`/MoveIt lo verían como una vuelta entera de golpe (puede abortar trayectorias).
- **Para el hardware:** el tope está cerca de 907 cuentas (encoder ~2090); con el OFS actual el rango físico cruza el corte y el servo puede elegir el lado del tope.
- **Solución (`[R08]`):** recalibrar el `wrist_roll` con `--record-wrist-roll` de modo que **todo su rango físico quede dentro de 0..4095 sin cruzar el corte**, con el cero cerca del centro y alineado con el cero del URDF. Así desaparecen el salto y el camino largo, y la EEPROM lo frena antes del tope. Hasta entonces, no mandarle objetivos lejos de donde está.

## Tests en la PC (`test_host/`)

Funciones puras (sin hardware): se compilan con `gcc`, sin ESP-IDF. Desde `firmware/esp32_arm`:

```
gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
    test_host/test_frame_parser.c main/frame_parser.c -o /tmp/test_frame_parser && /tmp/test_frame_parser

gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
    test_host/test_servo_units.c main/servo_units.c -lm -o /tmp/test_servo_units && /tmp/test_servo_units

gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
    test_host/test_m_payload.c main/m_payload.c -o /tmp/test_m_payload && /tmp/test_m_payload

gcc -Wall -Wextra -Werror -fsanitize=address,undefined -I main \
    test_host/test_servo_bus.c main/servo_bus.c -o /tmp/test_servo_bus && /tmp/test_servo_bus
```

- `test_frame_parser.c` (14 casos): trama `M` de 44 bytes con LEN `0039`, byte a byte, incompleta que espera, 50 tramas en un solo bloque (más grande que el buffer), basura/log antes, 1000 bytes sin STX, LRC/ETX/FS malos, trama buena dentro de una corrupta, LEN corrupto que demora, LEN inválidos (`9999`, `0129`, `0003`, `00a9`) y límites (LEN 4 y 128), dos tramas cortadas en cada punto posible; `frame_build()` igual byte a byte a la trama de `send_m.py`, ida y vuelta por el parser y límites.
- `test_m_payload.c`: payload válido (signos, ceros, ±3142), fuera de rango bien formado (se acepta), 10 malformados (vacío, 5 o 7 joints, sin signo, espacio como signo, letra, último campo malo, coma en vez de FS, campo corto + largo, FS al final): todos devuelven `false` y dejan la salida intacta; `m_payload_format()` igual al formato del plugin, recorte a ±9999, `parse(format(x)) == x` para todo [−3142, 3142].
- `test_servo_bus.c`: paquete de 2 servos idéntico byte a byte al ejemplo 2 de `[E04]`; 6 servos (56 bytes, Length, little-endian, suma del receptor = `0xFF`); buffer un byte corto; Length desbordado (31 servos entran, 32 no); `SYNC_READ` de 6 servos (Length N + 4, suma `0xFF`); respuesta del ejemplo 2 del manual (`FF FF 01 04 00 18 05 DD` → 1304); 6 respuestas después de un eco y ruido, bit de signo y ERROR; servo que no responde, checksum malo y orden equivocado (salida intacta).
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
- [x] Enviar `SYNC_WRITE` al bus de servos (`servo_bus.{h,c}`, velocidad/aceleración en Kconfig), con tests en la PC
- [x] Probar el `SYNC_WRITE` con el brazo (ver "Resultados de la prueba con el brazo")
- [x] Leer las posiciones reales (`SYNC_READ`) y responder con una `M` de feedback, con tests en la PC
- [x] Probar la `M` de feedback con el brazo: vuelta completa verificada en las tres vistas (bus, consola, PC)
- [x] Ver la convergencia del feedback con `--count 250`: 5 joints convergen (±1°), 250/250 respuestas tras corregir el timeout del `SYNC_READ`
- [ ] Recalibrar el `wrist_roll` (`[R08]`): rango físico dentro de 0..4095 sin cruzar el corte, cero centrado y alineado con el URDF, límites en la EEPROM
- [ ] Comprar adaptador USB-serie TTL 3.3V
