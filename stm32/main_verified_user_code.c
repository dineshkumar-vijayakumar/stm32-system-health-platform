/* USER CODE sections of main.c that were built, flashed and observed on
 * NUCLEO-F446RE (see docs/evidence). Paste into the matching CubeIDE
 * USER CODE regions. Add: #include "diag_log.h" "diag_watchdog.h" "diag.h" <stdio.h>
 * and in PV:  uint32_t g_tick_ms = 0; wdg_task_handle_t g_main_task_handle;
 * Add the .c files from core/ to Core/Src and core/*.h to Core/Inc. */

/* USER CODE BEGIN 0 */
static bool read_tick_did(uint8_t* out_value, uint8_t max_len, uint8_t* out_len)
{
    if (max_len < 4U) return false;
    out_value[0] = (uint8_t)(g_tick_ms >> 24);
    out_value[1] = (uint8_t)(g_tick_ms >> 16);
    out_value[2] = (uint8_t)(g_tick_ms >> 8);
    out_value[3] = (uint8_t)(g_tick_ms & 0xFF);
    *out_len = 4U;
    return true;
}
static bool self_test_start(void) { return true; }
static bool self_test_stop(void)  { return true; }
/* USER CODE END 0 */

/* USER CODE BEGIN 2 */
  diag_log_init(LOG_DEBUG);
  diag_watchdog_init();
  diag_init();
  diag_watchdog_register(500U, 2U, &g_main_task_handle);
  diag_did_descriptor_t tick_did = { 0x1234U, "TICK_MS", read_tick_did };
  diag_register_did(tick_did);
  diag_routine_descriptor_t self_test;
  self_test.routine_id = 0x0203U; self_test.name = "SELF_TEST";
  self_test.start_fn = self_test_start; self_test.stop_fn = self_test_stop;
  self_test.max_runtime_ms = 500U;
  diag_register_routine(self_test);
/* USER CODE END 2 */

/* USER CODE BEGIN 3  (inside while(1)) */
  g_tick_ms += 100U;
  diag_watchdog_tick(g_tick_ms);
  diag_watchdog_pet(g_main_task_handle, g_tick_ms);

  diag_slot_handle_t slot;
  if (diag_log_start(LOG_INFO, "MAIN", &slot) == DIAG_OK)
  {
      log_value_t v; v.type = LOG_VAL_U32; v.as.u32 = g_tick_ms;
      diag_log_write(slot, v);
      diag_log_stop(slot);
      char m[64];
      int n = sprintf(m, "[tick=%lu] watchdog=%s\r\n", g_tick_ms,
          (diag_watchdog_get_state(g_main_task_handle) == WDG_STATE_ACTIVE) ? "ACTIVE" : "FAULT");
      HAL_UART_Transmit(&huart2, (uint8_t*)m, n, 100);
  }
  uint8_t request[3] = { UDS_SID_READ_DATA_BY_IDENTIFIER, 0x12U, 0x34U };
  uint8_t response[16]; uint8_t response_len = 0U;
  diag_process_request(request, 3U, response, sizeof(response), &response_len);
  char d[64];
  int dn = sprintf(d, "UDS RDBI response: SID=0x%02X len=%u\r\n", response[0], response_len);
  HAL_UART_Transmit(&huart2, (uint8_t*)d, dn, 100);
  HAL_Delay(100);
/* USER CODE END 3 */
