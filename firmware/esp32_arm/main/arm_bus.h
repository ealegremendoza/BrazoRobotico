#ifndef ARM_BUS_H
#define ARM_BUS_H

/**
 * @file arm_bus.h
 * @brief Servo bus task: the only owner of the servo bus (UART2) and of arm_state
 *        (doc/md/bitacora-E10-panel-lcd.md, DS1, DS3, DS14).
 *
 * Every user of the servos sends a request to its queue (8 entries):
 *
 * - pc_rx_task: M (targets from ROS) and S (start, with the limits of its param_init).
 * - The UI: CTL request (enter CTL on one joint), CTL exit and CTL command (one joint).
 *
 * Each request runs ONE transaction "write the servos it may move (if allowed) + SYNC_READ of
 * the 6": M writes the 6 only when OPERATING; a CTL command writes only its joint, only with
 * CTL granted on it; S and the CTL request only read. The task answers the PC on UART0
 * (M feedback; S -> M with the positions, or E with a code), publishes the positions and the
 * current limits in a length-1 mailbox (arm_bus_peek_feedback()) and reports state changes
 * and CTL answers through the event callback.
 *
 * It waits on its queue at most ARM_BUS_POLL_MS: the link watchdog is checked on every
 * round, whether a frame arrived or not.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "arm_limits.h"
#include "arm_state.h"
#include "m_payload.h"

/** Max wait on the request queue: period of the link watchdog check (DS18 #3). */
#define ARM_BUS_POLL_MS 100

/**
 * Link watchdog: with no frame from the PC for this long while OPERATING, STOPPED. The
 * ros2_control plugin waits longer than this after opening the port, so it always starts
 * against a STOPPED ESP32 even when opening the port does not reset it.
 */
#define ARM_BUS_LINK_TIMEOUT_MS 500

/** Request types. */
typedef enum {
    ARM_BUS_REQ_M,         /**< M from the PC: targets of the 6 joints. */
    ARM_BUS_REQ_S,         /**< S from the PC: start, with the decoded param_init. */
    ARM_BUS_REQ_CTL_ENTER, /**< The panel asks for CTL on one joint. */
    ARM_BUS_REQ_CTL_EXIT,  /**< The panel left CTL. */
    ARM_BUS_REQ_CTL_MOVE,  /**< The panel commands its CTL joint. */
} arm_bus_req_type_t;

/**
 * @brief One request for the bus task (the message on its queue).
 */
typedef struct {
    arm_bus_req_type_t type; /**< What to do. */
    int64_t rx_us;           /**< M / S: esp_timer time the frame arrived (link watchdog). */
    union {
        int32_t m_mrad[M_PAYLOAD_JOINTS]; /**< M: target of each joint, mrad. */
        struct {
            arm_limits_parse_t params;    /**< S: what the param_init contained. */
            arm_limits_t limits;          /**< S: the limits, valid on ARM_LIMITS_OK. */
        } s;
        struct {
            uint8_t joint;                /**< CTL: joint index, 0..5. */
            int32_t mrad;                 /**< CTL_MOVE: target, mrad (clamped to the limits). */
        } ctl;
    };
} arm_bus_request_t;

/**
 * @brief What the UI reads from the mailbox: the last positions read and the current limits
 *        (DS5, DS6).
 */
typedef struct {
    bool valid;                       /**< mrad holds a reading (false until the first one). */
    int32_t mrad[M_PAYLOAD_JOINTS];   /**< Last position read of each joint, mrad. */
    arm_limits_t limits;              /**< Limits in use (defaults or the last S). */
} arm_bus_feedback_t;

/** Events for the UI (rare, must not be lost: they go to the UI event queue). */
typedef enum {
    ARM_BUS_EV_STATE,       /**< The state changed: value = arm_state_id_t. */
    ARM_BUS_EV_CTL_GRANTED, /**< CTL granted: value = current angle of the joint, mrad. */
    ARM_BUS_EV_CTL_DENIED,  /**< CTL denied: value = arm_ctl_result_t (the reason). */
} arm_bus_event_t;

/**
 * @brief Receives the bus task events. Runs in the bus task: it must not block.
 *
 * @param ev    Event.
 * @param joint Joint of a CTL answer (0 for ARM_BUS_EV_STATE).
 * @param value See arm_bus_event_t.
 * @param ctx   The ctx given to arm_bus_start().
 */
typedef void (*arm_bus_event_cb_t)(arm_bus_event_t ev, unsigned joint, int32_t value, void *ctx);

/**
 * @brief Creates the queue and the mailbox and starts the bus task.
 *
 * The UARTs must already be configured (UART0 to the PC, UART2 to the servos).
 *
 * @param cb  Event callback, may be NULL.
 * @param ctx Passed to cb.
 * @return ESP_OK, or ESP_ERR_NO_MEM.
 */
esp_err_t arm_bus_start(arm_bus_event_cb_t cb, void *ctx);

/**
 * @brief Queues a request for the bus task.
 *
 * @param req  Request (copied).
 * @param wait Max ticks to wait for room (0: drop it at once if the queue is full).
 * @return true if queued, false if dropped (logged).
 */
bool arm_bus_send(const arm_bus_request_t *req, TickType_t wait);

/**
 * @brief Reads the newest feedback without taking it out of the mailbox.
 *
 * @param out Output.
 * @return true if the bus task already published one (always after arm_bus_start()).
 */
bool arm_bus_peek_feedback(arm_bus_feedback_t *out);

#endif // ARM_BUS_H
