# Bitácora — E03: ¿Hace falta adaptador lógico entre ESP32 y driver de servos?

## Objetivo
Averiguar si es necesario un conversor de nivel lógico entre el ESP32 y el driver de servos (Waveshare Bus Servo Adapter (A)) antes de cablear la UART entre ambos, y conseguir uno si hiciera falta.

## Contexto
- Niveles del ESP32 ya confirmados en [E02](bitacora-E02-niveles.md): 0V (LOW) / 3.3V (HIGH), CMOS estándar.
- La documentación pública de Waveshare (wiki + `docs.waveshare.com/Bus_Servo_Adapter_A`) especifica la alimentación de potencia (9~12.6V, o 5~8.4V) pero **no indica explícitamente** el nivel lógico de los pines UART de host.
- El ejemplo de wiring del fabricante muestra la interfaz UART conectada directo a una Raspberry Pi Zero (GPIO a 3.3V, no tolerante a 5V) sin level shifter visible — indicio fuerte pero no concluyente.

## Análisis del esquemático
Esquemático disponible en `doc/datasheets/Bus Servo Adapter (A)-Sch.pdf`. Hallazgo con evidencia directa (no inferencia):

- **U2 (AMS1117-3.3)**: regulador que genera el riel **3V3** de la placa a partir de la alimentación USB/5V.
- **U3 (SN74LVC1G126)** y **U4 (SN74LVC1G125)**: buffers de línea que manejan las señales `U1TXD`/`U1RXD` hacia el conector de host (H4, el que se cablea al ESP32). Ambos tienen su pin **VCC alimentado del riel 3V3**, no de los 5V de VDDUSB ni del DC_IN de los servos.
- El riel de 5V de la placa solo alimenta la etapa USB (chip USB-UART CH343P) y la conversión de potencia hacia los servos — nunca las líneas de datos del conector de host.

**Conclusión del esquemático:** las líneas TXD/RXD expuestas al host (ESP32) están ancladas a lógica 3.3V, igual que el ESP32.

Detalle adicional (no afecta el nivel de tensión, pero relevante para el firmware): hay un transistor Q1 (MMBT3906) con una señal `TXEN` en la misma etapa, que parece un circuito de auto-detección/control de dirección del buffer (se activa cuando el host transmite). Si el ESP32 no recibe nada la primera vez, revisar esto antes que la alimentación.

## Conclusión

**No hace falta adaptador/conversor de nivel lógico.** ESP32 (3.3V CMOS, confirmado en E02) y el conector de host del Bus Servo Adapter (A) (3.3V TTL, confirmado por esquemático: U2/U3/U4) son directamente compatibles. Se puede cablear directo entre ambos, **sin cruzar: TX-TX, RX-RX** (ver "Cableado ESP32 ↔ driver").

> **Corrección (`[E06]`):** esta conclusión decía originalmente "TX-RX / RX-TX" (cruzado). Es incorrecto: el fabricante, el esquemático y la serigrafía indican TX-TX / RX-RX.

## Cableado ESP32 ↔ driver (verificado en `[E06]`)

| ESP32 | Driver (conector `UART`, serigrafía `TX RX GND`) |
|---|---|
| `TX2` (GPIO17) | **TX** |
| `RX2` (GPIO16) | **RX** |
| `GND` | **GND** |

**Jumper amarillo en A** (`A = UART – SERVO`). En **B** (`USB – SERVO`) el bus lo maneja el USB del driver (como con `STServo_Python`) y el ESP32 no llega a los servos.

Tres fuentes independientes que coinciden:

1. **Wiki oficial de Waveshare** ([Bus Servo Adapter (A)](https://www.waveshare.com/wiki/Bus_Servo_Adapter_(A)), sección "UART Control"): *"For hardware devices without USB connectors, such as Raspberry Pi Zero, ESP32, Arduino, and STM32 development board, you can realize serial bus servo control and data feedback via the UART port with the hardware device, and the connection must be RX-RX, TX-TX. Note that the jumper cap of the bus servo adapter must be on the A as shown below."*
2. **Esquemático** (`doc/datasheets/Bus Servo Adapter (A)-Sch.pdf`):
   - `H2` es el conector del host: pin 1 `GND`, pin 2 `RXD`, pin 3 `TXD`.
   - `H4` (2×3) es el selector: fila `RXD/TXD` (host por `H2`), fila `U1RXD/U1TXD` (adaptador), fila `CH_RXD/CH_TXD` (chip USB CH343). Jumpers `U1↔H2` = modo UART; `U1↔CH` = modo USB.
   - En modo USB, `U1TXD` se une a `CH_TXD`, la **salida** TX del CH343: `U1TXD` es la línea por donde **transmite el host** (entra a Q1 y a U3, que la pasa al bus `DATA`). `U1RXD` sale de U4 (bus → host) y va a `CH_RXD`, la entrada del CH343. En modo UART, `U1TXD` se une al `TXD` de `H2`: **las etiquetas de `H2` nombran lo que hace el host en ese pin**, por eso no se cruza.
3. **Serigrafía** (`doc/img/bus-servo-driver.jpeg`, placa V1.1): conector `TX RX GND` etiquetado `UART`; tabla `A = UART – SERVO`, `B = USB – SERVO`; alimentación `DC 9~12.6V` (jack o bornera).

Del esquemático, además:

- **Sin eco:** mientras el host transmite, Q1 activa `TXEN`, que deshabilita U4 (SN74LVC1G125, OE activo bajo); `U1RXD` queda en alto por R8. El ESP32 no recibe lo que manda (el parser de `servo_bus` igual lo tolera).
- **La lógica no necesita el USB:** el 3.3V (U2, AMS1117) se alimenta del 5V de la fuente de servos (vía D3) o del USB (vía D4).

## Próximos pasos

- [x] Revisar documentación pública de Waveshare (wiki + docs) — nivel lógico no especificado explícitamente
- [x] Analizar esquemático oficial (`Bus Servo Adapter (A)-Sch.pdf`) — confirmado riel 3V3 en la etapa de buffers de host (U2/U3/U4)
- [x] Concluir si hace falta conversor de nivel — **no hace falta**
- [ ] (Recomendado antes de cablear en firme) Verificar con multímetro la tensión de reposo (idle HIGH) del TX del adaptador, como chequeo final de hardware
