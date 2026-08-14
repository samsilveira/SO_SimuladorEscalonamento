#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "queue.h"

typedef enum {
    SCHEDULER_FCFS,
    SCHEDULER_RR,
    SCHEDULER_PRIORITY,
    SCHEDULER_PROPRIO
} SchedulerKind;

typedef struct Scheduler {
    SchedulerKind kind;
    int quantum;
    ProcessQueue *ready;
} Scheduler;

Scheduler *scheduler_create(const char *name, int rr_quantum);
void scheduler_destroy(Scheduler *scheduler);
int scheduler_enqueue(Scheduler *scheduler, Process *process);
Process *scheduler_next(Scheduler *scheduler);
int scheduler_should_preempt(const Scheduler *scheduler, int quantum_used);
const char *scheduler_name(const Scheduler *scheduler);

#endif
