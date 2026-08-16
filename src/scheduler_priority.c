#include "scheduler_internal.h"

#include <stdlib.h>

typedef struct PriorityNode {
    SchedulerProcessView process;
    struct PriorityNode *next;
} PriorityNode;

typedef struct {
    PriorityNode *head;
} PriorityContext;

static int valid_view(const SchedulerProcessView *process) {
    return process != NULL && process->pid > 0
        && process->priority >= 0 && process->priority <= 9
        && process->arrival >= 0 && process->ready_since >= process->arrival;
}

static int precedes(const SchedulerProcessView *left,
                    const SchedulerProcessView *right) {
    if (left->priority != right->priority) {
        return left->priority < right->priority;
    }
    if (left->ready_since != right->ready_since) {
        return left->ready_since < right->ready_since;
    }
    return left->pid < right->pid;
}

static int enqueue(void *opaque, const SchedulerProcessView *process) {
    PriorityContext *context = (PriorityContext *)opaque;
    PriorityNode *current;
    PriorityNode *node;

    if (context == NULL || !valid_view(process)) return 0;
    for (current = context->head; current != NULL; current = current->next) {
        if (current->process.pid == process->pid) return 0;
    }

    node = (PriorityNode *)malloc(sizeof(PriorityNode));
    if (node == NULL) return 0;
    node->process = *process;
    node->next = NULL;

    if (context->head == NULL || precedes(process, &context->head->process)) {
        node->next = context->head;
        context->head = node;
    } else {
        current = context->head;
        while (current->next != NULL
               && !precedes(process, &current->next->process)) {
            current = current->next;
        }
        node->next = current->next;
        current->next = node;
    }
    return 1;
}

static int on_finish(void *opaque, int pid) {
    return opaque != NULL && pid > 0;
}

static SchedulerSelectResult select_next(void *opaque, int *pid) {
    PriorityContext *context = (PriorityContext *)opaque;
    PriorityNode *node;

    if (context == NULL || pid == NULL) return SCHEDULER_SELECT_ERROR;
    if (context->head == NULL) {
        *pid = 0;
        return SCHEDULER_SELECT_EMPTY;
    }

    node = context->head;
    context->head = node->next;
    *pid = node->process.pid;
    free(node);
    return SCHEDULER_SELECT_OK;
}

static int should_preempt(const void *opaque, int quantum_used) {
    (void)opaque;
    (void)quantum_used;
    return 0;
}

static void destroy(void *opaque) {
    PriorityContext *context = (PriorityContext *)opaque;
    PriorityNode *node;

    if (context == NULL) return;
    node = context->head;
    while (node != NULL) {
        PriorityNode *next = node->next;
        free(node);
        node = next;
    }
    free(context);
}

static const SchedulerOperations PRIORITY_OPERATIONS = {
    enqueue,
    enqueue,
    enqueue,
    on_finish,
    select_next,
    should_preempt,
    destroy
};

Scheduler *scheduler_priority_create(void) {
    Scheduler *scheduler = (Scheduler *)malloc(sizeof(Scheduler));
    PriorityContext *context =
        (PriorityContext *)calloc(1, sizeof(PriorityContext));

    if (scheduler == NULL || context == NULL) {
        free(scheduler);
        free(context);
        return NULL;
    }

    scheduler->name = "prioridade";
    scheduler->operations = &PRIORITY_OPERATIONS;
    scheduler->context = context;
    return scheduler;
}
