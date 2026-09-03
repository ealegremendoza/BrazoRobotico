# Bitácora — E05: Probar TX-RX ESP32 con Arduino Nano

## Objetivo
Probar el código UART del ESP32 (de E01) comunicándose con un Arduino Nano, programando este último para enviar y recibir mensajes por los pines TX-RX.

## Decisiones tomadas

- **Hace falta un conversor de nivel lógico bidireccional entre ambas placas.** El ESP32 trabaja con lógica de 3.3V (confirmado en E02: VL=0V, VH=3.29V) y el Arduino Nano trabaja con lógica de 5V — no se pueden conectar TX-RX directo entre ambos.
- **Módulo disponible: HW-221**, con un chip marcado **YF08E**.
  - Investigado y confirmado: **YF08E no es un chip distinto ni un clon** — es el código de marcado (topside marking) que Texas Instruments graba en el paquete TSSOP-20 del **TXS0108E**, su traductor de nivel bidireccional de 8 bits. El datasheet correspondiente es el oficial de TI para el TXS0108E.
  - Guardado en `doc/datasheets/txs0108e.pdf`.
  - Specs relevantes: VCCA acepta 1.2V–3.6V, VCCB acepta 1.65V–5.5V, traducción bidireccional no inversora, compatible con UART/I2C/SPI — encaja para el par 3.3V (ESP32) / 5V (Arduino Nano).
- **Pin OE (Output Enable) del TXS0108E:** entrada digital referenciada a VCCA. En alto habilita la traducción normal en todos los pines A/B; en bajo, todos los pines A1-A8/B1-B8 pasan a alta impedancia (chip "apagado" a nivel de señal). Su propósito según el datasheet es evitar glitches durante power-up/power-down.
  - Verificado con el multímetro en modo continuidad en el módulo HW-221: **OE está puenteado a VCCA en la placa** → queda siempre habilitado apenas se alimenta el módulo, no requiere manejo aparte.

## Circuito objetivo

```
PC --USB--> ESP32 <--UART2--> HW-221 <--SoftwareSerial--> Arduino Nano --D9/res--> LED
```

Objetivo funcional: enviar "ON"/"OFF" desde la PC (por el monitor serie del ESP32, UART0/USB) y que el ESP32 lo retransmita por UART2 al Arduino Nano a través del HW-221, que a su vez prenda/apague el LED en D9.

```mermaid
graph LR
    PC["PC"] -->|USB| ESP32

    subgraph ESP32["ESP32"]
        E_G17["G17 (TX, UART2)"]
        E_G16["G16 (RX, UART2)"]
        E_3V3["3V3"]
        E_GND1["GND"]
    end

    subgraph HW221["HW-221 (TXS0108E)"]
        H_VA["VA"]
        H_A1["A1"]
        H_A2["A2"]
        H_VB["VB"]
        H_B1["B1"]
        H_B2["B2"]
        H_GND["GND"]
    end

    subgraph NANO["Arduino Nano"]
        N_D2["D2 (RX, SoftwareSerial)"]
        N_D3["D3 (TX, SoftwareSerial)"]
        N_5V["5V"]
        N_GND["GND"]
        N_D9["D9"]
    end

    E_3V3 --> H_VA
    E_GND1 --- H_GND
    E_G17 -->|TX| H_A1
    H_A1 --> H_B1
    H_B1 -->|RX| N_D2

    N_D3 -->|TX| H_B2
    H_B2 --> H_A2
    H_A2 -->|RX| E_G16

    H_VB --> N_5V
    H_GND --- N_GND

    N_D9 -->|"1kΩ"| LED_A["Ánodo LED"]
    LED_A --> LED["LED"]
    LED --> LED_K["Cátodo"]
    LED_K --- N_GND
```

### Tabla de conexiones

| ESP32 | HW-221 | Arduino Nano | LED / resistencia |
|-------|--------|--------------|---------------------|
| GND   | GND    | GND          | Cátodo (K)          |
| 3V3   | VA     |              |                      |
| G17 (TX, UART2) | A1 |         |                      |
| G16 (RX, UART2) | A2 |         |                      |
|       | VB     | 5V           |                      |
|       | B1     | D2 (RX, SoftwareSerial) |          |
|       | B2     | D3 (TX, SoftwareSerial) |          |
|       |        | D9           | → resistencia 1kΩ → Ánodo (A) |

### Decisiones de diseño

- **Mapeo de canales A/B validado:** G17 (TX ESP32) → A1 → B1 → D2 (RX Nano); D3 (TX Nano) → B2 → A2 → G16 (RX ESP32). El shifter no cruza TX/RX por sí mismo (solo traduce voltaje por canal) — el cruce lógico lo da la asignación D2=RX/D3=TX del lado Nano.
- **D2/D3 con `SoftwareSerial` en el Nano, no D0/D1 (UART hardware):** se dejan D0/D1 libres para conectar el Nano también a la PC y poder ver por monitor serie lo que recibe — mismo criterio que UART0 del ESP32 reservado para el monitor en E01.
- **Baud rate: 9600** para el enlace UART2 (ESP32) ↔ SoftwareSerial (Nano). Se bajó de los 115200 usados en E01 porque `SoftwareSerial` es bit-banging por software y no es confiable a baudios altos. **Pendiente:** UART2 del ESP32 está configurado a 115200 vía Kconfig desde E01 — hay que reconfigurarlo a 9600 (vía `menuconfig`) para que coincida con el Nano, o usar una tercera UART/instancia a 9600 separada de la que se usa con la PC.
- **LED con resistencia limitadora de 1kΩ en serie** (D9 → resistencia → ánodo, cátodo → GND) — corrige el problema visto en E02 de conectar un LED directo a un pin sin resistencia. Con 5V y una caída típica de LED (~2V), da ~3mA: corriente baja pero segura, LED va a verse tenue.

## Próximos pasos

- [ ] Cablear el circuito según la tabla de conexiones
- [x] Revisar en el datasheet del TXS0108E el pin OE (output enable) y confirmar cómo debe quedar habilitado en el módulo HW-221
- [ ] Programar el Arduino Nano: `SoftwareSerial` en D2(RX)/D3(TX) a 9600 baudios, control de LED en D9 según comandos "ON"/"OFF"
- [ ] Adaptar el código del ESP32 (UART2) para retransmitir lo recibido por USB/UART0 hacia el Arduino, y viceversa
- [ ] Validar comunicación end-to-end: PC → ESP32 → HW-221 → Arduino → LED
