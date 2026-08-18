CC = gcc

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin

TARGET_DEV = $(BIN_DIR)/simulador_dev
TARGET_RELEASE = $(BIN_DIR)/simulador

SRCS = $(wildcard $(SRC_DIR)/*.c)
DEV_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/dev/%.o,$(SRCS))
RELEASE_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/release/%.o,$(SRCS))

CPPFLAGS = -I$(INC_DIR)
WARNINGS = -Wall -Wextra -Werror
CSTD = -std=c11
DEV_CFLAGS = $(CSTD) $(WARNINGS) -O0 -g -fsanitize=address,undefined
RELEASE_CFLAGS = $(CSTD) $(WARNINGS) -O2
DEV_LDFLAGS = -fsanitize=address,undefined

all: release

dev: $(TARGET_DEV)

release: $(TARGET_RELEASE)

$(TARGET_DEV): $(DEV_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(DEV_OBJS) -o $@ $(DEV_LDFLAGS)

$(TARGET_RELEASE): $(RELEASE_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(RELEASE_OBJS) -o $@

$(BUILD_DIR)/dev/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(DEV_CFLAGS) -c $< -o $@

$(BUILD_DIR)/release/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(RELEASE_CFLAGS) -c $< -o $@

test: dev
	./$(TARGET_DEV) --self-test
	sh tests/test_cli.sh ./$(TARGET_DEV)
	python3 tests/test_experiment.py

simulate: release
	./$(TARGET_RELEASE) --config configs/default.conf --algorithm fcfs

batch: release
	python3 scripts/run_experiment.py --experiment-id main --binary $(TARGET_RELEASE)

batch-reduced: release
	python3 scripts/run_experiment.py --experiment-id smoke --binary $(TARGET_RELEASE) --reduced

batch-verify: release
	python3 scripts/run_experiment.py --experiment-id main --binary $(TARGET_RELEASE) --verify-only

graphs:
	@mkdir -p results/figures results/consolidated

analyze:
	$(CC) $(CPPFLAGS) $(CSTD) $(WARNINGS) -fanalyzer -fsyntax-only $(SRCS)

compile_commands:
	@if command -v bear >/dev/null 2>&1; then \
		bear -- $(MAKE) clean release; \
	elif command -v compiledb >/dev/null 2>&1; then \
		compiledb -- $(MAKE) clean release; \
	else \
		echo "Aviso: bear ou compiledb nao instalados. compile_commands.json nao foi alterado."; \
	fi

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) results/raw

.PHONY: all dev release test simulate batch batch-reduced batch-verify graphs analyze compile_commands clean
