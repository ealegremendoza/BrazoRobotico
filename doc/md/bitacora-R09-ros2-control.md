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
STX | LEN | CID | payload | ETX | LRC
```

| Campo | Formato | Descripción |
|---|---|---|
| `STX` | `0x02` | Inicio de trama |
| `LEN` | 4 dígitos ASCII decimal | Largo de **CID + payload** (sin contarse a sí mismo) |
| `CID` | 1 char ASCII | Identificador de comando |
| `payload` | ASCII | Depende del CID |
| `ETX` | `0x03` | Fin de trama (verificación extra para resincronizar) |
| `LRC` | 1 byte | XOR de **LEN + CID + payload + ETX** (todo menos STX) |

Ejemplo: `CID=D` con payload `"07"` → `LEN = "0003"`.

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
| Counts | 4 | ~37 bytes | ~3.2 ms |
| Miliradianes | 5 | ~43 bytes | ~3.7 ms |

La diferencia (~0.5 ms) es despreciable frente a un ciclo de 20 ms (loop a 50 Hz).

## Feedback (decidido)

### Modelo request/response

La PC manda `M` (objetivo); el ESP32 escribe a los servos, lee sus posiciones y responde con `M` (posición actual).

- **Un solo maestro**: la PC marca el ritmo. Con streaming periódico del ESP32, si la PC se atrasa se acumulan mensajes viejos en la cola (como en `[R11]`).
- **Bus de servos half-duplex**: el ESP32 no puede escribir y leer a la vez; request/response ordena solo: escribir → leer → responder.
- **Detección de fallas**: si no llega la respuesta, algo se cortó.

`read()` de `ros2_control` no puede bloquear: `write()` manda `M` en el ciclo N y `read()` del ciclo N+1 lee sin bloquear la respuesta ya llegada. Latencia: un ciclo (20 ms a 50 Hz).

Los eventos `E` son asíncronos: el ESP32 los manda cuando ocurren. El parser de la PC tiene que aceptar cualquier CID en cualquier momento.

## Pendientes

- [ ] Mover la apertura del puerto serie de `on_activate` a `on_configure` (ver `[R11]`). Por ahora se sigue la estructura del curso.
- [ ] `write()`: reemplazar el protocolo de texto del curso (`b090,s090,...`) por tramas `M`.
- [ ] `read()`: usar la posición real informada por los servos en vez de lazo abierto.
