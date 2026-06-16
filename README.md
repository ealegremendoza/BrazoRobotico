# Brazo robótico programable mediante ROS para automatización de tareas repetitivas basado en grabación y reproducción de trayectorias

Proyecto Final de Ingeniería Electrónica orientado al desarrollo de un brazo robótico de bajo costo para automatización de tareas repetitivas mediante grabación y reproducción de trayectorias.

El proyecto propone construir un manipulador robótico capaz de ser movido manualmente por el usuario, registrar una trayectoria y luego reproducirla de forma automática con precisión y repetibilidad. La solución se basa en ROS 2 para la lógica de control, cinemática, planificación de movimientos y futura integración con simulación en Gazebo.

## Objetivo general

Diseñar e implementar un brazo robótico programable que permita automatizar tareas simples de manipulación de objetos mediante un esquema de programación por demostración, evitando que el usuario tenga que escribir código para enseñarle una secuencia de movimientos al robot.

## Idea de funcionamiento

1. El usuario coloca el brazo en modo grabación.
2. El usuario mueve manualmente el brazo hasta las posiciones deseadas.
3. El sistema registra la trayectoria realizada.
4. El usuario selecciona una trayectoria guardada desde la interfaz física.
5. El brazo reproduce automáticamente la secuencia de movimientos.

## Alcance del proyecto

El entregable inicial contempla:

- Brazo robótico de 5 grados de libertad más pinza.
- Movimiento de objetos pequeños y livianos.
- Grabación, reproducción y borrado de trayectorias.
- Interfaz física simple mediante display y botones.
- Control de servomotores desde una computadora integrada compatible con ROS 2.
- Simulación del brazo en Gazebo para validar movimientos antes de la implementación física.
- Fabricación mediante piezas impresas en 3D, tomando como referencia el diseño SO-ARM100/SO-ARM101.

Quedan fuera del alcance inicial:

- Visión artificial.
- Detección de objetos por cámara.
- Algoritmos de inteligencia artificial.
- Navegación autónoma.
- Aplicación de escritorio para control remoto del brazo.
- Control por voz o interfaz conversacional.

## Aplicaciones previstas

El sistema está pensado para automatización ligera en entornos industriales, educativos o de laboratorio. Algunos casos de uso posibles son:

- Operaciones de pick-and-place de objetos livianos.
- Movimiento repetitivo de piezas entre posiciones predefinidas.
- Estaciones de testing electrónico.
- Demostraciones educativas de robótica, ROS 2 y cinemática de manipuladores.
- Prototipos de automatización para PyMEs donde un robot industrial de alta gama no resulta justificable.

## Arquitectura conceptual

El sistema se organiza en los siguientes bloques principales:

![Diagrama en bloques](/doc/img/arch.drawio.png)


## Hardware previsto

| Componente | Descripción |
|---|---|
| Brazo robótico | Basado en el proyecto SO-ARM100/SO-ARM101. |
| Estructura | Piezas impresas en 3D. Prototipado en PLA y posible versión final en PETG. |
| Actuadores | 6 servomotores STS3215: 5 DOF + pinza. |
| Controlador de servos | Placa driver compatible con servomotores STS3215. |
| Computadora integrada | A definir. Debe ser compatible con ROS 2. |
| Interfaz de usuario | Display LCD y botonera física. |
| Alimentación | A definir según consumo final de servos, computadora integrada e interfaz. |

## Software previsto

| Módulo | Función |
|---|---|
| ROS 2 | Framework principal para control y comunicación entre nodos. |
| Nodo de control del brazo | Envío de comandos hacia el controlador de servomotores. |
| Nodo de lectura de estado | Lectura de posición/estado de cada servomotor. |
| Nodo de grabación | Registro de trayectorias generadas por el usuario. |
| Nodo de reproducción | Ejecución automática de trayectorias guardadas. |
| Nodo de interfaz | Gestión de botones, display y estados del sistema. |
| Simulación Gazebo | Validación virtual del modelo y de los movimientos. |

## Requisitos funcionales principales

El brazo robótico deberá ser capaz de:

- Agarrar, levantar, sostener y trasladar objetos pequeños y livianos.
- Mover objetos desde una zona predefinida hacia otra zona predefinida.
- Permitir el encendido y apagado seguro del sistema.
- Permitir el movimiento manual del brazo durante el modo de grabación.
- Guardar trayectorias realizadas por el usuario.
- Borrar trayectorias previamente almacenadas.
- Reproducir trayectorias guardadas.
- Informar el estado del sistema mediante una interfaz simple.

## Estado actual del repositorio

Actualmente el repositorio contiene principalmente documentación del proyecto:

```text
BrazoRobotico/
└── doc/
    ├── img/          # Imágenes y diagramas del proyecto
    ├── md/           # Documentación en Markdown
    ├── pdf/          # Documentos exportados en PDF
    └── tables/       # Planillas de gestión y trazabilidad
    └── datasheets/   # Hojas de datos  
```

Archivos Markdown principales:

| Archivo | Contenido |
|---|---|
| `doc/md/agent.md` | Índice general de la documentación del proyecto. |
| `doc/md/fundamentos.md` | Idea base, alcance, motivación y funcionamiento esperado. |
| `doc/md/propuesta.md` | Propuesta aprobada por la cátedra. |
| `doc/md/proyecto-objetivos-v3.md` | Desarrollo ampliado de objetivos y alcance. |
| `doc/md/requisitos.md` | Requisitos funcionales y técnicos. |
| `doc/md/referencias.md` | Referencias de proyectos similares y brazo elegido. |
| `doc/md/materiales.md` | Materiales y componentes considerados. |
| `doc/md/modelos.md` | Modelos 3D evaluados. |
| `doc/md/cursos.md` | Recursos de aprendizaje relacionados con modelado 3D. |
| `doc/md/links.md` | Links útiles, tablero de seguimiento y bitácora. |
| `doc/md/respaldo.md` | Documento de respaldo con descripción general, FODA y desafío tecnológico. |
| `doc/md/tareas.md` | Listado de tareas tareas del proyecto. |


## Próximos pasos

- Definir computadora integrada compatible con ROS 2.
- Definir controlador de servomotores STS3215.
- Definir display, botonera y esquema de interacción con el usuario.
- Incorporar el modelo URDF/Xacro del brazo.
- Crear simulación en Gazebo.
- Implementar nodos ROS 2 para lectura de servos, grabación y reproducción de trayectorias.
- Validar el comportamiento primero en simulación y luego en el prototipo físico.
- Documentar procedimiento de instalación, calibración y operación.

## Roadmap tentativo

| Etapa | Descripción | Estado |
|---|---|---|
| Documentación inicial | Propuesta, fundamentos, requisitos y referencias. | En desarrollo |
| Selección de hardware | Servos, controlador, computadora integrada, display y fuente. | Pendiente |
| Simulación | Modelo del brazo en ROS 2/Gazebo. | Pendiente |
| Control básico | Movimiento de articulaciones desde ROS 2. | Pendiente |
| Grabación de trayectorias | Registro de posiciones y tiempos. | Pendiente |
| Reproducción automática | Ejecución de trayectorias guardadas. | Pendiente |
| Modelo mecánico | Adaptación e impresión 3D del brazo. | Pendiente |
| Integración final | Hardware, software e interfaz física. | Pendiente |

## Licencia

A definir.

## Autor
- Alumno: Ezequiel Alegre Mendoza 
- Tutores: Ing. Fernando Fiamberti, Ing. Claudia Orlandi
- Docente: Ing. Silvio Tapino
- Proyecto Final - Ingeniería Electrónica

