#include "scheduler_internal.h"

#include <stdlib.h>
#include <string.h>

Scheduler *scheduler_create(const char *name, int rr_quantum) {
    if (name == NULL || rr_quantum <= 0) return NULL;
    if (strcmp(name, "fcfs") == 0) return scheduler_fcfs_create();
    if (strcmp(name, "rr") == 0) return scheduler_rr_create(rr_quantum);
    if (strcmp(name, "prioridade") == 0) return scheduler_priority_create();
    if (strcmp(name, "proprio") == 0) {
        return scheduler_pdbh_create();
    }
    return NULL;
}

void scheduler_destroy(Scheduler *scheduler) {
    if (scheduler == NULL) return;
    scheduler->operations->destroy(scheduler->context);
    free(scheduler);
}

int scheduler_on_arrival(Scheduler *scheduler, const SchedulerProcessView *process) {
    return scheduler != NULL
        && scheduler->operations->on_arrival(scheduler->context, process);
}

int scheduler_on_io_complete(Scheduler *scheduler, const SchedulerProcessView *process) {
    return scheduler != NULL
        && scheduler->operations->on_io_complete(scheduler->context, process);
}

int scheduler_on_preempted(Scheduler *scheduler, const SchedulerProcessView *process) {
    return scheduler != NULL
        && scheduler->operations->on_preempted(scheduler->context, process);
}

int scheduler_on_finish(Scheduler *scheduler, int pid) {
    return scheduler != NULL && scheduler->operations->on_finish(scheduler->context, pid);
}

SchedulerSelectResult scheduler_select_next(Scheduler *scheduler,
                                            int64_t current_time, int *pid) {
    if (scheduler == NULL) return SCHEDULER_SELECT_ERROR;
    return scheduler->operations->select_next(scheduler->context, current_time, pid);
}

int scheduler_should_preempt(const Scheduler *scheduler, int quantum_used) {
    return scheduler != NULL
        && scheduler->operations->should_preempt(scheduler->context, quantum_used);
}

const char *scheduler_name(const Scheduler *scheduler) {
    return scheduler != NULL ? scheduler->name : "unknown";
}
