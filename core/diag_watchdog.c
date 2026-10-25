/**
 * @file    diag_watchdog.c
 * @brief   Implementation of diag_watchdog.h.
 * @see     diag_watchdog.h for design rationale and citation of the
 *          Bosch Research software_watchdogs project this is inspired by.
 */

#include "diag_watchdog.h"
#include <stddef.h>

typedef struct
{
    bool     registered;
    uint32_t lease_ms;
    uint16_t max_misses;
    uint16_t miss_count;
    uint32_t last_pet_ms;
    wdg_task_state_t state;
} wdg_task_t;

static wdg_task_t s_tasks[DIAG_WATCHDOG_MAX_TASKS];
static bool s_initialized = false;

wdg_status_t diag_watchdog_init(void)
{
    for (uint8_t i = 0U; i < DIAG_WATCHDOG_MAX_TASKS; i++)
    {
        s_tasks[i].registered = false;
        s_tasks[i].lease_ms = 0U;
        s_tasks[i].max_misses = 0U;
        s_tasks[i].miss_count = 0U;
        s_tasks[i].last_pet_ms = 0U;
        s_tasks[i].state = WDG_STATE_ACTIVE;
    }
    s_initialized = true;
    return WDG_OK;
}

wdg_status_t diag_watchdog_register(uint32_t lease_ms,
                                     uint16_t max_misses,
                                     wdg_task_handle_t* out_handle)
{
    if (!s_initialized)
    {
        return WDG_E_NOT_INITIALIZED;
    }
    if (out_handle == NULL)
    {
        return WDG_E_NULL_PTR;
    }

    for (uint8_t i = 0U; i < DIAG_WATCHDOG_MAX_TASKS; i++)
    {
        if (!s_tasks[i].registered)
        {
            s_tasks[i].registered = true;
            s_tasks[i].lease_ms = lease_ms;
            s_tasks[i].max_misses = (max_misses == 0U) ? 1U : max_misses;
            s_tasks[i].miss_count = 0U;
            s_tasks[i].last_pet_ms = 0U;
            s_tasks[i].state = WDG_STATE_ACTIVE;

            out_handle->index = i;
            out_handle->valid = true;
            return WDG_OK;
        }
    }

    out_handle->index = 0U;
    out_handle->valid = false;
    return WDG_E_TABLE_FULL;
}

static bool handle_is_valid(wdg_task_handle_t handle)
{
    return handle.valid
        && (handle.index < DIAG_WATCHDOG_MAX_TASKS)
        && s_tasks[handle.index].registered;
}

wdg_status_t diag_watchdog_pet(wdg_task_handle_t handle, uint32_t now_ms)
{
    if (!handle_is_valid(handle))
    {
        return WDG_E_INVALID_HANDLE;
    }

    wdg_task_t* t = &s_tasks[handle.index];

    /* Mirrors their heartbeat_sub_ callback: "lease_misses_ = 0" on every
     * successful receive, and (for a previously-faulted task) recovery
     * back to the active state once heartbeats resume. */
    t->last_pet_ms = now_ms;
    t->miss_count = 0U;
    t->state = WDG_STATE_ACTIVE;

    return WDG_OK;
}

void diag_watchdog_tick(uint32_t now_ms)
{
    for (uint8_t i = 0U; i < DIAG_WATCHDOG_MAX_TASKS; i++)
    {
        wdg_task_t* t = &s_tasks[i];

        if (!t->registered)
        {
            continue;
        }

        uint32_t elapsed = now_ms - t->last_pet_ms; /* relies on unsigned
                                                       * wraparound being
                                                       * well-defined in C,
                                                       * same as HAL_GetTick()
                                                       * based timing idioms */

        if (elapsed > t->lease_ms)
        {
            /* Lease violated since we last checked -- mirrors their
             * deadline_callback: "lease_misses_.fetch_add(...)" */
            if (t->miss_count < 0xFFFFU)
            {
                t->miss_count++;
            }

            /* Move the deadline forward by one lease period so a single
             * long stall is counted once per lease period, not once per
             * tick call -- otherwise a fast tick rate would inflate the
             * miss count arbitrarily for one real violation. */
            t->last_pet_ms += t->lease_ms;

            if (t->miss_count >= t->max_misses)
            {
                /* Mirrors "if (lease_misses_ >= max_misses_) deactivate();" */
                t->state = WDG_STATE_FAULT;
            }
        }
    }
}

wdg_task_state_t diag_watchdog_get_state(wdg_task_handle_t handle)
{
    if (!handle_is_valid(handle))
    {
        return WDG_STATE_FAULT; /* fail-safe: unknown handle reads as faulted */
    }
    return s_tasks[handle.index].state;
}

uint16_t diag_watchdog_get_miss_count(wdg_task_handle_t handle)
{
    if (!handle_is_valid(handle))
    {
        return 0U;
    }
    return s_tasks[handle.index].miss_count;
}
