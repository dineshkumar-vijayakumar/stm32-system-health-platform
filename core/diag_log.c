/**
 * @file    diag_log.c
 * @brief   Implementation of the lightweight diagnostic logging module.
 * @see     diag_log.h for full design rationale and citations to the
 *          Bosch Eclipse S-CORE mw::log patterns this module is inspired by.
 */

#include "diag_log.h"
#include <string.h>

#ifdef USE_HAL_DRIVER
/* Target build (STM32CubeIDE defines USE_HAL_DRIVER): flush goes out USART2. */
#include "main.h"
extern UART_HandleTypeDef huart2;
#endif

/* ============================================================
 * Internal slot storage
 *
 * Fixed-size, statically-allocated array -> no malloc/free anywhere
 * in this file. Mirrors mw::log's CircularBuffer-based slot pool
 * (score/mw/log/README.md: "numberOfSlots -- default value: 8 ...
 * based on CircularBuffer with the given amount of slots").
 * ============================================================ */

typedef struct
{
    bool     in_use;                          /* true if reserved by diag_log_start() */
    uint8_t  write_offset;                    /* current write position within buffer */
    uint8_t  buffer[DIAG_LOG_SLOT_SIZE];       /* raw formatted payload, filled by diag_log_write() */
    log_level_t level;
    char     context[5];                      /* 4 chars + null terminator */
} log_slot_t;

static log_slot_t s_slots[DIAG_LOG_NUM_SLOTS];
static log_level_t s_min_level = LOG_OFF;
static bool s_initialized = false;

/* ============================================================
 * Internal helpers
 * ============================================================ */

static diag_status_t append_bytes(log_slot_t* slot, const uint8_t* data, uint8_t len)
{
    if ((uint16_t)slot->write_offset + (uint16_t)len > DIAG_LOG_SLOT_SIZE)
    {
        return DIAG_E_PAYLOAD_TOO_LARGE;
    }
    memcpy(&slot->buffer[slot->write_offset], data, len);
    slot->write_offset = (uint8_t)(slot->write_offset + len);
    return DIAG_OK;
}

static diag_status_t append_string(log_slot_t* slot, const char* str)
{
    size_t len = strlen(str);
    if (len > 0xFFU)
    {
        len = 0xFFU; /* truncate defensively; never overrun the slot */
    }
    return append_bytes(slot, (const uint8_t*)str, (uint8_t)len);
}

/* ============================================================
 * Public API implementation
 * ============================================================ */

diag_status_t diag_log_init(log_level_t min_level)
{
    if (s_initialized)
    {
        return DIAG_E_ALREADY_INIT;
    }

    for (uint8_t i = 0U; i < DIAG_LOG_NUM_SLOTS; i++)
    {
        s_slots[i].in_use = false;
        s_slots[i].write_offset = 0U;
        s_slots[i].level = LOG_OFF;
        s_slots[i].context[0] = '\0';
    }

    s_min_level = min_level;
    s_initialized = true;

    return DIAG_OK;
}

bool diag_log_is_enabled(log_level_t level)
{
    /* LOG_OFF (0x00) never matches; higher enum value = more verbose,
     * mirrors score::mw::log::LogLevel ordering exactly. */
    if (!s_initialized)
    {
        return false;
    }
    if (s_min_level == LOG_OFF)
    {
        return false;
    }
    return (level <= s_min_level) && (level != LOG_OFF);
}

diag_status_t diag_log_start(log_level_t level,
                              const char* context,
                              diag_slot_handle_t* out_slot)
{
    if (!s_initialized)
    {
        return DIAG_E_NOT_INITIALIZED;
    }
    if (out_slot == NULL)
    {
        return DIAG_E_NULL_PTR;
    }

    /* Filter BEFORE reserving a slot -- mirrors mw::log's FileRecorder::
     * StartRecord() early-exit ("if (IsLogEnabled(...) == false) return {};")
     * so a filtered-out message never costs a slot. */
    if (!diag_log_is_enabled(level))
    {
        out_slot->valid = false;
        out_slot->index = 0U;
        return DIAG_E_LOG_DISABLED;
    }

    for (uint8_t i = 0U; i < DIAG_LOG_NUM_SLOTS; i++)
    {
        if (!s_slots[i].in_use)
        {
            s_slots[i].in_use = true;
            s_slots[i].write_offset = 0U;
            s_slots[i].level = level;

            const char* ctx = (context != NULL) ? context : DIAG_LOG_DEFAULT_CONTEXT;
            size_t n = strlen(ctx);
            if (n > 4U)
            {
                n = 4U;
            }
            memcpy(s_slots[i].context, ctx, n);
            s_slots[i].context[n] = '\0';

            out_slot->index = i;
            out_slot->valid = true;
            return DIAG_OK;
        }
    }

    /* All slots busy. mw::log's documented behaviour in this situation
     * (see README "overwriteOnFull") is configurable; here we choose the
     * simpler "drop new message" strategy, which is explicitly named as
     * a valid, supported strategy in mw::log's own configuration docs. */
    out_slot->valid = false;
    out_slot->index = 0U;
    return DIAG_E_NO_FREE_SLOT;
}

static bool slot_is_valid_and_active(diag_slot_handle_t slot)
{
    return slot.valid
        && (slot.index < DIAG_LOG_NUM_SLOTS)
        && s_slots[slot.index].in_use;
}

diag_status_t diag_log_write(diag_slot_handle_t slot, log_value_t value)
{
    if (!slot_is_valid_and_active(slot))
    {
        return DIAG_E_INVALID_SLOT;
    }

    log_slot_t* s = &s_slots[slot.index];

    /* Type-tagged dispatch -- the C equivalent of mw::log's overloaded
     * Log(bool)/Log(uint8_t)/Log(float)/... functions (file_recorder.cpp),
     * since C has no function overloading. */
    switch (value.type)
    {
        case LOG_VAL_U8:
            return append_bytes(s, &value.as.u8, sizeof(value.as.u8));
        case LOG_VAL_I8:
            return append_bytes(s, (const uint8_t*)&value.as.i8, sizeof(value.as.i8));
        case LOG_VAL_U16:
            return append_bytes(s, (const uint8_t*)&value.as.u16, sizeof(value.as.u16));
        case LOG_VAL_I16:
            return append_bytes(s, (const uint8_t*)&value.as.i16, sizeof(value.as.i16));
        case LOG_VAL_U32:
            return append_bytes(s, (const uint8_t*)&value.as.u32, sizeof(value.as.u32));
        case LOG_VAL_I32:
            return append_bytes(s, (const uint8_t*)&value.as.i32, sizeof(value.as.i32));
        case LOG_VAL_FLOAT:
            return append_bytes(s, (const uint8_t*)&value.as.f32, sizeof(value.as.f32));
        case LOG_VAL_STRING:
            return append_string(s, value.as.str);
        case LOG_VAL_HEX32:
            return append_bytes(s, (const uint8_t*)&value.as.u32, sizeof(value.as.u32));
        default:
            return DIAG_E_PAYLOAD_TOO_LARGE; /* unknown tag, reject defensively */
    }
}

diag_status_t diag_log_stop(diag_slot_handle_t slot)
{
    if (!slot_is_valid_and_active(slot))
    {
        return DIAG_E_INVALID_SLOT;
    }

    log_slot_t* s = &s_slots[slot.index];

    /* --- Actual "flush" step ---
     * Target build: send the buffered record out USART2 (ST-Link VCP).
     * Host build (no USE_HAL_DRIVER): no-op, so the module stays testable. */
#ifdef USE_HAL_DRIVER
    (void)HAL_UART_Transmit(&huart2, s->buffer, s->write_offset, 100U);
#endif

    s->in_use = false;
    s->write_offset = 0U;

    return DIAG_OK;
}
