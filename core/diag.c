/**
 * @file    diag.c
 * @brief   Implementation of diag.h.
 * @see     diag.h for full design rationale and citations.
 */

#include "diag.h"
#include "diag_log.h"
#include <stddef.h>

#define DIAG_MAX_DIDS      8U
#define DIAG_MAX_ROUTINES  4U

static diag_did_descriptor_t s_dids[DIAG_MAX_DIDS];
static uint8_t s_did_count = 0U;

static diag_routine_descriptor_t s_routines[DIAG_MAX_ROUTINES];
static uint8_t s_routine_count = 0U;

static bool s_initialized = false;

diags_status_t diag_init(void)
{
    s_did_count = 0U;
    s_routine_count = 0U;
    s_initialized = true;
    return DIAGS_OK;
}

diags_status_t diag_register_did(diag_did_descriptor_t descriptor)
{
    if (!s_initialized)
    {
        return DIAGS_E_NOT_INITIALIZED;
    }
    if (s_did_count >= DIAG_MAX_DIDS)
    {
        return DIAGS_E_TABLE_FULL;
    }
    s_dids[s_did_count] = descriptor;
    s_did_count++;
    return DIAGS_OK;
}

diags_status_t diag_register_routine(diag_routine_descriptor_t descriptor)
{
    if (!s_initialized)
    {
        return DIAGS_E_NOT_INITIALIZED;
    }
    if (s_routine_count >= DIAG_MAX_ROUTINES)
    {
        return DIAGS_E_TABLE_FULL;
    }
    s_routines[s_routine_count] = descriptor;
    s_routine_count++;
    return DIAGS_OK;
}

static const diag_did_descriptor_t* find_did(uint16_t did)
{
    for (uint8_t i = 0U; i < s_did_count; i++)
    {
        if (s_dids[i].did == did)
        {
            return &s_dids[i];
        }
    }
    return NULL;
}

static const diag_routine_descriptor_t* find_routine(uint16_t routine_id)
{
    for (uint8_t i = 0U; i < s_routine_count; i++)
    {
        if (s_routines[i].routine_id == routine_id)
        {
            return &s_routines[i];
        }
    }
    return NULL;
}

static void write_negative_response(uint8_t sid, uint8_t nrc,
                                     uint8_t* response, uint8_t* response_len)
{
    response[0] = UDS_NEGATIVE_RESPONSE;
    response[1] = sid;
    response[2] = nrc;
    *response_len = 3U;
}

/* Logs an error-level entry via diag_log for any diagnostic failure,
 * tying this module to Project 1's logging component. */
static void log_diag_error(const char* context, const char* message)
{
    diag_slot_handle_t slot;
    if (diag_log_start(LOG_ERROR, context, &slot) == DIAG_OK)
    {
        log_value_t v;
        v.type = LOG_VAL_STRING;
        v.as.str = message;
        diag_log_write(slot, v);
        diag_log_stop(slot);
    }
}

static diags_status_t handle_read_data_by_identifier(const uint8_t* request, uint8_t request_len,
                                                       uint8_t* response, uint8_t response_max,
                                                       uint8_t* response_len)
{
    if (request_len < 3U)
    {
        write_negative_response(UDS_SID_READ_DATA_BY_IDENTIFIER,
                                 UDS_NRC_REQUEST_OUT_OF_RANGE, response, response_len);
        return DIAGS_OK;
    }

    uint16_t did = ((uint16_t)request[1] << 8) | (uint16_t)request[2];
    const diag_did_descriptor_t* d = find_did(did);

    if (d == NULL)
    {
        log_diag_error("DIAG", "RDBI: unknown DID requested");
        write_negative_response(UDS_SID_READ_DATA_BY_IDENTIFIER,
                                 UDS_NRC_REQUEST_OUT_OF_RANGE, response, response_len);
        return DIAGS_OK;
    }

    if (response_max < 4U)
    {
        return DIAGS_E_BUFFER_TOO_SMALL;
    }

    uint8_t value_len = 0U;
    bool ok = d->read_fn(&response[3], (uint8_t)(response_max - 3U), &value_len);

    if (!ok)
    {
        log_diag_error("DIAG", "RDBI: read_fn reported failure");
        write_negative_response(UDS_SID_READ_DATA_BY_IDENTIFIER,
                                 UDS_NRC_CONDITIONS_NOT_CORRECT, response, response_len);
        return DIAGS_OK;
    }

    response[0] = UDS_SID_READ_DATA_BY_IDENTIFIER + 0x40U; /* positive response SID */
    response[1] = request[1];
    response[2] = request[2];
    *response_len = (uint8_t)(3U + value_len);

    return DIAGS_OK;
}

static diags_status_t handle_routine_control(const uint8_t* request, uint8_t request_len,
                                              uint8_t* response, uint8_t response_max,
                                              uint8_t* response_len)
{
    if (request_len < 4U)
    {
        write_negative_response(UDS_SID_ROUTINE_CONTROL,
                                 UDS_NRC_REQUEST_OUT_OF_RANGE, response, response_len);
        return DIAGS_OK;
    }

    uint8_t sub_function = request[1];
    uint16_t routine_id = ((uint16_t)request[2] << 8) | (uint16_t)request[3];

    const diag_routine_descriptor_t* r = find_routine(routine_id);
    if (r == NULL)
    {
        log_diag_error("DIAG", "RC: unknown routine ID requested");
        write_negative_response(UDS_SID_ROUTINE_CONTROL,
                                 UDS_NRC_REQUEST_OUT_OF_RANGE, response, response_len);
        return DIAGS_OK;
    }

    bool ok = false;
    switch (sub_function)
    {
        case UDS_ROUTINE_START:
            ok = r->start_fn();
            break;
        case UDS_ROUTINE_STOP:
            ok = (r->stop_fn != NULL) ? r->stop_fn() : true;
            break;
        case UDS_ROUTINE_REQUEST_RESULTS:
            /* This simplified responder reports "still OK" for results
             * requests; a full implementation would return routine-specific
             * result payload here. */
            ok = true;
            break;
        default:
            write_negative_response(UDS_SID_ROUTINE_CONTROL,
                                     UDS_NRC_REQUEST_OUT_OF_RANGE, response, response_len);
            return DIAGS_OK;
    }

    if (!ok)
    {
        log_diag_error("DIAG", "RC: routine start/stop reported failure");
        write_negative_response(UDS_SID_ROUTINE_CONTROL,
                                 UDS_NRC_CONDITIONS_NOT_CORRECT, response, response_len);
        return DIAGS_OK;
    }

    if (response_max < 4U)
    {
        return DIAGS_E_BUFFER_TOO_SMALL;
    }

    response[0] = UDS_SID_ROUTINE_CONTROL + 0x40U;
    response[1] = sub_function;
    response[2] = request[2];
    response[3] = request[3];
    *response_len = 4U;

    return DIAGS_OK;
}

diags_status_t diag_process_request(const uint8_t* request, uint8_t request_len,
                                     uint8_t* response, uint8_t response_max,
                                     uint8_t* response_len)
{
    if (!s_initialized)
    {
        return DIAGS_E_NOT_INITIALIZED;
    }
    if ((request == NULL) || (response == NULL) || (response_len == NULL))
    {
        return DIAGS_E_NULL_PTR;
    }
    if (response_max < 3U)
    {
        return DIAGS_E_BUFFER_TOO_SMALL;
    }
    if (request_len < 1U)
    {
        write_negative_response(0x00U, UDS_NRC_GENERAL_REJECT, response, response_len);
        return DIAGS_OK;
    }

    uint8_t sid = request[0];

    switch (sid)
    {
        case UDS_SID_READ_DATA_BY_IDENTIFIER:
            return handle_read_data_by_identifier(request, request_len,
                                                   response, response_max, response_len);
        case UDS_SID_ROUTINE_CONTROL:
            return handle_routine_control(request, request_len,
                                           response, response_max, response_len);
        default:
            write_negative_response(sid, UDS_NRC_GENERAL_REJECT, response, response_len);
            return DIAGS_OK;
    }
}
