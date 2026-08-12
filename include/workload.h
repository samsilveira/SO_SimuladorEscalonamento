#ifndef WORKLOAD_H
#define WORKLOAD_H

#include "queue.h"
#include <stdint.h>

int workload_is_valid_scenario(const char *scenario);
int workload_is_valid_algorithm(const char *algorithm);

ProcessQueue* workload_generate(int process_count, const char *scenario, uint64_t seed);

int workload_export_csv(ProcessQueue *q, const char *filename);

#endif 