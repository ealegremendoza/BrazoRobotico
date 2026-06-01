# Titulo del proyecto
Brazo robótico programable mediante ROS para automatización de tareas repetitivas basado en grabación y reproducción de trayectorias.

## Propuesta
El proyecto consiste en el desarrollo de un brazo robótico orientado a la
automatización de tareas repetitivas de manipulación de objetos, implementando un
sistema básico de programación por demostración (Programming by Demonstration,
PbD). El objetivo principal es construir un manipulador robótico capaz de aprender
trayectorias mediante el movimiento manual realizado por el usuario y
posteriormente reproducirlas de forma automática con precisión y repetibilidad.
El sistema estará basado en el conjunto de librerías de ROS (Robot
Operating System), el cual permitirá manejar la lógica de control, cinemática y
planificación de movimientos del brazo robótico. El manipulador contará inicialmente
con cinco grados de libertad, utilizando servomotores de precisión y una pinza
capaz de sujetar objetos livianos para operaciones de tipo pick-and-place.
La metodología PbD busca simplificar la interacción entre el usuario y el
brazo robótico al quitar la barrera técnica del lenguaje de programación. Para esto,
el brazo dispondrá de una botonera física integrada al sistema que permitirá grabar,
reproducir y eliminar trayectorias sin necesidad de utilizar una computadora externa
durante la operación normal. De esta manera, el usuario podrá mover manualmente
el brazo hacia distintas posiciones, registrando la secuencia de movimientos para
luego automatizar tareas repetitivas.
El proyecto está orientado principalmente a aplicaciones de automatización
ligera en entornos industriales y educativos, especialmente en líneas de montaje o
estaciones de testing electrónico donde se requiera trasladar objetos ligeros entre
posiciones predefinidas con precisión.En esta etapa del desarrollo, el sistema no incluirá visión artificial,
procesamiento de imágenes, aprendizaje automático ni navegación autónoma. El
objetivo es concentrarse en la implementación de la lógica de manipulación robótica,
capaz de ejecutar movimientos programados de manera confiable y reproducible.
Con el fin de enfocarse en el desarrollo del software de control, reducir
tiempos y abaratar costos, el proyecto contempla la utilización de modelos 3D
imprimibles desarrollados por terceros y adaptados a las nuevas dimensiones que
puedan surgir de los servomotores a utilizar.
Adicionalmente, el proyecto empleará una simulación del brazo robótico en
entornos virtuales como Gazebo, permitiendo validar el comportamiento del robot
antes de su implementación física.
A futuro, la plataforma podrá expandirse mediante la incorporación de
cámaras, reconocimiento de objetos y herramientas modulares especializadas,
aunque dichas funcionalidades quedan fuera del alcance inicial del proyecto.
Finalmente, se espera que al finalizar esta etapa del proyecto se cuente con
un brazo robótico capaz de reproducir movimientos generados por el usuario
convirtiéndose en una herramienta valiosa al momento de realizar trabajos
repetitivos.

## Idea
La idea:
1. El usuario mueve manualmente el brazo
2. El sistema registra posiciones
3. El brazo reproduce automáticamente la secuencia


## Qué quiero hacer?
- Quiero hacer un brazo robótico que sea capaz de agarrar y mover objetos de un lugar a otro.
- Quiero que tenga un modo de funcionamiento en el cual se puedan grabar trayectorias para que se puedan automatizar operaciones. Esto entiendo que sería Programacion por demostracion (PbD).
- Quiero que los movimientos del brazo sean precisos.
- Quiero hacer el proyecto con ROS.

## Qué cosas no contendrá el entregable para la materia?
El brazo robótico no contará con:
- Cámara
- Procesamiento de imágenes
- Detección de objetos mediante imágenes y algoritmos de IA.
- Software en PC para controlar el brazo robotico. En esta instancia, no se vinculará el brazo robótico a la PC. Se usará una botonera asociada al brazo para grabar, reproducir y borrar tareas.

El brazo robótico no será:
- un robot autónomo inteligente
- un sistema de IA
- un robot humanoide

## Para quiénes lo quiero hacer?
- Industrias que requieran automatizar rápidamente lineas de montaje.
    - Operarios que no requieran conocimientos de programacion para manipular el brazo. Simplemente presionar grabar, mover el robot y luego reproducir.

## Para qué lo quiero hacer?
- Automatizar tareas repetitivas en lineas de montaje.

## Aplicaciones
- Pick and place de precisión.

## Qué me gustaría que hiciera el brazo de acá a diciembre?
	- Mover con precisión objetos de un lugar a otro.
	- Grabar trayectorias para reproducirlas automáticamente.
	
## Cómo lo quiero hacer?
- Conseguir modelo de brazo 3D para imprimir y para exportar a GAZEBO.
- Conseguir controlador de servos: https://www.hiwonder.com/products/serial-bus-servo-controller
- Conseguir servos: https://www.hiwonder.com/products/hts-35h?_pos=1&_sid=f8aa987be&_ss=r
- Imprimir brazo en 3D. Aun no decido si 5DOF o 6DOF. Usar servos comunes.
- Adaptar modelo 3D a servos de presición HTS-35H.
- Imprimir nuevo modelo de brazo 3D.
- Conseguir placa controladora. Aún no decido si usa beaglebone black o jetson nano. La jetson nano deberia comprarla: https://developer.nvidia.com/embedded/learn/get-started-jetson-nano-devkit
- Programar en ROS la logica de cinematica del brazo.
- Agregar un boton que permita encender el equipo.
- Agregar controles para grabar, reproducir y borrar tarea. 
- Grabar hasta 3 tareas.
	

## Qué quisiera agregar a futuro? (Post proyecto)
    - Agregar cámara de profundidad y LLM para reconocimiento de objetos.
    - Agregar micrófono para recibir comandos de voz.
    - Agregar parlantes para recibir feedback de parte del robot.
    - Diseño de pinza modular para:
        - pick&place smd
        - grabado láser
        
## Links con videos ejemplos de lo que quiero hacer
- Mini Brazo Robótico con Arduino - Guardar/Reproducir Posiciones.: https://www.youtube.com/watch?v=cWuJPlkmxCE
- Kinesthetic programming by demonstration - Wood planing: https://www.youtube.com/watch?v=psaiT0D9Ag0
    - De este video solo la idea de cómo enseñar al robot. No quiero hacer una herramienta para talla madera.
- Kinesthetic Teaching in Virtual Reality: https://www.youtube.com/watch?v=waBYCskHE-4 
    - De este video solo la idea de cómo enseñar al robot. No quiero hacer nada relacionado a realidad virtual.
- Kinesthetic Teaching with Haptic Feedback | Physical AI | Bota Systems: https://www.youtube.com/watch?v=MCL7rcnIfWE
    - De este video solo la idea de cómo enseñar al robot. No quiero hacer nada de Haptic Feedback ni physical AI.
- This $150 Robot Arm Is The Best Way to Start With Advanced Robotics: https://www.youtube.com/watch?v=59JTCvpG_Ec
    - De este video la idea de cómo enseñar al robot. El proyecto se ve parecido a lo que quiero hacer solo que este usa 2 brazos y yo quisiera usar solo 1 y además este proyecto usa cámara y yo en esta instancia no quiero usarla.
    - Proyecto base de ese video: https://github.com/huggingface/lerobotk
- Curricula for teaching end-users to kinesthetically program collaborative robots : https://pmc.ncbi.nlm.nih.gov/articles/PMC10691692/
- Trajectories and keyframes for kinesthetic teaching: A human-robot interaction perspective: https://sci-hub.box/https://dl.acm.org/doi/10.1145/2157689.2157815
