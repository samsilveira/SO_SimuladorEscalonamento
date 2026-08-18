#include "scheduler_internal.h"

#include <limits.h>
#include <stdlib.h>

#define PDBH_AGING_FACTOR 10
#define PDBH_PENALTY_FACTOR 5

typedef struct PdbhNode {
    SchedulerProcessView process;
    struct PdbhNode *next;
} PdbhNode;

typedef struct {
    PdbhNode *head;
} PdbhContext;

static int valid_view(const SchedulerProcessView *process) {
    return process != NULL && process->pid > 0
        && process->priority >= 0 && process->priority <= 9
        && process->arrival >= 0 && process->ready_since >= process->arrival
        && process->cpu_consumed >= 0;
}

static int enqueue(void *opaque, const SchedulerProcessView *process) {
    PdbhContext *context = (PdbhContext *)opaque;
    PdbhNode *current;
    PdbhNode *node;

    if (context == NULL || !valid_view(process)) return 0;
    for (current = context->head; current != NULL; current = current->next) {
        if (current->process.pid == process->pid) return 0;
    }
    node = (PdbhNode *)malloc(sizeof(PdbhNode));
    if (node == NULL) return 0;
    node->process = *process;
    node->next = context->head;
    context->head = node;
    return 1;
}

static int64_t score(const SchedulerProcessView *process, int64_t current_time) {
    int64_t wait_bonus = (current_time - process->ready_since) / PDBH_AGING_FACTOR;
    int64_t cpu_penalty = process->cpu_consumed / PDBH_PENALTY_FACTOR;
    int64_t base = (int64_t)process->priority - wait_bonus;

    if (base > INT64_MAX - cpu_penalty) return INT64_MAX;
    return base + cpu_penalty;
}

static int precedes(const SchedulerProcessView *left,
                    const SchedulerProcessView *right, int64_t current_time) {
    int64_t left_score = score(left, current_time);
    int64_t right_score = score(right, current_time);

    if (left_score != right_score) return left_score < right_score;
    if (left->ready_since != right->ready_since) {
        return left->ready_since < right->ready_since;
    }
    return left->pid < right->pid;
}

static int on_finish(void *opaque, int pid) {
    return opaque != NULL && pid > 0;
}

static SchedulerSelectResult select_next(void *opaque, int64_t current_time,
                                         int *pid) {
    PdbhContext *context = (PdbhContext *)opaque;
    PdbhNode *best;
    PdbhNode *best_previous = NULL;
    PdbhNode *previous;
    PdbhNode *current;

    if (context == NULL || pid == NULL || current_time < 0) {
        return SCHEDULER_SELECT_ERROR;
    }
    if (context->head == NULL) {
        *pid = 0;
        return SCHEDULER_SELECT_EMPTY;
    }
    best = context->head;
    previous = context->head;
    for (current = context->head->next; current != NULL; current = current->next) {
        if (current->process.ready_since > current_time) {
            return SCHEDULER_SELECT_ERROR;
        }
        if (precedes(&current->process, &best->process, current_time)) {
            best = current;
            best_previous = previous;
        }
        previous = current;
    }
    if (best->process.ready_since > current_time) return SCHEDULER_SELECT_ERROR;
    if (best_previous == NULL) context->head = best->next;
    else best_previous->next = best->next;
    *pid = best->process.pid;
    free(best);
    return SCHEDULER_SELECT_OK;
}

static int should_preempt(const void *opaque, int quantum_used) {
    (void)opaque;
    (void)quantum_used;
    return 0;
}

static void destroy(void *opaque) {
    PdbhContext *context = (PdbhContext *)opaque;
    PdbhNode *node;

    if (context == NULL) return;
    node = context->head;
    while (node != NULL) {
        PdbhNode *next = node->next;
        free(node);
        node = next;
    }
    free(context);
}

static const SchedulerOperations PDBH_OPERATIONS = {
    enqueue, enqueue, enqueue, on_finish, select_next, should_preempt, destroy
};

Scheduler *scheduler_pdbh_create(void) {
    Scheduler *scheduler = (Scheduler *)malloc(sizeof(Scheduler));
    PdbhContext *context = (PdbhContext *)calloc(1, sizeof(PdbhContext));

    if (scheduler == NULL || context == NULL) {
        free(scheduler);
        free(context);
        return NULL;
    }
    scheduler->name = "proprio";
    scheduler->operations = &PDBH_OPERATIONS;
    scheduler->context = context;
    return scheduler;
}
