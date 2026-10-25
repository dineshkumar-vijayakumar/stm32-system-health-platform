/**
 * @file    diag_watchdog_test.c
 * @brief   Unit tests for diag_watchdog.c/.h
 *
 * Compile with:
 *     gcc -o diag_watchdog_test diag_watchdog_test.c diag_watchdog.c
 *     ./diag_watchdog_test
 */

#include "test_framework.h"
#include "diag_watchdog.h"

TEST(WatchdogInit, InitSucceeds)
{
    wdg_status_t status = diag_watchdog_init();
    EXPECT_EQ(status, WDG_OK);
}

TEST(WatchdogRegister, RegisterSucceedsAndStartsActive)
{
    diag_watchdog_init();

    wdg_task_handle_t h;
    wdg_status_t status = diag_watchdog_register(100U, 1U, &h);

    EXPECT_EQ(status, WDG_OK);
    EXPECT_TRUE(h.valid);
    EXPECT_EQ(diag_watchdog_get_state(h), WDG_STATE_ACTIVE);
    EXPECT_EQ(diag_watchdog_get_miss_count(h), 0);
}

TEST(WatchdogRegister, RegisterRejectsNullOutHandle)
{
    diag_watchdog_init();
    wdg_status_t status = diag_watchdog_register(100U, 1U, NULL);
    EXPECT_EQ(status, WDG_E_NULL_PTR);
}

TEST(WatchdogRegister, RegisterFailsWhenTableFull)
{
    diag_watchdog_init();

    wdg_task_handle_t h;
    for (uint8_t i = 0U; i < DIAG_WATCHDOG_MAX_TASKS; i++)
    {
        wdg_status_t status = diag_watchdog_register(100U, 1U, &h);
        EXPECT_EQ(status, WDG_OK);
    }

    /* table should now be full */
    wdg_task_handle_t overflow;
    wdg_status_t status = diag_watchdog_register(100U, 1U, &overflow);
    EXPECT_EQ(status, WDG_E_TABLE_FULL);
    EXPECT_FALSE(overflow.valid);
}

/* ============================================================
 * SimpleWatchdog-equivalent behavior: max_misses = 1
 * A single missed lease trips FAULT immediately.
 * ============================================================ */

TEST(WatchdogSimple, TaskStaysActiveWhileHeartbeatsArriveInTime)
{
    diag_watchdog_init();
    wdg_task_handle_t h;
    diag_watchdog_register(100U /*lease_ms*/, 1U /*max_misses*/, &h);

    diag_watchdog_pet(h, 0U);
    diag_watchdog_tick(50U);   /* within lease */
    EXPECT_EQ(diag_watchdog_get_state(h), WDG_STATE_ACTIVE);

    diag_watchdog_pet(h, 90U); /* renew before lease expires */
    diag_watchdog_tick(150U);  /* only 60ms since last pet, still fine */
    EXPECT_EQ(diag_watchdog_get_state(h), WDG_STATE_ACTIVE);
}

TEST(WatchdogSimple, SingleMissedLeaseTripsFaultImmediately)
{
    diag_watchdog_init();
    wdg_task_handle_t h;
    diag_watchdog_register(100U /*lease_ms*/, 1U /*max_misses*/, &h);

    diag_watchdog_pet(h, 0U);
    diag_watchdog_tick(250U); /* 250ms elapsed, way past the 100ms lease */

    EXPECT_EQ(diag_watchdog_get_state(h), WDG_STATE_FAULT);
    EXPECT_TRUE(diag_watchdog_get_miss_count(h) >= 1U);
}

TEST(WatchdogSimple, PettingAFaultedTaskRecoversItToActive)
{
    diag_watchdog_init();
    wdg_task_handle_t h;
    diag_watchdog_register(100U, 1U, &h);

    diag_watchdog_pet(h, 0U);
    diag_watchdog_tick(300U);
    EXPECT_EQ(diag_watchdog_get_state(h), WDG_STATE_FAULT);

    /* recovery: a fresh heartbeat should clear the fault, mirroring their
     * heartbeat_sub_ callback resetting lease_misses_ to 0 */
    diag_watchdog_pet(h, 300U);
    EXPECT_EQ(diag_watchdog_get_state(h), WDG_STATE_ACTIVE);
    EXPECT_EQ(diag_watchdog_get_miss_count(h), 0);
}

/* ============================================================
 * WindowedWatchdog-equivalent behavior: max_misses > 1
 * Tolerates repeated misses before tripping FAULT.
 * ============================================================ */

TEST(WatchdogWindowed, ToleratesMissesBelowThreshold)
{
    diag_watchdog_init();
    wdg_task_handle_t h;
    diag_watchdog_register(100U /*lease_ms*/, 3U /*max_misses*/, &h);

    diag_watchdog_pet(h, 0U);

    /* first violation */
    diag_watchdog_tick(150U);
    EXPECT_EQ(diag_watchdog_get_state(h), WDG_STATE_ACTIVE);
    EXPECT_EQ(diag_watchdog_get_miss_count(h), 1);

    /* second violation (still below max_misses=3) */
    diag_watchdog_tick(300U);
    EXPECT_EQ(diag_watchdog_get_state(h), WDG_STATE_ACTIVE);
    EXPECT_EQ(diag_watchdog_get_miss_count(h), 2);
}

TEST(WatchdogWindowed, TripsFaultOnceMaxMissesReached)
{
    diag_watchdog_init();
    wdg_task_handle_t h;
    diag_watchdog_register(100U, 3U, &h);

    diag_watchdog_pet(h, 0U);

    diag_watchdog_tick(150U); /* miss 1 */
    diag_watchdog_tick(300U); /* miss 2 */
    EXPECT_EQ(diag_watchdog_get_state(h), WDG_STATE_ACTIVE);

    diag_watchdog_tick(450U); /* miss 3 -> reaches max_misses */
    EXPECT_EQ(diag_watchdog_get_state(h), WDG_STATE_FAULT);
    EXPECT_EQ(diag_watchdog_get_miss_count(h), 3);
}

TEST(WatchdogWindowed, HeartbeatBetweenMissesResetsCounter)
{
    diag_watchdog_init();
    wdg_task_handle_t h;
    diag_watchdog_register(100U, 3U, &h);

    diag_watchdog_pet(h, 0U);
    diag_watchdog_tick(150U); /* miss 1 */
    EXPECT_EQ(diag_watchdog_get_miss_count(h), 1);

    /* a fresh heartbeat arrives before max_misses is reached */
    diag_watchdog_pet(h, 160U);
    EXPECT_EQ(diag_watchdog_get_miss_count(h), 0);
    EXPECT_EQ(diag_watchdog_get_state(h), WDG_STATE_ACTIVE);
}

/* ============================================================
 * Multiple independent tasks
 * ============================================================ */

TEST(WatchdogMultiTask, TasksAreMonitoredIndependently)
{
    diag_watchdog_init();

    wdg_task_handle_t fast_task, slow_task;
    diag_watchdog_register(50U, 1U, &fast_task);   /* strict lease */
    diag_watchdog_register(500U, 1U, &slow_task);  /* relaxed lease */

    diag_watchdog_pet(fast_task, 0U);
    diag_watchdog_pet(slow_task, 0U);

    diag_watchdog_tick(200U); /* fast_task's 50ms lease is long expired;
                                * slow_task's 500ms lease is still fine */

    EXPECT_EQ(diag_watchdog_get_state(fast_task), WDG_STATE_FAULT);
    EXPECT_EQ(diag_watchdog_get_state(slow_task), WDG_STATE_ACTIVE);
}

/* ============================================================
 * Invalid handle handling
 * ============================================================ */

TEST(WatchdogInvalidHandle, PetOnUnregisteredHandleReturnsError)
{
    diag_watchdog_init();

    wdg_task_handle_t bogus;
    bogus.index = 0U;
    bogus.valid = false;

    wdg_status_t status = diag_watchdog_pet(bogus, 0U);
    EXPECT_EQ(status, WDG_E_INVALID_HANDLE);
}

TEST(WatchdogInvalidHandle, GetStateOnUnregisteredHandleFailsSafeAsFault)
{
    diag_watchdog_init();

    wdg_task_handle_t bogus;
    bogus.index = 0U;
    bogus.valid = false;

    /* fail-safe design decision: an unknown/invalid task handle must never
     * silently report itself as healthy */
    EXPECT_EQ(diag_watchdog_get_state(bogus), WDG_STATE_FAULT);
}

int main(void)
{
    printf("Running diag_watchdog unit tests...\n\n");

    RUN_TEST(WatchdogInit, InitSucceeds);

    RUN_TEST(WatchdogRegister, RegisterSucceedsAndStartsActive);
    RUN_TEST(WatchdogRegister, RegisterRejectsNullOutHandle);
    RUN_TEST(WatchdogRegister, RegisterFailsWhenTableFull);

    RUN_TEST(WatchdogSimple, TaskStaysActiveWhileHeartbeatsArriveInTime);
    RUN_TEST(WatchdogSimple, SingleMissedLeaseTripsFaultImmediately);
    RUN_TEST(WatchdogSimple, PettingAFaultedTaskRecoversItToActive);

    RUN_TEST(WatchdogWindowed, ToleratesMissesBelowThreshold);
    RUN_TEST(WatchdogWindowed, TripsFaultOnceMaxMissesReached);
    RUN_TEST(WatchdogWindowed, HeartbeatBetweenMissesResetsCounter);

    RUN_TEST(WatchdogMultiTask, TasksAreMonitoredIndependently);

    RUN_TEST(WatchdogInvalidHandle, PetOnUnregisteredHandleReturnsError);
    RUN_TEST(WatchdogInvalidHandle, GetStateOnUnregisteredHandleFailsSafeAsFault);

    TEST_SUMMARY();
    return 0;
}
