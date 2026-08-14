#include "scheduler_internal.h"

#include <stdlib.h>

typedef struct FcfsNode {
    SchedulerProcessView process;
    struct FcfsNode *next;
} FcfsNode;

typedef struct {
    FcfsNode *head;
} FcfsContext;

static int precedes(const SchedulerProcessView *left,
                    const SchedulerProcessView *right) {
    if (left->ready_since != right->ready_since) {
        return left->ready_since < right->ready_since;
    }
    return left->pid < right->pid;
}

static int enqueue(void *opaque, const SchedulerProcessView *process) {
    FcfsContext *context = (FcfsContext *)opaque;
    FcfsNode *current;
    FcfsNode *node;

    if (context == NULL || process == NULL || process->pid <= 0
        || process->priority < 0 || process->priority > 9
        || process->arrival < 0 || process->ready_since < process->arrival) {
        return 0;
    }
    for (current = context->head; current != NULL; current = current->next) {
        if (current->process.pid == process->pid) return 0;
    }

    node = (FcfsNode *)malloc(sizeof(FcfsNode));
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
    FcfsContext *context = (FcfsContext *)opaque;
    FcfsNode *node;

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
    FcfsContext *context = (FcfsContext *)opaque;
    FcfsNode *node;

    if (context == NULL) return;
    node = context->head;
    while (node != NULL) {
        FcfsNode *next = node->next;
        free(node);
        node = next;
    }
    free(context);
}

static const SchedulerOperations FCFS_OPERATIONS = {
    enqueue,
    enqueue,
    enqueue,
    on_finish,
    select_next,
    should_preempt,
    destroy
};

Scheduler *scheduler_fcfs_create(void) {
    Scheduler *scheduler = (Scheduler *)malloc(sizeof(Scheduler));
    FcfsContext *context = (FcfsContext *)calloc(1, sizeof(FcfsContext));

    if (scheduler == NULL || context == NULL) {
        free(scheduler);
        free(context);
        return NULL;
    }
    scheduler->name = "fcfs";
    scheduler->operations = &FCFS_OPERATIONS;
    scheduler->context = context;
    return scheduler;
}
