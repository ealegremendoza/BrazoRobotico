/**
 * @file arm_bus.c
 * @brief Servo bus task: requests -> one bus transaction -> answers, feedback and events
 *        (see arm_bus.h).
 */

#include "arm_bus.h"

#include <stdio.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "board_pins.h"
#include "frame_parser.h"
#include "servo_bus.h"
#include "servo_units.h"

// Above pc_rx_task (10): a queued M is answered before the next frame is parsed (DS18 #1).
#define BUS_TASK_STACK_SIZE 4096
#define BUS_TASK_PRIORITY 11
#define BUS_QUEUE_LEN 8

// Max wait for all servos to answer a SYNC_READ. They answer in < 1 ms at 1 Mbaud;
// on timeout no M reply is sent and the PC sees a missing answer for that cycle.
#define SERVO_READ_TIMEOUT_MS 10
// Room for an echo of our own SYNC_WRITE + SYNC_READ (if the bus adapter echoes TX)
// plus every servo's status packet.
#define SERVO_RX_BUF_SIZE                                        \
    (SERVO_BUS_SYNC_WRITE_POS_SIZE(M_PAYLOAD_JOINTS) +           \
     SERVO_BUS_SYNC_READ_SIZE(M_PAYLOAD_JOINTS) + M_PAYLOAD_JOINTS * SERVO_BUS_POS_STATUS_SIZE)

// E payload: two decimal digits (DS8).
#define E_PAYLOAD_SIZE 2

/** Servo bus IDs in M payload order: joint j drives servo j + 1 (shoulder_pan = 1 .. gripper = 6). */
static const uint8_t SERVO_IDS[M_PAYLOAD_JOINTS] = {1, 2, 3, 4, 5, 6};

/** STOPPED / OPERATING, watchdog and CTL grant (see arm_state.h). Bus task only. */
static arm_state_t s_arm;

/** Last feedback published; the mailbox holds a copy. Bus task only. */
static arm_bus_feedback_t s_feedback;

static QueueHandle_t s_queue;    // requests, BUS_QUEUE_LEN entries
static QueueHandle_t s_mailbox;  // arm_bus_feedback_t, length 1, overwritten
static arm_bus_event_cb_t s_event_cb;
static void *s_event_ctx;

static const char *TAG = "ARM-BUS";

/**
 * @brief Reports an event to the UI, if anyone listens.
 *
 * @param ev    Event.
 * @param joint Joint of a CTL answer.
 * @param value Event value.
 */
static void notify(arm_bus_event_t ev, unsigned joint, int32_t value)
{
    if (s_event_cb != NULL) {
        s_event_cb(ev, joint, value, s_event_ctx);
    }
}

/**
 * @brief Reads every servo's present position with one SYNC_READ.
 *
 * Collects the answers until all servos replied or SERVO_READ_TIMEOUT_MS passes.
 * Reads SERVO_BUS_POS_STATUS_SIZE bytes per call after the first batch, so each
 * call returns as soon as the next answer arrives instead of waiting for a full buffer.
 *
 * Timeouts are in FreeRTOS ticks (10 ms at CONFIG_FREERTOS_HZ=100). A 1-tick timeout
 * ends at the next tick interrupt, anywhere from 0 to 10 ms later: with only
 * pdMS_TO_TICKS(10) the read sometimes gave up before the servos (~1.5 ms) answered
 * ("0 bytes received", ~14% of cycles). Each wait adds one tick, so it lasts at
 * least the remaining time; the deadline itself is measured with esp_timer.
 *
 * @param positions Output: present position of each servo in counts, untouched on failure.
 * @return true if every servo answered in time, false otherwise.
 */
static bool read_servo_positions(int32_t positions[M_PAYLOAD_JOINTS])
{
    uint8_t request[SERVO_BUS_SYNC_READ_SIZE(M_PAYLOAD_JOINTS)];
    size_t request_size = servo_bus_build_sync_read_pos(request, sizeof(request), SERVO_IDS,
                                                        M_PAYLOAD_JOINTS);
    uart_write_bytes(SERVOS_UART_PORT, request, request_size);

    static uint8_t last_errors = 0;
    uint8_t rx[SERVO_RX_BUF_SIZE];
    size_t rx_len = 0;
    size_t want = M_PAYLOAD_JOINTS * SERVO_BUS_POS_STATUS_SIZE;  // no echo: exactly the answers
    int64_t deadline_us = esp_timer_get_time() + SERVO_READ_TIMEOUT_MS * 1000;

    while (rx_len < sizeof(rx)) {
        int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0) {
            break;
        }
        // Round up to whole ms, then add one tick: see the tick granularity note above.
        TickType_t wait = pdMS_TO_TICKS((uint32_t)((remaining_us + 999) / 1000)) + 1;

        size_t space = sizeof(rx) - rx_len;
        int n = uart_read_bytes(SERVOS_UART_PORT, rx + rx_len, want < space ? want : space, wait);
        if (n <= 0) {
            continue;  // nothing arrived in this wait: the deadline check ends the loop
        }
        rx_len += (size_t)n;

        uint8_t errors;
        if (servo_bus_parse_positions(rx, rx_len, SERVO_IDS, M_PAYLOAD_JOINTS, positions, &errors)) {
            if (errors != last_errors) {
                // Only log changes: at 50 Hz a persistent error would flood the console.
                ESP_LOGW(TAG, "Servo status error flags: 0x%02x", errors);
                last_errors = errors;
            }
            return true;
        }
        want = SERVO_BUS_POS_STATUS_SIZE;  // still missing answers (or an echo arrived first)
    }

    ESP_LOGW(TAG, "SYNC_READ: not every servo answered (%u bytes received)", (unsigned)rx_len);
    return false;
}

/**
 * @brief The bus transaction (DS1, DS14): writes the given targets (none = read only), then
 *        reads the 6 servos and publishes the positions in the mailbox.
 *
 * @param targets Servos to move (any subset), or NULL.
 * @param count   Number of targets (0 = read only).
 * @param mrad    Output: present angle of each joint in mrad, untouched on failure.
 * @return true if every servo answered the read.
 */
static bool transaction(const servo_target_t *targets, size_t count,
                        int32_t mrad[M_PAYLOAD_JOINTS])
{
    uart_flush_input(SERVOS_UART_PORT);  // drop stale bytes: only this transaction's answers count
    if (count > 0) {
        uint8_t packet[SERVO_BUS_SYNC_WRITE_POS_SIZE(M_PAYLOAD_JOINTS)];
        size_t packet_size = servo_bus_build_sync_write_pos(packet, sizeof(packet), targets, count);
        uart_write_bytes(SERVOS_UART_PORT, packet, packet_size);
    }

    int32_t counts[M_PAYLOAD_JOINTS];
    if (!read_servo_positions(counts)) {
        return false;
    }
    for (size_t j = 0; j < M_PAYLOAD_JOINTS; j++) {
        mrad[j] = counts_to_mrad(counts[j]);
        s_feedback.mrad[j] = mrad[j];
    }
    s_feedback.valid = true;
    xQueueOverwrite(s_mailbox, &s_feedback);  // never blocks: the UI peeks the newest
    return true;
}

/**
 * @brief Sends a frame to the PC (UART0). Only the bus task writes to UART0.
 *
 * @param cid         Command id.
 * @param payload     Payload bytes.
 * @param payload_len Number of payload bytes.
 */
static void send_frame(char cid, const uint8_t *payload, size_t payload_len)
{
    uint8_t frame[FRAME_MAX_SIZE];
    size_t frame_size = frame_build(frame, sizeof(frame), cid, payload, payload_len);
    uart_write_bytes(PC_UART_PORT, frame, frame_size);
}

/**
 * @brief Sends the M feedback frame with the present joint angles to the PC.
 *
 * @param mrad Present angle of each joint, mrad.
 */
static void send_m_feedback(const int32_t mrad[M_PAYLOAD_JOINTS])
{
    uint8_t payload[M_PAYLOAD_SIZE];
    size_t payload_len = m_payload_format(payload, sizeof(payload), mrad);
    send_frame('M', payload, payload_len);

    ESP_LOGI(TAG, "Feedback: mrad [%ld %ld %ld %ld %ld %ld]", (long)mrad[0], (long)mrad[1],
             (long)mrad[2], (long)mrad[3], (long)mrad[4], (long)mrad[5]);
}

/**
 * @brief Sends an E frame with a two-digit code to the PC (DS8).
 *
 * @param code Code, 0..99 (ARM_E_*).
 */
static void send_e(uint8_t code)
{
    uint8_t payload[E_PAYLOAD_SIZE] = {(uint8_t)('0' + code / 10 % 10), (uint8_t)('0' + code % 10)};
    send_frame('E', payload, sizeof(payload));
}

/**
 * @brief Logs a state change and tells the UI.
 *
 * @param why What changed it (for the log).
 */
static void report_state(const char *why)
{
    arm_state_id_t state = arm_state_can_move(&s_arm) ? ARM_OPERATING : ARM_STOPPED;
    ESP_LOGI(TAG, "%s: %s", why, state == ARM_OPERATING ? "OPERATING" : "STOPPED");
    notify(ARM_BUS_EV_STATE, 0, state);
}

/**
 * @brief Logs and reports the link watchdog stop.
 */
static void report_link_timeout(void)
{
    ESP_LOGW(TAG, "No frame from the PC for > %d ms (waiting for S)", ARM_BUS_LINK_TIMEOUT_MS);
    report_state("Link watchdog");
}

/**
 * @brief Registers a frame from the PC in the link watchdog.
 *
 * @param rx_us Time the frame arrived.
 */
static void link_frame(int64_t rx_us)
{
    if (arm_state_on_frame(&s_arm, rx_us)) {
        report_link_timeout();
    }
}

/**
 * @brief M: moves the 6 servos (only when OPERATING), reads them and answers the feedback.
 *
 * @param target Target of each joint, mrad.
 */
static void handle_m(const int32_t target[M_PAYLOAD_JOINTS])
{
    // Same joint order as robotic_arm_ros2_control.xacro (see SERVO_IDS).
    servo_target_t targets[M_PAYLOAD_JOINTS];
    for (size_t j = 0; j < M_PAYLOAD_JOINTS; j++) {
        targets[j] = (servo_target_t){
            .id = SERVO_IDS[j],
            .position = (uint16_t)mrad_to_counts(target[j]),  // always 0..4095
            .speed = CONFIG_ARM_SERVO_MOVE_SPEED,
            .acc = CONFIG_ARM_SERVO_MOVE_ACC,
        };
    }

    bool move = arm_state_can_move(&s_arm);
    ESP_LOGI(TAG, "M (%s): mrad [%ld %ld %ld %ld %ld %ld] -> counts [%u %u %u %u %u %u]",
             move ? "move" : "stopped, read only", (long)target[0], (long)target[1],
             (long)target[2], (long)target[3], (long)target[4], (long)target[5],
             targets[0].position, targets[1].position, targets[2].position, targets[3].position,
             targets[4].position, targets[5].position);

    // Request/response (R09): one M feedback per M command. No answer if the
    // servos did not reply; the PC detects the missing frame.
    int32_t present[M_PAYLOAD_JOINTS];
    if (transaction(targets, move ? M_PAYLOAD_JOINTS : 0, present)) {
        send_m_feedback(present);
    }
}

/**
 * @brief S (start, D2/D3/D5): reads the servos and answers M (accepted) or E (rejected).
 *
 * @param params What the param_init contained.
 * @param limits The limits, valid on ARM_LIMITS_OK.
 */
static void handle_s(arm_limits_parse_t params, const arm_limits_t *limits)
{
    static const arm_s_params_t PARAMS[] = {
        [ARM_LIMITS_EMPTY] = ARM_S_PARAMS_EMPTY,
        [ARM_LIMITS_OK] = ARM_S_PARAMS_LIMITS,
        [ARM_LIMITS_INVALID] = ARM_S_PARAMS_INVALID,
    };

    // Read for every S: the M answer and the transition are atomic (D3).
    int32_t present[M_PAYLOAD_JOINTS];
    bool answered = transaction(NULL, 0, present);
    arm_s_decision_t d = arm_state_on_start(&s_arm, PARAMS[params], answered);

    if (d.reply == ARM_S_REPLY_E) {
        ESP_LOGW(TAG, "S rejected: E %02u", d.e_code);
        send_e(d.e_code);
        return;
    }

    if (d.apply_limits) {
        s_feedback.limits = *limits;
        xQueueOverwrite(s_mailbox, &s_feedback);
        ESP_LOGI(TAG, "S: limits min [%ld %ld %ld %ld %ld %ld] max [%ld %ld %ld %ld %ld %ld] mrad",
                 (long)limits->min_mrad[0], (long)limits->min_mrad[1], (long)limits->min_mrad[2],
                 (long)limits->min_mrad[3], (long)limits->min_mrad[4], (long)limits->min_mrad[5],
                 (long)limits->max_mrad[0], (long)limits->max_mrad[1], (long)limits->max_mrad[2],
                 (long)limits->max_mrad[3], (long)limits->max_mrad[4], (long)limits->max_mrad[5]);
    } else {
        ESP_LOGW(TAG, "S without limits: keeping the current ones");
    }
    send_m_feedback(present);
    if (d.state_changed) {
        report_state("S accepted, M frames now move the servos");
    }
}

/**
 * @brief CTL request (DS4, DS14): reads the servos, then grants or denies.
 *
 * @param joint Joint the panel wants to move.
 */
static void handle_ctl_enter(unsigned joint)
{
    int32_t present[M_PAYLOAD_JOINTS];
    bool answered = transaction(NULL, 0, present);
    arm_ctl_result_t result = arm_state_ctl_request(&s_arm, joint, answered);
    if (result == ARM_CTL_GRANTED) {
        ESP_LOGI(TAG, "CTL granted on J%u (at %ld mrad)", joint + 1, (long)present[joint]);
        notify(ARM_BUS_EV_CTL_GRANTED, joint, present[joint]);
    } else {
        ESP_LOGW(TAG, "CTL denied on J%u (reason %d)", joint + 1, (int)result);
        notify(ARM_BUS_EV_CTL_DENIED, joint, result);
    }
}

/**
 * @brief CTL command: moves only the CTL joint (DS14), with the CTL speed (DS15), and reads.
 *
 * @param joint Joint.
 * @param mrad  Target, clamped to the current limits.
 */
static void handle_ctl_move(unsigned joint, int32_t mrad)
{
    if (!arm_state_ctl_can_move(&s_arm, joint)) {
        ESP_LOGW(TAG, "CTL command for J%u without CTL granted: ignored", joint + 1);
        return;
    }
    int32_t target = arm_limits_clamp(&s_feedback.limits, joint, mrad);
    const servo_target_t servo = {
        .id = SERVO_IDS[joint],
        .position = (uint16_t)mrad_to_counts(target),
        .speed = CONFIG_ARM_CTL_MOVE_SPEED,
        .acc = CONFIG_ARM_CTL_MOVE_ACC,
    };
    ESP_LOGI(TAG, "CTL J%u -> %ld mrad (%u counts)", joint + 1, (long)target, servo.position);
    int32_t present[M_PAYLOAD_JOINTS];
    transaction(&servo, 1, present);
}

/**
 * @brief Runs one request.
 *
 * @param req Request.
 */
static void handle(const arm_bus_request_t *req)
{
    switch (req->type) {
    case ARM_BUS_REQ_M:
        link_frame(req->rx_us);
        handle_m(req->m_mrad);
        break;
    case ARM_BUS_REQ_S:
        link_frame(req->rx_us);
        handle_s(req->s.params, &req->s.limits);
        break;
    case ARM_BUS_REQ_CTL_ENTER:
        handle_ctl_enter(req->ctl.joint);
        break;
    case ARM_BUS_REQ_CTL_EXIT:
        if (arm_state_ctl_active(&s_arm)) {
            ESP_LOGI(TAG, "CTL exit");
        }
        arm_state_ctl_exit(&s_arm);
        break;
    case ARM_BUS_REQ_CTL_MOVE:
        handle_ctl_move(req->ctl.joint, req->ctl.mrad);
        break;
    default:
        break;
    }
}

/**
 * @brief Bus task: one request at a time; the link watchdog on every round (DS3).
 *
 * @param arg Unused.
 */
static void bus_task(void *arg)
{
    (void)arg;

    while (1) {
        arm_bus_request_t req;
        if (xQueueReceive(s_queue, &req, pdMS_TO_TICKS(ARM_BUS_POLL_MS)) == pdTRUE) {
            handle(&req);
        }
        // Also without frames: if the plugin dies, STOPPED within ~timeout + poll period.
        if (arm_state_poll(&s_arm, esp_timer_get_time())) {
            report_link_timeout();
        }
    }
}

/**
 * @brief Creates the queue and the mailbox and starts the bus task.
 *
 * @param cb  Event callback, may be NULL.
 * @param ctx Passed to cb.
 * @return ESP_OK, or ESP_ERR_NO_MEM.
 */
esp_err_t arm_bus_start(arm_bus_event_cb_t cb, void *ctx)
{
    s_event_cb = cb;
    s_event_ctx = ctx;
    arm_state_init(&s_arm, ARM_BUS_LINK_TIMEOUT_MS * 1000LL, M_PAYLOAD_JOINTS);
    s_feedback.valid = false;
    arm_limits_set_default(&s_feedback.limits);

    s_queue = xQueueCreate(BUS_QUEUE_LEN, sizeof(arm_bus_request_t));
    s_mailbox = xQueueCreate(1, sizeof(arm_bus_feedback_t));
    if (s_queue == NULL || s_mailbox == NULL) {
        return ESP_ERR_NO_MEM;
    }
    xQueueOverwrite(s_mailbox, &s_feedback);  // the limits are known before any reading

    if (xTaskCreate(bus_task, "bus", BUS_TASK_STACK_SIZE, NULL, BUS_TASK_PRIORITY, NULL) !=
        pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/**
 * @brief Queues a request for the bus task.
 *
 * @param req  Request.
 * @param wait Max ticks to wait for room.
 * @return true if queued, false if dropped.
 */
bool arm_bus_send(const arm_bus_request_t *req, TickType_t wait)
{
    if (s_queue != NULL && xQueueSend(s_queue, req, wait) == pdTRUE) {
        return true;
    }
    ESP_LOGW(TAG, "Bus queue full: request type %d dropped", (int)req->type);
    return false;
}

/**
 * @brief Reads the newest feedback without taking it out of the mailbox.
 *
 * @param out Output.
 * @return true if a feedback was published.
 */
bool arm_bus_peek_feedback(arm_bus_feedback_t *out)
{
    return s_mailbox != NULL && xQueuePeek(s_mailbox, out, 0) == pdTRUE;
}
