# Bitácora — E02: Niveles de voltaje TX/RX del ESP32

## Objetivo
Medir el voltaje mínimo (nivel LOW) y máximo (nivel HIGH) de las señales digitales de salida del ESP32, para conocer sus características eléctricas antes de interfacearlo con el driver de motores (relacionado con E03: evaluar si hace falta un conversor lógico).

## Decisiones tomadas

- **El analizador lógico U6041 (24MHz/8CH) no sirve para esta tarea.** Es puramente digital por umbral, no mide voltaje analógico: según su datasheet (`doc/datasheets/logical-analyzer-U6041.pdf`), el rango de entrada es 0–5.5V con umbral fijo en 1.5V — por debajo se interpreta `LOW`, por arriba `HIGH`. No devuelve ningún valor numérico de voltios.
- **Medir la señal UART en tránsito con un multímetro DC tampoco sirve.** Una trama es una sucesión de 1s y 0s a alta frecuencia; el multímetro en DC integra/promedia en el tiempo, así que la lectura sería un valor intermedio dependiente del duty cycle de esa trama puntual, no el mínimo ni el máximo reales.
- **Solución elegida:** forzar el pin en un estado fijo (alto o bajo, sin conmutar) para poder tomar una lectura DC estable y real con el multímetro en cada uno de los dos niveles.
- **Implementación:** reutilizar el proyecto `firmware/blink` (ya usa un GPIO genérico, no el LED de la placa) cambiando:
  - `BLINK_GPIO` de `23` a `17` (pin TX)
  - el delay de `1000ms` a `10000ms`, para tener una ventana de 10s en cada nivel y dar tiempo a tomar la lectura con el multímetro
- **Solo se mide un GPIO de salida digital genérico (no se repite para TX/RX específicos ni otros pines).** Se asume el mismo nivel de voltaje en todos los pines GPIO digitales de salida del ESP32, porque comparten el mismo driver de salida push-pull CMOS referenciado al mismo riel de alimentación digital (VDD_IO ≈ 3.3V) — no hay nada pin-específico que cambie ese nivel.

## Medición realizada

- **Pin medido:** GPIO23 (el `BLINK_GPIO` original de `firmware/blink`, no se cambió a 17 — no hacía falta, ver asunción de arriba)
- **Multímetro:** escala DC de 20V
- **Resultado:**
  - VL (LOW) = **0.00 VDC**
  - VH (HIGH) = **3.29 VDC**
- Coincide con lo esperado (nivel lógico CMOS 3.3V del ESP32). Diferencia de 0.01V frente al nominal, dentro de la tolerancia normal del regulador y la resolución del instrumento.

### Hallazgo: carga del pin al conectar un LED directo
Al intentar medir con un LED conectado directamente al pin (sin resistencia limitadora), la tensión leída caía por debajo del valor real. Causa: el pin de un GPIO se modela como Thévenin — una fuente ideal en **serie** con una resistencia interna de salida **baja** (no alta; la resistencia alta es la del multímetro, del orden de megaohms). Con el multímetro solo, la corriente que circula es casi nula, por lo que la caída sobre esa resistencia interna (Ley de Ohm, V=I×R) es despreciable y se mide prácticamente el valor ideal de la fuente. Con el LED conectado (que sin resistencia serie se comporta casi como un cortocircuito una vez polarizado), circula corriente apreciable, y buena parte de los 3.3V cae sobre la resistencia interna del pin en vez de llegar al LED — de ahí la lectura baja. Se resolvió midiendo a circuito abierto (solo el multímetro, sin el LED).

## Conclusión

Niveles TX/RX del ESP32 confirmados: **0V (LOW) / 3.3V (HIGH)**, lógica CMOS estándar de 3.3V. Insumo para E03: cualquier dispositivo que espere niveles distintos (p. ej. 5V) va a necesitar un conversor lógico de nivel.

## Próximos pasos

- [x] Flashear `firmware/blink` modificado (delay 10s)
- [x] Medir con el multímetro en DC: voltaje en estado HIGH y voltaje en estado LOW
- [x] Registrar los valores medidos acá
- [x] Comparar contra lo esperado (0V / 3.3V) y evaluar impacto en E03 (necesidad de conversor lógico)
- [x] Revertir `firmware/blink/main/main.c` a su estado original una vez terminada la medición
