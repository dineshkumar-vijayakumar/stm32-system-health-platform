/**
 * @file    diag_log_test.c
 * @brief   Unit tests for diag_log.c/.h
 *
 * @details Test naming and structure mirrors the pattern observed in
 *          Bosch Eclipse S-CORE's score/mw/log/backend_table_test.cpp
 *          and score/mw/com/runtime_test.cpp: one TEST per behavior,
 *          explicit boundary/failure-path coverage, not just happy path.
 *
 *          Run on host (not target) for fast iteration -- compile with:
 *              gcc -o diag_log_test diag_log_test.c diag_log.c
 *              ./diag_log_test
 */

#include "test_framework.h"
#include "diag_log.h"

/* ============================================================
 * Helper: fully reset module state between tests.
 * diag_log has no public de-init, so tests that need a clean
 * slate re-link is out of scope for a single test binary; instead
 * each test is written to be order-independent where possible,
 * and slot-exhaustion tests clean up their own slots explicitly.
 * ============================================================ */

/* ============================================================
 * Init behavior
 * ============================================================ */

/* NOTE: diag_log has no public de-init function -- by design, mirroring
 * score::mw::com::runtime::InitializeRuntime()'s documented contract:
 * "shall only be called ONCE per application/process lifetime". This
 * means init-once behavior must be verified within a SINGLE test (not
 * split across two), since a second test would observe an
 * already-initialized module and get a false failure. */
TEST(DiagLogInit, FirstCallSucceedsSecondCallReturnsAlreadyInit)
{
    diag_status_t first = diag_log_init(LOG_INFO);
    diag_status_t second = diag_log_init(LOG_INFO);
    EXPECT_EQ(first, DIAG_OK);
    EXPECT_EQ(second, DIAG_E_ALREADY_INIT);
}

/* ============================================================
 * Log-level filtering (checked BEFORE slot reservation)
 * ============================================================ */

TEST(DiagLogFilter, LevelBelowThresholdIsEnabled)
{
    /* module already initialized with LOG_INFO from prior tests in this
     * translation unit's execution order -- verifies INFO is enabled */
    EXPECT_TRUE(diag_log_is_enabled(LOG_ERROR));  /* more severe than INFO */
    EXPECT_TRUE(diag_log_is_enabled(LOG_INFO));   /* exactly at threshold */
}

TEST(DiagLogFilter, LevelAboveThresholdIsDisabled)
{
    EXPECT_FALSE(diag_log_is_enabled(LOG_DEBUG));   /* more verbose than INFO */
    EXPECT_FALSE(diag_log_is_enabled(LOG_VERBOSE));
}

TEST(DiagLogFilter, LogOffLevelIsAlwaysDisabled)
{
    EXPECT_FALSE(diag_log_is_enabled(LOG_OFF));
}

TEST(DiagLogFilter, StartRecordRejectsFilteredLevelWithoutReservingSlot)
{
    diag_slot_handle_t slot;
    diag_status_t status = diag_log_start(LOG_VERBOSE, "TEST", &slot);

    EXPECT_EQ(status, DIAG_E_LOG_DISABLED);
    EXPECT_FALSE(slot.valid);
}

/* ============================================================
 * Slot reservation / three-phase API
 * ============================================================ */

TEST(DiagLogSlots, StartRecordReservesValidSlotForEnabledLevel)
{
    diag_slot_handle_t slot;
    diag_status_t status = diag_log_start(LOG_ERROR, "TEST", &slot);

    EXPECT_EQ(status, DIAG_OK);
    EXPECT_TRUE(slot.valid);

    /* clean up so this slot doesn't leak into later tests */
    diag_log_stop(slot);
}

TEST(DiagLogSlots, StartRecordUsesDefaultContextWhenNullPassed)
{
    diag_slot_handle_t slot;
    diag_status_t status = diag_log_start(LOG_ERROR, NULL, &slot);

    EXPECT_EQ(status, DIAG_OK);
    EXPECT_TRUE(slot.valid);

    diag_log_stop(slot);
}

TEST(DiagLogSlots, StartRecordRejectsNullOutSlotPointer)
{
    diag_status_t status = diag_log_start(LOG_ERROR, "TEST", NULL);
    EXPECT_EQ(status, DIAG_E_NULL_PTR);
}

TEST(DiagLogSlots, AllSlotsCanBeExhaustedAndReportNoFreeSlot)
{
    diag_slot_handle_t slots[DIAG_LOG_NUM_SLOTS];
    diag_status_t status;
    uint8_t i;

    /* reserve every available slot */
    for (i = 0U; i < DIAG_LOG_NUM_SLOTS; i++)
    {
        status = diag_log_start(LOG_ERROR, "TEST", &slots[i]);
        EXPECT_EQ(status, DIAG_OK);
    }

    /* one more reservation must fail: pool exhausted */
    diag_slot_handle_t overflow_slot;
    status = diag_log_start(LOG_ERROR, "TEST", &overflow_slot);
    EXPECT_EQ(status, DIAG_E_NO_FREE_SLOT);
    EXPECT_FALSE(overflow_slot.valid);

    /* release everything so subsequent tests aren't affected */
    for (i = 0U; i < DIAG_LOG_NUM_SLOTS; i++)
    {
        diag_log_stop(slots[i]);
    }
}

TEST(DiagLogSlots, StopRecordReleasesSlotForReuse)
{
    diag_slot_handle_t slot_a;
    diag_status_t status = diag_log_start(LOG_ERROR, "TEST", &slot_a);
    EXPECT_EQ(status, DIAG_OK);

    status = diag_log_stop(slot_a);
    EXPECT_EQ(status, DIAG_OK);

    /* the freed slot should now be reusable */
    diag_slot_handle_t slot_b;
    status = diag_log_start(LOG_ERROR, "TEST", &slot_b);
    EXPECT_EQ(status, DIAG_OK);
    EXPECT_EQ(slot_b.index, slot_a.index); /* first-fit reuse of the same index */

    diag_log_stop(slot_b);
}

TEST(DiagLogSlots, StopRecordOnAlreadyStoppedSlotReturnsInvalid)
{
    diag_slot_handle_t slot;
    diag_log_start(LOG_ERROR, "TEST", &slot);
    diag_log_stop(slot);

    /* second stop on the same (now-released) handle must be rejected */
    diag_status_t status = diag_log_stop(slot);
    EXPECT_EQ(status, DIAG_E_INVALID_SLOT);
}

/* ============================================================
 * Writing typed values into a reserved slot
 * ============================================================ */

TEST(DiagLogWrite, WriteU32ValueSucceedsOnValidSlot)
{
    diag_slot_handle_t slot;
    diag_log_start(LOG_ERROR, "TEST", &slot);

    log_value_t value;
    value.type = LOG_VAL_U32;
    value.as.u32 = 123456U;

    diag_status_t status = diag_log_write(slot, value);
    EXPECT_EQ(status, DIAG_OK);

    diag_log_stop(slot);
}

TEST(DiagLogWrite, WriteStringValueSucceedsOnValidSlot)
{
    diag_slot_handle_t slot;
    diag_log_start(LOG_ERROR, "TEST", &slot);

    log_value_t value;
    value.type = LOG_VAL_STRING;
    value.as.str = "sensor_fault";

    diag_status_t status = diag_log_write(slot, value);
    EXPECT_EQ(status, DIAG_OK);

    diag_log_stop(slot);
}

TEST(DiagLogWrite, WriteOnInvalidSlotIsRejected)
{
    diag_slot_handle_t bogus_slot;
    bogus_slot.index = 0U;
    bogus_slot.valid = false;

    log_value_t value;
    value.type = LOG_VAL_U8;
    value.as.u8 = 1U;

    diag_status_t status = diag_log_write(bogus_slot, value);
    EXPECT_EQ(status, DIAG_E_INVALID_SLOT);
}

TEST(DiagLogWrite, MultipleWritesToSameSlotAllSucceedUntilFull)
{
    diag_slot_handle_t slot;
    diag_log_start(LOG_ERROR, "TEST", &slot);

    log_value_t value;
    value.type = LOG_VAL_U8;
    value.as.u8 = 0xAAU;

    /* DIAG_LOG_SLOT_SIZE 1-byte writes should all succeed */
    diag_status_t status = DIAG_OK;
    for (uint8_t i = 0U; i < DIAG_LOG_SLOT_SIZE; i++)
    {
        status = diag_log_write(slot, value);
        EXPECT_EQ(status, DIAG_OK);
    }

    /* one more byte must overflow the fixed-size buffer */
    status = diag_log_write(slot, value);
    EXPECT_EQ(status, DIAG_E_PAYLOAD_TOO_LARGE);

    diag_log_stop(slot);
}

/* ============================================================
 * Test runner
 * ============================================================ */

int main(void)
{
    printf("Running diag_log unit tests...\n\n");

    RUN_TEST(DiagLogInit, FirstCallSucceedsSecondCallReturnsAlreadyInit);

    RUN_TEST(DiagLogFilter, LevelBelowThresholdIsEnabled);
    RUN_TEST(DiagLogFilter, LevelAboveThresholdIsDisabled);
    RUN_TEST(DiagLogFilter, LogOffLevelIsAlwaysDisabled);
    RUN_TEST(DiagLogFilter, StartRecordRejectsFilteredLevelWithoutReservingSlot);

    RUN_TEST(DiagLogSlots, StartRecordReservesValidSlotForEnabledLevel);
    RUN_TEST(DiagLogSlots, StartRecordUsesDefaultContextWhenNullPassed);
    RUN_TEST(DiagLogSlots, StartRecordRejectsNullOutSlotPointer);
    RUN_TEST(DiagLogSlots, AllSlotsCanBeExhaustedAndReportNoFreeSlot);
    RUN_TEST(DiagLogSlots, StopRecordReleasesSlotForReuse);
    RUN_TEST(DiagLogSlots, StopRecordOnAlreadyStoppedSlotReturnsInvalid);

    RUN_TEST(DiagLogWrite, WriteU32ValueSucceedsOnValidSlot);
    RUN_TEST(DiagLogWrite, WriteStringValueSucceedsOnValidSlot);
    RUN_TEST(DiagLogWrite, WriteOnInvalidSlotIsRejected);
    RUN_TEST(DiagLogWrite, MultipleWritesToSameSlotAllSucceedUntilFull);

    TEST_SUMMARY();
    return 0;
}
