# Propuesta aprobada por la cátedra
El proyecto consiste en el desarrollo de un brazo robótico orientado a la automatización de tareas repetitivas de manipulación de objetos, implementando un sistema básico de programación por demostración (Programming by Demonstration, PbD). El objetivo principal es construir un manipulador robótico capaz de aprender trayectorias mediante el movimiento manual realizado por el usuario y posteriormente reproducirlas de forma automática con precisión y repetibilidad.

El sistema estará basado en el conjunto de librerías de ROS (Robot Operating System), el cual permitirá manejar la lógica de control, cinemática y planificación de movimientos del brazo robótico. El manipulador contará inicialmente con cinco grados de libertad, utilizando servomotores de precisión y una pinza capaz de sujetar objetos livianos para operaciones de tipo pick-and-place.

La metodología PbD busca simplificar la interacción entre el usuario y el brazo robótico al quitar la barrera técnica del lenguaje de programación. Para esto, el brazo dispondrá de una botonera física integrada al sistema que permitirá grabar, reproducir y eliminar trayectorias sin necesidad de utilizar una computadora externa durante la operación normal. De esta manera, el usuario podrá mover manualmente el brazo hacia distintas posiciones, registrando la secuencia de movimientos para luego automatizar tareas repetitivas.

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
