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

## Próximos pasos

- Recompilar el workspace y verificar que `robotic_arm_firmware` compila.
- Completar `CMakeLists.txt` y `package.xml` con lo de arriba.
- Implementar ambos nodos con las correcciones.
- Probar contra el ESP32 / Arduino por puerto serie.
