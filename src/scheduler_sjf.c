#include "scheduler_internal.h"

#include <stdlib.h>

#define ALPHA 0.5
#define INITIAL_TAU 10.0

typedef struct SjfProcessState {
    int pid;
    int64_t last_cpu_consumed;
    double tau;
} SjfProcessState;

typedef struct SjfNode {
    SchedulerProcessView process;
    double expected_burst;
    struct SjfNode *next;
} SjfNode;

typedef struct {
    SjfProcessState *states;
    int states_capacity;
    SjfNode *head;
} SjfContext;

static int valid_view(const SchedulerProcessView *process) {
    return process != NULL && process->pid > 0
        && process->priority >= 0 && process->priority <= 9
        && process->arrival >= 0 && process->ready_since >= process->arrival
        && process->cpu_consumed >= 0;
}

static SjfProcessState *get_state(SjfContext *ctx, int pid) {
    if (pid >= ctx->states_capacity) {
        int new_cap = ctx->states_capacity == 0 ? 32 : ctx->states_capacity * 2;
        while (new_cap <= pid) new_cap *= 2;
        SjfProcessState *new_states = (SjfProcessState *)realloc(ctx->states, new_cap * sizeof(SjfProcessState));
        if (new_states == NULL) return NULL;
        for (int i = ctx->states_capacity; i < new_cap; i++) {
            new_states[i].pid = 0;
            new_states[i].last_cpu_consumed = 0;
            new_states[i].tau = INITIAL_TAU;
        }
        ctx->states = new_states;
        ctx->states_capacity = new_cap;
    }
    
    if (ctx->states[pid].pid == 0) {
        ctx->states[pid].pid = pid;
        ctx->states[pid].last_cpu_consumed = 0;
        ctx->states[pid].tau = INITIAL_TAU;
    }
    return &ctx->states[pid];
}

static int precedes(double expected_burst, const SchedulerProcessView *process,
                    double other_expected_burst, const SchedulerProcessView *other_process) {
    if (expected_burst != other_expected_burst) {
        return expected_burst < other_expected_burst;
    }
    if (process->ready_since != other_process->ready_since) {
        return process->ready_since < other_process->ready_since;
    }
    return process->pid < other_process->pid;
}

static int enqueue_internal(SjfContext *context, const SchedulerProcessView *process, int update_estimate) {
    SjfNode *current;
    SjfNode *node;
    SjfProcessState *state;
    double expected_burst;

    if (context == NULL || !valid_view(process)) return 0;
    for (current = context->head; current != NULL; current = current->next) {
        if (current->process.pid == process->pid) return 0;
    }
    
    state = get_state(context, process->pid);
    if (state == NULL) return 0;
    
    if (update_estimate) {
        int64_t tn = process->cpu_consumed - state->last_cpu_consumed;
        state->tau = ALPHA * (double)tn + (1.0 - ALPHA) * state->tau;
        state->last_cpu_consumed = process->cpu_consumed;
    }
    
    expected_burst = state->tau;

    node = (SjfNode *)malloc(sizeof(SjfNode));
    if (node == NULL) return 0;
    node->process = *process;
    node->expected_burst = expected_burst;
    node->next = NULL;

    if (context->head == NULL || precedes(expected_burst, process, context->head->expected_burst, &context->head->process)) {
        node->next = context->head;
        context->head = node;
    } else {
        current = context->head;
        while (current->next != NULL && !precedes(expected_burst, process, current->next->expected_burst, &current->next->process)) {
            current = current->next;
        }
        node->next = current->next;
        current->next = node;
    }
    return 1;
}

static int on_arrival(void *opaque, const SchedulerProcessView *process) {
    return enqueue_internal((SjfContext *)opaque, process, 0);
}

static int on_io_complete(void *opaque, const SchedulerProcessView *process) {
    return enqueue_internal((SjfContext *)opaque, process, 1);
}

static int on_preempted(void *opaque, const SchedulerProcessView *process) {
    return enqueue_internal((SjfContext *)opaque, process, 0);
}

static int on_finish(void *opaque, int pid) {
    return opaque != NULL && pid > 0;
}

static SchedulerSelectResult select_next(void *opaque, int64_t current_time,
                                         int *pid) {
    SjfContext *context = (SjfContext *)opaque;
    SjfNode *node;

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

static int should_preempt(const void *opaque, int quantum_used) {
    (void)opaque;
    (void)quantum_used;
    return 0;
}

static void destroy(void *opaque) {
    SjfContext *context = (SjfContext *)opaque;
    SjfNode *node;

    if (context == NULL) return;
    node = context->head;
    while (node != NULL) {
        SjfNode *next = node->next;
        free(node);
        node = next;
    }
    if (context->states != NULL) {
        free(context->states);
    }
    free(context);
}

static const SchedulerOperations SJF_OPERATIONS = {
    on_arrival,
    on_io_complete,
    on_preempted,
    on_finish,
    select_next,
    should_preempt,
    destroy
};

Scheduler *scheduler_sjf_create(void) {
    Scheduler *scheduler = (Scheduler *)malloc(sizeof(Scheduler));
    SjfContext *context = (SjfContext *)calloc(1, sizeof(SjfContext));

    if (scheduler == NULL || context == NULL) {
        free(scheduler);
        free(context);
        return NULL;
    }

    scheduler->name = "sjf";
    scheduler->operations = &SJF_OPERATIONS;
    scheduler->context = context;
    return scheduler;
}
