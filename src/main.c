#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIMULADOR_VERSION "0.1.0"

typedef struct {
    const char *scenario;
    const char *algorithm;
    const char *output_path;
    uint64_t seed;
    int process_count;
    int context_switch_cost;
    int rr_quantum;
    int self_test;
} Config;

static Config default_config(void) {
    Config cfg = {
        "equilibrado",
        "fcfs",
        NULL,
        1,
        1000,
        1,
        4,
        0,
    };
    return cfg;
}

static int parse_u64(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value;

    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    *out = (uint64_t)value;
    return 1;
}

static int parse_positive_int(const char *text, int *out) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value <= 0 || value > 1000000L) {
        return 0;
    }

    *out = (int)value;
    return 1;
}

static int next_arg(int argc, char **argv, int *index, const char **value) {
    if (*index + 1 >= argc) {
        fprintf(stderr, "argumento sem valor: %s\n", argv[*index]);
        return 0;
    }
    *index += 1;
    *value = argv[*index];
    return 1;
}

static int parse_args(int argc, char **argv, Config *cfg) {
    int i;

    for (i = 1; i < argc; i += 1) {
        const char *value = NULL;

        if (strcmp(argv[i], "--help") == 0) {
            printf("uso: %s [--scenario nome] [--seed n] [--algorithm nome] [--processes n] [--output arquivo]\n", argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("%s\n", SIMULADOR_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--self-test") == 0) {
            cfg->self_test = 1;
        } else if (strcmp(argv[i], "--scenario") == 0) {
            if (!next_arg(argc, argv, &i, &cfg->scenario)) return -1;
        } else if (strcmp(argv[i], "--algorithm") == 0) {
            if (!next_arg(argc, argv, &i, &cfg->algorithm)) return -1;
        } else if (strcmp(argv[i], "--output") == 0) {
            if (!next_arg(argc, argv, &i, &cfg->output_path)) return -1;
        } else if (strcmp(argv[i], "--seed") == 0) {
            if (!next_arg(argc, argv, &i, &value) || !parse_u64(value, &cfg->seed)) {
                fprintf(stderr, "seed invalida\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--processes") == 0) {
            if (!next_arg(argc, argv, &i, &value) || !parse_positive_int(value, &cfg->process_count)) {
                fprintf(stderr, "quantidade de processos invalida\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--context-switch-cost") == 0) {
            if (!next_arg(argc, argv, &i, &value) || !parse_positive_int(value, &cfg->context_switch_cost)) {
                fprintf(stderr, "custo de troca invalido\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--rr-quantum") == 0) {
            if (!next_arg(argc, argv, &i, &value) || !parse_positive_int(value, &cfg->rr_quantum)) {
                fprintf(stderr, "quantum invalido\n");
                return -1;
            }
        } else {
            fprintf(stderr, "argumento desconhecido: %s\n", argv[i]);
            return -1;
        }
    }

    return 1;
}

static void write_result(FILE *out, const Config *cfg) {
    fprintf(out, "schema_version,run_id,workload_sha256,algorithm,scenario,seed,process_count,context_switch_cost,rr_quantum,makespan,mean_turnaround,context_switches,jain_slowdown_pct,status\n");
    fprintf(out, "1,%s-%" PRIu64 ",pending,%s,%s,%" PRIu64 ",%d,%d,%d,0,0,0,100,stub\n",
            cfg->scenario,
            cfg->seed,
            cfg->algorithm,
            cfg->scenario,
            cfg->seed,
            cfg->process_count,
            cfg->context_switch_cost,
            cfg->rr_quantum);
}

static int run_self_test(void) {
    uint64_t seed = 0;
    int value = 0;
    Config cfg = default_config();

    assert(parse_u64("42", &seed));
    assert(seed == 42);
    assert(!parse_u64("42x", &seed));
    assert(parse_positive_int("1000", &value));
    assert(value == 1000);
    assert(!parse_positive_int("0", &value));
    assert(cfg.process_count == 1000);
    assert(cfg.context_switch_cost == 1);
    assert(cfg.rr_quantum == 4);

    return 0;
}

int main(int argc, char **argv) {
    Config cfg = default_config();
    int parsed = parse_args(argc, argv, &cfg);

    if (parsed <= 0) {
        return parsed == 0 ? 0 : 2;
    }

    if (cfg.self_test) {
        return run_self_test();
    }

    if (cfg.output_path != NULL) {
        FILE *file = fopen(cfg.output_path, "w");
        if (file == NULL) {
            perror(cfg.output_path);
            return 1;
        }
        write_result(file, &cfg);
        fclose(file);
        return 0;
    }

    write_result(stdout, &cfg);
    return 0;
}erro_intencional
erro_intencional
