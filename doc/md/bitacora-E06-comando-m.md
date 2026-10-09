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
- **`sdkconfig` no se versiona** (lo ignora el `.gitignore`, igual que en `esp32_bridge`): lo configurado con `menuconfig` no viaja con el repo. Pendiente: agregar un `sdkconfig.defaults` versionado con lo esencial (target, pines, baudrates).

## Placa

La DevKitC de 38 pines (WROOM) está ocupada en otra cosa; E06 se desarrolla sobre una placa genérica de 30 pines (tipo "DevKit V1"). Fotos: `doc/img/esp32.jpeg`, `doc/img/esp32-wroom-32.jpeg`, `doc/img/esp32-vs-esp32wroom.jpeg`.

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
| UART0 | Protocolo con la PC (USB de la placa) | USB | 115200 |
| UART1 | Consola de logs (solo TX) | TX=21 | 115200 |
| UART2 | Bus de servos | TX=17, RX=16 (ver `[E01]`) | 1000000 |

### La consola sale de UART0

Por defecto la consola de ESP-IDF (`ESP_LOGx`, `printf`) sale por UART0, que es el mismo canal del protocolo:

- Texto suelto **entre** tramas: el parser de `read()` en `[R09]` lo descarta (busca `STX`, y el texto ASCII imprimible no contiene `0x02`).
- Un log escrito **en medio** de una trama la corrompe: falla el LRC y la trama se descarta. El ciclo de feedback se pierde sin aviso visible desde la PC.

**Regla: UART0 tiene un solo dueño, el protocolo.** La consola se mueve a UART1 (`menuconfig` → `Component config → ESP System Settings → Channel for console output → Custom UART`), usando solo el pin TX.

Los mensajes del bootloader (ROM) siguen saliendo por UART0 al arrancar; el parser los descarta y no hay tramas en vuelo en ese momento.

En la versión final se puede apagar la consola (`CONFIG_ESP_CONSOLE_NONE`).

### Pin del TX de consola: GPIO21

Los pines por defecto de UART1 (GPIO9/10) van a la flash; la señal se rutea a otro pin por la matriz GPIO. GPIO21:

- no es de strapping (0, 2, 5, 12, 15), no va a la flash (6-11), no es de solo entrada (34-39);
- no es ADC: deja libre el **ADC1 (GPIO32-39) para los potenciómetros** (preferir ADC1: el ADC2 no funciona con WiFi activo);
- no es pin nativo de VSPI (23, 19, 18, 5): quedan libres por si se agrega una SD. Igual el SPI también se puede rutear por la matriz (límite ~26 MHz full-duplex; la SD por SPI usa 20 MHz).

GPIO23 también era válido; se descartó solo para dejar VSPI completo. En Arduino, GPIO21/22 son el I2C por defecto; en ESP-IDF el I2C se rutea a cualquier pin.

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
- [x] Elegir el GPIO del TX de consola (UART1): GPIO21
- [ ] Puerto serie configurable en el plugin (fuera de E06, pasó al backlog como `[R13]`): hoy `/dev/ttyACM0` está fijo en `robotic_arm_ros2_control.xacro`; con la DevKitC (CP2102) es `/dev/ttyUSB0` y no conectaría. Opciones: argumento del launch o regla udev con nombre fijo (p. ej. `/dev/robotic_arm`)
- [ ] `Kconfig.projbuild`: puerto, pines TX/RX y baudrate de cada UART (ojo: el rango `1200..115200` de `esp32_bridge` rechaza 1000000)
- [ ] `sdkconfig.defaults` versionado
- [ ] `mrad_to_counts(int32_t mrad)` con redondeo simétrico
- [ ] Comprar adaptador USB-serie TTL 3.3V
