#include "scheduler_internal.h"

#include <stdlib.h>

typedef struct RrNode {
    SchedulerProcessView process;
    struct RrNode *next;
} RrNode;

typedef struct {
    RrNode *head;
    RrNode *tail;
    int quantum;
} RrContext;

static int valid_view(const SchedulerProcessView *process) {
    return process != NULL && process->pid > 0
        && process->priority >= 0 && process->priority <= 9
        && process->arrival >= 0 && process->ready_since >= process->arrival
        && process->cpu_consumed >= 0;
}

static int enqueue(void *opaque, const SchedulerProcessView *process) {
    RrContext *context = (RrContext *)opaque;
    RrNode *current;
    RrNode *node;

    if (context == NULL || !valid_view(process)) return 0;
    for (current = context->head; current != NULL; current = current->next) {
        if (current->process.pid == process->pid) return 0;
    }

    node = (RrNode *)malloc(sizeof(RrNode));
    if (node == NULL) return 0;
    node->process = *process;
    node->next = NULL;

    if (context->tail == NULL) {
        context->head = node;
    } else {
        context->tail->next = node;
    }
    context->tail = node;
    return 1;
}

static int on_finish(void *opaque, int pid) {
    return opaque != NULL && pid > 0;
}

static SchedulerSelectResult select_next(void *opaque, int64_t current_time,
                                         int *pid) {
    RrContext *context = (RrContext *)opaque;
    RrNode *node;

    (void)current_time;
    if (context == NULL || pid == NULL) return SCHEDULER_SELECT_ERROR;
    if (context->head == NULL) {
        *pid = 0;
        return SCHEDULER_SELECT_EMPTY;
    }

    node = context->head;
    context->head = node->next;
    if (context->head == NULL) context->tail = NULL;
    *pid = node->process.pid;
    free(node);
    return SCHEDULER_SELECT_OK;
}

static int should_preempt(const void *opaque, int quantum_used) {
    const RrContext *context = (const RrContext *)opaque;
    return context != NULL && quantum_used >= context->quantum;
}

static void destroy(void *opaque) {
    RrContext *context = (RrContext *)opaque;
    RrNode *node;

    if (context == NULL) return;
    node = context->head;
    while (node != NULL) {
        RrNode *next = node->next;
        free(node);
        node = next;
    }
    free(context);
}

static const SchedulerOperations RR_OPERATIONS = {
    enqueue,
    enqueue,
    enqueue,
    on_finish,
    select_next,
    should_preempt,
    destroy
};

Scheduler *scheduler_rr_create(int quantum) {
    Scheduler *scheduler;
    RrContext *context;

    if (quantum <= 0) return NULL;
    scheduler = (Scheduler *)malloc(sizeof(Scheduler));
    context = (RrContext *)calloc(1, sizeof(RrContext));
    if (scheduler == NULL || context == NULL) {
        free(scheduler);
        free(context);
        return NULL;
    }

    context->quantum = quantum;
    scheduler->name = "rr";
    scheduler->operations = &RR_OPERATIONS;
    scheduler->context = context;
    return scheduler;
}
