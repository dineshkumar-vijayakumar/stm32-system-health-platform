# REQUIREMENTS.md

Lightweight requirement traceability for the System Health Platform.

This project cannot produce formal, certified requirements (that needs a
requirements-management tool such as IBM DOORS, which is enterprise-licensed
and unavailable to a student). What it can honestly do -- and what this file
is -- is a small, explicit list of REQ-IDs that each module is built against,
referenced from code comments and from unit test names, mirroring (in spirit,
not in tooling) the `RecordProperty("Verifies", "SCR-XXXXX")` pattern seen
throughout Bosch's Eclipse S-CORE test suite (e.g.
`score/mw/com/runtime_test.cpp`).

## diag_log (logging)

| REQ-ID    | Requirement                                                                 | Verified by |
|-----------|------------------------------------------------------------------------------|-------------|
| REQ-LOG-01 | The module shall not use dynamic memory allocation.                        | Code review: no malloc/free anywhere in diag_log.c |
| REQ-LOG-02 | The module shall provide exactly `DIAG_LOG_NUM_SLOTS` concurrent log slots. | `DiagLogSlots.AllSlotsCanBeExhaustedAndReportNoFreeSlot` |
| REQ-LOG-03 | A message below the configured severity threshold shall not consume a slot. | `DiagLogFilter.StartRecordRejectsFilteredLevelWithoutReservingSlot` |
| REQ-LOG-04 | `diag_log_stop()` shall release the slot for reuse.                        | `DiagLogSlots.StopRecordReleasesSlotForReuse` |
| REQ-LOG-05 | Writing past a slot's fixed capacity shall be rejected, not overflow it.   | `DiagLogWrite.MultipleWritesToSameSlotAllSucceedUntilFull` |
| REQ-LOG-06 | The module shall be initializable exactly once.                            | `DiagLogInit.FirstCallSucceedsSecondCallReturnsAlreadyInit` |

## diag_watchdog

| REQ-ID     | Requirement                                                                | Verified by |
|------------|-----------------------------------------------------------------------------|-------------|
| REQ-WDG-01 | A task with `max_misses = 1` shall fault on the first missed lease.        | `WatchdogSimple.SingleMissedLeaseTripsFaultImmediately` |
| REQ-WDG-02 | A task with `max_misses > 1` shall tolerate misses below that threshold.  | `WatchdogWindowed.ToleratesMissesBelowThreshold` |
| REQ-WDG-03 | A task shall fault once its miss count reaches `max_misses`.               | `WatchdogWindowed.TripsFaultOnceMaxMissesReached` |
| REQ-WDG-04 | A fresh pet shall reset the miss counter and clear a fault.               | `WatchdogSimple.PettingAFaultedTaskRecoversItToActive` |
| REQ-WDG-05 | Tasks shall be monitored independently of one another.                     | `WatchdogMultiTask.TasksAreMonitoredIndependently` |
| REQ-WDG-06 | An invalid/unregistered task handle shall report FAULT, never a healthy state (fail-safe default). | `WatchdogInvalidHandle.GetStateOnUnregisteredHandleFailsSafeAsFault` |

## diag (UDS-inspired diagnostic services)

| REQ-ID     | Requirement                                                                 | Verified by |
|------------|------------------------------------------------------------------------------|-------------|
| REQ-UDS-01 | ReadDataByIdentifier for a registered DID shall return SID+0x40 with the current value. | `DiagRDBI.KnownDidReturnsPositiveResponseWithValue` |
| REQ-UDS-02 | ReadDataByIdentifier for an unregistered DID shall return a negative response with NRC 0x31. | `DiagRDBI.UnknownDidReturnsNegativeResponse` |
| REQ-UDS-03 | RoutineControl start on a registered routine shall invoke its start function and return SID+0x40. | `DiagRoutineControl.StartKnownRoutineReturnsPositiveResponse` |
| REQ-UDS-04 | RoutineControl on an unregistered routine ID shall return a negative response. | `DiagRoutineControl.UnknownRoutineIdReturnsNegativeResponse` |
| REQ-UDS-05 | A failing read/start function shall be reported as NRC 0x22 (conditionsNotCorrect), not silently ignored. | `DiagRDBI.ReadFunctionFailureReturnsNegativeResponse`, `DiagRoutineControl.StartFunctionFailureReturnsNegativeResponse` |
| REQ-UDS-06 | Every diagnostic failure path shall be logged via diag_log.                | Code review: `log_diag_error()` called on every negative-response path |

## Explicit non-requirements (honest scope limits)

- This is **not** a certified UDS stack; it implements the request/response
  *shape* of two real ISO 14229 services for demonstration purposes.
- This is **not** MISRA-C certified; it follows MISRA-adjacent discipline
  (no dynamic memory, no recursion, explicit status-code returns) but has
  not been run through a MISRA static analysis tool.
- Formal ISO 26262 ASIL classification is out of scope for a portfolio
  project; where relevant, code comments note which Bosch/S-CORE concept
  (e.g. ASIL_QM enum usage) inspired a given design choice, without
  claiming the actual classification applies here.
