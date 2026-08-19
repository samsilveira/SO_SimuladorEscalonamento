#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "io_utils.h"
#include "process.h"
#include "rng.h"
#include "simulation.h"
#include "test_process.h"
#include "test_scheduler.h"
#include "test_simulation.h"
#include "test_workload.h"
#include "workload.h"

typedef struct {
    const Config *config;
    const SimulationResult *result;
    const char *workload_hash;
} ResultWriteContext;

typedef struct {
    const char *bytes;
    size_t length;
} BufferWriteContext;

static int write_buffer(FILE *file, void *opaque) {
    const BufferWriteContext *context = (const BufferWriteContext *)opaque;

    return fwrite(context->bytes, 1, context->length, file) == context->length;
}

static int write_aggregate_csv(FILE *file, void *opaque) {
    const ResultWriteContext *context = (const ResultWriteContext *)opaque;
    const Config *cfg = context->config;
    const SimulationResult *result = context->result;

    if (fprintf(file,
                "schema_version,run_id,workload_sha256,algorithm,scenario,seed,"
                "process_count,context_switch_cost,rr_quantum,makespan,"
                "mean_turnaround,context_switches,jain_slowdown_pct,status\n") < 0) {
        return 0;
    }
    return fprintf(file,
                   "%d,%s,%s,%s,%s,%" PRIu64 ",%d,%d,%d,%" PRId64
                   ",%.17g,%" PRIu64 ",%.17g,success\n",
                   cfg->schema_version, cfg->run_id, context->workload_hash,
                   cfg->algorithm, cfg->scenario, cfg->seed, result->process_count,
                   cfg->context_switch_cost, cfg->rr_quantum, result->makespan,
                   result->mean_turnaround, result->context_switches,
                   result->jain_slowdown_pct) >= 0;
}

static int write_individual_csv(FILE *file, void *opaque) {
    const ResultWriteContext *context = (const ResultWriteContext *)opaque;
    const SimulationResult *result = context->result;
    size_t i;

    if (fprintf(file,
                "run_id,pid,arrival,completion,turnaround,ideal_time,slowdown,"
                "priority,total_cpu,total_io,io_requests\n") < 0) {
        return 0;
    }
    for (i = 0; i < result->process_metrics_count; i += 1) {
        const ProcessMetrics *metrics = &result->process_metrics[i];
        if (fprintf(file,
                    "%s,%d,%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64
                    ",%.17g,%d,%" PRId64 ",%" PRId64 ",%d\n",
                    context->config->run_id, metrics->pid, metrics->arrival,
                    metrics->completion, metrics->turnaround, metrics->ideal_time,
                    metrics->slowdown, metrics->priority, metrics->total_cpu,
                    metrics->total_io, metrics->io_requests) < 0) {
            return 0;
        }
    }
    return 1;
}

static int run_self_test(void) {
    uint32_t first;
    uint32_t second;
    ProcessQueue *workload;

    rng_init(42, 1);
    first = rng_next();
    second = rng_next();
    rng_init(42, 1);
    if (rng_next() != first || rng_next() != second) return 1;
    if (workload_generate(10, "invalido", 42) != NULL) return 1;
    workload = workload_generate(10, "equilibrado", 42);
    if (workload == NULL || workload_process_count(workload) != 10) {
        workload_destroy(workload);
        return 1;
    }
    workload_destroy(workload);

    if (process_run_all_tests() != 0) return 1;
    if (scheduler_run_all_tests() != 0) return 1;
    if (simulation_run_all_tests() != 0) return 1;
    if (workload_run_all_tests() != 0) return 1;
    printf("Todos os self-tests passaram com sucesso!\n");
    return 0;
}

static int report_output_error(const char *kind, const char *path) {
    fprintf(stderr, "Erro ao escrever %s '%s': %s\n", kind, path, strerror(errno));
    return 1;
}

int main(int argc, char **argv) {
    Config cfg;
    SimulationResult result = {0};
    ProcessQueue *workload = NULL;
    ResultWriteContext write_context;
    AtomicWriteRequest output_requests[3];
    size_t output_request_count = 0;
    char *workload_csv = NULL;
    size_t workload_csv_size = 0;
    BufferWriteContext workload_write_context;
    char workload_hash[65];
    char import_error[256];
    int actual_process_count = 0;
    int parsed;

    config_set_defaults(&cfg);
    parsed = config_parse(argc, argv, &cfg);
    if (parsed <= 0) return parsed == 0 ? 0 : 2;
    if (cfg.self_test) return run_self_test();

    if (cfg.workload_input_path != NULL) {
        workload = workload_import_csv(cfg.workload_input_path, &actual_process_count,
                                       import_error, sizeof(import_error));
        if (workload == NULL) {
            fprintf(stderr, "Erro ao importar workload: %s\n", import_error);
            return 1;
        }
        if (actual_process_count != cfg.process_count) {
            fprintf(stderr,
                    "Erro: workload possui %d processos, mas a configuracao efetiva exige %d\n",
                    actual_process_count, cfg.process_count);
            workload_destroy(workload);
            return 1;
        }
    } else {
        workload = workload_generate(cfg.process_count, cfg.scenario, cfg.seed);
        actual_process_count = cfg.process_count;
        if (workload == NULL) {
            fprintf(stderr, "Erro critico na geracao da carga de processos\n");
            return 1;
        }
    }

    if (!workload_sha256(workload, workload_hash)) {
        fprintf(stderr, "Erro ao calcular SHA-256 do workload normalizado\n");
        workload_destroy(workload);
        return 1;
    }
    if (cfg.workload_output_path != NULL) {
        FILE *stream = open_memstream(&workload_csv, &workload_csv_size);
        int stream_ok = 0;

        if (stream != NULL) {
            int saved_errno = 0;

            if (!workload_write_csv(stream, workload)
                || fflush(stream) != 0 || ferror(stream)) {
                saved_errno = errno != 0 ? errno : EIO;
            }
            if (fclose(stream) != 0 && saved_errno == 0) saved_errno = errno;
            stream = NULL;
            if (saved_errno == 0) {
                stream_ok = 1;
            } else {
                errno = saved_errno;
            }
        }
        if (!stream_ok) {
            int saved_errno = errno;
            if (stream != NULL) fclose(stream);
            free(workload_csv);
            workload_destroy(workload);
            errno = saved_errno != 0 ? saved_errno : EIO;
            return report_output_error("workload", cfg.workload_output_path);
        }
    }

    if (!simulation_run(workload, cfg.algorithm, cfg.context_switch_cost,
                        cfg.rr_quantum, &result)) {
        fprintf(stderr, "Erro critico na simulacao. Verifique algoritmo e parametros\n");
        free(workload_csv);
        return 1;
    }
    if (result.process_count != actual_process_count) {
        fprintf(stderr, "Erro interno: quantidade simulada diverge do workload\n");
        free(workload_csv);
        simulation_result_destroy(&result);
        return 1;
    }

    write_context.config = &cfg;
    write_context.result = &result;
    write_context.workload_hash = workload_hash;

    if (cfg.workload_output_path != NULL) {
        workload_write_context = (BufferWriteContext){workload_csv, workload_csv_size};
        output_requests[output_request_count++] = (AtomicWriteRequest){
            cfg.workload_output_path, write_buffer, &workload_write_context
        };
    }

    if (cfg.individual_output_path != NULL) {
        output_requests[output_request_count++] = (AtomicWriteRequest){
            cfg.individual_output_path, write_individual_csv, &write_context
        };
    }
    if (cfg.output_path != NULL) {
        output_requests[output_request_count++] = (AtomicWriteRequest){
            cfg.output_path, write_aggregate_csv, &write_context
        };
    }
    if (output_request_count > 0
        && !atomic_write_files(output_requests, output_request_count)) {
        if (output_request_count == 1) {
            const char *kind;
            const char *path;

            if (cfg.workload_output_path != NULL) {
                kind = "workload";
                path = cfg.workload_output_path;
            } else if (cfg.individual_output_path != NULL) {
                kind = "metricas individuais";
                path = cfg.individual_output_path;
            } else {
                kind = "resultado agregado";
                path = cfg.output_path;
            }
            simulation_result_destroy(&result);
            free(workload_csv);
            return report_output_error(kind, path);
        }
        fprintf(stderr, "Erro ao publicar as saidas da execucao: %s\n", strerror(errno));
        simulation_result_destroy(&result);
        free(workload_csv);
        return 1;
    }
    if (cfg.output_path == NULL
        && (!write_aggregate_csv(stdout, &write_context)
            || fflush(stdout) != 0 || ferror(stdout))) {
        fprintf(stderr, "Erro ao escrever resultado agregado na saida padrao\n");
        simulation_result_destroy(&result);
        free(workload_csv);
        return 1;
    }

    simulation_result_destroy(&result);
    free(workload_csv);
    return 0;
}
