/**
 * @file    diag_log.h
 * @brief   Lightweight diagnostic logging module for STM32 embedded systems.
 *
 * @details Architecture inspired by Bosch's Eclipse S-CORE mw::log module
 *          (https://github.com/eclipse-score/baselibs/tree/main/score/mw/log)
 *          and Bosch Sensortec driver API conventions
 *          (https://github.com/boschsensortec/BME280_SensorAPI).
 *
 *          Key design decisions adopted from mw::log's real implementation:
 *            - Fixed-size, statically-allocated slot pool (no malloc/free in
 *              the logging hot path) -> mirrors mw::log's default
 *              numberOfSlots = 8, deterministic-memory-management motivation.
 *            - Three-phase logging API: reserve -> write -> flush
 *              (diag_log_start / diag_log_write / diag_log_stop)
 *              -> mirrors mw::log's StartRecord/Log/StopRecord pattern seen
 *              in score/mw/log/detail/file_recorder/file_recorder.cpp.
 *            - Log-level check performed BEFORE reserving a slot, to avoid
 *              wasting a slot on a message that will be filtered anyway.
 *            - LogLevel enum values match score::mw::log::LogLevel exactly
 *              (see score/mw/log/README.md), for direct conceptual mapping.
 *            - Every function returns an explicit status code instead of
 *              using exceptions/aborts -> mirrors mw::log's `noexcept`
 *              discipline and the Result<T>-style error handling seen in
 *              score/mw/com/runtime.cpp (ResolveInstanceIDs).
 *            - Tagged-union value type (LogValue) is the C equivalent of
 *              mw::log's type-safe overloaded Log(bool/int8_t/.../float...)
 *              functions -- C has no overloading, so a discriminated union
 *              plus an enum tag achieves the same type-safety intent.
 *
 * @note    This is an educational/portfolio project. It is NOT a copy of
 *          Bosch's code -- no Bosch source lines are reproduced here. Only
 *          architectural patterns and naming conventions are adapted for a
 *          bare-metal STM32 target (no OS, no dynamic memory, no C++).
 */

#ifndef DIAG_LOG_H
#define DIAG_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Configuration constants
 * (values chosen to mirror mw::log's documented defaults,
 *  scaled down for STM32 RAM budget)
 * ============================================================ */

/** Number of pre-allocated log slots (mw::log default numberOfSlots = 8) */
#define DIAG_LOG_NUM_SLOTS      8U

/** Max length of a single log message payload, in bytes.
 *  (mw::log default slotSizeBytes = 2048; scaled down here for STM32 SRAM) */
#define DIAG_LOG_SLOT_SIZE      64U

/** Default context identifier, mirrors mw::log's "DFLT" 4-char default */
#define DIAG_LOG_DEFAULT_CONTEXT "DFLT"

/* ============================================================
 * Status codes (Result<T>-style return, no exceptions)
 * ============================================================ */

typedef enum
{
    DIAG_OK                = 0,  /**< Success */
    DIAG_E_NULL_PTR         = -1, /**< A required pointer argument was NULL */
    DIAG_E_NO_FREE_SLOT     = -2, /**< Slot pool exhausted (all 8 slots busy) */
    DIAG_E_LOG_DISABLED     = -3, /**< Message filtered out by log-level threshold */
    DIAG_E_INVALID_SLOT     = -4, /**< Slot handle is out of range or already released */
    DIAG_E_NOT_INITIALIZED  = -5, /**< diag_log_init() was not called before use */
    DIAG_E_ALREADY_INIT     = -6, /**< diag_log_init() called more than once */
    DIAG_E_PAYLOAD_TOO_LARGE = -7 /**< Value would overflow the slot's fixed buffer */
} diag_status_t;

/* ============================================================
 * Log levels
 * Values intentionally match score::mw::log::LogLevel exactly,
 * see score/mw/log/README.md "Usage" section.
 * ============================================================ */

typedef enum
{
    LOG_OFF     = 0x00,
    LOG_FATAL   = 0x01,
    LOG_ERROR   = 0x02,
    LOG_WARN    = 0x03,
    LOG_INFO    = 0x04,
    LOG_DEBUG   = 0x05,
    LOG_VERBOSE = 0x06
} log_level_t;

/* ============================================================
 * Tagged-union value type
 * C equivalent of mw::log's overloaded Log(bool/int8_t/uint32_t/
 * float/string_view/LogHex32/...) functions.
 * ============================================================ */

typedef enum
{
    LOG_VAL_U8,
    LOG_VAL_I8,
    LOG_VAL_U16,
    LOG_VAL_I16,
    LOG_VAL_U32,
    LOG_VAL_I32,
    LOG_VAL_FLOAT,
    LOG_VAL_STRING,
    LOG_VAL_HEX32   /* logged in 0xXXXXXXXX form, mirrors mw::log LogHex32 */
} log_value_type_t;

typedef struct
{
    log_value_type_t type;
    union
    {
        uint8_t  u8;
        int8_t   i8;
        uint16_t u16;
        int16_t  i16;
        uint32_t u32;
        int32_t  i32;
        float    f32;
        const char* str; /* must remain valid until diag_log_stop() is called */
    } as;
} log_value_t;

/* ============================================================
 * Opaque slot handle
 * Mirrors mw::log's SlotHandle (score/mw/log/slot_handle.h) --
 * a lightweight, copyable reference to a reserved slot.
 * ============================================================ */

typedef struct
{
    uint8_t index;    /**< index into the internal slot pool */
    bool    valid;    /**< false if this handle does not refer to a reserved slot */
} diag_slot_handle_t;

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * @brief Initializes the diagnostic logging subsystem.
 *
 * Must be called exactly once before any other diag_log_* function.
 * Mirrors the init-once discipline seen in score::mw::com::runtime::
 * InitializeRuntime() (score/mw/com/runtime.cpp): "This function shall
 * only be called ONCE per application/process lifetime!"
 *
 * @param[in] min_level  Minimum severity that will be recorded; anything
 *                        below this threshold is filtered at StartRecord
 *                        time, before a slot is ever reserved.
 *
 * @retval DIAG_OK             Initialization succeeded.
 * @retval DIAG_E_ALREADY_INIT init was already called; no effect.
 */
diag_status_t diag_log_init(log_level_t min_level);

/**
 * @brief Checks whether a given severity level would currently be recorded.
 *
 * Mirrors mw::log's FileRecorder::IsLogEnabled(), called internally by
 * diag_log_start() before reserving a slot, so callers never pay the cost
 * of preparing a message that will be discarded.
 */
bool diag_log_is_enabled(log_level_t level);

/**
 * @brief Reserves a log slot and begins a new log record.
 *
 * Three-phase API step 1 of 3 (start -> write -> stop), mirroring
 * mw::log's StartRecord/Log/StopRecord split in file_recorder.cpp.
 *
 * @param[in]  level     Severity of this log entry.
 * @param[in]  context   4-char context identifier (e.g. "ADC0"). Pass
 *                        DIAG_LOG_DEFAULT_CONTEXT if none is relevant.
 * @param[out] out_slot  Receives the handle to the reserved slot on success.
 *
 * @retval DIAG_OK                Slot reserved, ready for diag_log_write().
 * @retval DIAG_E_LOG_DISABLED    level is below the configured threshold;
 *                                 no slot was reserved (by design, mirrors
 *                                 mw::log's early-exit optimization).
 * @retval DIAG_E_NO_FREE_SLOT    All DIAG_LOG_NUM_SLOTS slots are busy.
 * @retval DIAG_E_NOT_INITIALIZED diag_log_init() was never called.
 * @retval DIAG_E_NULL_PTR        out_slot was NULL.
 */
diag_status_t diag_log_start(log_level_t level,
                              const char* context,
                              diag_slot_handle_t* out_slot);

/**
 * @brief Appends one typed value to an in-progress log record.
 *
 * Three-phase API step 2 of 3. May be called multiple times per slot to
 * build up a multi-field log entry (e.g. timestamp + sensor_id + value),
 * mirroring mw::log's ability to stream multiple Log() calls into one
 * DLT-formatted message before StopRecord() flushes it.
 *
 * @param[in] slot   Handle returned by diag_log_start().
 * @param[in] value  Tagged-union value to append.
 *
 * @retval DIAG_OK                  Value appended successfully.
 * @retval DIAG_E_INVALID_SLOT      slot is not a currently-reserved handle.
 * @retval DIAG_E_PAYLOAD_TOO_LARGE Appending value would exceed
 *                                   DIAG_LOG_SLOT_SIZE for this slot.
 */
diag_status_t diag_log_write(diag_slot_handle_t slot, log_value_t value);

/**
 * @brief Flushes and releases a log record's slot back to the pool.
 *
 * Three-phase API step 3 of 3, mirrors mw::log's StopRecord(), which
 * calls backend_->FlushSlot(slot). After this call, `slot` is invalid
 * and must not be reused.
 *
 * @retval DIAG_OK              Slot flushed and released.
 * @retval DIAG_E_INVALID_SLOT  slot was already released or never valid.
 */
diag_status_t diag_log_stop(diag_slot_handle_t slot);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_LOG_H */
