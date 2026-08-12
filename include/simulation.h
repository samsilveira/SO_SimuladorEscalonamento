#ifndef SIMULATION_H
#define SIMULATION_H

#include "queue.h"

#include <stddef.h>

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
    int time;
    SimulationEventType type;
    int pid;
} SimulationEvent;

typedef struct {
    int makespan;
    double mean_turnaround;
    int context_switches;
    double jain_slowdown_pct;
    int process_count;
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
