#include "config.h"

#include "io_utils.h"
#include "workload.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_SCHEMA_VERSION 1
#define SIMULADOR_VERSION "1.1.0"

enum ConfigKey {
    KEY_SCHEMA_VERSION,
    KEY_SCENARIO,
    KEY_ALGORITHM,
    KEY_SEED,
    KEY_PROCESS_COUNT,
    KEY_CONTEXT_SWITCH_COST,
    KEY_RR_QUANTUM,
    KEY_ARRIVAL_MIN,
    KEY_ARRIVAL_MAX,
    KEY_COUNT
};

static int parse_u64(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value;

    if (text == NULL || *text == '\0' || isspace((unsigned char)*text) || *text == '-') {
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

    if (text == NULL || *text == '\0' || isspace((unsigned char)*text)) {
        return 0;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < min || value > max) {
        return 0;
    }
    *out = (int)value;
    return 1;
}

static int copy_text(char destination[CONFIG_TEXT_CAPACITY], const char *source) {
    size_t length;

    if (source == NULL) return 0;
    length = strlen(source);
    if (length == 0 || length >= CONFIG_TEXT_CAPACITY) return 0;
    memcpy(destination, source, length + 1);
    return 1;
}

static char *trim(char *text) {
    char *end;

    while (isspace((unsigned char)*text)) text += 1;
    if (*text == '\0') return text;
    end = text + strlen(text) - 1;
    while (end >= text && isspace((unsigned char)*end)) {
        *end = '\0';
        end -= 1;
    }
    return text;
}

static int key_index(const char *key) {
    if (strcmp(key, "schema_version") == 0) return KEY_SCHEMA_VERSION;
    if (strcmp(key, "scenario") == 0) return KEY_SCENARIO;
    if (strcmp(key, "algorithm") == 0) return KEY_ALGORITHM;
    if (strcmp(key, "seed") == 0) return KEY_SEED;
    if (strcmp(key, "process_count") == 0) return KEY_PROCESS_COUNT;
    if (strcmp(key, "context_switch_cost") == 0) return KEY_CONTEXT_SWITCH_COST;
    if (strcmp(key, "rr_quantum") == 0) return KEY_RR_QUANTUM;
    if (strcmp(key, "arrival_min") == 0) return KEY_ARRIVAL_MIN;
    if (strcmp(key, "arrival_max") == 0) return KEY_ARRIVAL_MAX;
    return -1;
}

static int apply_config_value(Config *cfg, int key, const char *value, int line_number) {
    int parsed;

    switch (key) {
        case KEY_SCHEMA_VERSION:
            if (!parse_int_in_range(value, &parsed, CONFIG_SCHEMA_VERSION, CONFIG_SCHEMA_VERSION)) break;
            cfg->schema_version = parsed;
            return 1;
        case KEY_SCENARIO:
            if (workload_is_valid_scenario(value) && copy_text(cfg->scenario, value)) return 1;
            break;
        case KEY_ALGORITHM:
            if (workload_is_valid_algorithm(value) && copy_text(cfg->algorithm, value)) return 1;
            break;
        case KEY_SEED:
            if (parse_u64(value, &cfg->seed)) return 1;
            break;
        case KEY_PROCESS_COUNT:
            if (parse_int_in_range(value, &cfg->process_count, 1, 100000)) return 1;
            break;
        case KEY_CONTEXT_SWITCH_COST:
            if (parse_int_in_range(value, &cfg->context_switch_cost, 0, 1000000)) return 1;
            break;
        case KEY_RR_QUANTUM:
            if (parse_int_in_range(value, &cfg->rr_quantum, 1, 1000000)) return 1;
            break;
        case KEY_ARRIVAL_MIN:
            if (parse_int_in_range(value, &cfg->arrival_min, 0, 0)) return 1;
            break;
        case KEY_ARRIVAL_MAX:
            if (parse_int_in_range(value, &cfg->arrival_max, 3, 3)) return 1;
            break;
    }

    fprintf(stderr, "Erro: valor invalido na linha %d do arquivo de configuracao: '%s'\n",
            line_number, value);
    return 0;
}

static int load_config_file(const char *path, Config *cfg) {
    FILE *file;
    char line[512];
    unsigned int seen = 0;
    int line_number = 0;
    int ok = 1;

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "Erro ao abrir configuracao '%s': %s\n", path, strerror(errno));
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *content;
        char *separator;
        char *key;
        char *value;
        int index;

        line_number += 1;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            fprintf(stderr, "Erro: linha %d da configuracao excede o limite\n", line_number);
            ok = 0;
            break;
        }
        content = trim(line);
        if (*content == '\0' || *content == '#') continue;
        separator = strchr(content, '=');
        if (separator == NULL || strchr(separator + 1, '=') != NULL) {
            fprintf(stderr, "Erro: linha %d da configuracao deve usar chave=valor\n", line_number);
            ok = 0;
            break;
        }
        *separator = '\0';
        key = trim(content);
        value = trim(separator + 1);
        index = key_index(key);
        if (index < 0) {
            fprintf(stderr, "Erro: chave desconhecida na linha %d: '%s'\n", line_number, key);
            ok = 0;
            break;
        }
        if ((seen & (1U << (unsigned int)index)) != 0U) {
            fprintf(stderr, "Erro: chave duplicada na linha %d: '%s'\n", line_number, key);
            ok = 0;
            break;
        }
        seen |= 1U << (unsigned int)index;
        if (!apply_config_value(cfg, index, value, line_number)) {
            ok = 0;
            break;
        }
    }

    if (ok && ferror(file)) {
        fprintf(stderr, "Erro ao ler configuracao '%s'\n", path);
        ok = 0;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "Erro ao fechar configuracao '%s': %s\n", path, strerror(errno));
        ok = 0;
    }
    return ok;
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

static int valid_run_id(const char *run_id) {
    size_t i;
    size_t length;

    if (run_id == NULL) return 0;
    length = strlen(run_id);
    if (length == 0 || length >= CONFIG_TEXT_CAPACITY) return 0;
    for (i = 0; i < length; i += 1) {
        unsigned char c = (unsigned char)run_id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
              || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return 0;
        }
    }
    return 1;
}

static void print_help(const char *program) {
    printf("Uso: %s [opcoes]\n", program);
    printf("\nConfiguracao e simulacao:\n");
    printf("  --config ARQUIVO         Carrega configuracao chave=valor\n");
    printf("  --scenario NOME          equilibrado, io_bound, cpu_bound ou prioridades_desbalanceadas\n");
    printf("  --algorithm NOME         fcfs, rr, prioridade, sjf ou proprio\n");
    printf("  --seed N                 Seed de 64 bits sem sinal\n");
    printf("  --processes N            Quantidade de processos (1 a 100000)\n");
    printf("  --context-switch-cost N  Custo da troca (0 a 1000000)\n");
    printf("  --rr-quantum N           Quantum do RR (1 a 1000000)\n");
    printf("  --allow-zero-context-switch-cost  Autoriza analise complementar com custo zero\n");
    printf("\nCarga e resultados:\n");
    printf("  --workload-input CSV     Importa carga normalizada\n");
    printf("  --workload-output CSV    Exporta carga normalizada\n");
    printf("  --run-id ID              Obrigatorio para resultados persistidos\n");
    printf("  --output CSV             Resultado agregado da execucao\n");
    printf("  --individual-output CSV  Metricas individuais opcionais\n");
    printf("\nOutras opcoes:\n");
    printf("  --self-test              Executa testes internos\n");
    printf("  --version                Exibe a versao\n");
    printf("  --help                   Exibe esta ajuda\n");
}

void config_set_defaults(Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->schema_version = CONFIG_SCHEMA_VERSION;
    copy_text(cfg->scenario, "equilibrado");
    copy_text(cfg->algorithm, "fcfs");
    cfg->run_id = "adhoc";
    cfg->seed = 1;
    cfg->process_count = 1000;
    cfg->context_switch_cost = 1;
    cfg->rr_quantum = 4;
    cfg->arrival_min = 0;
    cfg->arrival_max = 3;
}

int config_parse(int argc, char **argv, Config *cfg) {
    int i;
    int config_count = 0;

    for (i = 1; i < argc; i += 1) {
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Erro: argumento sem valor para --config\n");
                return -1;
            }
            cfg->config_path = argv[i + 1];
            config_count += 1;
            i += 1;
        }
    }
    if (config_count > 1) {
        fprintf(stderr, "Erro: --config foi informado mais de uma vez\n");
        return -1;
    }
    if (cfg->config_path != NULL && !load_config_file(cfg->config_path, cfg)) {
        return -1;
    }

    for (i = 1; i < argc; i += 1) {
        const char *value = NULL;

        if (strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("Versao: %s\n", SIMULADOR_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--self-test") == 0) {
            cfg->self_test = 1;
        } else if (strcmp(argv[i], "--allow-zero-context-switch-cost") == 0) {
            cfg->allow_zero_context_switch_cost = 1;
        } else if (strcmp(argv[i], "--config") == 0) {
            if (!next_arg(argc, argv, &i, &value)) return -1;
        } else if (strcmp(argv[i], "--scenario") == 0) {
            if (!next_arg(argc, argv, &i, &value) || !workload_is_valid_scenario(value)
                || !copy_text(cfg->scenario, value)) {
                fprintf(stderr, "Erro: cenario invalido\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--algorithm") == 0) {
            if (!next_arg(argc, argv, &i, &value) || !workload_is_valid_algorithm(value)
                || !copy_text(cfg->algorithm, value)) {
                fprintf(stderr, "Erro: algoritmo invalido\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--seed") == 0) {
            if (!next_arg(argc, argv, &i, &value) || !parse_u64(value, &cfg->seed)) {
                fprintf(stderr, "Erro: seed invalida\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--processes") == 0) {
            if (!next_arg(argc, argv, &i, &value)
                || !parse_int_in_range(value, &cfg->process_count, 1, 100000)) {
                fprintf(stderr, "Erro: quantidade de processos invalida\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--context-switch-cost") == 0) {
            if (!next_arg(argc, argv, &i, &value)
                || !parse_int_in_range(value, &cfg->context_switch_cost, 0, 1000000)) {
                fprintf(stderr, "Erro: custo de troca invalido\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--rr-quantum") == 0) {
            if (!next_arg(argc, argv, &i, &value)
                || !parse_int_in_range(value, &cfg->rr_quantum, 1, 1000000)) {
                fprintf(stderr, "Erro: quantum invalido\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--output") == 0) {
            if (!next_arg(argc, argv, &i, &cfg->output_path)) return -1;
        } else if (strcmp(argv[i], "--individual-output") == 0) {
            if (!next_arg(argc, argv, &i, &cfg->individual_output_path)) return -1;
        } else if (strcmp(argv[i], "--workload-input") == 0) {
            if (!next_arg(argc, argv, &i, &cfg->workload_input_path)) return -1;
        } else if (strcmp(argv[i], "--workload-output") == 0) {
            if (!next_arg(argc, argv, &i, &cfg->workload_output_path)) return -1;
        } else if (strcmp(argv[i], "--run-id") == 0) {
            if (!next_arg(argc, argv, &i, &cfg->run_id) || !valid_run_id(cfg->run_id)) {
                fprintf(stderr, "Erro: run_id deve usar apenas letras, numeros, ponto, hifen ou sublinhado\n");
                return -1;
            }
            cfg->run_id_explicit = 1;
        } else {
            fprintf(stderr, "Erro: argumento desconhecido: %s\n", argv[i]);
            return -1;
        }
    }

    if ((cfg->output_path != NULL || cfg->individual_output_path != NULL)
        && !cfg->run_id_explicit) {
        fprintf(stderr, "Erro: --run-id e obrigatorio para resultados persistidos\n");
        return -1;
    }
    if ((cfg->output_path != NULL && *cfg->output_path == '\0')
        || (cfg->config_path != NULL && *cfg->config_path == '\0')
        || (cfg->individual_output_path != NULL && *cfg->individual_output_path == '\0')
        || (cfg->workload_input_path != NULL && *cfg->workload_input_path == '\0')
        || (cfg->workload_output_path != NULL && *cfg->workload_output_path == '\0')) {
        fprintf(stderr, "Erro: caminhos de entrada e saida nao podem ser vazios\n");
        return -1;
    }
    if (cfg->context_switch_cost == 0 && !cfg->allow_zero_context_switch_cost) {
        fprintf(stderr, "Erro: custo zero exige --allow-zero-context-switch-cost\n");
        return -1;
    }
    if (paths_refer_to_same_file(cfg->output_path, cfg->individual_output_path)
        || paths_refer_to_same_file(cfg->output_path, cfg->workload_output_path)
        || paths_refer_to_same_file(cfg->individual_output_path, cfg->workload_output_path)
        || paths_refer_to_same_file(cfg->output_path, cfg->workload_input_path)
        || paths_refer_to_same_file(cfg->individual_output_path, cfg->workload_input_path)
        || paths_refer_to_same_file(cfg->workload_output_path, cfg->workload_input_path)
        || paths_refer_to_same_file(cfg->output_path, cfg->config_path)
        || paths_refer_to_same_file(cfg->individual_output_path, cfg->config_path)
        || paths_refer_to_same_file(cfg->workload_output_path, cfg->config_path)) {
        fprintf(stderr, "Erro: caminhos de entrada e saida conflitantes\n");
        return -1;
    }
    return 1;
}
