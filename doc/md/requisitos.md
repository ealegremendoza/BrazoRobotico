# Requisitos del proyecto

## Título del proyecto

Brazo robótico programable mediante ROS para automatización de tareas repetitivas basado en grabación y reproducción de trayectorias.

## Descripción general

El proyecto consiste en el desarrollo de un brazo robótico de bajo costo, fabricable mediante impresión 3D, orientado a tareas repetitivas de manipulación liviana.

El sistema deberá permitir que un usuario enseñe una tarea moviendo manualmente el brazo, registre la trayectoria realizada y luego reproduzca dicha trayectoria de forma automática. El objetivo es automatizar tareas simples de pick-and-place sin requerir que el usuario programe manualmente los movimientos del robot.

El proyecto se desarrollará como un prototipo funcional para entorno interior, educativo, de laboratorio o de automatización liviana.

## Alcance del proyecto

El alcance inicial del proyecto incluye:

* Construcción de un brazo robótico de bajo costo basado en piezas impresas en 3D.
* Manipulación de objetos pequeños y livianos.
* Movimiento manual del brazo para enseñanza de tareas.
* Grabación de trayectorias.
* Almacenamiento de trayectorias.
* Selección de trayectorias previamente grabadas.
* Reproducción automática de trayectorias.
* Borrado de trayectorias almacenadas.
* Interfaz de usuario local para operación básica.
* Visualización del estado del sistema.
* Control de movimiento mediante ROS 2.
* Validación previa mediante simulación.
* Alimentación desde la red eléctrica.
* Diseño preparado para futuras expansiones, como visión artificial, inteligencia artificial, telemetría e interfaz remota.

Quedan fuera del alcance inicial:

* Detección automática de objetos mediante cámara.
* Visión artificial.
* Inteligencia artificial aplicada a la toma de decisiones.
* Teleoperación remota avanzada.
* Aplicación de escritorio para control remoto.
* Control por voz.
* Operación en ambientes industriales agresivos.
* Manipulación de cargas pesadas.

## Modos de operación

### Modo enseñanza

En este modo, el usuario mueve manualmente el brazo robótico para enseñarle una tarea. El sistema deberá registrar la secuencia de posiciones realizada por el usuario.

Este modo se corresponde con la función de programación por demostración.

### Modo reproducción

En este modo, el brazo robótico ejecuta automáticamente una trayectoria previamente almacenada.

El sistema deberá reproducir la secuencia de movimientos de forma consistente, permitiendo automatizar tareas repetitivas.

### Modo detenido o seguro

En este modo, el sistema no ejecuta movimientos automáticos. Deberá utilizarse ante una condición anormal, error, parada solicitada por el usuario o finalización de operación.

## Definiciones técnicas adoptadas

### Objeto liviano

Para este proyecto se considera objeto liviano a todo objeto cuya masa no supere los 200 g, considerando que el brazo será impreso en 3D y que se utilizará para tareas de automatización liviana.

### Objeto pequeño

Para este proyecto se considera objeto pequeño a todo objeto que pueda ser tomado por la pinza del brazo sin requerir herramientas especiales ni modificaciones mecánicas adicionales. La dimensión exacta queda pendiente de definir.

Como criterio de validación, el objeto deberá poder ser agarrado, sostenido, trasladado y liberado entre dos posiciones predefinidas.

### Tarea repetitiva

Se considera tarea repetitiva a una secuencia de movimientos que pueda ser enseñada una vez por el usuario y luego ejecutada varias veces por el brazo robótico.

### Trayectoria

Se considera trayectoria a la secuencia ordenada de posiciones articulares y tiempos asociados que permiten reproducir un movimiento enseñado por el usuario.

### Posición de referencia

Se considera posición de referencia, a una configuración conocida del brazo desde la cual se puede iniciar o finalizar una operación de forma ordenada.

## Requisitos de usuario y operación

### R1.0 - Enseñanza manual de tareas

El usuario deberá poder enseñar una tarea moviendo manualmente el brazo, sin necesidad de escribir código.

El sistema deberá permitir que el usuario coloque el brazo en modo enseñanza, mueva el brazo hacia las posiciones deseadas y registre la trayectoria realizada.

### R1.1 - Facilidad de uso

El usuario deberá poder operar el sistema sin conocimientos avanzados de programación, robótica o electrónica.

La operación principal deberá estar basada en acciones simples como iniciar, grabar, guardar, seleccionar, reproducir, borrar, detener, agarrar y liberar.

### R1.2 - Preparación rápida de tareas

El usuario deberá poder enseñar una nueva tarea en pocos minutos.

El sistema deberá reducir el tiempo necesario para adaptar el brazo a una nueva operación repetitiva.

## Requisitos de manipulación y movimiento

### R2.0 - Manipulación de objetos

El brazo robótico deberá permitir agarrar, sostener, trasladar y liberar objetos livianos entre posiciones predefinidas.

El peso máximo objetivo para el prototipo inicial será de 200 g.

El sistema deberá estar orientado a tareas de pick-and-place de baja carga, tales como traslado de piezas pequeñas, objetos de laboratorio, componentes livianos o elementos de demostración.

### R2.1 - Precisión funcional

El brazo deberá mover objetos entre posiciones predefinidas con precisión suficiente para tareas de pick-and-place.

Como prueba de validación funcional, el sistema deberá poder trasladar un objeto liviano desde una posición inicial hacia una posición final definida.

Como prueba de precisión demostrativa, se propone que el sistema pueda colocar un anillo sobre un dedo de una mano impresa en 3D.

### R2.2 - Repetibilidad

El sistema deberá ejecutar una misma trayectoria varias veces obteniendo resultados consistentes.

La repetibilidad será considerada aceptable cuando el brazo pueda repetir una trayectoria grabada sin perder la secuencia de movimientos ni fallar en la toma o liberación del objeto dentro del escenario de prueba definido.

## Requisitos de gestión de trayectorias

### R3.0 - Grabación de trayectorias

El sistema deberá registrar la secuencia de movimientos realizada por el usuario durante una sesión de enseñanza.

La grabación deberá incluir las posiciones articulares necesarias para reproducir posteriormente el movimiento.

### R3.1 - Almacenamiento de tareas

El sistema deberá permitir guardar las trayectorias registradas por el usuario.

Cada trayectoria almacenada deberá poder identificarse posteriormente para su selección y reproducción.

### R3.2 - Reproducción automática

El brazo robótico deberá ejecutar automáticamente una trayectoria previamente guardada.

Durante la reproducción, el sistema deberá mover el brazo siguiendo la secuencia registrada durante la etapa de enseñanza.

### R3.3 - Selección de tareas

El usuario deberá poder seleccionar qué trayectoria almacenada desea ejecutar.

La selección deberá realizarse desde la interfaz local del sistema.

### R3.4 - Eliminación de tareas

El usuario deberá poder borrar trayectorias almacenadas que ya no sean necesarias.

El sistema deberá evitar que una trayectoria eliminada quede disponible para reproducción.

### R3.5 - Conservación de tareas

Las trayectorias grabadas deberán conservarse luego de apagar y volver a encender el equipo.

El sistema deberá contar con almacenamiento persistente para evitar que el usuario tenga que enseñar nuevamente una tarea ya registrada.

## Requisitos de interfaz de usuario

### R4.0 - Interfaz de usuario local

El sistema deberá contar con una interfaz local que permita operar las funciones principales del brazo.

La interfaz deberá permitir, como mínimo:

* Encender o iniciar el sistema de operación.
* Seleccionar modo de enseñanza.
* Iniciar grabación de trayectoria.
* Guardar trayectoria.
* Seleccionar trayectoria almacenada.
* Reproducir trayectoria.
* Borrar trayectoria.
* Detener la operación.

### R4.1 - Visualización de estado

El sistema deberá informar al usuario el estado actual del brazo robótico.

Como mínimo, se deberán contemplar los siguientes estados:

* Sistema listo.
* Modo enseñanza.
* Grabando trayectoria.
* Trayectoria guardada.
* Reproduciendo trayectoria.
* Tarea finalizada.
* Error.
* Sistema detenido.

## Requisitos de seguridad e inicialización

### R5.0 - Encendido y apagado seguro

El usuario deberá poder encender y apagar el brazo robótico de forma sencilla y segura.

El sistema no deberá iniciar movimientos automáticos inesperados al energizarse.

### R5.1 - Detención de operación

El sistema deberá permitir detener la ejecución de una tarea ante una situación inesperada o condición anormal.

La función de detención deberá tener prioridad sobre la ejecución de trayectorias.

### R5.2 - Inicialización del sistema

El brazo deberá realizar una preparación inicial antes de comenzar la operación normal.

Durante la inicialización, el sistema deberá preparar los servicios de control, verificar el estado general y dejar el brazo en condiciones conocidas de operación.

### R5.3 - Posición de referencia

El brazo deberá contar con una posición de referencia para iniciar o finalizar operaciones.

La posición de referencia deberá utilizarse para ordenar el ciclo de trabajo, facilitar la repetición de tareas y reducir errores al iniciar una reproducción.

## Requisitos de software y control

### R6.0 - Operación autónoma local

El brazo deberá poder ejecutar sus funciones principales sin requerir una computadora externa durante el uso normal.

La computadora integrada deberá ejecutar la lógica de control, la gestión de trayectorias y la comunicación con la interfaz de usuario.

### R6.1 - Organización del control del sistema

El software deberá organizarse de forma modular para coordinar el movimiento del brazo, el estado de operación, la gestión de trayectorias y la interfaz de usuario.

El sistema deberá separar, como mínimo, las siguientes responsabilidades:

* Control del brazo.
* Lectura de estado de actuadores.
* Grabación de trayectorias.
* Reproducción de trayectorias.
* Gestión de trayectorias almacenadas.
* Interfaz de usuario.
* Seguridad y estados de operación.

### R6.2 - Plataforma de control

El sistema deberá utilizar ROS 2 como plataforma principal de software para la implementación de la lógica de control, comunicación entre módulos y futura integración con simulación.

### R6.3 - Lectura de estado del brazo

El sistema deberá conocer el estado de las articulaciones del brazo durante la operación.

La lectura de estado deberá permitir registrar trayectorias, reproducir movimientos y detectar condiciones anormales.

### R6.4 - Control de actuadores

El sistema deberá enviar comandos de movimiento a los actuadores del brazo robótico.

El control deberá permitir ejecutar movimientos manuales asistidos, grabación y reproducción automática de trayectorias.

### R6.5 - Arranque automático del sistema de control

El sistema deberá iniciar automáticamente los servicios necesarios al encenderse.

El usuario no deberá ejecutar manualmente comandos internos del sistema para comenzar la operación normal del producto.

## Requisitos de hardware e integración física

### R7.0 - Uso en entorno interior

El producto deberá estar pensado para operar en espacios interiores de trabajo, laboratorio o aula.

El prototipo inicial no estará diseñado para operar a la intemperie, bajo lluvia, polvo excesivo, humedad elevada o ambientes industriales agresivos.

### R7.1 - Alimentación por red eléctrica

El sistema deberá poder alimentarse desde la red eléctrica disponible en el lugar de uso.

Para el contexto de uso previsto, se considera alimentación desde red domiciliaria de 220 VCA y 50 Hz, mediante una fuente de alimentación adecuada para el sistema.

La alimentación interna del equipo deberá proveer las tensiones necesarias para los actuadores, la computadora integrada, la interfaz de usuario y los módulos auxiliares.

### R7.2 - Dimensionamiento preliminar de fuente

El sistema deberá contar con una fuente principal dimensionada para alimentar simultáneamente el brazo robótico, la computadora integrada y la interfaz de usuario.

Como criterio preliminar de diseño, se adopta una fuente principal switching de 12 VCC y 20 A como mínimo.

Este dimensionamiento contempla:

* Alimentación de servomotores del brazo.
* Alimentación de la computadora integrada.
* Alimentación de la interfaz de usuario.
* Margen de seguridad para picos de consumo.
* Posibilidad de incorporar convertidores DC-DC para las tensiones internas requeridas.

El dimensionamiento final deberá validarse mediante mediciones de consumo durante las pruebas del prototipo.

### R7.3 - Actuadores del brazo

El brazo utilizará servomotores inteligentes con realimentación de posición para permitir grabación y reproducción de trayectorias.

El prototipo inicial estará basado en servomotores STS3215 o equivalentes funcionales.

Los actuadores deberán permitir:

* Control de posición.
* Lectura de posición.
* Lectura de estado.
* Comunicación con el controlador del brazo.
* Ejecución de movimientos repetitivos.

### R7.4 - Capacidad de carga

El brazo deberá estar diseñado para manipular objetos de hasta 200 g en el prototipo inicial.

Este valor representa el objetivo de carga útil del proyecto y deberá validarse experimentalmente.

El brazo no estará destinado a manipular cargas pesadas ni objetos que comprometan la estabilidad mecánica del conjunto.

### R7.5 - Fabricación de bajo costo

El brazo robótico deberá contemplar una construcción que permita reducir costos de fabricación y facilitar la reposición de piezas.

La estructura mecánica principal deberá fabricarse mediante impresión 3D, priorizando piezas de bajo costo y fáciles de reemplazar.

### R7.6 - Material de impresión

Para la etapa de prototipo se utilizará PLA o PLA+.

Para una versión final o de mayor robustez se contempla el uso de PETG en piezas estructurales, siempre que la impresora disponible y los ensayos mecánicos lo permitan.

La selección final del material deberá considerar:

* Rigidez de las piezas.
* Facilidad de impresión.
* Costo.
* Disponibilidad.
* Resistencia mecánica suficiente para la carga útil objetivo.
* Facilidad de reposición ante rotura o desgaste.

### R7.7 - Modelo mecánico de referencia

El diseño mecánico tomará como referencia el proyecto SO-ARM100/SO-ARM101.

El proyecto no construirá dos brazos separados de tipo líder y seguidor. En su lugar, se construirá un único brazo capaz de funcionar como brazo de enseñanza durante la grabación y como brazo ejecutor durante la reproducción.

### R7.8 - Gabinete e integración electrónica

El sistema deberá contar con una integración física ordenada para alojar la electrónica, la alimentación y la interfaz de usuario.

El gabinete o soporte de electrónica deberá proteger los componentes internos frente a contactos accidentales durante el uso normal.

### R7.9 - Grado de protección

El equipo será considerado de uso interior.

Como criterio inicial, el conjunto general se considerará apto para entorno interior controlado. El gabinete de electrónica deberá brindar protección suficiente para evitar contactos accidentales con los componentes internos.

No se contempla, en la versión inicial, operación bajo lluvia, salpicaduras, polvo industrial o ambientes exteriores.

## Requisitos de validación y pruebas

### R8.0 - Validación previa de funcionamiento

El sistema deberá permitir verificar el comportamiento del brazo antes de operar físicamente.

La validación previa podrá realizarse mediante simulación, pruebas parciales o ensayos controlados de movimiento.

### R8.1 - Simulación

El sistema deberá contar con un modelo de simulación que permita validar movimientos y funcionalidades antes de su implementación física.

La simulación deberá utilizarse para reducir errores durante la puesta en marcha y facilitar el desarrollo del control.

### R8.2 - Prueba de manipulación

El sistema deberá superar una prueba de manipulación en la cual el brazo tome un objeto liviano, lo traslade y lo libere en una posición predefinida.

### R8.3 - Prueba de repetibilidad

El sistema deberá ejecutar una misma trayectoria repetidas veces manteniendo un resultado consistente.

La prueba deberá realizarse con una trayectoria previamente grabada por el usuario.

### R8.4 - Prueba de precisión funcional

El sistema deberá superar una prueba de precisión funcional acorde al objetivo del proyecto.

Como prueba demostrativa, se propone que el brazo pueda colocar un anillo sobre un dedo de una mano impresa en 3D.

### R8.5 - Validación de consumo eléctrico

El consumo eléctrico del sistema deberá medirse durante las pruebas del prototipo.

La medición deberá contemplar, como mínimo:

* Sistema encendido en reposo.
* Movimiento manual.
* Grabación de trayectoria.
* Reproducción automática.
* Movimiento con objeto liviano.
* Condiciones de mayor esfuerzo mecánico.

El resultado de estas mediciones deberá utilizarse para confirmar o ajustar el dimensionamiento de la fuente.

## Requisitos de documentación

### RD1 - Documentación de requisitos

El proyecto deberá mantener actualizado el archivo de requisitos del sistema.

Los requisitos deberán estar organizados por tipo o categoría y deberán mantener relación con la matriz de trazabilidad del proyecto.

### RD2 - Documentación de trazabilidad

El proyecto deberá contar con una matriz de trazabilidad que vincule los requisitos con sus objetivos, implementación, importancia, fecha y comentarios.

### RD3 - Documentación técnica

El proyecto deberá documentar las decisiones técnicas principales, incluyendo:

* Arquitectura general del sistema.
* Componentes seleccionados.
* Materiales de fabricación.
* Alimentación eléctrica.
* Procedimiento de operación.
* Procedimiento de calibración.
* Validaciones realizadas.
* Limitaciones del prototipo.

## Requisitos de expansiones futuras

### R9.0 - Expansión futura con visión artificial e inteligencia artificial

El sistema inicial no incluirá visión artificial ni inteligencia artificial.

Sin embargo, el diseño deberá contemplar la posibilidad de incorporar en futuras versiones:

* Cámara.
* Reconocimiento de objetos.
* Procesamiento de imágenes.
* Clasificación de objetos.
* Selección automática de objetos a manipular.
* Algoritmos de asistencia inteligente.

Estas funciones se consideran fuera del alcance inicial del proyecto.

### R9.1 - Expansión futura con telemetría

El sistema deberá contemplar la posibilidad de incorporar en futuras versiones funciones de telemetría.

La telemetría podrá incluir:

* Registro de estados del sistema.
* Registro de errores.
* Registro de trayectorias ejecutadas.
* Monitoreo de consumo.
* Monitoreo de posición.
* Monitoreo de temperatura o carga de actuadores, si el hardware lo permite.

Estas funciones se consideran deseables para diagnóstico, mantenimiento y mejora del prototipo.

### R9.2 - Expansión futura de interfaz remota

El sistema inicial no dependerá de una aplicación externa para su operación normal.

Sin embargo, deberá poder evolucionar hacia una interfaz remota de supervisión, configuración o monitoreo.

Esta expansión podrá implementarse mediante una aplicación de escritorio, panel web o herramienta equivalente.

### R9.3 - Expansión futura de herramientas

El brazo deberá contemplar la posibilidad de incorporar herramientas o accesorios de manipulación específicos en futuras versiones.

Estas herramientas podrán incluir variantes de pinza, soportes, adaptadores o efectores finales especializados.

## Restricciones del proyecto

El proyecto estará sujeto a las siguientes restricciones:

* El desarrollo será realizado por un único integrante.
* El alcance deberá mantenerse acotado para poder finalizar el prototipo dentro del plazo académico.
* El sistema inicial no incluirá visión artificial ni inteligencia artificial.
* El sistema inicial no incluirá teleoperación remota avanzada.
* El brazo estará orientado a objetos livianos.
* El prototipo estará destinado a uso interior.
* La estructura será fabricada mediante impresión 3D.
* Las especificaciones finales deberán validarse experimentalmente durante la etapa de pruebas.

## Criterios de aceptación generales

El proyecto se considerará funcionalmente aceptable si cumple con los siguientes criterios:

* El usuario puede encender el sistema de forma segura.
* El usuario puede colocar el brazo en modo enseñanza.
* El usuario puede mover manualmente el brazo para enseñar una tarea.
* El sistema puede grabar una trayectoria.
* El sistema puede guardar una trayectoria.
* El usuario puede seleccionar una trayectoria guardada.
* El brazo puede reproducir una trayectoria automáticamente.
* El usuario puede borrar una trayectoria almacenada.
* El sistema conserva trayectorias luego de apagarse.
* El sistema informa su estado de operación.
* El usuario puede detener la operación ante una condición inesperada.
* El brazo puede manipular un objeto liviano de hasta 200 g.
* El brazo puede repetir una trayectoria con resultados consistentes.
* El sistema puede validarse mediante simulación o pruebas previas.
* El prototipo puede alimentarse desde la red eléctrica mediante una fuente adecuada.
* Las piezas principales del brazo pueden fabricarse mediante impresión 3D.
