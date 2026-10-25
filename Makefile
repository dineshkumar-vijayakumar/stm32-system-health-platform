# Host-side build + test (gcc). Target build is done in STM32CubeIDE.
CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Icore -Itests
BIN     := build

all: test

$(BIN):
	mkdir -p $(BIN)

$(BIN)/diag_log_test: tests/diag_log_test.c core/diag_log.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
$(BIN)/diag_watchdog_test: tests/diag_watchdog_test.c core/diag_watchdog.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
$(BIN)/diag_test: tests/diag_test.c core/diag.c core/diag_log.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^
$(BIN)/example_main: examples/example_main.c core/diag_log.c core/diag_watchdog.c core/diag.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

test: $(BIN)/diag_log_test $(BIN)/diag_watchdog_test $(BIN)/diag_test
	./$(BIN)/diag_log_test
	./$(BIN)/diag_watchdog_test
	./$(BIN)/diag_test

demo: $(BIN)/example_main
	./$(BIN)/example_main

clean:
	rm -rf $(BIN)
.PHONY: all test demo clean
