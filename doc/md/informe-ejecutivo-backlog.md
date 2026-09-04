# Informe Ejecutivo — Avance del Backlog

**Proyecto:** BrazoRobotico (brazo SO-ARM101, control ESP32 + simulación ROS2/Gazebo)
**Fecha del informe:** 2026-09-04 (primer informe)
**Fuente:** `doc/md/BACKLOG.md` y bitácoras individuales por tarea (`doc/md/bitacora-*.md`)

## Resumen general

| Categoría | Completadas | Pendientes | Total |
|---|---|---|---|
| 🔧 ESP32 / Embedded Systems | 3 (E01, E02, E05) | 5 (E03, E04, E06, E07, E08) | 8 |
| 🦾 Brazo Robótico | 2 (R01, R02) | 1 (R03) | 3 |
| **Total** | **5** | **6** | **11** |

**Avance global: 5 de 11 tareas (~45%)**, distribuidas en dos frentes independientes: comunicación serie ESP32↔Arduino (hardware/firmware) y simulación del brazo en Gazebo/ROS2 (software). Ambos frentes tienen su primera integración end-to-end funcionando.

---

## 🔧 ESP32 / Embedded Systems

### ✅ E01 — UART TX-RX en el ESP32
Implementado y validado un loopback UART2 (TX↔RX puenteados) sobre ESP-IDF/FreeRTOS en `firmware/uart_test/`. Se confirmó a nivel esquemático el módulo (ESP32-WROOM-32UE) y la elección de pines (UART2, evitando UART0 reservada para programación/monitor y UART1 por conflicto con la flash SPI). Señal validada físicamente con analizador lógico. Configuración de pines migrada de `#define` a Kconfig (`idf.py menuconfig`), quedando parametrizable sin recompilar código.

### ✅ E02 — Medición de niveles TX/RX del ESP32
Medidos los niveles lógicos reales de un GPIO de salida del ESP32 con multímetro: **0.00V (LOW) / 3.29V (HIGH)**, confirmando lógica CMOS estándar de 3.3V. Este dato fue el insumo directo para decidir en E05 que hacía falta un conversor de nivel lógico hacia el Arduino Nano (5V).

### ✅ E05 — Comunicación ESP32 ↔ Arduino Nano vía HW-221
Tarea más extensa del frente embedded. Resultado: **comunicación bidireccional validada end-to-end** — PC (monitor serie del ESP32) → ESP32 → conversor de nivel HW-221 (TXS0108E) → Arduino Nano → LED, con protocolo de framing propio (STX/FS/ETX/LRC) y ACK de vuelta por el mismo camino. Comandos "ON"/"OFF" funcionando de forma confiable en ambas direcciones.

Puntos clave resueltos en el camino:
- Identificación del chip HW-221 (marcado YF08E = TXS0108E de Texas Instruments) y su datasheet.
- Diseño y cableado del circuito completo (ver diagrama en la bitácora).
- Protocolo de framing STX/FS/ETX/LRC, reutilizable para comandos multi-campo futuros (control de servos).
- Varios bugs de firmware corregidos: máquina de estados que nunca ejecutaba su estado final, `stdin` no bloqueante del ESP32 partiendo comandos en fragmentos, lectura UART que no esperaba una línea completa.
- Un problema físico (cable TX del Nano desconectado) diagnosticado por asimetría en el comportamiento (una dirección funcionaba, la otra no).

### ⏳ Pendientes — E03, E04, E06, E07, E08
No iniciadas. Encadenadas: E03 (adaptador lógico hacia el driver de motores) y E04 (protocolo de comando del driver) habilitan E06 (traducción de comandos PC→driver en el ESP32); E07/E08 son una decisión de arquitectura (desacoplar la gestión del driver hacia el ESP32) todavía sin resolver.

---

## 🦾 Brazo Robótico

### ✅ R01 — Investigación de URDF disponible
Confirmado que el repositorio base (`TheRobotStudio/SO-ARM100`) sí provee URDF (SO100 y SO101, dos calibraciones), aunque sin paquete ROS2/Gazebo armado — solo geometría + mallas. Se vendorizó el URDF de SO101 (calibración nueva) al repo en `ros2/urdf/so101/`, quedando como base de trabajo para R02.

### ✅ R02 — Brazo funcionando en simulación Gazebo/ROS2
Tarea de mayor alcance del proyecto hasta ahora. Se armó desde cero el workspace ROS2 (`robotic_arm_ws/`) con dos paquetes:

- **`robotic_arm_description`**: URDF/xacro completo del brazo (9 links + 8 joints, límites de calibración reales), integración con `ros2_control` (`<ros2_control>` + plugin `gz_ros2_control`), launch files para visualizar en RViz y para spawnear en Gazebo Sim (Harmonic).
- **`robotic_arm_controller`**: configuración de controladores (`joint_state_broadcaster`, `arm_controller`, `gripper_controller` sobre `JointTrajectoryController`), launch para activarlos, y un nodo de control interactivo por sliders (`slider_control.py`) para mover el brazo en vivo desde una GUI.

**Resultado validado end-to-end:** el brazo se visualiza correctamente en RViz y Gazebo, `ros2_control` se activa sin errores, y se comandó y ejecutó exitosamente una trayectoria real (`Goal successfully reached!`) — el brazo responde a comandos ROS2 en simulación.

Pendiente no bloqueante: la geometría de `<collision>` usa las mismas mallas STL de detalle completo que el `<visual>`, y el motor de física de Gazebo (`dartsim`) no soporta construir colisión desde malla — confirmado con evidencia directa (log propio) y cruzado contra el proyecto de referencia del curso (mismo problema, limitación general de `dartsim`, no específica de este proyecto). El brazo hoy no tiene colisión física real en ningún link; para física realista (agarre de objetos, contacto con superficies) hace falta reemplazar esa geometría por formas primitivas (cajas/cilindros).

### ⏳ Pendiente — R03
Definir los nodos ROS2 que van a manejar el brazo robótico real (no solo simulado), basándose en el curso de referencia. Depende conceptualmente de lo armado en R02 (misma estructura de controladores aplicaría al hardware real).

---

## Próximos pasos sugeridos

1. **Frente embedded:** definir E03/E04 (adaptador lógico + protocolo del driver de motores) para desbloquear E06.
2. **Frente robótica:** R03 (nodos de control real) y, en paralelo, resolver la geometría de colisión de R02 si se necesita física realista antes de avanzar a manipulación de objetos.
3. Evaluar si conviene una tarea nueva que conecte ambos frentes (el driver de motores real controlado desde los mismos nodos ROS2 que hoy mueven la simulación).
