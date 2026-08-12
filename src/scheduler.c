#include "scheduler.h"

#include <stdlib.h>
#include <string.h>

Scheduler *scheduler_create(const char *name, int rr_quantum) {
    Scheduler *scheduler = (Scheduler *)malloc(sizeof(Scheduler));
    bool (*comparator)(const Process *, const Process *) = compare_fcfs;

    if (scheduler == NULL || name == NULL || rr_quantum <= 0) {
        free(scheduler);
        return NULL;
    }

    if (strcmp(name, "fcfs") == 0) {
        scheduler->kind = SCHEDULER_FCFS;
        comparator = compare_fcfs;
    } else if (strcmp(name, "rr") == 0) {
        scheduler->kind = SCHEDULER_RR;
        comparator = NULL;
    } else if (strcmp(name, "prioridade") == 0) {
        scheduler->kind = SCHEDULER_PRIORITY;
        comparator = compare_priority;
    } else if (strcmp(name, "proprio") == 0) {
        scheduler->kind = SCHEDULER_PROPRIO;
        comparator = compare_priority;
    } else {
        free(scheduler);
        return NULL;
    }

    scheduler->quantum = rr_quantum;
    scheduler->ready = queue_create(QUEUE_READY, comparator);
    if (scheduler->ready == NULL) {
        free(scheduler);
        return NULL;
    }

    return scheduler;
}

void scheduler_destroy(Scheduler *scheduler) {
    if (scheduler == NULL) {
        return;
    }
    queue_destroy(scheduler->ready);
    free(scheduler);
}

int scheduler_enqueue(Scheduler *scheduler, Process *process) {
    return scheduler != NULL && queue_insert(scheduler->ready, process);
}

Process *scheduler_next(Scheduler *scheduler) {
    if (scheduler == NULL) {
        return NULL;
    }
    return queue_pop(scheduler->ready);
}

int scheduler_should_preempt(const Scheduler *scheduler, int quantum_used) {
    return scheduler != NULL
        && scheduler->kind == SCHEDULER_RR
        && quantum_used >= scheduler->quantum;
}

const char *scheduler_name(const Scheduler *scheduler) {
    if (scheduler == NULL) {
        return "unknown";
    }

    switch (scheduler->kind) {
        case SCHEDULER_FCFS: return "fcfs";
        case SCHEDULER_RR: return "rr";
        case SCHEDULER_PRIORITY: return "prioridade";
        case SCHEDULER_PROPRIO: return "proprio";
    }

    return "unknown";
}
