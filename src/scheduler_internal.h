#ifndef SCHEDULER_INTERNAL_H
#define SCHEDULER_INTERNAL_H

#include "scheduler.h"

typedef struct {
    int (*on_arrival)(void *context, const SchedulerProcessView *process);
    int (*on_io_complete)(void *context, const SchedulerProcessView *process);
    int (*on_preempted)(void *context, const SchedulerProcessView *process);
    int (*on_finish)(void *context, int pid);
    SchedulerSelectResult (*select_next)(void *context, int64_t current_time,
                                         int *pid);
    int (*should_preempt)(const void *context, int quantum_used);
    void (*destroy)(void *context);
} SchedulerOperations;

struct Scheduler {
    const char *name;
    const SchedulerOperations *operations;
    void *context;
};

Scheduler *scheduler_fcfs_create(void);
Scheduler *scheduler_pdbh_create(void);
Scheduler *scheduler_rr_create(int quantum);
Scheduler *scheduler_priority_create(void);
Scheduler *scheduler_sjf_create(void);

#endif
