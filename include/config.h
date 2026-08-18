#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define CONFIG_TEXT_CAPACITY 128

typedef struct {
    int schema_version;
    char scenario[CONFIG_TEXT_CAPACITY];
    char algorithm[CONFIG_TEXT_CAPACITY];
    const char *config_path;
    const char *output_path;
    const char *individual_output_path;
    const char *workload_input_path;
    const char *workload_output_path;
    const char *run_id;
    uint64_t seed;
    int process_count;
    int context_switch_cost;
    int rr_quantum;
    int arrival_min;
    int arrival_max;
    int allow_zero_context_switch_cost;
    int run_id_explicit;
    int self_test;
} Config;

void config_set_defaults(Config *cfg);
int config_parse(int argc, char **argv, Config *cfg);

#endif
