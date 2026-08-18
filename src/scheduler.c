#include "scheduler_internal.h"

#include <stdlib.h>
#include <string.h>

typedef enum {
    VIEW_QUEUE_FIFO,
    VIEW_QUEUE_PRIORITY
} ViewQueueOrder;

typedef struct ViewNode {
    SchedulerProcessView process;
    struct ViewNode *next;
} ViewNode;

typedef struct {
    ViewNode *head;
    ViewQueueOrder order;
    int quantum;
    int preemptive;
} GenericSchedulerContext;

static int valid_view(const SchedulerProcessView *process) {
    return process != NULL && process->pid > 0 && process->priority >= 0
        && process->priority <= 9 && process->arrival >= 0
        && process->ready_since >= process->arrival
        && process->cpu_consumed >= 0;
}

static int generic_precedes(const GenericSchedulerContext *context,
                            const SchedulerProcessView *left,
                            const SchedulerProcessView *right) {
    if (context->order == VIEW_QUEUE_PRIORITY) {
        if (left->priority != right->priority) {
            return left->priority < right->priority;
        }
        if (left->ready_since != right->ready_since) {
            return left->ready_since < right->ready_since;
        }
        return left->pid < right->pid;
    }
    return 0;
}

static int generic_enqueue(void *opaque, const SchedulerProcessView *process) {
    GenericSchedulerContext *context = (GenericSchedulerContext *)opaque;
    ViewNode *current;
    ViewNode *node;

    if (context == NULL || !valid_view(process)) return 0;
    for (current = context->head; current != NULL; current = current->next) {
        if (current->process.pid == process->pid) return 0;
    }

    node = (ViewNode *)malloc(sizeof(ViewNode));
    if (node == NULL) return 0;
    node->process = *process;
    node->next = NULL;

    if (context->head == NULL) {
        context->head = node;
    } else if (context->order == VIEW_QUEUE_PRIORITY
               && generic_precedes(context, process, &context->head->process)) {
        node->next = context->head;
        context->head = node;
    } else {
        current = context->head;
        while (current->next != NULL
               && (context->order == VIEW_QUEUE_FIFO
                   || !generic_precedes(context, process, &current->next->process))) {
            current = current->next;
        }
        node->next = current->next;
        current->next = node;
    }
    return 1;
}

static int generic_finish(void *opaque, int pid) {
    return opaque != NULL && pid > 0;
}

static SchedulerSelectResult generic_select(void *opaque, int64_t current_time,
                                             int *pid) {
    GenericSchedulerContext *context = (GenericSchedulerContext *)opaque;
    ViewNode *node;

    (void)current_time;
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

static int generic_should_preempt(const void *opaque, int quantum_used) {
    const GenericSchedulerContext *context = (const GenericSchedulerContext *)opaque;
    return context != NULL && context->preemptive
        && quantum_used >= context->quantum;
}

static void generic_destroy(void *opaque) {
    GenericSchedulerContext *context = (GenericSchedulerContext *)opaque;
    ViewNode *node;

    if (context == NULL) return;
    node = context->head;
    while (node != NULL) {
        ViewNode *next = node->next;
        free(node);
        node = next;
    }
    free(context);
}

static const SchedulerOperations GENERIC_OPERATIONS = {
    generic_enqueue,
    generic_enqueue,
    generic_enqueue,
    generic_finish,
    generic_select,
    generic_should_preempt,
    generic_destroy
};

static Scheduler *generic_create(const char *name, ViewQueueOrder order,
                                 int quantum, int preemptive) {
    Scheduler *scheduler = (Scheduler *)malloc(sizeof(Scheduler));
    GenericSchedulerContext *context =
        (GenericSchedulerContext *)calloc(1, sizeof(GenericSchedulerContext));

    if (scheduler == NULL || context == NULL) {
        free(scheduler);
        free(context);
        return NULL;
    }
    context->order = order;
    context->quantum = quantum;
    context->preemptive = preemptive;
    scheduler->name = name;
    scheduler->operations = &GENERIC_OPERATIONS;
    scheduler->context = context;
    return scheduler;
}

Scheduler *scheduler_create(const char *name, int rr_quantum) {
    if (name == NULL || rr_quantum <= 0) return NULL;
    if (strcmp(name, "fcfs") == 0) return scheduler_fcfs_create();
    if (strcmp(name, "rr") == 0) {
        return generic_create("rr", VIEW_QUEUE_FIFO, rr_quantum, 1);
    }
    if (strcmp(name, "prioridade") == 0) {
        return generic_create("prioridade", VIEW_QUEUE_PRIORITY, rr_quantum, 0);
    }
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
