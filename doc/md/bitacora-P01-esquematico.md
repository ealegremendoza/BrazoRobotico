# Bitácora [P01] — Esquemático de la placa controladora

Proyecto KiCad 9: `pcb/arm_controller_board`. Librería del proyecto: `arm_controller_board.kicad_sym` (símbolos) y `arm_controller_board.pretty` (footprints).

## Medidas

Calibre analógico de 0,02 mm (ver `~/esp-ws/calibre.md`). Todas las medidas se tomaron con las mordazas grandes (exteriores), así que llevan la corrección de **+0,18 mm**. Para pasar de "borde de un pin" a "centro de un pin" se resta medio pin (0,32 mm, pines cuadrados de 0,64 mm).

### ESP32-DevKitC V4 (38 pines)

| Medida | Lectura | Corregida |
|---|---|---|
| Largo de la placa (sin el USB) | 54,80 | **54,98 mm** |
| Ancho de la placa | 28,10 | **28,28 mm** |
| Borde del lado de la antena → cara del pin 1 que mira al pin 2 | 4,06 | 4,24 mm → centro del pin 1 a **3,92 mm** (igual en las dos tiras) |
| USB: cuánto sobresale del borde | 0,80 | **0,98 mm** |
| USB: ancho (centrado) | 7,44 | **7,62 mm** |
| Entre filas, centro a centro (a ojo) | 25,10 | 25,28 mm → se usa **25,40 mm** (10 × 2,54) |

- Derivadas: centro del pin 19 a 49,64 mm del borde de la antena; del pin 19 al borde del USB quedan 5,34 mm.
- Base Goouuu ("ESP32S 38P/V4/Goouuu Expansion board", `doc/img/esp32-38pines-base-1.jpeg`): la DevKit va en la **tira de afuera** (posición ancha). La serigrafía dice 24,00 y 25,500 mm, pero las dos tiras están a 2,54 mm entre sí, así que esos números son aproximados.
- La DevKit queda a la altura del zócalo hembra (~8,5 mm típico, **sin medir**): ese es el límite de altura para componentes debajo del módulo.

### HW-221 (TXS0108E, marcado YF08E)

| Medida | Lectura | Corregida |
|---|---|---|
| Ancho | 16,28 | **16,46 mm** |
| Largo | 26,40 | **26,58 mm** |
| Borde de arriba → cara del pin 1 que mira al pin 2 | 1,50 | 1,68 mm → centro del pin 1 a **1,36 mm** |
| Entre filas, borde exterior a borde exterior | 13,00 | 13,18 − 0,64 = 12,54 mm → se usa **12,70 mm** (5 × 2,54) |

- 10 pines por fila, paso 2,54 mm: del pin 1 al 10 hay 22,86 mm; del pin 10 al borde de abajo quedan 2,36 mm.
- Orden de pines (de arriba hacia abajo, vista desde la serigrafía): fila 1 `VA, A1–A8, OE`; fila 2 `VB, B1–B8, GND`.
- OE está puenteado a VA en el módulo (medido con continuidad en `[E05]`).

## Símbolos y footprints

| Elemento | Símbolo | Footprint |
|---|---|---|
| ESP32-DevKitC V4 | `ESP32_DevKitC_V4_38` (referencia `A`, disposición física: pines 1–19 a la izquierda de 3V3 a 5V, 20–38 a la derecha de GND a CLK) | `ESP32_DevKitC_V4_38` (courtyard completo) y `ESP32_DevKitC_V4_38_Underfill` (courtyard solo en los zócalos, permite componentes bajos debajo) |
| HW-221 | `HW221_TXS0108E` (pines 1–10 fila 1, 11–20 fila 2) | `HW221_TXS0108E` |

Tipos de pin de la DevKit: IO34/35/36/39 y EN `input`; pines de la flash (SD0–SD3, CMD, CLK) `passive`; GND `power_in`; 3V3 y 5V `power_out` (la DevKit se alimenta por su USB y es la fuente de la placa).

## Conexiones (de `firmware/esp32_arm`)

Verificadas contra `sdkconfig`, el código y la tabla de conexionado de `bitacora-E10-panel-lcd.md`.

| Bloque | ESP32 | Fuente |
|---|---|---|
| Driver de servos (Waveshare A) | IO17 → TX del driver, IO16 → RX del driver, GND | `CONFIG_ARM_SERVO_UART_TX/RX_GPIO` |
| LCD 1602 I2C vía HW-221 | IO25 SDA, IO26 SCL; lado A 3V3, lado B 5V al LCD | `CONFIG_LCD_I2C_SDA/SCL_GPIO` |
| Potenciómetro B1K | cursor IO34, extremos 3V3 y GND | `ui_panel.c` (ADC1 canal 6) |
| Pulsador | IO33, pull-up 10 kΩ a 3V3, a GND con 100 nF en paralelo | `CONFIG_NAV_BUTTON_GPIO`, `button_esp32.c` |
| Consola (CP2102) | IO14 TX, IO27 RX, GND, sin VCC | `CONFIG_CONSOLE_UART_TX/RX_GPIO` |

- El driver tiene su propia fuente (9–12,6 V): con la placa comparte solo GND.
- El 5V del LCD sale del pin 5V de la DevKit (USB).
- Driver, LCD, potenciómetro, pulsador y consola van por conector; el pull-up y el capacitor del pulsador van en la placa; el HW-221 va enchufado en zócalos.

## Esquemático (primera versión)

| Ref | Qué | Pines / red | Footprint |
|---|---|---|---|
| A1 | ESP32-DevKitC V4 | ver tabla de conexiones | `ESP32_DevKitC_V4_38_Underfill` |
| A2 | HW-221 | VA y OE a +3V3, VB a +5V; A1/B1 = SDA, A2/B2 = SCL; A3–A8 y B3–B8 sin conectar | `HW221_TXS0108E` |
| J1 | SERVO | 1 SERVO_TX (IO17), 2 SERVO_RX (IO16), 3 GND | pin header 1x03 |
| J2 | LCD | 1 GND, 2 +5V, 3 SDA_5V, 4 SCL_5V (orden del módulo PCF8574) | pin header 1x04 |
| J3 | POT | 1 +3V3, 2 POT_WIPER (IO34), 3 GND | pin header 1x03 |
| J4 | BUTTON | 1 NAV_BTN (IO33), 2 GND | pin header 1x02 |
| J5 | CONSOLE | 1 GND, 2 CON_TX (IO14, al RXD del adaptador), 3 CON_RX (IO27, al TXD del adaptador) | pin header 1x03 |
| R1 | 10 kΩ pull-up | +3V3 – NAV_BTN | THT axial DIN0207 |
| C1 | 100 nF | NAV_BTN – GND | THT disco 5 mm |

- PWR_FLAG en GND (ninguna salida de potencia maneja GND); +3V3 y +5V los entrega la DevKit (`power_out`).
- ERC de KiCad 9.0.7 (`kicad-cli sch erc --severity-all`): 0 violaciones. El netlist exportado tiene las 13 redes de la tabla.
- R1 y C1 quedaron THT por facilidad de soldado; se pueden pasar a SMD 0805 si se quiere ponerlos debajo de la DevKit.
