#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

/*
 * Visao deliberadamente limitada de um processo pronto. O motor conserva o
 * Process completo; a politica recebe apenas fatos que ja foram observados.
 */
typedef struct {
    int pid;
    int priority;
    int64_t arrival;
    int64_t ready_since;
    int64_t cpu_consumed;
} SchedulerProcessView;

typedef struct Scheduler Scheduler;

typedef enum {
    SCHEDULER_SELECT_ERROR = -1,
    SCHEDULER_SELECT_EMPTY = 0,
    SCHEDULER_SELECT_OK = 1
} SchedulerSelectResult;

Scheduler *scheduler_create(const char *name, int rr_quantum);
void scheduler_destroy(Scheduler *scheduler);

int scheduler_on_arrival(Scheduler *scheduler, const SchedulerProcessView *process);
int scheduler_on_io_complete(Scheduler *scheduler, const SchedulerProcessView *process);
int scheduler_on_preempted(Scheduler *scheduler, const SchedulerProcessView *process);
int scheduler_on_finish(Scheduler *scheduler, int pid);
SchedulerSelectResult scheduler_select_next(Scheduler *scheduler,
                                            int64_t current_time,
                                            int *pid);
int scheduler_should_preempt(const Scheduler *scheduler, int quantum_used);
const char *scheduler_name(const Scheduler *scheduler);

#endif
