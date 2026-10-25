/**
 * @file    example_main.c
 * @brief   Demonstrates diag_log + diag_watchdog + diag together as one
 *          integrated "system health" platform.
 *
 * @details This file shows the intended usage pattern end-to-end:
 *            1. Initialize all three modules (order matters: diag depends
 *               on diag_log being ready first).
 *            2. Register a watchdog task for a periodic "sensor read" job.
 *            3. Register a UDS Data Identifier that reports a live sensor
 *               reading, and a UDS Routine that runs a self-test.
 *            4. Simulate a tester tool sending UDS requests and print the
 *               responses -- exactly the request/response bytes a real UDS
 *               tester (e.g. over CAN) would exchange with this ECU.
 *
 *          On real STM32 hardware, replace:
 *            - the simulated `now_ms` counter with HAL_GetTick()
 *            - the printf() calls with HAL_UART_Transmit()
 *            - the fake ADC read with an actual HAL_ADC_GetValue() call
 *          diag_log.h's flush hook (in diag_log_stop) is the correct place
 *          to wire in the real UART transmit call.
 *
 * Compile (host-side demo):
 *     gcc -o example_main example_main.c diag_log.c diag_watchdog.c diag.c
 *     ./example_main
 */

#include <stdio.h>
#include "diag_log.h"
#include "diag_watchdog.h"
#include "diag.h"

/* ------------------------------------------------------------
 * Simulated sensor + watchdog task
 * ------------------------------------------------------------ */

static wdg_task_handle_t s_sensor_task_handle;
static uint16_t s_simulated_adc_value = 2048U; /* pretend 12-bit ADC reading */

/** Simulated periodic task: reads the "sensor" and pets its watchdog. */
static void sensor_task_cycle(uint32_t now_ms)
{
    /* pretend to read hardware */
    s_simulated_adc_value = (uint16_t)((s_simulated_adc_value + 17U) % 4096U);

    diag_watchdog_pet(s_sensor_task_handle, now_ms);

    diag_slot_handle_t slot;
    if (diag_log_start(LOG_DEBUG, "SNS0", &slot) == DIAG_OK)
    {
        log_value_t v;
        v.type = LOG_VAL_U16;
        v.as.u16 = s_simulated_adc_value;
        diag_log_write(slot, v);
        diag_log_stop(slot);
    }
}

/* ------------------------------------------------------------
 * UDS DID: report the current sensor value
 * ------------------------------------------------------------ */

static bool read_sensor_did(uint8_t* out_value, uint8_t max_len, uint8_t* out_len)
{
    if (max_len < 2U)
    {
        return false;
    }
    out_value[0] = (uint8_t)(s_simulated_adc_value >> 8);
    out_value[1] = (uint8_t)(s_simulated_adc_value & 0xFFU);
    *out_len = 2U;
    return true;
}

/* ------------------------------------------------------------
 * UDS Routine: a trivial "self test"
 * ------------------------------------------------------------ */

static bool self_test_start(void)
{
    diag_slot_handle_t slot;
    if (diag_log_start(LOG_INFO, "DIAG", &slot) == DIAG_OK)
    {
        log_value_t v;
        v.type = LOG_VAL_STRING;
        v.as.str = "self-test routine started";
        diag_log_write(slot, v);
        diag_log_stop(slot);
    }
    return true; /* pretend the self-test always passes */
}

static bool self_test_stop(void)
{
    diag_slot_handle_t slot;
    if (diag_log_start(LOG_INFO, "DIAG", &slot) == DIAG_OK)
    {
        log_value_t v;
        v.type = LOG_VAL_STRING;
        v.as.str = "self-test routine stopped";
        diag_log_write(slot, v);
        diag_log_stop(slot);
    }
    return true;
}

/* ------------------------------------------------------------
 * Helper: print a byte buffer as hex, the way a real UDS tester log would
 * ------------------------------------------------------------ */

static void print_bytes(const char* label, const uint8_t* buf, uint8_t len)
{
    printf("%s [", label);
    for (uint8_t i = 0U; i < len; i++)
    {
        printf("%02X%s", buf[i], (i + 1U < len) ? " " : "");
    }
    printf("]\n");
}

/* ============================================================
 * main
 * ============================================================ */

int main(void)
{
    /* --- Init order matters: diag_log first, then diag_watchdog,
     * then diag (which logs through diag_log internally). --- */
    diag_log_init(LOG_DEBUG);
    diag_watchdog_init();
    diag_init();

    /* Register the sensor task with the watchdog: expects a pet every
     * 100ms, tolerates 2 consecutive misses before declaring fault. */
    diag_watchdog_register(100U, 2U, &s_sensor_task_handle);

    /* Register the UDS-facing Data Identifier for the sensor. */
    diag_did_descriptor_t sensor_did;
    sensor_did.did = 0x1234U;
    sensor_did.name = "SENSOR0_RAW";
    sensor_did.read_fn = read_sensor_did;
    diag_register_did(sensor_did);

    /* Register the UDS-facing self-test Routine. */
    diag_routine_descriptor_t self_test;
    self_test.routine_id = 0x0203U;
    self_test.name = "SELF_TEST";
    self_test.start_fn = self_test_start;
    self_test.stop_fn = self_test_stop;
    self_test.max_runtime_ms = 500U;
    diag_register_routine(self_test);

    printf("=== System Health Platform demo ===\n\n");

    /* --- Simulate 5 normal task cycles, 50ms apart --- */
    uint32_t now_ms = 0U;
    for (int i = 0; i < 5; i++)
    {
        sensor_task_cycle(now_ms);
        diag_watchdog_tick(now_ms);
        now_ms += 50U;
    }
    printf("After 5 healthy cycles: watchdog state = %s\n\n",
           (diag_watchdog_get_state(s_sensor_task_handle) == WDG_STATE_ACTIVE) ? "ACTIVE" : "FAULT");

    /* --- Simulate a tester tool sending a UDS ReadDataByIdentifier request --- */
    {
        uint8_t request[3] = { UDS_SID_READ_DATA_BY_IDENTIFIER, 0x12U, 0x34U };
        uint8_t response[16];
        uint8_t response_len = 0U;

        diag_process_request(request, 3U, response, sizeof(response), &response_len);

        print_bytes("Tester -> ECU (RDBI request) ", request, 3U);
        print_bytes("ECU -> Tester (RDBI response)", response, response_len);
        printf("\n");
    }

    /* --- Simulate a tester tool starting the self-test routine --- */
    {
        uint8_t request[4] = { UDS_SID_ROUTINE_CONTROL, UDS_ROUTINE_START, 0x02U, 0x03U };
        uint8_t response[16];
        uint8_t response_len = 0U;

        diag_process_request(request, 4U, response, sizeof(response), &response_len);

        print_bytes("Tester -> ECU (RoutineControl start request) ", request, 4U);
        print_bytes("ECU -> Tester (RoutineControl start response)", response, response_len);
        printf("\n");
    }

    /* --- Simulate the sensor task hanging: stop petting the watchdog --- */
    printf("Simulating sensor task hang (no more heartbeats)...\n");
    for (int i = 0; i < 4; i++)
    {
        diag_watchdog_tick(now_ms); /* no diag_watchdog_pet() call this time */
        now_ms += 100U;
    }
    printf("After hang: watchdog state = %s, miss_count = %u\n\n",
           (diag_watchdog_get_state(s_sensor_task_handle) == WDG_STATE_FAULT) ? "FAULT" : "ACTIVE",
           diag_watchdog_get_miss_count(s_sensor_task_handle));

    /* --- Simulate a tester requesting an unknown DID, to show the
     * negative-response path --- */
    {
        uint8_t request[3] = { UDS_SID_READ_DATA_BY_IDENTIFIER, 0x99U, 0x99U };
        uint8_t response[16];
        uint8_t response_len = 0U;

        diag_process_request(request, 3U, response, sizeof(response), &response_len);

        print_bytes("Tester -> ECU (RDBI, unknown DID)", request, 3U);
        print_bytes("ECU -> Tester (negative response) ", response, response_len);
    }

    return 0;
}
