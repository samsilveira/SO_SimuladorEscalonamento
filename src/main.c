#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_process.h"
#include "workload.h"

#define SIMULADOR_VERSION "1.0.0"

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

    if (text == NULL || *text == '\0') {
        return 0;
    }
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    if (*text == '-') {
        return 0; 
    }

    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    *out = (uint64_t)value;
    return 1;
}

static int parse_int_in_range(const char *text, int *out, int min, int max) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < min || value > max) {
        return 0; // Rejeita letras, fora dos limites, ou erros de parse
    }

    *out = (int)value;
    return 1;
}

static int next_arg(int argc, char **argv, int *index, const char **value) {
    if (*index + 1 >= argc) {
        fprintf(stderr, "Erro: argumento sem valor para a opcao %s\n", argv[*index]);
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
            printf("Uso: %s [opcoes]\n", argv[0]);
            printf("\nOpcoes de Simulacao:\n");
            printf("  --scenario NOME         Cenario de simulacao (equilibrado, io_bound, cpu_bound, prioridades_desbalanceadas). Padrao: equilibrado\n");
            printf("  --algorithm NOME        Algoritmo de escalonamento (fcfs, rr, prioridade, proprio). Padrao: fcfs\n");
            printf("  --seed N                Semente do gerador pseudoaleatorio (inteiro 64 bits nao negativo). Padrao: 1\n");
            printf("  --processes N           Quantidade de processos a simular (1 a 100000). Padrao: 1000\n");
            printf("  --context-switch-cost N Custo de troca de contexto em ticks (0 a 1000000). Padrao: 1\n");
            printf("  --rr-quantum N          Quantum do algoritmo Round Robin em ticks (1 a 1000000). Padrao: 4\n");
            printf("\nOutras Opcoes:\n");
            printf("  --output ARQUIVO        Arquivo para salvar o resultado em formato JSON\n");
            printf("  --self-test             Executa os testes internos de validacao dos modulos\n");
            printf("  --version               Exibe a versao do simulador\n");
            printf("  --help                  Exibe esta mensagem de ajuda detalhada\n");
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("Versao: %s\n", SIMULADOR_VERSION);
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
                fprintf(stderr, "Erro: Semente invalida. Deve ser um inteiro positivo (64-bits).\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--processes") == 0) {
            if (!next_arg(argc, argv, &i, &value) || !parse_int_in_range(value, &cfg->process_count, 1, 100000)) {
                fprintf(stderr, "Erro: Quantidade de processos invalida. Permitido: 1 a 100000.\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--context-switch-cost") == 0) {
            if (!next_arg(argc, argv, &i, &value) || !parse_int_in_range(value, &cfg->context_switch_cost, 0, 1000000)) {
                fprintf(stderr, "Erro: Custo de troca de contexto invalido. Permitido: 0 a 1000000.\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--rr-quantum") == 0) {
            if (!next_arg(argc, argv, &i, &value) || !parse_int_in_range(value, &cfg->rr_quantum, 1, 1000000)) {
                fprintf(stderr, "Erro: Quantum invalido. Permitido: 1 a 1000000.\n");
                return -1;
            }
        } else {
            fprintf(stderr, "Erro: Argumento desconhecido: %s\n", argv[i]);
            return -1;
        }
    }

    return 1;
}

static void write_result(FILE *out, const Config *cfg) {
    fprintf(out, "{\n");
    fprintf(out, "  \"version\": \"1.0\",\n");
    fprintf(out, "  \"algorithm\": \"%s\",\n", cfg->algorithm);
    fprintf(out, "  \"scenario\": \"%s\",\n", cfg->scenario);
    fprintf(out, "  \"seed\": %" PRIu64 ",\n", cfg->seed);
    fprintf(out, "  \"quantum\": %d,\n", cfg->rr_quantum);
    fprintf(out, "  \"context_switch_cost\": %d,\n", cfg->context_switch_cost);
    fprintf(out, "  \"n_processes\": %d,\n", cfg->process_count);
    fprintf(out, "  \"metrics\": {\n");
    fprintf(out, "    \"avg_turnaround\": 0,\n");
    fprintf(out, "    \"total_context_switches\": 0,\n");
    fprintf(out, "    \"jain_fairness_pct\": 0\n");
    fprintf(out, "  }\n");
    fprintf(out, "}\n");
}

static int run_self_test(void) {
    uint64_t seed = 0;
    int value = 0;
    Config cfg = default_config();

    if (!parse_u64("42", &seed) || seed != 42) return 1;
    if (parse_u64("42x", &seed)) return 1;
    if (parse_u64("-1", &seed)) return 1;
    
    if (!parse_int_in_range("1000", &value, 1, 100000) || value != 1000) return 1;
    if (parse_int_in_range("0", &value, 1, 100000)) return 1; 
    if (parse_int_in_range("100001", &value, 1, 100000)) return 1; 
    if (parse_int_in_range("-1", &value, 0, 1000000)) return 1; 

    if (cfg.process_count != 1000) return 1;
    if (cfg.context_switch_cost != 1) return 1;
    if (cfg.rr_quantum != 4) return 1;

    if (process_run_all_tests() != 0) return 1;

    printf("Todos os self-tests passaram com sucesso!\n");
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

    ProcessQueue *workload = workload_generate(cfg.process_count, cfg.scenario, cfg.seed);
    if (workload != NULL) {
        if (!workload_export_csv(workload, "load.csv")) {
            fprintf(stderr, "Erro ao tentar salvar o arquivo load.csv\n");
        }
    } else {
        fprintf(stderr, "Erro crítico na geração da carga de processos.\n");
        return 1;
    }

    if (cfg.output_path != NULL) {
        FILE *file = fopen(cfg.output_path, "w");
        if (file == NULL) {
            perror(cfg.output_path);
            return 1;
        }
        write_result(file, &cfg);
        fclose(file);
    } else {
        write_result(stdout, &cfg);
    }

    if (workload != NULL) {
        queue_destroy(workload);
    }

    return 0;
}