/**
 * @file    diag_watchdog.h
 * @brief   Software watchdog module for monitoring periodic task "heartbeats"
 *          on bare-metal STM32.
 *
 * @details Architecture inspired by Bosch Research's software_watchdogs
 *          project (https://github.com/boschresearch/software_watchdogs),
 *          specifically the SimpleWatchdog and WindowedWatchdog classes in
 *          sw_watchdog/src/simple_watchdog.cpp and windowed_watchdog.cpp.
 *
 *          That implementation is ROS2/DDS-based (relies on QoS liveliness
 *          and deadline policies, lifecycle node state machines) and cannot
 *          run on bare-metal STM32. This module re-implements the same
 *          *behavioral contract* from scratch in plain C:
 *
 *            - A "lease": the maximum time allowed between two heartbeats
 *              from a monitored task (mirrors their lease_duration_).
 *            - SimpleWatchdog behavior = WindowedWatchdog with max_misses=1
 *              (a single missed lease trips the fault state immediately) --
 *              this module unifies both into one configurable max_misses
 *              parameter per watchdog instance, exactly as their two C++
 *              classes differ only in this one parameter.
 *            - A miss counter that increments when a lease is not renewed
 *              in time, and resets to 0 the moment a fresh heartbeat/"pet"
 *              arrives (mirrors their heartbeat_sub_ callback: "lease_misses_
 *              = 0" on every successful receive).
 *            - A simple two-state model (ACTIVE / FAULT) standing in for
 *              their full ROS2 lifecycle state machine (Unconfigured ->
 *              Inactive -> Active -> ...), since a bare-metal target has no
 *              such framework available.
 *
 *          Checking is done by polling diag_watchdog_tick() from a periodic
 *          timer interrupt or the main superloop -- there is no OS-level
 *          QoS deadline callback available on bare-metal, so the equivalent
 *          behavior is achieved by comparing elapsed time against the lease
 *          on every tick, which is the standard bare-metal watchdog pattern.
 */

#ifndef DIAG_WATCHDOG_H
#define DIAG_WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Configuration
 * ============================================================ */

/** Max number of independently-monitored tasks this module can track. */
#define DIAG_WATCHDOG_MAX_TASKS 4U

/* ============================================================
 * Status codes
 * ============================================================ */

typedef enum
{
    WDG_OK               = 0,
    WDG_E_NULL_PTR        = -1,
    WDG_E_TABLE_FULL      = -2,  /**< DIAG_WATCHDOG_MAX_TASKS already registered */
    WDG_E_INVALID_HANDLE  = -3,  /**< handle does not refer to a registered task */
    WDG_E_NOT_INITIALIZED = -4
} wdg_status_t;

/* ============================================================
 * Task state
 * Mirrors the two states relevant on bare-metal from their fuller
 * ROS2 lifecycle state machine: Active (heartbeats on time) and
 * Inactive/Fault (lease exceeded, misses >= max_misses).
 * ============================================================ */

typedef enum
{
    WDG_STATE_ACTIVE = 0,
    WDG_STATE_FAULT  = 1
} wdg_task_state_t;

/** Opaque handle to a registered watchdog task. */
typedef struct
{
    uint8_t index;
    bool    valid;
} wdg_task_handle_t;

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * @brief Initializes the watchdog module. Must be called once before use.
 */
wdg_status_t diag_watchdog_init(void);

/**
 * @brief Registers a new monitored task.
 *
 * @param[in]  lease_ms     Max time (ms) allowed between two heartbeats
 *                            before this counts as one "miss". Mirrors
 *                            SimpleWatchdog/WindowedWatchdog's lease_duration_.
 * @param[in]  max_misses   Number of consecutive misses tolerated before
 *                            the task transitions to WDG_STATE_FAULT.
 *                            max_misses=1 reproduces SimpleWatchdog's
 *                            trip-on-first-violation behavior; higher
 *                            values reproduce WindowedWatchdog's tolerance
 *                            window.
 * @param[out] out_handle   Receives the handle for this task on success.
 *
 * @retval WDG_OK               Task registered.
 * @retval WDG_E_TABLE_FULL     No free task slots remain.
 * @retval WDG_E_NULL_PTR       out_handle was NULL.
 * @retval WDG_E_NOT_INITIALIZED diag_watchdog_init() was not called.
 */
wdg_status_t diag_watchdog_register(uint32_t lease_ms,
                                     uint16_t max_misses,
                                     wdg_task_handle_t* out_handle);

/**
 * @brief Renews a task's lease -- the bare-metal equivalent of receiving
 *        a heartbeat message. Call this from the monitored task itself,
 *        each time it completes a normal cycle of work.
 *
 * Resets the miss counter to 0 and clears WDG_STATE_FAULT back to
 * WDG_STATE_ACTIVE, mirroring their heartbeat_sub_ callback setting
 * "lease_misses_ = 0" on every successful receive.
 *
 * @retval WDG_OK                 Lease renewed.
 * @retval WDG_E_INVALID_HANDLE   handle does not refer to a registered task.
 */
wdg_status_t diag_watchdog_pet(wdg_task_handle_t handle, uint32_t now_ms);

/**
 * @brief Advances the watchdog's internal time check for all registered
 *        tasks. Call this periodically (e.g. every 10ms from a timer ISR
 *        or the main superloop) with the current system tick.
 *
 * For every task whose lease has expired since the last pet, increments
 * that task's miss counter (mirrors their deadline_callback incrementing
 * lease_misses_). If a task's miss counter reaches its configured
 * max_misses, that task transitions to WDG_STATE_FAULT (mirrors their
 * "if (lease_misses_ >= max_misses_) deactivate();").
 *
 * @param[in] now_ms  Current system time in milliseconds (e.g. HAL_GetTick()).
 */
void diag_watchdog_tick(uint32_t now_ms);

/**
 * @brief Returns the current state of a registered task.
 */
wdg_task_state_t diag_watchdog_get_state(wdg_task_handle_t handle);

/**
 * @brief Returns the current consecutive-miss count for a registered task.
 *        Mirrors their Status message's missed_number field.
 */
uint16_t diag_watchdog_get_miss_count(wdg_task_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_WATCHDOG_H */
