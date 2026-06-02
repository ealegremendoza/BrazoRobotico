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
    
- El controlador del brazo robótico debe ser capaz de:
    - enviar señales de control a cada servomotor.
    - conocer el estado de cada servomotor en cada instante.

- La computadora integrada debe ser capaz de:
    - enviar comandos de movimiento al controlador del brazo robótico.
    - calibrar el brazo robótico.
    - guardar trayectorias grabadas por el usuario.
    - autoiniciar el sistema de control del robot cuando se enciende.

## Requisitos técnicos
- El brazo robótico debe contar con 4 grados de libertad? 5? No se!
    - Cómo decidir el modelo?
        - Según los servos que voy a comprar?
            - Los Hiwonder HTD-35H no me convienen porque son muy nuevos en la industria y no parece tener mucho soporte de parte de la comunidad.
            - Me convienen comprar los STS3215 porque ya estan asentados y están en ML incluso. Sacrifico un pelin de precision pero creo que para el proyecto vendran bien.
        - El curso que estoy haciendo es de 3+1 DOF. Me gustaría que pudiera rotar la pieza pero siento que será más dificil.

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
    
- Testing of Feetech STS3215 Servomotor: Backlash, Repeatability, and Torque
    - Link https://robonine.com/testing-of-feetech-sts3215-servomotor-backlash-repeatability-and-torque/
    
## Materiales
- 6 Servos Sts3215 De 7,4 V Para Brazo Robótico So-arm100 De 1
    - Link: https://www.mercadolibre.com.ar/6-servos-sts3215-de-74-v-para-brazo-robotico-so-arm100-de-1/p/MLA2037577958#polycard_client=search-desktop&be_origin=backend&search_layout=grid&position=11&type=product&tracking_id=47835afb-007a-4a73-8f4d-cf8db33b5aca&wid=MLA1790180591&sid=search
