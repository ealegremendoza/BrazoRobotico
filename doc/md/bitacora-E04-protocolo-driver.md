# Bitácora — E04: Cómo comandar el driver de motores vía TX-RX

## Objetivo
Averiguar qué protocolo hay que hablar por TX-RX para comandar el driver de servos (Waveshare Bus Servo Adapter (A)), de cara al firmware puente ESP32 <-> driver.

## Contexto
- E03 ya confirmó que ESP32 y el conector de host del adaptador son compatibles en 3.3V, sin conversor de nivel.
- El adaptador convierte USB a UART mediante el chip **CH343P** (visible en el esquemático `doc/datasheets/Bus Servo Adapter (A)-Sch.pdf`).
- `STServo_Python` ya controla los servos hoy por USB, a través de este mismo adaptador.

## Hallazgo

- Fuente: [components101 — CH343 pinout/datasheet](https://components101.com/ics/ch343-high-speed-usb-to-serial-chip-pinout-datasheet): *"The CH343 is a USB bus converter chip, which converts USB to a high-speed serial UART interface."*
- Esto confirma que el CH343P es un **puente USB-UART transparente** (misma familia de función que un FTDI FT232 o un CP210x), no un traductor de protocolo. No interpreta ni reformatea los bytes que pasan por él.
- Coincide con lo visto en el esquemático: el CH343P expone pines típicos de un puente serie genérico (TXD/RXD/RI/CTS/DSR/DCD/DTR), sin ninguna lógica de protocolo de servos.

**Consecuencia para el diseño:** los bytes que hoy viajan por USB desde `STServo_Python` hacia los servos son *exactamente* los mismos bytes que van a viajar por TX/RX cuando el host sea el ESP32 en vez de la PC. El protocolo a implementar en el ESP32 es el mismo que ya usa el SDK de Python — no hay que inventar ni traducir nada nuevo en el adaptador.

## Ubicación del SDK en el repo

- `servo_control_ui.py` importa `sms_sts` desde `scservo_sdk` (línea ~63-69).
- El paquete `scservo_sdk` no vive como archivo propio del repo: está descomprimido dentro del virtualenv, en `STServo_Python/stservo-env/scservo_sdk/` (no se ve en el explorador de VS Code porque `stservo-env` suele quedar excluido de la búsqueda/gitignored). Nota: en la raíz del venv también aparecen carpetas sueltas `STservo_sdk`, `sms_sts`, `scscl` — probablemente restos de cómo se descomprimió el SDK ahí en vez de instalarlo con pip; no afectan el funcionamiento actual.
- Archivos relevantes dentro de `scservo_sdk/`: `protocol_packet_handler.py` (armado/parseo de tramas — el que se documenta acá), `port_handler.py` (capa serie), `sms_sts.py` (comandos de alto nivel para servos STS/SMS), `scservo_def.py` (constantes: instrucciones, IDs, bits de error).

## Formato de trama (protocolo estilo Feetech/Dynamixel)

Paquete binario simple, sin capa extra. Definido en `protocol_packet_handler.py` y `scservo_def.py`.

| Byte(s) | Campo | Descripción |
|---|---|---|
| 0-1 | `0xFF 0xFF` | Header fijo |
| 2 | ID | ID del servo destino (0-253). `0xFE` (254, `BROADCAST_ID`) = todos |
| 3 | Length | Bytes restantes: instrucción + parámetros + checksum |
| 4 | Instrucción | Ver tabla de instrucciones |
| 5..N-1 | Parámetros | Datos según instrucción (p. ej. dirección de registro + valor) |
| N | Checksum | `~(ID + Length + Instrucción + Parámetros) & 0xFF` (complemento a 1 de la suma, bytes 2 a N-1) |

Paquete de respuesta (status packet) del servo: misma estructura, pero el byte 4 es un **byte de error** (bit 0=voltaje, bit 1=ángulo, bit 2=sobretemperatura, bit 3=sobrecorriente, bit 5=sobrecarga; 0 = sin error) en vez de instrucción.

### Instrucciones (`scservo_def.py`)

| Valor | Nombre | Uso |
|---|---|---|
| 1 | `INST_PING` | "¿Estás ahí?" — responde con el modelo |
| 2 | `INST_READ` | Leer un registro (ej. posición actual) |
| 3 | `INST_WRITE` | Escribir un registro (ej. posición objetivo) — la más usada |
| 4 | `INST_REG_WRITE` | Como WRITE pero diferido, se ejecuta al llegar un ACTION |
| 5 | `INST_ACTION` | Dispara los REG_WRITE pendientes (sincroniza varios servos) |
| 130 (0x82) | `INST_SYNC_READ` | Leer el mismo registro de varios servos en un solo paquete |
| 131 (0x83) | `INST_SYNC_WRITE` | Escribir valores distintos a varios servos en un solo paquete (lo que usa la UI para mover el brazo completo) |

### Registros usados por `WritePosEx`/`SyncWritePosEx` (`sms_sts.py`)

Bloque de 7 registros contiguos en SRAM, arrancando en `SMS_STS_ACC = 41`:

| Offset desde ACC | Dirección | Registro | Contenido |
|---|---|---|---|
| +0 | 41 (0x29) | ACC | Aceleración |
| +1 | 42 | GOAL_POSITION_L | Posición objetivo, byte bajo |
| +2 | 43 | GOAL_POSITION_H | Posición objetivo, byte alto |
| +3 | 44 | GOAL_TIME_L | Sin usar en este SDK → `0x00` |
| +4 | 45 | GOAL_TIME_H | Sin usar → `0x00` |
| +5 | 46 | GOAL_SPEED_L | Velocidad, byte bajo |
| +6 | 47 | GOAL_SPEED_H | Velocidad, byte alto |

Los valores de 16 bits van en **little-endian** (byte bajo primero) — así arman `scs_lobyte`/`scs_hibyte` en `protocol_packet_handler.py` cuando `scs_end=0` (el caso de `sms_sts`).

## Ejemplo 1 — WRITE simple: servo ID 1 a posición 2048, velocidad 500, acc 50

Valores: `acc=50 (0x32)`, `position=2048 (0x0800)` → posL=`0x00`, posH=`0x08`; `speed=500 (0x01F4)` → speedL=`0xF4`, speedH=`0x01`.

**Hexadecimal del paquete completo:**
```
FF FF 01 0A 03 29 32 00 08 00 00 F4 01 99
```

**Parseado:**

| Bytes (hex) | Campo | Valor |
|---|---|---|
| `FF FF` | Header | fijo |
| `01` | ID | servo 1 |
| `0A` | Length | 10 (INST + ADDR + 7 datos + checksum) |
| `03` | Instrucción | `INST_WRITE` |
| `29` | Parámetro 0 | dirección de inicio = 41 (ACC) |
| `32` | Parámetro 1 | acc = 50 |
| `00 08` | Parámetro 2-3 | posición = 0x0800 = 2048 (little-endian: L=00, H=08) |
| `00 00` | Parámetro 4-5 | goal_time (sin usar) = 0 |
| `F4 01` | Parámetro 6-7 | velocidad = 0x01F4 = 500 (L=F4, H=01) |
| `99` | Checksum | `~(01+0A+03+29+32+00+08+00+00+F4+01) & 0xFF` = `~0x66 & 0xFF` = `0x99` |

El servo responde con un status packet (mismo formato, byte 4 = error 0x00 si todo OK) porque `WritePosEx` usa `writeTxRx` (espera respuesta), a diferencia de `SyncWritePosEx` que es fire-and-forget.

## Ejemplo 2 — SYNC_WRITE: 2 servos en un solo paquete

Servo 1: posición 2048 (0x0800), velocidad 500 (0x01F4), acc 50 (0x32) — igual que arriba.
Servo 2: posición 1024 (0x0400), velocidad 300 (0x012C), acc 30 (0x1E).

**Hexadecimal del paquete completo:**
```
FF FF FE 14 83 29 07 01 32 00 08 00 00 F4 01 02 1E 00 04 00 00 2C 01 B9
```

**Parseado:**

| Bytes (hex) | Campo | Valor |
|---|---|---|
| `FF FF` | Header | fijo |
| `FE` | ID | `BROADCAST_ID` (0xFE) — un solo paquete para todos |
| `14` | Length | 20 (INST + start_addr + data_len + 2×8 bytes de servo + checksum) |
| `83` | Instrucción | `INST_SYNC_WRITE` (131) |
| `29` | start_address | 41 (ACC) — mismo bloque de registros que en WRITE simple |
| `07` | data_length | 7 bytes de datos por servo |
| `01 32 00 08 00 00 F4 01` | Bloque servo 1 | ID=1, acc=50, pos=2048 (00 08), time=0, speed=500 (F4 01) |
| `02 1E 00 04 00 00 2C 01` | Bloque servo 2 | ID=2, acc=30, pos=1024 (00 04), time=0, speed=300 (2C 01) |
| `B9` | Checksum | `~(FE+14+83+29+07+ suma de los 16 bytes de datos) & 0xFF` = `~0x46 & 0xFF` = `0xB9` |

**Nada responde a este paquete** (broadcast → sin status packet, ver `protocol_packet_handler.py:234-236`). Para el brazo completo (6 servos) el paquete se arma igual, solo agregando más bloques de 8 bytes (uno por servo) antes del checksum.

## Conclusión

Protocolo identificado y documentado por completo: header fijo `0xFF 0xFF`, ID, length, instrucción, parámetros, checksum de complemento a 1. Es el mismo protocolo que ya habla `STServo_Python` por USB — el firmware del ESP32 tiene que armar/parsear exactamente estas mismas tramas para reenviarlas por su UART hacia el driver.

## Próximos pasos

- [x] Confirmar si el adaptador traduce protocolo o es un puente transparente — **es transparente** (CH343P)
- [x] Identificar el protocolo exacto que usa `STServo_Python` para hablar con los servos — `scservo_sdk/protocol_packet_handler.py`
- [x] Documentar el formato de trama (header, ID, instrucción, checksum, etc.)
- [x] Revisar el formato de `INST_SYNC_WRITE` en detalle, con ejemplos en hexadecimal parseados byte a byte
