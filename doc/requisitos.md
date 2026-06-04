# Requisitos del proyecto

## Titulo del proyecto
Brazo robótico programable mediante ROS para automatización de tareas repetitivas basado en grabación y reproducción de trayectorias.

## Requisitos funcionales
- El brazo robótico tiene que ser capaz de:
    - agarrar objetos pequeños.
    - levantar objetos ligeros.
    - sostener objetos ligeros.
    - trasladar objetos ligeros de una zona predefinida a otra predefinida.
    
- El usuario debe poder:
    - encender y apagar el brazo robótico de manera segura.
    - mover manualmente el brazo robótico. Esto sería, tomar el brazo robótico de las articulaciones y posicionarlo en en la posición final.
    - guardar trayectorias realizadas con el brazo robótico.
    - borrar trayectorias realizadas con el brazo robótico.
    
- La UI del brazo robótico debe:
    - ser sencilla de manejar.
    - brindar al usuario la posibilidad de encender/apagar el brazo robótico.
    - brindar al usuario la posibilidad de ver trayectorias guadardas previamente.
    - brindar al usuario la posibilidad de grabar las trayectorias.
    - brindar al usuario información sobre el estado del brazo robótico.
    
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


### Controlador de brazo
- A definir

### Computadora integrada
- A definir.
- Tiene que soportar ROS2.

### UI
- A definir
- Deberá contar con display
- Deberá contar con botones

## Referencias
Ver referencias.md
    
## Materiales
Ver materiales.md
