# Bitácora [R06] — Aprender el manejo de Actions en ROS2

_Inicio: 2026-09-18_

## Contexto

Estudio del ejemplo de Actions del curso Udemy "Robotics and ROS 2 - Learn by Doing: Manipulators", en:

```
/home/ezequiel/cursos/Robotics-and-ROS2-Manipulators/Robotics-and-ROS-2-Learn-by-Doing-Manipulators/Section7_Application/arduinobot_ws
```

Archivos revisados:
- `src/arduinobot_msgs/action/Fibonacci.action`
- `src/arduinobot_py_examples/arduinobot_py_examples/simple_action_server.py`
- `src/arduinobot_py_examples/arduinobot_py_examples/simple_action_client.py`

## ¿Por qué Actions y no Services/Topics?

Los Actions son para tareas **largas**, donde hace falta:
- feedback continuo mientras se ejecuta (no solo el resultado final),
- posibilidad de cancelar,
- confirmación de aceptación/rechazo del goal antes de arrancar.

Un Service es fire-and-wait-for-one-answer; un Topic no tiene noción de goal/resultado. Para algo como "mové el brazo a esta pose" (tarda, se puede cancelar, interesa ver el progreso), Actions es la herramienta correcta.

## Estructura de un `.action`

```
# Goal
int32 order
---
# Result
int32[] sequence
---
# Feedback
int32[] partial_sequence
```

Tres bloques separados por `---`: **Goal** (lo que pide el cliente), **Result** (lo que devuelve al terminar), **Feedback** (actualizaciones parciales mientras corre).

## Action Server (`simple_action_server.py`)

```python
self.action_server = ActionServer(
    self, Fibonacci, "fibonacci", self.goalCallback
)
```
Se registra con: el nodo, el tipo de acción, el nombre de la acción, y el callback que corre cuando llega un goal.

Dentro de `goalCallback`:
- Lee `goal_handle.request.order`.
- En un loop, va calculando y llamando `goal_handle.publish_feedback(feedback_msg)` en cada paso — así el cliente ve progreso en tiempo real.
- Al terminar: `goal_handle.succeed()` + `return result`.

**Nota:** este ejemplo es la versión mínima del curso — no separa explícitamente `goal_callback` (aceptar/rechazar) de `execute_callback`, y no maneja cancelación. Un Action Server "serio" sí separa esos tres callbacks.

## Patrón Future/Promise

Un `Future` es una promesa de un valor que todavía no existe. La llamada async (`send_goal_async`, `get_result_async`) no bloquea: devuelve el Future al instante, y seguís ejecutando otras cosas. Cuando el valor está listo, se dispara el callback que le registraste con `add_done_callback()`. Equivalente a `.then()` en JS/Promises.

## Action Client (`simple_action_client.py`)

Cadena de dos Futures **encadenados**, más un canal de feedback aparte:

1. `send_goal_async(goal, feedback_callback=feedbackCallback)` → Future #1 ("¿aceptaste el goal?"), resuelto por rclpy apenas el server entra al callback → dispara `responseCallback`.
2. Dentro de `responseCallback`, si `goal_handle.accepted`, se pide `goal_handle.get_result_async()` → Future #2 ("¿cuál es el resultado final?") → dispara `resultCallback` cuando el server hace `succeed()`.
3. En paralelo, cada `publish_feedback` del server dispara `feedbackCallback` en el cliente — esto NO usa Future, porque un Future se resuelve una sola vez y el feedback puede llegar múltiples veces.

Timeline del ejemplo Fibonacci (`order=10`):
```
t=0s   cliente: send_goal_async() ─────────────► server: goalCallback() arranca
t=0s   server: goal aceptado (implícito) ──────► Future#1 resuelto → responseCallback → pide Future#2
t=1..9s server: publish_feedback(...) ─────────► feedbackCallback (log), una vez por segundo
t=9s   server: succeed() + return result ──────► Future#2 resuelto → resultCallback → shutdown
```

## Próximos pasos

- Ver cómo aplicar esto a un goal real del brazo (ej. "mover a pose X", con feedback de posición actual y cancelación).
- Revisar cómo MoveIt2 (ver [[bitacora-R05-moveit2]]) expone sus propios Actions (`MoveGroup` action) — probablemente ya estamos usando este patrón sin haberlo hecho explícito.
