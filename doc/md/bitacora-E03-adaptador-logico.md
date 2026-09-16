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

**No hace falta adaptador/conversor de nivel lógico.** ESP32 (3.3V CMOS, confirmado en E02) y el conector de host del Bus Servo Adapter (A) (3.3V TTL, confirmado por esquemático: U2/U3/U4) son directamente compatibles. Se puede cablear TX-RX / RX-TX directo entre ambos.

## Próximos pasos

- [x] Revisar documentación pública de Waveshare (wiki + docs) — nivel lógico no especificado explícitamente
- [x] Analizar esquemático oficial (`Bus Servo Adapter (A)-Sch.pdf`) — confirmado riel 3V3 en la etapa de buffers de host (U2/U3/U4)
- [x] Concluir si hace falta conversor de nivel — **no hace falta**
- [ ] (Recomendado antes de cablear en firme) Verificar con multímetro la tensión de reposo (idle HIGH) del TX del adaptador, como chequeo final de hardware
