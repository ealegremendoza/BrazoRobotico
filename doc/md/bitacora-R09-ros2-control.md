# Bitácora [R09] — Plugin `ros2_control` en C++ para el brazo real

_Inicio: 2026-09-25_

## Contexto

Implementar en C++ el plugin `ros2_control` (`hardware_interface::SystemInterface`) para el brazo real. La comunicación es directa por puerto serie, reusando la lógica de `STServo_Python`, y la interfaz tiene que quedar reemplazable más adelante por una variante con ESP32 (ver `[E07]`).

Depende de `[R05]` (MoveIt2). Se apoya en lo visto en:
- `[R10]`: nodos puente topic ↔ puerto serie.
- `[R11]`: ciclo de vida de nodos (estados y transiciones).

## Referencia

Curso Udemy "Robotics and ROS 2 - Learn by Doing: Manipulators":

```
/home/ezequiel/cursos/Robotics-and-ROS2-Manipulators/Robotics-and-ROS-2-Learn-by-Doing-Manipulators/Section9_Build/arduinobot_ws/src/arduinobot_controller
```

Archivos de referencia:
- `include/arduinobot_controller/arduinobot_interface.hpp`
- `src/arduinobot_interface.cpp`
- `arduinobot_controller.xml` (descripción del plugin para `pluginlib`)

## Arquitectura

```
PC (ROS2, plugin ros2_control) <-Serial-> ESP32 <-UART-> Driver / Servos STS3215
```

El ESP32, además de manejar los servos, maneja un **display** (mensajes prearmados, seleccionados por ID) y **botones** (eventos que se informan a la PC).

### Túnel vs protocolo propio

Se descartó el ESP32 como túnel puro (reenviar a los servos lo que llega de la PC). Cuando por el mismo enlace viajan dos cosas distintas (servos y display/botones), el receptor necesita distinguirlas: hace falta un framing con identificador de canal, o sea, un protocolo. Mejor diseñarlo desde el principio.

Esto responde `[E07]`: con display y botones, conviene desacoplar la gestión del driver al ESP32.

## Protocolo PC ↔ ESP32 (decidido)

Basado en el protocolo de `[E05]` (`firmware/arduino-uart-test/send_cmd.py`).

```
STX | LEN | CID | FS | payload | ETX | LRC
```

| Campo | Formato | Descripción |
|---|---|---|
| `STX` | `0x02` | Inicio de trama |
| `LEN` | 4 dígitos ASCII decimal | Largo de **CID + FS + payload + ETX + LRC** (sin contarse a sí mismo ni al STX) |
| `CID` | 1 char ASCII | Identificador de comando |
| `FS` | `0x1C` | Separa el CID del payload |
| `payload` | ASCII | Depende del CID |
| `ETX` | `0x03` | Fin de trama (verificación extra para resincronizar) |
| `LRC` | 1 byte | XOR de **LEN + CID + FS + payload + ETX** (todo menos STX) |

Ejemplo: `CID=D` con payload `"07"` → `LEN = "0006"` (`D` + FS + `07` + ETX + LRC).

Largo total de la trama desde STX = `1 + 4 + LEN`. Se incluyen ETX y LRC en LEN para que el parser calcule el fin de trama sin sumar la cola fija.

**FS después del CID:** el CID es de 1 char en posición fija, así que el FS no es estrictamente necesario (el parser podría leerlo por posición). Se agrega igual para poder partir toda la trama con un solo `split(FS)` y dejar abierta la puerta a CIDs de más de un char. Costo: 1 byte por trama.

### CID

| CID | Significado | Dirección | Payload |
|---|---|---|---|
| `M` | Motores / movimiento | PC → ESP32 | Posición objetivo de cada joint: `j1 FS j2 FS ... FS j6` |
| `M` | Motores / movimiento | ESP32 → PC | Posición actual de cada joint, mismo formato |
| `D` | Display | PC → ESP32 | ID de mensaje de una tabla prearmada en el ESP32 |
| `E` | Eventos / errores | ESP32 → PC | ID de evento de botón o de error |

`FS` = `0x1C` (mismo separador que en `[E05]`).

### Unidades de `M`: miliradianes

Se usan **unidades físicas** y no counts, para que el código ROS2 no dependa del servo usado. Los counts son específicos del STS3215 (4096 por vuelta, cero en 2048); el ESP32 es la única capa que conoce el hardware y hace la conversión mrad ↔ counts.

- Formato: **ancho fijo con signo**, 5 chars (`+1571`, `-0785`). Rango ±π → −3142 … +3142.
- Resolución: 1 mrad = 0.057°, más fino que 1 count del STS3215 (0.088°, ver `STServo_Python/calibrate-arm.py`). No se pierde precisión.

Largo de trama `M` (6 joints):

| Unidad | Chars por joint | Trama | Tiempo a 115200 (8N1, ~87 µs/byte) |
|---|---|---|---|
| Counts | 4 | 38 bytes | ~3.3 ms |
| Miliradianes | 5 | 44 bytes | ~3.8 ms |

La diferencia (~0.5 ms) es despreciable frente a un ciclo de 20 ms (loop a 50 Hz).

## Feedback (decidido)

### Modelo request/response

La PC manda `M` (objetivo); el ESP32 escribe a los servos, lee sus posiciones y responde con `M` (posición actual).

- **Un solo maestro**: la PC marca el ritmo. Con streaming periódico del ESP32, si la PC se atrasa se acumulan mensajes viejos en la cola (como en `[R11]`).
- **Bus de servos half-duplex**: el ESP32 no puede escribir y leer a la vez; request/response ordena solo: escribir → leer → responder.
- **Detección de fallas**: si no llega la respuesta, algo se cortó.

`read()` de `ros2_control` no puede bloquear: `write()` manda `M` en el ciclo N y `read()` del ciclo N+1 lee sin bloquear la respuesta ya llegada. Latencia: un ciclo (20 ms a 50 Hz).

Los eventos `E` son asíncronos: el ESP32 los manda cuando ocurren. El parser de la PC tiene que aceptar cualquier CID en cualquier momento.

## Diseño de `write()` (decidido)

### Sin early return: mandar `M` en todos los ciclos

El `write()` del curso no manda nada si los comandos no cambiaron. Con request/response eso rompe el feedback: sin `M` no hay respuesta, y `read()` se queda sin posición actualizada mientras el brazo está quieto (aunque alguien lo empuje o el gripper choque). Además, "no hay respuesta" deja de servir para detectar una desconexión.

Costo de mandar siempre: 44 bytes × 50 Hz = 2200 B/s, menos del 20 % de lo que da 115200 baudios (~11520 B/s). Mandar el mismo objetivo repetido no afecta al servo.

Al sacarlo, `prev_position_commands_` deja de tener uso.

### Sin offsets ni inversiones en la PC

Se eliminan las cuentas del curso (`(pos + π/2) * 180/π`, `180 - ...` en el shoulder), que compensaban el montaje de los servos del arduinobot. La PC manda el ángulo del joint en mrad tal cual; toda la calibración de hardware vive en el ESP32.

### Conversión y formato de cada joint

1. `rad * 1000` → `std::lround` (redondeo al más cercano; `static_cast<int>` trunca hacia cero, con error de hasta 1 mrad y sesgado).
2. `std::clamp` a ±3142, con `RCLCPP_WARN` si recorta (un valor fuera de ±π indica un bug más arriba). Sin clamp, `+12000` daría 6 chars y rompería la trama.
3. `snprintf` con `%+05ld`: `+` fuerza el signo, `0` rellena con ceros, `5` es el ancho **incluyendo el signo** (`-785` → `-0785`, `1571` → `+1571`). `%ld` porque `lround` devuelve `long`.

Pérdida por redondeo: máx 0.5 mrad ≈ 0.029°, unas 3 veces menor que la resolución del STS3215 (2π/4096 ≈ 1.53 mrad ≈ 0.088°). El límite es el servo, no la trama.

Joints separados por `FS` (`0x1C`).

## Diseño de `read()` (decidido)

### Lectura sin bloqueo

El puerto entrega bytes, no tramas: puede llegar media trama y el resto en el ciclo siguiente. Los bytes se acumulan en `rx_buffer_`, **miembro de la clase** (no global en el namespace anónimo): es estado de cada puerto, no se comparte entre instancias y se limpia en `on_activate`.

Trampa de LibSerial (`/usr/include/libserial/SerialPort.h`): en `Read(str, n, msTimeout)`, **`msTimeout = 0` bloquea** hasta recibir los `n` bytes. Si el ESP32 no responde, se congela el loop de `ros2_control`. Solución:

1. `GetNumberOfBytesAvailable()` → `n`. Si `n == 0`, no llamar a `Read()`.
2. `Read()` de exactamente `n` bytes (ya están en el buffer del SO, vuelve al instante) a un string local.
3. Agregar ese string a `rx_buffer_`.

### Parseo y resincronización

Hay dos formas de no tener trama válida: **inválida** (se descarta) e **incompleta** (se espera; descartarla rompería una trama buena).

1. Descartar lo que haya antes del primer STX. Sin STX → vaciar el buffer.
2. No llegaron los 4 bytes de LEN → salir y esperar al próximo `read()`.
3. LEN no son 4 dígitos ASCII o está fuera de rango (mín. 4 = CID + FS + ETX + LRC; máx. 64) → descartar 1 byte, volver a 1.
4. No está la trama completa (`1 + 4 + LEN` bytes) → salir y esperar.
5. ETX no está en su posición o el LRC no coincide (XOR de LEN hasta ETX, sin STX ni el LRC) → descartar 1 byte, volver a 1.
6. Trama válida → procesarla, sacarla del buffer, volver a 1.

- **Descartar 1 byte y no la trama entera**: el LRC puede valer `0x02` y pasar por STX. Si se descarta todo, se pierde la trama real que empezaba adentro.
- **Tope de LEN = 64**: sin él, un LEN corrupto (`9039`) haría esperar miles de bytes y las tramas buenas quedarían atrapadas. No es parte del formato (LEN es simétrico en ambas direcciones) sino un límite de cordura del parser. La trama más larga esperada es una `M` de 6 joints (LEN = 39); 64 deja margen para más joints o campos.
- **Loop hasta vaciar**: puede haber varias tramas por ciclo (un `E` + una `M`, o dos `M` si el loop se atrasó). Procesar una por llamada acumula latencia (como la cola de `[R11]`). Si llegan varias `M`, gana la última.

### Payload de `M` (respuesta)

La PC no ve el formato de los servos (binario Feetech, counts): el ESP32 convierte counts → mrad y responde con el mismo formato que la `M` de `write()`.

- Cantidad de campos = `position_states_.size()`.
- Cada campo: 5 chars, signo `+`/`-` y 4 dígitos.
- Si un campo falla, se descarta la trama **entera** y `position_states_` conserva los valores anteriores (nunca actualizar a medias).
- Conversión: `mrad / 1000.0` (con `1000` entero, `1571 / 1000` daría `1`).

## Pendientes

- [ ] Mover la apertura del puerto serie de `on_activate` a `on_configure` (ver `[R11]`). Por ahora se sigue la estructura del curso.
- [x] `write()`: reemplazar el protocolo de texto del curso (`b090,s090,...`) por tramas `M`.
- [x] `read()`: usar la posición real informada por los servos en vez de lazo abierto.
