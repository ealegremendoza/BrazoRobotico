# Backlog de Tareas

_Última actualización: 2026-08-30_

## Cómo usar este documento
- Agregar tareas nuevas bajo la categoría correspondiente (o crear una nueva sección si hace falta).
- Marcar como hecha cambiando `[ ]` por `[x]`.
- Mover tareas completadas a la sección "Hecho" si querés mantener el backlog activo limpio.

---

## 🔧 ESP32 / Embedded Systems

- [x] **[E01]** Programar ESP32 para poder enviar y recibir mensajes por puerto serie a través de los pines TX-RX
- [x] **[E02]** Medir niveles de las señales que salen por TX y RX del ESP32 usando el analizador lógico
- [ ] **[E03]** Averiguar si es necesario un adaptador lógico entre el ESP32 y el driver de los servomotores; si se necesita, conseguir uno
- [ ] **[E04]** Averiguar cómo comandar el driver de motores vía TX-RX
- [x] **[E05]** Probar el código del ESP32 para manejar TX-RX con un Arduino Nano; programar el Arduino Nano para que pueda enviar y recibir mensajes por los pines TX-RX
- [ ] **[E06]** (Depende de E01) Hacer un código en el ESP32 que traduzca los comandos recibidos de la computadora principal en los comandos que precisa el driver de los motores; estudiar el código Python de ejemplo
- [ ] **[E07]** Evaluar si hace falta realmente desacoplar la gestión del driver de motores hacia el ESP32, o si está bien como está
- [ ] **[E08]** (Depende de E07) Hacer un nuevo script de Python para delegar la parte de gestión del driver de motores al ESP32

## 🦾 Brazo Robótico

- [x] **[R01]** Averiguar si el proyecto armso-101 (en el cual está basado mi proyecto) tiene un URDF para simular con Gazebo ROS2 — ver `doc/md/bitacora-R01-urdf.md`
- [ ] **[R02]** (Depende de R01) Si tiene URDF: tratar de levantar el modelo en Gazebo. Si no tiene: ver cómo generarlo usando los STL disponibles
- [ ] **[R03]** Definir los nodos que se usarán para manejar el brazo robótico, basándose en los mismos vistos en el curso de manejo de brazo robótico con ROS2

## 📌 Otros

- [ ] (agregar tareas aquí)

---

## ✅ Hecho

- [x] (ejemplo de tarea completada)
