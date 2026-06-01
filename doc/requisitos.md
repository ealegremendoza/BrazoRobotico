# Titulo del proyecto
Brazo robótico programable mediante ROS para automatización de tareas repetitivas basado en grabación y reproducción de trayectorias.

# Requisitos del proyecto

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

## Requisitos técnicos
- El brazo robótico debe contar con 4 grados de libertad.

## Referencias
- Brazo Robótico (KUKA inspired): Diseño y Ensamblaje Completo. https://www.youtube.com/watch?v=ixrZgr7dIdw
    - 4 DOF + grip
    - Solo que usa Matlab como UI, yo no estoy pensando usar una aplicación de escritorio. 
    - Tampoco usa ROS, solamente envia ordenes de movimiento a los servomotores. 
    - Pero ejemplifica lo que quiero hacer. 
    
- Construí un Brazo Robótico (Inspirado en KUKA) desde cero!. https://www.youtube.com/watch?v=D84KoitsC-o
    - 4 DOF + grip
    - Me gusta más el diseño.
    - Solo que usa Matlab como UI, yo no estoy pensando usar una aplicación de escritorio. 
    - Tampoco usa ROS, solamente envia ordenes de movimiento a los servomotores. 
    - Pero ejemplifica lo que quiero hacer. 
    
- Brazo robótico con Arduino - Robotic Arm - Guardar/Reproducir/Exportar/Importar Movimientos. https://www.youtube.com/watch?v=1b1YxPmp97I
    - 5 DOF +grip
    - Solo que usa Matlab como UI, yo no estoy pensando usar una aplicación de escritorio. 
    - Tampoco usa ROS, solamente envia ordenes de movimiento a los servomotores. 
    - Pero ejemplifica lo que quiero hacer. 
    - El proveedor provee archivos formato STEP para hacer modificaciones.
