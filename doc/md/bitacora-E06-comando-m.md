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

**Decisión: TX = GPIO14, RX = GPIO27.** Con los pines invertidos el diseño es seguro sea cierto o no lo del PWM: si existe, sale por TX hacia la entrada del adaptador y como mucho aparecen caracteres basura en la consola al arrancar. Pendiente: confirmar con el analizador mirando GPIO14 durante un reset.

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

## Pendientes

- [x] Crear `firmware/esp32_arm` (`set-target esp32`, `.gitignore`)
- [x] Elegir los pines de la consola (UART1): TX 14, RX 27
- [x] `board_pins.h` con puertos, pines y baudrates de UART0 (PC) y UART2 (servos)
- [x] Agregar a `board_pins.h` los pines de UART0 (TX 1, RX 3), con comentario de por qué son fijos
- [ ] Reconectar los pines de UART0 con `uart_set_pin()` en el código
- [x] Configurar la consola en `menuconfig` (UART1, TX 14, RX 27)
- [ ] Verificar con el analizador si GPIO14 saca PWM durante un reset
- [ ] Puerto serie configurable en el plugin (fuera de E06, pasó al backlog como `[R13]`): hoy `/dev/ttyACM0` está fijo en `robotic_arm_ros2_control.xacro`; con la DevKitC (CP2102) es `/dev/ttyUSB0` y no conectaría. Opciones: argumento del launch o regla udev con nombre fijo (p. ej. `/dev/robotic_arm`)
- [x] Migrar `board_pins.h` a `Kconfig.projbuild` (menú "Robotic Arm Configuration"): `ARM_PC_UART_BAUD_RATE` (115200), `ARM_SERVO_UART_BAUD_RATE` (1000000), `ARM_SERVO_UART_TX_GPIO` (17), `ARM_SERVO_UART_RX_GPIO` (16). Puertos y pines de UART0 quedan fijos en `board_pins.h`, que ahora mapea sus nombres a `CONFIG_ARM_*` (los `.c` no cambian). Baudrates con rango 9600..5000000 (el `1200..115200` de `esp32_bridge` rechazaba 1000000); GPIO con rango `0..SOC_GPIO_OUT_RANGE_MAX` (TX, excluye los de solo entrada 34-39) y `0..SOC_GPIO_IN_RANGE_MAX` (RX). Los defaults del Kconfig ya son los valores elegidos: no van en `sdkconfig.defaults`
- [x] `sdkconfig.defaults` versionado (target + consola). `Kconfig.projbuild` **crea** opciones; `sdkconfig.defaults` **elige valores** (propios y de ESP-IDF); `sdkconfig` es el resultado local
- [ ] `mrad_to_counts(int32_t mrad)` con redondeo simétrico
- [ ] Comprar adaptador USB-serie TTL 3.3V
