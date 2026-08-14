#include "workload.h"

#include "io_utils.h"
#include "process.h"
#include "rng.h"
#include "sha256.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORKLOAD_HEADER "pid,arrival,priority,burst_index,burst_type,duration\n"

int workload_is_valid_scenario(const char *scenario) {
    if (!scenario) return 0;
    if (strcmp(scenario, "equilibrado") == 0) return 1;
    if (strcmp(scenario, "io_bound") == 0) return 1;
    if (strcmp(scenario, "cpu_bound") == 0) return 1;
    if (strcmp(scenario, "prioridades_desbalanceadas") == 0) return 1;
    return 0;
}

int workload_is_valid_algorithm(const char *algorithm) {
    if (!algorithm) return 0;
    if (strcmp(algorithm, "fcfs") == 0) return 1;
    if (strcmp(algorithm, "rr") == 0) return 1;
    if (strcmp(algorithm, "prioridade") == 0) return 1;
    if (strcmp(algorithm, "proprio") == 0) return 1;
    return 0;
}

static int get_scenario_id(const char *scenario) {
    if (!scenario) return 0;
    if (strcmp(scenario, "equilibrado") == 0) return 1;
    if (strcmp(scenario, "io_bound") == 0) return 2;
    if (strcmp(scenario, "cpu_bound") == 0) return 3;
    if (strcmp(scenario, "prioridades_desbalanceadas") == 0) return 4;
    return 0;
}

void workload_destroy(ProcessQueue *q) {
    if (!q) return;
    while (!queue_is_empty(q)) {
        process_destroy(queue_pop(q));
    }
    queue_destroy(q);
}

int workload_process_count(const ProcessQueue *q) {
    const ProcessNode *node;
    int count = 0;

    if (q == NULL) return 0;
    for (node = q->head; node != NULL; node = node->next) {
        count += 1;
    }
    return count;
}

ProcessQueue* workload_generate(int process_count, const char *scenario, uint64_t seed) {
    int scenario_id = get_scenario_id(scenario);
    ProcessQueue *q;
    int arrival = 0;
    int pid;

    if (scenario_id <= 0 || process_count < 1 || process_count > 100000) return NULL;
    q = queue_create(QUEUE_FUTURE, compare_arrival);
    if (!q) return NULL;
    rng_init(seed, (uint64_t)scenario_id);

    for (pid = 1; pid <= process_count; pid += 1) {
        int priority;
        int is_pouca_es = 0;
        int num_bursts;
        int i;
        Process *p;

        // Etapa 2: Lógica de chegada distribuída (evita t=0 para todos)
        if (pid > 1) arrival += rng_next_range(0, 3);
        
        // Etapa 2: Distribuição de Prioridades (70% Alta / 30% Baixa)
        if (scenario_id == 4) {
            // Alta prioridade mapeada para [0, 2] e Baixa para [7, 9] (considerando range total 0-9)
            priority = (rng_next_range(0, 99) < 70) 
                ? rng_next_range(0, 2) 
                : rng_next_range(7, 9);
        } else {
            // Outros cenários: Prioridade Uniforme [0, 9] (representando 1 a 10)
            priority = rng_next_range(0, 9);
        }
        
        if (scenario_id == 1 || scenario_id == 4) {
            is_pouca_es = rng_next_range(0, 1);
        }
        if (scenario_id == 2) {
            num_bursts = rng_next_range(5, 8);
        } else if (scenario_id == 3) {
            num_bursts = rng_next_range(1, 3);
        } else {
            num_bursts = is_pouca_es ? rng_next_range(1, 2) : rng_next_range(4, 7);
        }

        p = process_create(pid, arrival, priority);
        if (!p) {
            workload_destroy(q);
            return NULL;
        }
        for (i = 0; i < num_bursts; i += 1) {
            int cpu;
            int io = 0;

            if (scenario_id == 2) cpu = rng_next_range(2, 8);
            else if (scenario_id == 3) cpu = rng_next_range(20, 60);
            else cpu = rng_next_range(5, 20);

            if (i < num_bursts - 1) {
                if (scenario_id == 2) io = rng_next_range(15, 40);
                else if (scenario_id == 3) io = rng_next_range(2, 8);
                else io = rng_next_range(5, 20);
            }
            if (!process_add_burst(p, cpu, io)) {
                process_destroy(p);
                workload_destroy(q);
                return NULL;
            }
        }
        if (!queue_insert(q, p)) {
            process_destroy(p);
            workload_destroy(q);
            return NULL;
        }
    }
    return q;
}

static int emit_bytes(FILE *file, Sha256Context *hash, const char *bytes, size_t length) {
    if (hash != NULL) sha256_update(hash, bytes, length);
    if (file != NULL && fwrite(bytes, 1, length, file) != length) return 0;
    return 1;
}

static int serialize_normalized(FILE *file, const ProcessQueue *q, Sha256Context *hash) {
    const ProcessNode *node;

    if (q == NULL || q->head == NULL) {
        errno = EINVAL;
        return 0;
    }
    if (!emit_bytes(file, hash, WORKLOAD_HEADER, strlen(WORKLOAD_HEADER))) return 0;
    for (node = q->head; node != NULL; node = node->next) {
        const Process *process = node->process;
        const Burst *burst;
        int burst_index = 1;

        for (burst = process->bursts; burst != NULL; burst = burst->next) {
            char line[256];
            int length = snprintf(line, sizeof(line), "%d,%" PRId64 ",%d,%d,CPU,%" PRId64 "\n",
                                  process->pid, process->arrival_time, process->priority,
                                  burst_index, burst->cpu_time);
            if (length < 0 || (size_t)length >= sizeof(line)
                || !emit_bytes(file, hash, line, (size_t)length)) return 0;
            burst_index += 1;
            if (burst->io_time > 0) {
                length = snprintf(line, sizeof(line), "%d,%" PRId64 ",%d,%d,IO,%" PRId64 "\n",
                                  process->pid, process->arrival_time, process->priority,
                                  burst_index, burst->io_time);
                if (length < 0 || (size_t)length >= sizeof(line)
                    || !emit_bytes(file, hash, line, (size_t)length)) return 0;
                burst_index += 1;
            }
        }
    }
    return 1;
}

int workload_write_csv(FILE *file, const ProcessQueue *q) {
    if (file == NULL) {
        errno = EINVAL;
        return 0;
    }
    return serialize_normalized(file, q, NULL);
}

static int write_workload_callback(FILE *file, void *context) {
    return workload_write_csv(file, (const ProcessQueue *)context);
}

int workload_export_csv(ProcessQueue *q, const char *filename) {
    return atomic_write_file(filename, write_workload_callback, q);
}

int workload_sha256(const ProcessQueue *q, char hash_hex[65]) {
    Sha256Context context;
    unsigned char digest[32];

    if (hash_hex == NULL) return 0;
    sha256_init(&context);
    if (!serialize_normalized(NULL, q, &context)) return 0;
    sha256_final(&context, digest);
    sha256_digest_hex(digest, hash_hex);
    return 1;
}

static void set_error(char *error, size_t error_size, const char *format, ...) {
    va_list arguments;

    if (error == NULL || error_size == 0) return;
    va_start(arguments, format);
    vsnprintf(error, error_size, format, arguments);
    va_end(arguments);
}

static int parse_i64(const char *text, int64_t min, int64_t max, int64_t *out) {
    char *end = NULL;
    intmax_t value;

    if (text == NULL || *text == '\0') return 0;
    errno = 0;
    value = strtoimax(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < min || value > max) return 0;
    *out = (int64_t)value;
    return 1;
}

static int split_csv_line(char *line, char *fields[6]) {
    int count = 0;
    char *start = line;
    char *cursor = line;

    for (;;) {
        if (*cursor == ',' || *cursor == '\0') {
            char separator = *cursor;
            if (count >= 6 || cursor == start) return 0;
            *cursor = '\0';
            fields[count++] = start;
            if (separator == '\0') break;
            start = cursor + 1;
        }
        cursor += 1;
    }
    return count == 6;
}

static int finish_imported_process(ProcessQueue *queue, Process **current,
                                   int64_t pending_cpu, int expecting_io_or_end) {
    if (*current == NULL || !expecting_io_or_end
        || !process_add_burst(*current, pending_cpu, 0)
        || !queue_insert(queue, *current)) {
        return 0;
    }
    *current = NULL;
    return 1;
}

ProcessQueue *workload_import_csv(const char *filename, int *process_count,
                                  char *error, size_t error_size) {
    FILE *file = NULL;
    ProcessQueue *queue = NULL;
    Process *current = NULL;
    char line[512];
    int line_number = 0;
    int count = 0;
    int expected_pid = 1;
    int expected_index = 1;
    int expecting_io_or_end = 0;
    int cpu_bursts = 0;
    int64_t pending_cpu = 0;
    int64_t previous_arrival = 0;
    int ok = 0;

    if (filename == NULL || process_count == NULL) {
        set_error(error, error_size, "argumentos invalidos para importar workload");
        return NULL;
    }
    file = fopen(filename, "r");
    if (file == NULL) {
        set_error(error, error_size, "nao foi possivel abrir '%s': %s", filename, strerror(errno));
        return NULL;
    }
    queue = queue_create(QUEUE_FUTURE, compare_arrival);
    if (queue == NULL) {
        set_error(error, error_size, "memoria insuficiente ao importar workload");
        goto cleanup;
    }

    if (fgets(line, sizeof(line), file) == NULL) {
        set_error(error, error_size, "workload vazio ou ilegivel");
        goto cleanup;
    }
    line_number = 1;
    if (strcmp(line, WORKLOAD_HEADER) != 0) {
        set_error(error, error_size, "cabecalho invalido na linha 1");
        goto cleanup;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[6];
        size_t length;
        int64_t pid_value, arrival, priority_value, index_value, duration;
        int pid, priority, index;
        const char *burst_type;

        line_number += 1;
        length = strlen(line);
        if (length == 0 || (line[length - 1] != '\n' && !feof(file))) {
            set_error(error, error_size, "linha %d excede o limite", line_number);
            goto cleanup;
        }
        if (length > 0 && line[length - 1] == '\n') line[--length] = '\0';
        if (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
        if (!split_csv_line(line, fields)
            || !parse_i64(fields[0], 1, 100000, &pid_value)
            || !parse_i64(fields[1], 0, INT64_MAX, &arrival)
            || !parse_i64(fields[2], 0, 9, &priority_value)
            || !parse_i64(fields[3], 1, 2000, &index_value)
            || !parse_i64(fields[5], 1, 1000000, &duration)) {
            set_error(error, error_size, "campos invalidos na linha %d", line_number);
            goto cleanup;
        }
        pid = (int)pid_value;
        priority = (int)priority_value;
        index = (int)index_value;
        burst_type = fields[4];

        if (current == NULL || pid != current->pid) {
            if (current != NULL) {
                if (pid != expected_pid + 1
                    || !finish_imported_process(queue, &current,
                                                pending_cpu, expecting_io_or_end)) {
                    set_error(error, error_size, "PID ou sequencia de rajadas invalida na linha %d", line_number);
                    goto cleanup;
                }
                count += 1;
                expected_pid += 1;
            }
            if (pid != expected_pid || count >= 100000
                || (pid == 1 && arrival != 0)
                || (pid > 1 && (arrival < previous_arrival || arrival - previous_arrival > 3))) {
                set_error(error, error_size, "PID ou chegada invalida na linha %d", line_number);
                goto cleanup;
            }
            current = process_create(pid, arrival, priority);
            if (current == NULL) {
                set_error(error, error_size, "processo invalido na linha %d", line_number);
                goto cleanup;
            }
            previous_arrival = arrival;
            expected_index = 1;
            expecting_io_or_end = 0;
            cpu_bursts = 0;
        } else if (arrival != current->arrival_time || priority != current->priority) {
            set_error(error, error_size, "metadados inconsistentes para o PID %d", pid);
            goto cleanup;
        }

        if (index != expected_index) {
            set_error(error, error_size, "burst_index invalido na linha %d", line_number);
            goto cleanup;
        }
        expected_index += 1;
        if (!expecting_io_or_end) {
            if (strcmp(burst_type, "CPU") != 0 || cpu_bursts >= 1000) {
                set_error(error, error_size, "era esperada uma rajada CPU na linha %d", line_number);
                goto cleanup;
            }
            pending_cpu = duration;
            cpu_bursts += 1;
            expecting_io_or_end = 1;
        } else {
            if (strcmp(burst_type, "IO") != 0
                || !process_add_burst(current, pending_cpu, duration)) {
                set_error(error, error_size, "era esperada uma rajada IO ou fim do PID na linha %d", line_number);
                goto cleanup;
            }
            expecting_io_or_end = 0;
        }
    }

    if (ferror(file)) {
        set_error(error, error_size, "erro de leitura em '%s'", filename);
        goto cleanup;
    }
    if (!finish_imported_process(queue, &current, pending_cpu, expecting_io_or_end)) {
        set_error(error, error_size, "workload termina sem rajada CPU final valida");
        goto cleanup;
    }
    count += 1;
    *process_count = count;
    ok = 1;

cleanup:
    if (fclose(file) != 0 && ok) {
        set_error(error, error_size, "erro ao fechar '%s': %s", filename, strerror(errno));
        ok = 0;
    }
    if (!ok) {
        process_destroy(current);
        workload_destroy(queue);
        return NULL;
    }
    return queue;
}