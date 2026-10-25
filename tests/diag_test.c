/**
 * @file    diag_test.c
 * @brief   Unit tests for diag.c/.h
 *
 * Compile with:
 *     gcc -o diag_test diag_test.c diag.c diag_log.c
 *     ./diag_test
 */

#include "test_framework.h"
#include "diag.h"
#include "diag_log.h"

/* ------------------------------------------------------------
 * Fake DID read functions used by tests
 * ------------------------------------------------------------ */

static bool fake_read_fixed_u8(uint8_t* out_value, uint8_t max_len, uint8_t* out_len)
{
    if (max_len < 1U) { return false; }
    out_value[0] = 0x2AU; /* arbitrary fixed test value */
    *out_len = 1U;
    return true;
}

static bool fake_read_always_fails(uint8_t* out_value, uint8_t max_len, uint8_t* out_len)
{
    (void)out_value;
    (void)max_len;
    (void)out_len;
    return false;
}

/* ------------------------------------------------------------
 * Fake routine start/stop functions used by tests
 * ------------------------------------------------------------ */

static bool s_routine_was_started = false;
static bool s_routine_was_stopped = false;

static bool fake_routine_start(void)
{
    s_routine_was_started = true;
    return true;
}

static bool fake_routine_stop(void)
{
    s_routine_was_stopped = true;
    return true;
}

static bool fake_routine_start_fails(void)
{
    return false;
}

/* ------------------------------------------------------------
 * Init / registration
 * ------------------------------------------------------------ */

TEST(DiagInit, InitSucceeds)
{
    diag_log_init(LOG_OFF); /* diag.c logs errors through diag_log; must be init'd first */
    diags_status_t status = diag_init();
    EXPECT_EQ(status, DIAGS_OK);
}

TEST(DiagRegister, RegisterDidSucceeds)
{
    diag_did_descriptor_t d;
    d.did = 0xF190U; /* real UDS VIN DID, used here only as a realistic example */
    d.name = "TEST_DID";
    d.read_fn = fake_read_fixed_u8;

    diags_status_t status = diag_register_did(d);
    EXPECT_EQ(status, DIAGS_OK);
}

TEST(DiagRegister, RegisterRoutineSucceeds)
{
    diag_routine_descriptor_t r;
    r.routine_id = 0x0203U;
    r.name = "TEST_ROUTINE";
    r.start_fn = fake_routine_start;
    r.stop_fn = fake_routine_stop;
    r.max_runtime_ms = 1000U;

    diags_status_t status = diag_register_routine(r);
    EXPECT_EQ(status, DIAGS_OK);
}

/* ------------------------------------------------------------
 * ReadDataByIdentifier (SID 0x22)
 * ------------------------------------------------------------ */

TEST(DiagRDBI, KnownDidReturnsPositiveResponseWithValue)
{
    uint8_t request[3] = { UDS_SID_READ_DATA_BY_IDENTIFIER, 0xF1U, 0x90U };
    uint8_t response[16];
    uint8_t response_len = 0U;

    diags_status_t status = diag_process_request(request, 3U, response, sizeof(response), &response_len);

    EXPECT_EQ(status, DIAGS_OK);
    EXPECT_EQ(response[0], UDS_SID_READ_DATA_BY_IDENTIFIER + 0x40U); /* 0x62 */
    EXPECT_EQ(response[1], 0xF1U);
    EXPECT_EQ(response[2], 0x90U);
    EXPECT_EQ(response[3], 0x2AU); /* fake_read_fixed_u8's value */
    EXPECT_EQ(response_len, 4);
}

TEST(DiagRDBI, UnknownDidReturnsNegativeResponse)
{
    uint8_t request[3] = { UDS_SID_READ_DATA_BY_IDENTIFIER, 0xDEU, 0xADU }; /* not registered */
    uint8_t response[16];
    uint8_t response_len = 0U;

    diag_process_request(request, 3U, response, sizeof(response), &response_len);

    EXPECT_EQ(response[0], UDS_NEGATIVE_RESPONSE);
    EXPECT_EQ(response[1], UDS_SID_READ_DATA_BY_IDENTIFIER);
    EXPECT_EQ(response[2], UDS_NRC_REQUEST_OUT_OF_RANGE);
    EXPECT_EQ(response_len, 3);
}

TEST(DiagRDBI, TooShortRequestReturnsNegativeResponse)
{
    uint8_t request[2] = { UDS_SID_READ_DATA_BY_IDENTIFIER, 0xF1U }; /* missing 2nd DID byte */
    uint8_t response[16];
    uint8_t response_len = 0U;

    diag_process_request(request, 2U, response, sizeof(response), &response_len);

    EXPECT_EQ(response[0], UDS_NEGATIVE_RESPONSE);
    EXPECT_EQ(response[2], UDS_NRC_REQUEST_OUT_OF_RANGE);
}

TEST(DiagRDBI, ReadFunctionFailureReturnsNegativeResponse)
{
    diag_did_descriptor_t d;
    d.did = 0xABCDU;
    d.name = "FAILING_DID";
    d.read_fn = fake_read_always_fails;
    diag_register_did(d);

    uint8_t request[3] = { UDS_SID_READ_DATA_BY_IDENTIFIER, 0xABU, 0xCDU };
    uint8_t response[16];
    uint8_t response_len = 0U;

    diag_process_request(request, 3U, response, sizeof(response), &response_len);

    EXPECT_EQ(response[0], UDS_NEGATIVE_RESPONSE);
    EXPECT_EQ(response[2], UDS_NRC_CONDITIONS_NOT_CORRECT);
}

/* ------------------------------------------------------------
 * RoutineControl (SID 0x31)
 * ------------------------------------------------------------ */

TEST(DiagRoutineControl, StartKnownRoutineReturnsPositiveResponse)
{
    s_routine_was_started = false;

    uint8_t request[4] = { UDS_SID_ROUTINE_CONTROL, UDS_ROUTINE_START, 0x02U, 0x03U };
    uint8_t response[16];
    uint8_t response_len = 0U;

    diags_status_t status = diag_process_request(request, 4U, response, sizeof(response), &response_len);

    EXPECT_EQ(status, DIAGS_OK);
    EXPECT_EQ(response[0], UDS_SID_ROUTINE_CONTROL + 0x40U); /* 0x71 */
    EXPECT_EQ(response[1], UDS_ROUTINE_START);
    EXPECT_EQ(response[2], 0x02U);
    EXPECT_EQ(response[3], 0x03U);
    EXPECT_TRUE(s_routine_was_started);
}

TEST(DiagRoutineControl, StopKnownRoutineReturnsPositiveResponse)
{
    s_routine_was_stopped = false;

    uint8_t request[4] = { UDS_SID_ROUTINE_CONTROL, UDS_ROUTINE_STOP, 0x02U, 0x03U };
    uint8_t response[16];
    uint8_t response_len = 0U;

    diag_process_request(request, 4U, response, sizeof(response), &response_len);

    EXPECT_EQ(response[0], UDS_SID_ROUTINE_CONTROL + 0x40U);
    EXPECT_EQ(response[1], UDS_ROUTINE_STOP);
    EXPECT_TRUE(s_routine_was_stopped);
}

TEST(DiagRoutineControl, UnknownRoutineIdReturnsNegativeResponse)
{
    uint8_t request[4] = { UDS_SID_ROUTINE_CONTROL, UDS_ROUTINE_START, 0x99U, 0x99U };
    uint8_t response[16];
    uint8_t response_len = 0U;

    diag_process_request(request, 4U, response, sizeof(response), &response_len);

    EXPECT_EQ(response[0], UDS_NEGATIVE_RESPONSE);
    EXPECT_EQ(response[2], UDS_NRC_REQUEST_OUT_OF_RANGE);
}

TEST(DiagRoutineControl, StartFunctionFailureReturnsNegativeResponse)
{
    diag_routine_descriptor_t r;
    r.routine_id = 0x0505U;
    r.name = "FAILING_ROUTINE";
    r.start_fn = fake_routine_start_fails;
    r.stop_fn = NULL;
    r.max_runtime_ms = 500U;
    diag_register_routine(r);

    uint8_t request[4] = { UDS_SID_ROUTINE_CONTROL, UDS_ROUTINE_START, 0x05U, 0x05U };
    uint8_t response[16];
    uint8_t response_len = 0U;

    diag_process_request(request, 4U, response, sizeof(response), &response_len);

    EXPECT_EQ(response[0], UDS_NEGATIVE_RESPONSE);
    EXPECT_EQ(response[2], UDS_NRC_CONDITIONS_NOT_CORRECT);
}

TEST(DiagRoutineControl, UnknownSubFunctionReturnsNegativeResponse)
{
    uint8_t request[4] = { UDS_SID_ROUTINE_CONTROL, 0xFFU /* invalid sub-function */, 0x02U, 0x03U };
    uint8_t response[16];
    uint8_t response_len = 0U;

    diag_process_request(request, 4U, response, sizeof(response), &response_len);

    EXPECT_EQ(response[0], UDS_NEGATIVE_RESPONSE);
    EXPECT_EQ(response[2], UDS_NRC_REQUEST_OUT_OF_RANGE);
}

/* ------------------------------------------------------------
 * General request handling
 * ------------------------------------------------------------ */

TEST(DiagGeneral, UnknownServiceIdReturnsNegativeResponse)
{
    uint8_t request[2] = { 0x99U, 0x00U }; /* not a service we implement */
    uint8_t response[16];
    uint8_t response_len = 0U;

    diag_process_request(request, 2U, response, sizeof(response), &response_len);

    EXPECT_EQ(response[0], UDS_NEGATIVE_RESPONSE);
    EXPECT_EQ(response[1], 0x99U);
    EXPECT_EQ(response[2], UDS_NRC_GENERAL_REJECT);
}

TEST(DiagGeneral, NullRequestPointerReturnsNullPtrStatus)
{
    uint8_t response[16];
    uint8_t response_len = 0U;

    diags_status_t status = diag_process_request(NULL, 3U, response, sizeof(response), &response_len);
    EXPECT_EQ(status, DIAGS_E_NULL_PTR);
}

TEST(DiagGeneral, ResponseBufferTooSmallIsRejected)
{
    uint8_t request[3] = { UDS_SID_READ_DATA_BY_IDENTIFIER, 0xF1U, 0x90U };
    uint8_t response[2]; /* smaller than the minimum 3-byte negative response */
    uint8_t response_len = 0U;

    diags_status_t status = diag_process_request(request, 3U, response, sizeof(response), &response_len);
    EXPECT_EQ(status, DIAGS_E_BUFFER_TOO_SMALL);
}

int main(void)
{
    printf("Running diag (UDS) unit tests...\n\n");

    RUN_TEST(DiagInit, InitSucceeds);
    RUN_TEST(DiagRegister, RegisterDidSucceeds);
    RUN_TEST(DiagRegister, RegisterRoutineSucceeds);

    RUN_TEST(DiagRDBI, KnownDidReturnsPositiveResponseWithValue);
    RUN_TEST(DiagRDBI, UnknownDidReturnsNegativeResponse);
    RUN_TEST(DiagRDBI, TooShortRequestReturnsNegativeResponse);
    RUN_TEST(DiagRDBI, ReadFunctionFailureReturnsNegativeResponse);

    RUN_TEST(DiagRoutineControl, StartKnownRoutineReturnsPositiveResponse);
    RUN_TEST(DiagRoutineControl, StopKnownRoutineReturnsPositiveResponse);
    RUN_TEST(DiagRoutineControl, UnknownRoutineIdReturnsNegativeResponse);
    RUN_TEST(DiagRoutineControl, StartFunctionFailureReturnsNegativeResponse);
    RUN_TEST(DiagRoutineControl, UnknownSubFunctionReturnsNegativeResponse);

    RUN_TEST(DiagGeneral, UnknownServiceIdReturnsNegativeResponse);
    RUN_TEST(DiagGeneral, NullRequestPointerReturnsNullPtrStatus);
    RUN_TEST(DiagGeneral, ResponseBufferTooSmallIsRejected);

    TEST_SUMMARY();
    return 0;
}
