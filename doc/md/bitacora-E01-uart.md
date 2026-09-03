# Bitácora — E01: UART ESP32 (TX-RX)

## Objetivo
Programar el ESP32 para enviar y recibir mensajes por puerto serie (UART) usando los pines TX-RX.

## Decisiones tomadas

- **UART elegida: UART2** (pines por defecto TX=GPIO17, RX=GPIO16)
  - Pin físico en el conector J3 del DevKit V4: TX=pin 11, RX=pin 12 (verificado contra el esquemático `esp32_devkitc_v4-sch.pdf`)
  - UART0 descartada: reservada para el monitor serie / programación por USB
  - UART1 descartada: pines por defecto GPIO9(RX)/GPIO10(TX) comparten líneas con la flash SPI (SD_DATA2/3) — riesgo de conflicto según el modo de flash
- **Módulo confirmado: ESP32-WROOM-32UE** (esquemático muestra el bloque WROVER como NC/no poblado) → GPIO16/17 libres, sin PSRAM
  - Nota de portabilidad: en un módulo **WROVER**, GPIO16/17 quedan reservados para el chip de PSRAM (chip-select/clock) — no usar ahí si se migra a un WROVER
  - GPIO6-11 reservados en prácticamente cualquier módulo ESP32 (flash integrada) — evitar siempre
- **Framework: ESP-IDF + FreeRTOS**, consistente con `firmware/blink` (no Arduino framework)
- **Proyecto nuevo:** `firmware/uart_test/` (no se reutiliza blink)
- **Configuración de pines:** por ahora vía `#define` en el código, para arrancar simple

## Herramientas

- Logic Analyzer 24 MHz / 8 CH — software Logic2
  - Instalado en: `/home/ezequiel/Downloads/Logic2`
  - Se ejecuta con: `sudo ./Logic-2.4.39-linux-x64.AppImage --no-sandbox`

## Próximos pasos

- [x] Crear proyecto `firmware/uart_test/`
- [x] Implementar envío de mensajes por UART2 usando el `#define` de pines
- [x] Capturar y validar la señal con el Logic Analyzer
- [x] **Migrar la configuración de pines a Kconfig** (`idf.py menuconfig` + `main/Kconfig.projbuild`)

## Entorno / Troubleshooting

- **Creación del proyecto por CLI** (en vez del wizard de la extensión ESP-IDF de VSCode):
  ```
  . ~/esp/esp-idf/export.sh
  idf.py -C firmware create-project uart_test
  ```
  El `export.sh` activa el entorno de ESP-IDF (agrega `idf.py` al PATH) en la terminal actual; hay que correrlo en cada terminal nueva.

- **Problema encontrado:** la extensión ESP-IDF de VSCode no cargaba los comandos para crear un proyecto nuevo. Diagnóstico:
  - El ícono de la extensión mostraba el símbolo de sincronización y aparecía el error "There is no data provider registered that can provide view data."
  - El panel Output no tenía canal "ESP-IDF" (la extensión nunca llegó a registrarlo).
  - `Developer: Show Running Extensions` mostró la extensión ESP-IDF en estado `Activating...` indefinido → activación colgada, no un error con stack trace.
  - Se descartó `IDF_PYTHON_ENV_PATH` como causa (variable vacía).
  - **Workaround usado:** crear el proyecto directamente con `idf.py` desde terminal, sin depender de la extensión.
  - **Pendiente sin resolver:** por qué la extensión se cuelga en `Activating...`. No es bloqueante para seguir (el flujo por CLI funciona), pero queda como deuda de entorno a investigar más adelante.

- **Ojo con `-C` en `idf.py create-project`:** si ya estás parado dentro de `firmware/`, correr `idf.py -C firmware create-project uart_test` crea una carpeta `firmware/` anidada de más (`firmware/firmware/uart_test`). El flag `-C` es relativo al directorio actual, no a la raíz del repo. Corregido moviendo el proyecto a `firmware/uart_test/`.
- El archivo `.c` principal generado por `idf.py create-project <nombre>` no se llama `main.c`, se llama `<nombre>.c` (en este caso `uart_test.c`).

## Conceptos — Driver UART (ESP-IDF)

### Estructura de una trama UART
UART no tiene reloj compartido entre TX y RX: cada lado infiere el timing de cada bit a partir de un baud rate acordado de antemano. Cada trama sigue esta estructura: **bit de start** → **bits de datos** → **bit de paridad (opcional)** → **bit(s) de stop**.

### Campos de `uart_config_t`
- **`baud_rate`**: velocidad en bits por segundo. Debe coincidir en ambos extremos o se desincroniza el muestreo.
- **`data_bits`** (`UART_DATA_8_BITS`): cantidad de bits de datos por trama (5 a 8; 8 = un byte completo, lo estándar).
- **`parity`** (`UART_PARITY_DISABLE`): bit extra opcional de detección de error (`EVEN`/`ODD`). Detección rudimentaria (no corrige, no detecta errores de 2 bits). `DISABLE` es común; muchos protocolos prefieren checksum a nivel de aplicación.
- **`stop_bits`** (`UART_STOP_BITS_1`): bits que marcan el fin de la trama (1, 1.5 o 2), dan margen al receptor antes del próximo start bit.
- **`flow_ctrl`** (`UART_HW_FLOWCTRL_DISABLE`): control de flujo por hardware vía pines extra RTS/CTS. Debe estar `DISABLE` si esos pines no están cableados (si no, el driver espera una señal que nunca llega y se bloquea). No aplica al loopback TX-RX simple.
- **`source_clk`** (`UART_SCLK_DEFAULT`): fuente de reloj interna del ESP32 de la que deriva el timing del periférico. `DEFAULT` deja que el framework elija la recomendada para el chip.

### `intr_alloc_flags` de `uart_driver_install()`
Controla cómo se registra la interrupción que usa el driver para atender eventos del UART (byte recibido, FIFO lleno, etc.) — prioridad, si se puede compartir, si el handler debe vivir en IRAM, etc. Son constantes `ESP_INTR_FLAG_*` (bitmask, se combinan con OR) definidas en `esp_intr_alloc.h`.

- Pasar `0` = sin flags especiales, asignación por defecto (lo que usa el ejemplo oficial `uart_echo`).
- **No usar `ESP_INTR_FLAG_IRAM`** con el driver UART: su ISR no está ubicada en IRAM, así que pedir ese flag lo rompe.
- IRAM es un recurso del chip completo (memoria interna rápida donde el código sigue siendo ejecutable aunque la caché de flash esté deshabilitada, p. ej. durante escritura/borrado de flash) — no es algo específico de UART1 ni de ningún puerto en particular; aplica igual a cualquier interrupción de cualquier periférico.

### `uart_set_pin()`: GPIO vs. pin físico del conector
`tx_io_num` y `rx_io_num` (y `rts_io_num`/`cts_io_num`) son **números de GPIO**, no la posición física en el header de la placa. Para este proyecto: `17` (TX) y `16` (RX) — no `11`/`12` (esos son solo la posición en el conector J3 del DevKit V4, útil para saber dónde poner el cable o la punta del analizador lógico, pero el chip y la API de ESP-IDF no conocen esa numeración de conector).

## Implementación: loopback TX-RX con `uart_echo`

Base: copiado y estudiado el ejemplo oficial [`uart_echo`](https://github.com/espressif/esp-idf/blob/v6.1/examples/peripherals/uart/uart_echo/main/uart_echo_example_main.c) (sin la parte de Kconfig todavía).

### Bug inicial: "no veo nada" en el monitor
El loop original de `echo_task` es puramente reactivo: primero lee (`uart_read_bytes`), y solo si `len > 0` escribe/loguea. En loopback puro (TX-RX puenteados, sin nada externo) nunca hay un primer byte que dispare el ciclo — silencio total, sin importar que el hardware esté bien armado.

**Fix aplicado:** invertir el orden — escribir primero un mensaje fijo, después leer. Cada vuelta del loop genera su propio evento sin depender de un disparador externo.

- Riesgo de carrera a tener en cuenta con este orden: `uart_write_bytes()` solo encola el dato, no espera a que salga físicamente por el cable. Si el `tick_to_wait` de `uart_read_bytes()` es muy corto, se podría intentar leer antes de que el byte complete el loopback. Con ~20ms de timeout y mensajes cortos a 115200 baudios sobra margen.

### `vTaskDelay` al final del loop
Se agregó `vTaskDelay(1000 / portTICK_PERIOD_MS)` al final del `while(1)` — no por necesidad del scheduler (`uart_read_bytes` con timeout ya cede el CPU mientras espera, no hace busy-waiting), sino para espaciar los eventos a un ritmo observable por el humano (1/segundo) en el monitor y al capturar con el Logic Analyzer, en vez de ~50 eventos/segundo sin el delay.

### Resultado
Loopback validado end-to-end: mensaje enviado por TX (`uart_write_bytes`), reflejado físicamente por el puente TX-RX, leído por RX (`uart_read_bytes`) y logueado (`ESP_LOGI`) cada ~1020ms (1000ms del delay + overhead del timeout de lectura), confirmando TX y RX funcionando.

## Captura con Logic Analyzer

Validado: se conectó GND del analizador a GND del ESP32 (además de los canales TX/RX) — imprescindible para tener referencia de voltaje común, sin eso las lecturas son ambiguas/ruidosas. Con el analizador de protocolo "Async Serial" (Logic2) configurado con los mismos parámetros del código, se pudo ver la trama completa del mensaje "Hola" saliendo por TX. Cierra la validación end-to-end: código → hardware (loopback) → confirmación visual de la trama real en el bus.

## Migración a Kconfig

Se migraron los `#define` de pines/baudrate/puerto/stack a macros `CONFIG_EXAMPLE_*` generadas por `main/Kconfig.projbuild` (copiado y adaptado del ejemplo oficial `uart_echo`).

### Sintaxis Kconfig aprendida
- Un mismo `config` puede tener varias líneas `range`/`default`, cada una con su propio `if <condición>` opcional. Se evalúan de arriba hacia abajo; se usa la primera cuya condición sea verdadera (o la que no tiene condición, como catch-all). Permite que un solo archivo sirva a varios chips target distintos con valores por defecto diferentes.
- `orsource "ruta"`: incluye otro archivo Kconfig, con ruta relativa al archivo actual y de forma opcional (no falla si no existe). Útil para dividir la config en varios archivos; no fue necesario para este proyecto.
- **`Kconfig.projbuild` aparece en el menú raíz de `menuconfig`**, no anidado bajo "Component config" (eso es solo para archivos `Kconfig` sin el sufijo `.projbuild`).

### Gotcha: defaults del ejemplo oficial no coinciden con el hardware
El `Kconfig.projbuild` de `uart_echo` trae como default `TXD=4`, `RXD=5` (Espressif eligió esos GPIOs arbitrariamente para su ejemplo) — no 16/17. Hubo que entrar a `idf.py menuconfig` → "Echo Example Configuration" y cambiarlos a mano para que coincidan con el cableado físico ya validado.

### Problemas de build encontrados y resueltos
- **`error: 'CONFIG_EXAMPLE_UART_*' undeclared`**: pasó porque nunca se corrió `idf.py menuconfig` (ni ningún reconfigure) después de agregar el `Kconfig.projbuild` nuevo — el `sdkconfig` no tenía esas opciones generadas todavía. Se resolvió corriendo `idf.py menuconfig` (que fuerza el reconfigure) y seteando los pines ahí mismo.
- **`CMake Error: Unknown CMake command "idf_component_register"`**: pasó por correr `idf.py menuconfig` estando parado dentro de `main/` en vez de la raíz del proyecto (`uart_test/`). Sin `-C`, `idf.py` usa el directorio actual como raíz; tomó el `CMakeLists.txt` del componente (`main/CMakeLists.txt`) como si fuera el de nivel superior del proyecto, donde `idf_component_register` no está definido. Se resolvió subiendo un nivel (`cd ..`) antes de correr el comando. Quedó una carpeta espuria `main/build/` de ese intento, sin impacto, borrable con `rm -rf main/build`.

### Resultado
Build, flash y monitor funcionando con la configuración por Kconfig — pines TX=17/RX=16 confirmados vía `menuconfig`, ya no hardcodeados en el código.
