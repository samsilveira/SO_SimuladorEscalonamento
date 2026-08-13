#ifndef SIMULATION_H
#define SIMULATION_H

#include "queue.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SIM_EVENT_ARRIVAL,
    SIM_EVENT_DISPATCH,
    SIM_EVENT_CPU_BURST_END,
    SIM_EVENT_IO_START,
    SIM_EVENT_IO_END,
    SIM_EVENT_CONTEXT_SWITCH,
    SIM_EVENT_PREEMPT,
    SIM_EVENT_FINISH,
    SIM_EVENT_IDLE
} SimulationEventType;

typedef struct {
    int64_t time;
    SimulationEventType type;
    int pid;
} SimulationEvent;

typedef struct {
    int pid;
    int64_t arrival;
    int64_t completion;
    int64_t turnaround;
    int64_t ideal_time;
    double slowdown;
    int64_t total_cpu;
    int64_t total_io;
} ProcessMetrics;

typedef struct {
    int64_t makespan;
    double mean_turnaround;
    uint64_t context_switches;
    double jain_slowdown_pct;
    int process_count;
    ProcessMetrics *process_metrics;
    size_t process_metrics_count;
    SimulationEvent *events;
    size_t event_count;
    size_t event_capacity;
} SimulationResult;

int simulation_run(ProcessQueue *workload,
                   const char *algorithm,
                   int context_switch_cost,
                   int rr_quantum,
                   SimulationResult *result);
void simulation_result_destroy(SimulationResult *result);
const char *simulation_event_name(SimulationEventType type);

#endif
