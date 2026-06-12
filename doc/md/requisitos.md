# Requisitos del proyecto

## Titulo del proyecto
Brazo robótico programable mediante ROS para automatización de tareas repetitivas basado en grabación y reproducción de trayectorias.

## Modos de operación
- Modo lider (leader): El brazo robótico graba trayectorias.
- Modo seguidor (follower): El brazo robótico repite una trayectoria guardada. 

# Requerimientos del Producto

## RP-01 - Manipulación de objetos
El brazo robótico deberá ser capaz de agarrar, sostener, trasladar y liberar objetos livianos entre posiciones predefinidas.

## RP-02 - Programación por demostración
El sistema deberá permitir enseñar tareas mediante el movimiento manual del brazo por parte del usuario.

## RP-03 - Grabación de trayectorias
El sistema deberá registrar las posiciones de todas las articulaciones durante una sesión de enseñanza.

## RP-04 - Almacenamiento de trayectorias
El sistema deberá almacenar trayectorias para su utilización posterior.

## RP-05 - Reproducción automática
El sistema deberá reproducir automáticamente las trayectorias almacenadas.

## RP-06 - Selección de tareas
El sistema deberá permitir seleccionar una trayectoria previamente almacenada para su ejecución.

## RP-07 - Eliminación de tareas
El sistema deberá permitir borrar trayectorias almacenadas.

## RP-08 - Interfaz de usuario local
El sistema deberá disponer de una interfaz local basada en botones y display para su operación.

## RP-09 - Visualización de estado
El sistema deberá informar al usuario el estado actual de operación.

## RP-10 - Control de servomotores
El sistema deberá controlar individualmente cada servomotor del brazo robótico.

## RP-11 - Lectura de posición
El sistema deberá conocer la posición actual de cada articulación.

## RP-12 - Calibración
El sistema deberá permitir realizar procedimientos de inicialización y calibración.

## RP-13 - Posición Home
El sistema deberá contar con una posición de referencia para el inicio de las operaciones.

## RP-14 - Almacenamiento persistente
Las trayectorias deberán mantenerse almacenadas aun después de apagar el equipo.

## RP-15 - Arranque automático
El sistema deberá iniciar automáticamente los servicios necesarios al encenderse.

## RP-16 - Operación autónoma
El brazo robótico deberá poder funcionar sin necesidad de una computadora externa durante la operación normal.

## RP-17 - Seguridad operacional
El sistema deberá permitir detener inmediatamente la ejecución de una tarea ante una condición anormal.

## RP-18 - Precisión de movimiento
El sistema deberá reproducir trayectorias con una precisión suficiente para tareas de pick-and-place de objetos livianos.

## RP-19 - Repetibilidad
El sistema deberá ejecutar una misma trayectoria múltiples veces obteniendo resultados consistentes.

## RP-20 - Arquitectura ROS 2
El sistema deberá utilizar ROS 2 como plataforma de software para la implementación de la lógica de control y movimiento.

## RP-21 - Simulación
El sistema deberá disponer de un modelo para simulación en Gazebo que permita validar funcionalidades antes de su implementación física.

## RP-22 - Visión artificial (posibilidad de expansion)
El sistema inicialmente no deberá incluir visión artificial, procesamiento de imágenes ni algoritmos de inteligencia artificial. Sin embargo, es deseable que el hardware posibilite la futura expansion a esas funcionalidades.

## PR-23 - Alimentación
El sistema deberá poder alimentarse, mediante una funte, a una red domestica de 220VAC@50Hz

## PR-24 - Grado de protección
El equipo será de uso interior. El conjunto general se considera IP20, mientras que el gabinete de electrónica se diseñará con protección IP40, pudiendo elevarse a IP54 en la zona de interfaz de usuario si se requiere mayor robustez.

## RP-25 - Telemetría
El sistema inicialmente no contará con cámara de asistencia visual y aplicación de escritorio que facilite la teleoperación. Sin embargo, es deseable que el hardware posibilite la futura expansion a esas funcionalidades.

## RP-26 - Precisión
Como prueba final de precisión, el sistema debe ser capaz de colocar un anillo en un dedo (mano impresa en 3D para la prueba).

---

# Requerimientos del Usuario
## RU-01 - Encendido del sistema
El usuario debe poder encender y apagar el brazo robótico de forma sencilla y segura.

## RU-02 - Movimiento manual
El usuario debe poder mover manualmente el brazo robótico para enseñarle una tarea sin necesidad de programar.

## RU-03 - Grabación de trayectorias
El usuario debe poder grabar una secuencia de movimientos realizada manualmente.

## RU-04 - Reproducción de trayectorias
El usuario debe poder reproducir una trayectoria previamente grabada.

## RU-05 - Eliminación de trayectorias
El usuario debe poder borrar trayectorias almacenadas que ya no sean necesarias.

## RU-06 - Selección de tareas
El usuario debe poder seleccionar cuál de las trayectorias almacenadas desea ejecutar.

## RU-07 - Estado del sistema
El usuario debe poder conocer el estado actual del brazo robótico (listo, grabando, reproduciendo, error, etc.).

## RU-08 - Manipulación de objetos
El usuario debe poder utilizar el brazo para agarrar, mover y soltar objetos livianos.

## RU-09 - Repetibilidad
El usuario debe poder repetir una misma tarea múltiples veces obteniendo resultados consistentes.

## RU-10 - Facilidad de uso
El usuario debe poder operar el sistema sin conocimientos de programación ni de robótica.

## RU-11 - Seguridad de operación
El usuario debe poder detener la ejecución de una tarea ante una situación inesperada.

## RU-12 - Conservación de tareas
El usuario debe poder conservar las trayectorias grabadas luego de apagar y volver a encender el equipo.

## RU-13 - Tiempo de preparación reducido
El usuario debe poder enseñar una nueva tarea en pocos minutos.

## RU-14 - Interfaz intuitiva
El usuario debe poder utilizar la botonera y el display sin requerir capacitación especializada.

## RU-15 - Automatización de tareas repetitivas
El usuario debe poder automatizar tareas repetitivas de traslado de objetos entre posiciones predefinidas.

# Requisitos funcionales

- El controlador del brazo robótico debe ser capaz de:
    - enviar señales de control a cada servomotor.
    - conocer el estado de cada servomotor en cada instante.

- La computadora integrada debe ser capaz de:
    - enviar comandos de movimiento al controlador del brazo robótico.
    - calibrar el brazo robótico.
    - guardar trayectorias grabadas por el usuario.
    - autoiniciar el sistema de control del robot cuando se enciende.

## Requisitos técnicos
### Brazo robótico
- Se realizará el brazo robótico que se muestra en el proyecto SO-ARM100. Ver referencias.md en el caso de que se quiera profundizar sobre el tema.
- El proyecto SO-ARM100 contempla 2 brazos robóticos, uno lider y otro seguidor. Sin embargo, se construirá un solo brazo de modo que sea lider cuando se ponga en modo grabación el brazo y en modo seguidor cuando se ejecute la trayectoria guardada.
- Las piezas del brazo robótico se imprimiran con impresora 3D. Se prototipará con PLA, pasando a PETG cuando se cuente con el diseño final.
- Utilizará 6 servomotores STS3215. Esto le proporcionará 5 grados de libertád más el grip. Los mismos se comprarán.
- Contará con una placa driver para manejo de servomotores STS3215. La misma se comprará.
- El sistema utilizará una fuente principal switching de 12V y 20A mínimo.
    - Display 5V 0.2A max
    - 6 Servomotores 7.4V 2.5A max (15A x 6 servos)
    - Jetson Nano 12V 5A max
    

### Controlador de brazo
- A definir si no se va a usar un SP32 o directamente el Jetson Nano Orin.

### Computadora integrada
- Jetson Nano Orin. Permitirá expandir el producto a IA y procesamiento de imágenes.

### UI
- Display LCD 20X4 y Botonera.

## Referencias
Ver referencias.md
    
## Materiales
Ver materiales.md
