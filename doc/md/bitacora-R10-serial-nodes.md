# Bitácora [R10] — Nodos `simple_serial_transmitter` y `simple_serial_receiver` (Python)

_Inicio: 2026-09-23_

## Contexto

Implementar dos nodos ROS2 en Python que hagan de puente entre un topic y el puerto serie, basados en el ejemplo del curso Udemy "Robotics and ROS 2 - Learn by Doing: Manipulators":

```
/home/ezequiel/cursos/Robotics-and-ROS2-Manipulators/Robotics-and-ROS-2-Learn-by-Doing-Manipulators/Section9_Build/arduinobot_ws/src/arduinobot_firmware
```

Archivos de referencia:
- `arduinobot_firmware/simple_serial_transmitter.py`
- `arduinobot_firmware/simple_serial_receiver.py`

(El curso también tiene versiones C++ en `src/` y sketches `.ino` en `firmware/`; en esta tarea solo se hace la versión Python.)

## Cómo funcionan los nodos del curso

| Nodo | Entrada | Salida | Mecanismo |
|------|---------|--------|-----------|
| `simple_serial_transmitter` | topic `serial_transmitter` (`std_msgs/String`) | puerto serie | subscriber → `serial.write()` |
| `simple_serial_receiver` | puerto serie | topic `serial_receiver` (`std_msgs/String`) | timer (10 ms) → `serial.readline()` → publish |

Ambos leen los parámetros `port` (default `/dev/ttyUSB0`) y `baudrate` (default `115200`).

### ¿Por qué el receiver usa un timer y no un callback?

El executor de ROS2 solo despierta callbacks por entidades que conoce (subscriptions, timers, services). El puerto serie de `pyserial` no es una de ellas: ROS no se entera de que llegaron bytes. Por eso se hace **polling** con un timer.

Alternativa: un thread dedicado bloqueado en `readline()` que publica al recibir. Menor latencia, pero agrega manejo de concurrencia y cierre ordenado del thread. Para esta tarea alcanza con el timer.

### Problema: `readline()` bloquea el executor

El puerto se abre con `timeout=0.1` (100 ms) y el timer corre cada 10 ms. Si no llega nada, `readline()` se queda bloqueado hasta 100 ms esperando un `\n`, con el executor trabado dentro del callback. Resultado: el timer corre de hecho a ~10 Hz en vez de 100 Hz, y ningún otro callback del nodo puede ejecutarse mientras tanto.

### Bugs del `timerCallback` del curso

```python
data = self.arduino_.readline()
try:
    data.decode("utf-8")
except:
    return
msg.data = str(data)
self.pub_.publish(msg)
```

1. **Publica aunque no haya llegado nada:** con timeout, `readline()` devuelve `b''` y se publica igual → mensajes basura cada ~100 ms.
2. **`str(data)` no decodifica:** devuelve la representación del objeto `bytes`. Se publica el texto literal `b''` o `b'hola\n'`. El resultado de `decode()` se descarta; solo se usa como validación.
3. **`except:` pelado:** también atrapa `KeyboardInterrupt` y errores propios, escondiendo bugs.

Corrección acordada:
- Si `data` viene vacío → `return`.
- Guardar el resultado de `data.decode("utf-8")` en una variable dentro del `try` y publicar esa variable.
- Capturar `UnicodeDecodeError` específicamente.
- `.strip()` para sacar el `\n` / `\r\n` final.

## Decisión: paquete nuevo `robotic_arm_firmware`

Los nodos van en un paquete nuevo, separado de `robotic_arm_controller` / `robotic_arm_remote`, porque son la **capa de hardware**. Esa separación es la que permite más adelante cambiar la comunicación directa por la variante con ESP32 (ver `[E07]`) sin tocar el resto. También es el lugar natural para el plugin `ros2_control` de `[R09]` (C++).

Build type: **`ament_cmake`** (igual que el resto del workspace), para poder tener nodos Python y C++ en el mismo paquete.

```bash
cd robotic_arm_ws/src
ros2 pkg create --build-type ament_cmake robotic_arm_firmware
```

### Qué hace falta para nodos Python en un paquete `ament_cmake`

`CMakeLists.txt` (referencia: `robotic_arm_controller/CMakeLists.txt`):
- `find_package(ament_cmake_python REQUIRED)`
- `ament_python_install_package(${PROJECT_NAME})` — requiere la carpeta `robotic_arm_firmware/` con `__init__.py`.
- `install(PROGRAMS ... DESTINATION lib/${PROJECT_NAME})`

`package.xml`:
- `<buildtool_depend>ament_cmake_python</buildtool_depend>`
- `<exec_depend>` para `rclpy`, `std_msgs` y **`python3-serial`** (clave rosdep de `pyserial`; sin ella, en otra máquina el nodo falla con `ModuleNotFoundError: serial`).

### ¿Para qué sirve `__init__.py`?

Marca una carpeta como **paquete** de Python (una carpeta de módulos importable). Usos:

1. **Hacerla importable:** con `robotic_arm_firmware/__init__.py` presente, funciona `from robotic_arm_firmware import algo`.
2. **Código de inicialización:** lo que contenga se ejecuta una vez, la primera vez que se importa el paquete. Normalmente queda vacío.
3. **Definir la API pública:** por ejemplo, `from .serial_utils import open_port` dentro de `__init__.py` permite `from robotic_arm_firmware import open_port` sin conocer el archivo interno.

Matiz: desde Python 3.3 existen los *namespace packages* (carpetas sin `__init__.py` que igual se pueden importar), pero muchas herramientas de empaquetado lo exigen. `ament_python_install_package` es una de ellas: sin `__init__.py` el build falla. Por eso en ROS2 siempre se crea, aunque esté vacío.

## Troubleshooting: build roto después de actualizar ROS con apt

Al correr `colcon build` después de crear el paquete, falló **`robotic_arm_msgs`** (paquete ya existente, no el nuevo):

```
gmake[2]: *** No rule to make target '/opt/ros/jazzy/lib/libfastcdr.so.2.2.7',
needed by 'librobotic_arm_msgs__rosidl_typesupport_fastrtps_c.so'.  Stop.
```

`robotic_arm_firmware` y `robotic_arm_controller` figuraron como *aborted* solo porque colcon cortó al fallar otro paquete.

**Causa:** un `apt upgrade` actualizó Fast CDR de 2.2.7 a 2.2.8. En `/opt/ros/jazzy/lib/` quedó `libfastcdr.so.2.2.8` y la 2.2.7 fue eliminada, pero la configuración de CMake cacheada en `build/robotic_arm_msgs/` (`build.make`, `link.txt`) conservaba la ruta absoluta a la versión vieja.

**Solución:** borrar el build de ese paquete para forzar una reconfiguración de CMake (son artefactos regenerables):

```bash
cd robotic_arm_ws
rm -rf build/robotic_arm_msgs install/robotic_arm_msgs
colcon build
```

**Regla general:** después de actualizar paquetes de ROS con apt, si aparece un error de `No rule to make target '/opt/ros/...'` con una versión de librería que ya no existe, es caché de CMake desactualizado. Se arregla borrando `build/<paquete>` e `install/<paquete>` del paquete afectado (o todo `build/ install/ log/` si son varios).

## Implementación

### `CMakeLists.txt` y `package.xml`

- `install(PROGRAMS ...)` sin `RENAME`: los nodos se ejecutan con `.py` (`ros2 run robotic_arm_firmware simple_serial_receiver.py`). Decisión consciente.
- `package.xml` final: `buildtool_depend` `ament_cmake` + `ament_cmake_python`; `depend` `rclpy` + `std_msgs`; `exec_depend` `python3-serial`.
- Se descartaron `rclcpp`, `libserial-dev` y el chequeo `pkg_check_modules(libserial)`: son dependencias de **C++**, ningún archivo del paquete las usa. Vuelven cuando se haga el plugin de `[R09]`.

Qué instala cada función (verificado en `install/robotic_arm_firmware/`):
- `ament_python_install_package` → `lib/python3.12/site-packages/robotic_arm_firmware/` (módulo importable con `import`).
- `install(PROGRAMS)` → `lib/robotic_arm_firmware/` (ejecutables, donde busca `ros2 run`). Requiere el shebang `#!/usr/bin/env python3`.

### `pyserial`: tres nombres para la misma librería

| Dónde | Nombre |
|---|---|
| pip / PyPI | `pyserial` |
| Código | `import serial` |
| apt / rosdep (`package.xml`) | `python3-serial` |

Trampa: en PyPI existe otro paquete llamado `serial` (serialización) que pisa el módulo; nunca `pip install serial`.

### Receiver

```python
def timerCallback(self):
    if rclpy.ok() and self.serial_port.is_open:
        try:
            data = self.serial_port.readline().strip()
            if not data:
                return
            msg = String()
            msg.data = data.decode("utf-8")
            self.pub_.publish(msg)
        except UnicodeDecodeError as e:
            self.get_logger().warning(f"Invalid UTF-8 on serial: {e}")
```

Logging en rclpy: `self.get_logger().<nivel>(...)` con niveles `debug`, `info`, `warning`, `error`, `fatal`. Para evitar spam: `throttle_duration_sec=1.0`.

### Transmitter: framing con `\n`

El receiver (`readline()`) y el firmware cortan mensajes por `\n`, pero el transmitter original mandaba `msg.data` tal cual → el eco nunca salía. El `\n` lo agrega el **transmitter**: el framing es un detalle del enlace serie; quien publica en el topic no debería conocerlo.

```python
data_to_send = (msg.data + "\n").encode("utf-8")
self.serial_port.write(data_to_send)
```

Error evitado: `msg.data.encode("utf-8") + "\n"` → `TypeError: can't concat str to bytes`. Alternativa válida: `msg.data.encode("utf-8") + b"\n"`.

El `\n` es una convención del protocolo, no una obligación. Opciones de framing:
- **Delimitador de texto (`\n`)**: simple y legible; el delimitador no puede aparecer en los datos.
- **Marcadores STX/ETX (+ checksum)**: como en `[E05]`; permite resincronizar y detectar corrupción.
- **Largo en el encabezado**: como el protocolo de los servos (`[E04]`); necesario con payload binario arbitrario.

Para otro delimitador en Python: `read_until(expected=b'\x03')`. Regla: transmitter, receiver y firmware tienen que usar la misma.

## Prueba con Arduino Nano

### ¿Por qué no con el monitor serie?

Un puerto serie lo abre **un solo proceso**. Si el nodo abre `/dev/ttyUSB0`, el Serial Monitor / `idf.py monitor` no puede usarlo (y si se fuerza, se reparten los bytes). Los topics pasan a ser la ventana: `ros2 topic pub` = teclado, `ros2 topic echo` = pantalla.

### ¿Por qué Nano y no ESP32?

Se evaluó usar el `uart_echo` del ESP32 sobre UART0 (USB). Es viable y más realista, pero:
- los logs de ESP-IDF salen por UART0 y el receiver los publicaría como datos (hay que bajar el log level o mover la consola en `menuconfig`),
- los mensajes del bootloader del ROM no se pueden desactivar desde menuconfig,
- el auto-reset por DTR/RTS reinicia la placa al abrir el puerto,
- hay que reconfigurar el ejemplo (UART 0 en vez de UART2 17/16).

Se eligió el Nano: su `Serial` **es** el USB y no hay ruido de logs → prueba más limpia.

### Sketch de eco

`firmware/arduino-uart-echo/uart-echo/uart-echo.ino`: acumula caracteres hasta `\n`, devuelve la línea con `Serial.println()` (que manda `\r\n`, limpiado por `.strip()` en el receiver) y además prende/apaga el LED del pin 13 con `ON`/`OFF`. Baudrate **115200**, igual al default de los nodos.

### Procedimiento

```bash
# T1
ros2 run robotic_arm_firmware simple_serial_receiver.py
# T2
ros2 run robotic_arm_firmware simple_serial_transmitter.py
# T3
ros2 topic echo /serial_receiver
# T4 (--once publica un mensaje y termina)
ros2 topic pub --once /serial_transmitter std_msgs/msg/String "{data: 'ON'}"
ros2 topic pub --once /serial_transmitter std_msgs/msg/String "{data: 'OFF'}"
```

Notas:
- El Nano (chip FTDI FT232R) aparece como `/dev/ttyUSB0`, el default del parámetro `port`. Si no, `--ros-args -p port:=/dev/ttyXXX`.
- El Nano se resetea al abrirse el puerto (DTR): esperar ~2 s antes de publicar.
- Para subir un sketch hay que **frenar los nodos** primero: ocupan el mismo puerto que usa el IDE para programar. (Primer intento sin respuesta: el sketch nunca se había subido.)

### Resultado

```
❯ ros2 topic echo /serial_receiver
data: 'ON'
---
data: 'OFF'
---
```

Eco correcto de punta a punta y LED respondiendo a `ON`/`OFF`. Los datos llegan decodificados (`'ON'`, no `b'ON'`), lo que confirma el arreglo del bug de `str(data)`.

_Completada: 2026-09-23_
