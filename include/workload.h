#ifndef WORKLOAD_H
#define WORKLOAD_H

#include "queue.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int workload_is_valid_scenario(const char *scenario);
int workload_is_valid_algorithm(const char *algorithm);

ProcessQueue* workload_generate(int process_count, const char *scenario, uint64_t seed);
int workload_write_csv(FILE *file, const ProcessQueue *q);
int workload_export_csv(ProcessQueue *q, const char *filename);
ProcessQueue *workload_import_csv(const char *filename, int *process_count,
                                  char *error, size_t error_size);
int workload_sha256(const ProcessQueue *q, char hash_hex[65]);
int workload_process_count(const ProcessQueue *q);
void workload_destroy(ProcessQueue *q);

#endif
