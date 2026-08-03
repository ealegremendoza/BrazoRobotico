# Fabricación de brazo robótico RO-101

## 1. Impresión de piezas en PLA
|ID|Pieza|Material|Tiempo de impresión|Cantidad de filamento utilizado|Notas|
|--|-----|-------------------|-------------------------------|-----|
|1|Base|PLA|5h 8m|74g|Imprimí varias 4 veces este modelo hasta dar con el tamaño que permitía insertar correctamente los servomotores. |
|2|Moving Jaw|PLA|1h 33m|26g||
|3|Under arm|PLA|2h 40m|43g||
|4|Motor holder|PLA|1h 47m|25g||
|5|Wrist Roll Pitch|PLA|2h 43m|31g||
|6|Rotation Pitch|PLA|1h 44m|30g||
|7|Wrist Roll|PLA|2h 13m|27g||
|8|Upper arm|PLA|2h 38m|50g||
|9|Wrist Roll Follower|PLA|2h 15m|32g||
|10|Base motor holder|PLA|1h 6m|18g||

> Todos los modelos descritos arriba fueron impresos con un tamaño de 102% respecto de su tamaño original para que entraran los actuadores que tengo.

- Los modelos fueron descargados de aquí: [https://github.com/TheRobotStudio/SO-ARM100](https://github.com/TheRobotStudio/SO-ARM100)
- Los modelos redimensionados se encuentran en [BrazoRobotico/3d/g_codes/scale_102](BrazoRobotico/3d/g_codes/scale_102)
- La impresora utilizada fue: [Creality Ender-3 V3 SE](https://www.creality.com/products/creality-ender-3-v3-se)

El tiempo total de impresión fue de: **23h 47m**

El total de filamento utilizado fue de: **356g**


## 2. Configuración de motores
1. [How to Use the ST3215 Servo Motor](https://www.youtube.com/watch?v=T5T7qCg4pGE)
2. [https://www.waveshare.com/wiki/ST3215_Servo](https://www.waveshare.com/wiki/ST3215_Servo)
3. [https://www.waveshare.com/wiki/Bus_Servo_Adapter_(A)](https://www.waveshare.com/wiki/Bus_Servo_Adapter_(A))

### Instalar dependencias de python
Según el video 1 del apartado anterior, una vez descargado [STServo_Python.zip](https://files.waveshare.com/wiki/Bus_Servo_Adapter_A/STServo_Python.zip), hay que:

1. Crear un entorno virtual. Tengo que estar parado en */BrazoRobotico/STServo_Python
```bash
python3 -m venv stservo-env
```

2. Activarlo.
```bash
source stservo-env/bin/activate
```

3. Instalar requerimientos
```bash
pip install -r requirements.txt
```
4. Escribir servos. Usar para esto los scripts de la carpeta sms_sts.
5. Para empezar usare el sms_sts/read_write.py. Dentro de este archivo hay que cargar el puerto USB que se va a utilizar.  Ojo! Lo conectaré via USB C, NO mediante UART. Esto es importante porque cambia la posición del jumper en la placa driver de los servos. Para USB hay que usar el jumper en la posición B.
6. Primero hay que conectar la placa driver al motor. Luego, hay que conectar la fuente de 12V a la placa driver y por ultimo el cable USB.
   1. Lo que hice primero fue conectar la fuente de 12 V a la red electrica y verifique con el multimetro la salida: 12.3V.
   2. Luego, conecte la fuente a la placa sin el motor y validé que en los pines de alimentación tenia 12.3V.
   3. Luego, desconecte la alimentacion, conecte el motor y volvi a conectar la alimentacion. En el motor se encendio un led rojo indicando que está energizado.
   4. Luego, conecte el cable USB C a la placa.
   5. Por último conecte el cable USB a la PC.
7. Para identificar el puerto USB
   1. En Windows: [System.IO.Ports.SerialPort]::GetPortNames()
   2. En Linux: ls /dev/ttyUSB*
      1. Cuando ejecuté esto en mi computadora no encontre nada. Tuve que ejecutar ls /dev/tty* y recién ahí encontré puertos. Comparé lo que arrojaba dicho comando con y sin la placa conectada y encontré que con la placa conectada aparece /dev/ttyACM0 
8. Configurar puerto en archivo read_write.py
9. Ejecutar python3 read_write.py. A continuación comparto los logs.
    ```bash
    ❯ python3 read_write.py
    Succeeded to open the port
    Succeeded to change the baudrate
    Press any key to continue! (or press ESC to quit!)
    [ID:001] GoalPos:0 PresPos:1074 PresSpd:-1150
    [ID:001] GoalPos:0 PresPos:1074 PresSpd:-1150
    [ID:001] GoalPos:0 PresPos:1073 PresSpd:-1150
    [ID:001] GoalPos:0 PresPos:1072 PresSpd:-1150
    [ID:001] GoalPos:0 PresPos:1072 PresSpd:-1150
    [ID:001] GoalPos:0 PresPos:1071 PresSpd:-1150
    [ID:001] GoalPos:0 PresPos:1069 PresSpd:-1150
    [ID:001] GoalPos:0 PresPos:1068 PresSpd:-1150
    [ID:001] GoalPos:0 PresPos:1068 PresSpd:-1150
    ```
    En el siguiente video se puede ver el resultado:
    [mover-servo](../videos/mover-servo.mp4)
### Configurar ID de motores
Los servos vienen con un ID predefinido con valor 1.
Tengo que cambiar el ID de cada uno para que tras ensamblar el brazo, pueda programar cada uno por separado.

Para esto voy a usar el script change_id.py. En el mismo se tiene que configurar el puerto y el nuevo id.

Listo. Configurados los 6 servomotores con ID: 1, 2, 3, 4, 5 y 6.

Se valida su funcionamiento en modo bus en este video: [mover servos](../videos/mover-servos.mp4)

## 3. Ensamblado
- [LeRobot SO-ARM101 Robotic Arm - Assembly and Setup Guide\](https://www.youtube.com/watch?v=70GuJf2jbYk)

## 4. Calibración
- [SO101 Arms: Calibration and Motor Configuration](https://www.youtube.com/watch?v=Nhfda8h7e2I)
