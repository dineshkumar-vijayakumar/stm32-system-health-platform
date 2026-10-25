/**
 * @file    diag.h
 * @brief   Simplified UDS (Unified Diagnostic Services) responder for
 *          bare-metal STM32, exposing ReadDataByIdentifier and
 *          RoutineControl-style services.
 *
 * @details Architecture inspired by:
 *            1) Bosch's Eclipse S-CORE inc_diagnostics module
 *               (https://github.com/eclipse-score/inc_diagnostics), whose
 *               diag_api crate defines a SOVD/UDS-facing DataResource
 *               abstraction (read/write a diagnostic value with metadata)
 *               and a RoutineControl abstraction (start/stop a diagnostic
 *               routine). This module is the plain-C, bare-metal
 *               equivalent of that same conceptual split.
 *            2) The real ISO 14229 UDS standard service IDs, which Bosch's
 *               own job postings and codebase (STM32_AUTOSAR CAN stack,
 *               braking-system JDs mentioning "UDS diagnostics") reference
 *               directly. This module uses the real service ID values:
 *                 - 0x22 ReadDataByIdentifier
 *                 - 0x31 RoutineControl
 *               so that the request/response byte layout is genuinely
 *               UDS-shaped, not an invented protocol.
 *
 *          Internally, every diagnostic read/write and every routine
 *          start/stop is logged through diag_log (see diag_log.h) and
 *          every registration is watched over by diag_watchdog (see
 *          diag_watchdog.h) so a routine that never completes is
 *          detectable -- this ties all three Project 1 modules together
 *          into one "system health" platform, rather than three unrelated
 *          files.
 *
 * @note    This is a simplified, from-scratch, portfolio-scale UDS
 *          responder. It implements the request/response *shape* of two
 *          real UDS services well enough to demonstrate the protocol
 *          concept on a single MCU with no external tester tool required
 *          (a "fake tester" driver function is provided in the example).
 *          It is NOT a certified or complete UDS stack.
 */

#ifndef DIAG_H
#define DIAG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Real ISO 14229 UDS service identifiers
 * ============================================================ */

#define UDS_SID_READ_DATA_BY_IDENTIFIER   0x22U
#define UDS_SID_ROUTINE_CONTROL           0x31U

/** Negative response service ID prefix, per ISO 14229. */
#define UDS_NEGATIVE_RESPONSE             0x7FU

/* Routine control sub-functions, per ISO 14229. */
#define UDS_ROUTINE_START                 0x01U
#define UDS_ROUTINE_STOP                  0x02U
#define UDS_ROUTINE_REQUEST_RESULTS       0x03U

/* Common negative response codes (NRC), per ISO 14229. */
#define UDS_NRC_REQUEST_OUT_OF_RANGE      0x31U
#define UDS_NRC_CONDITIONS_NOT_CORRECT    0x22U
#define UDS_NRC_GENERAL_REJECT            0x10U

/* ============================================================
 * Status codes
 * ============================================================ */

typedef enum
{
    DIAGS_OK               = 0,
    DIAGS_E_NULL_PTR        = -1,
    DIAGS_E_TABLE_FULL      = -2,
    DIAGS_E_UNKNOWN_DID     = -3, /**< requested Data Identifier not registered */
    DIAGS_E_UNKNOWN_ROUTINE = -4,
    DIAGS_E_NOT_INITIALIZED = -5,
    DIAGS_E_BUFFER_TOO_SMALL = -6
} diags_status_t;

/* ============================================================
 * Data Identifier (DID) registration -- ReadDataByIdentifier
 *
 * Mirrors inc_diagnostics' sovd::DataResource + DataResourceMetadata:
 * a DID has a 16-bit identifier (as in real UDS), a human-readable name,
 * a read-only flag, and a function pointer supplying the current value.
 * ============================================================ */

/**
 * @brief Function pointer type for a DID's data source.
 * @param[out] out_value  Caller-provided buffer to fill with the current value.
 * @param[in]  max_len    Size of out_value in bytes.
 * @param[out] out_len    Actual number of bytes written.
 * @return true on success, false if the value could not be read right now.
 */
typedef bool (*diag_did_read_fn)(uint8_t* out_value, uint8_t max_len, uint8_t* out_len);

typedef struct
{
    uint16_t did;              /**< Data Identifier, e.g. 0xF190 (real UDS VIN DID) */
    const char* name;          /**< human-readable name, for logging */
    diag_did_read_fn read_fn;  /**< supplies the current value on demand */
} diag_did_descriptor_t;

/* ============================================================
 * Routine registration -- RoutineControl
 *
 * Mirrors inc_diagnostics' uds::RoutineControl trait: start()/stop()
 * with an optional result payload.
 * ============================================================ */

typedef bool (*diag_routine_start_fn)(void);
typedef bool (*diag_routine_stop_fn)(void);

typedef struct
{
    uint16_t routine_id;               /**< e.g. 0x0203 (vendor-specific range) */
    const char* name;
    diag_routine_start_fn start_fn;
    diag_routine_stop_fn stop_fn;
    /** Max time (ms) this routine is allowed to run before diag_watchdog
     *  flags it as faulted -- ties this module to diag_watchdog.h. */
    uint32_t max_runtime_ms;
} diag_routine_descriptor_t;

/* ============================================================
 * Public API
 * ============================================================ */

/** Initializes the diagnostic service module. Call once at startup,
 *  after diag_log_init() and diag_watchdog_init(). */
diags_status_t diag_init(void);

/** Registers a Data Identifier that ReadDataByIdentifier can serve. */
diags_status_t diag_register_did(diag_did_descriptor_t descriptor);

/** Registers a Routine that RoutineControl can start/stop. */
diags_status_t diag_register_routine(diag_routine_descriptor_t descriptor);

/**
 * @brief Processes a raw UDS-style request and produces a response.
 *
 * Request layout (ReadDataByIdentifier):
 *   [0]    = 0x22 (SID)
 *   [1..2] = DID, big-endian
 *
 * Response layout (positive):
 *   [0]    = 0x62 (SID + 0x40, per UDS convention)
 *   [1..2] = DID, big-endian
 *   [3..]  = data value
 *
 * Request layout (RoutineControl):
 *   [0]    = 0x31 (SID)
 *   [1]    = sub-function (0x01 start / 0x02 stop / 0x03 request results)
 *   [2..3] = Routine ID, big-endian
 *
 * Response layout (positive):
 *   [0]    = 0x71 (SID + 0x40)
 *   [1]    = echoed sub-function
 *   [2..3] = echoed Routine ID
 *
 * Negative response layout (both services):
 *   [0]    = 0x7F
 *   [1]    = echoed SID
 *   [2]    = negative response code (NRC)
 *
 * @param[in]  request       Raw request bytes.
 * @param[in]  request_len   Length of request.
 * @param[out] response      Caller-provided output buffer.
 * @param[in]  response_max  Size of response buffer.
 * @param[out] response_len  Actual number of bytes written to response.
 *
 * @retval DIAGS_OK  A response (positive or negative) was produced.
 *                    Check response[0] to distinguish which.
 * @retval DIAGS_E_BUFFER_TOO_SMALL response_max was insufficient.
 * @retval DIAGS_E_NOT_INITIALIZED diag_init() was not called.
 */
diags_status_t diag_process_request(const uint8_t* request, uint8_t request_len,
                                     uint8_t* response, uint8_t response_max,
                                     uint8_t* response_len);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_H */
