CC = gcc

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin
PYTHON ?= python3
VENV_DIR ?= .venv
VENV_PYTHON = $(VENV_DIR)/bin/python
ANALYSIS_DEPS_STAMP = $(VENV_DIR)/.analysis-deps.stamp

TARGET_DEV = $(BIN_DIR)/simulador_dev
TARGET_RELEASE = $(BIN_DIR)/simulador
EXPERIMENT_ID ?= main
PILOT_ID ?= pilot
SJF_EXPERIMENT_ID ?= sjf-comparison

SRCS = $(wildcard $(SRC_DIR)/*.c)
DEV_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/dev/%.o,$(SRCS))
RELEASE_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/release/%.o,$(SRCS))
ANALYZE_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/analyze/%.o,$(SRCS))

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

$(BUILD_DIR)/analyze/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CSTD) $(WARNINGS) -fanalyzer -c $< -o $@

$(VENV_PYTHON):
	$(PYTHON) -m venv $(VENV_DIR)

$(ANALYSIS_DEPS_STAMP): requirements-analysis.txt $(VENV_PYTHON)
	$(VENV_PYTHON) -m pip install --requirement requirements-analysis.txt
	@touch $@

analysis-deps: $(ANALYSIS_DEPS_STAMP)

test: dev analysis-deps
	./$(TARGET_DEV) --self-test
	sh tests/test_cli.sh ./$(TARGET_DEV)
	$(VENV_PYTHON) tests/test_experiment.py
	$(VENV_PYTHON) tests/test_analysis.py
	$(VENV_PYTHON) tests/test_evidence.py

simulate: release
	./$(TARGET_RELEASE) --config configs/default.conf --algorithm fcfs

batch: release
	$(PYTHON) scripts/run_experiment.py --experiment-id main --binary $(TARGET_RELEASE) --include-sjf

batch-reduced: release
	$(PYTHON) scripts/run_experiment.py --experiment-id smoke --binary $(TARGET_RELEASE) --reduced

pilot: release
	$(PYTHON) scripts/run_experiment.py --experiment-id $(PILOT_ID) --binary $(TARGET_RELEASE) --pilot

pilot-verify: release
	$(PYTHON) scripts/run_experiment.py --experiment-id $(PILOT_ID) --binary $(TARGET_RELEASE) --pilot --verify-only

batch-verify: release
	$(PYTHON) scripts/run_experiment.py --experiment-id main --binary $(TARGET_RELEASE) --include-sjf --verify-only

batch-sjf: release
	$(PYTHON) scripts/run_experiment.py --experiment-id $(SJF_EXPERIMENT_ID) --binary $(TARGET_RELEASE) --include-sjf

batch-sjf-reduced: release
	$(PYTHON) scripts/run_experiment.py --experiment-id $(SJF_EXPERIMENT_ID)-smoke --binary $(TARGET_RELEASE) --reduced --include-sjf

batch-sjf-verify: release
	$(PYTHON) scripts/run_experiment.py --experiment-id $(SJF_EXPERIMENT_ID) --binary $(TARGET_RELEASE) --include-sjf --verify-only

graphs: analysis-deps
	$(VENV_PYTHON) scripts/analyze_experiment.py \
		--experiment-dir results/raw/$(EXPERIMENT_ID) \
		--summary-output results/consolidated/summary.csv \
		--figures-dir results/figures

comparison-graphs: analysis-deps
	$(VENV_PYTHON) scripts/generate_sample_comparison.py

evidence: analysis-deps
	$(VENV_PYTHON) scripts/package_experiment.py \
		--experiment-dir results/raw/main \
		--output-dir results/evidence \
		--summary results/consolidated/summary.csv \
		--figures-dir results/figures

evidence-verify: analysis-deps
	$(VENV_PYTHON) scripts/package_experiment.py \
		--experiment-dir results/raw/main \
		--output-dir results/evidence \
		--summary results/consolidated/summary.csv \
		--figures-dir results/figures \
		--verify-only

analyze: $(ANALYZE_OBJS)

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

.PHONY: all dev release analysis-deps test simulate batch batch-reduced pilot pilot-verify batch-verify batch-sjf batch-sjf-reduced batch-sjf-verify graphs evidence evidence-verify analyze compile_commands clean
