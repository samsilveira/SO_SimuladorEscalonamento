#include "simulation.h"

#include "scheduler.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    READY_FROM_ARRIVAL,
    READY_FROM_IO
} ReadyCause;

typedef struct {
    Process *process;
    ReadyCause cause;
} ReadyTransition;

static int checked_add_i64(int64_t left, int64_t right, int64_t *out) {
    if (out == NULL
        || (right > 0 && left > INT64_MAX - right)
        || (right < 0 && left < INT64_MIN - right)) {
        return 0;
    }
    *out = left + right;
    return 1;
}

static int count_queue(ProcessQueue *queue, int *out) {
    int count = 0;
    ProcessNode *node = queue != NULL ? queue->head : NULL;

    if (out == NULL) {
        return 0;
    }
    while (node != NULL) {
        if (count == 100000) {
            return 0;
        }
        count += 1;
        node = node->next;
    }

    *out = count;
    return 1;
}

static int remember_processes(ProcessQueue *queue, Process ***out, int *count) {
    int i = 0;
    ProcessNode *node = NULL;

    if (!count_queue(queue, count)) {
        return 0;
    }
    if ((size_t)*count > SIZE_MAX / sizeof(Process *)) {
        return 0;
    }
    if (*count == 0) {
        *out = NULL;
        return 1;
    }
    *out = (Process **)malloc(sizeof(Process *) * (size_t)*count);
    if (*out == NULL) {
        return 0;
    }

    node = queue->head;
    while (node != NULL) {
        (*out)[i] = node->process;
        i += 1;
        node = node->next;
    }

    return 1;
}

static int validate_processes(Process **processes, int count) {
    int i;

    if (processes == NULL || count <= 0 || count > 100000) {
        return 0;
    }

    for (i = 0; i < count; i += 1) {
        Process *process = processes[i];
        Burst *burst;
        int64_t cpu_sum = 0;
        int64_t io_sum = 0;

        if (process == NULL || process->pid <= 0 || process->arrival_time < 0
            || process->priority < 0 || process->priority > 9
            || process->state != PROCESS_NEW || process->bursts == NULL
            || process->current_burst != process->bursts) {
            return 0;
        }

        for (burst = process->bursts; burst != NULL; burst = burst->next) {
            if (burst->cpu_time <= 0 || burst->cpu_time > 1000000
                || burst->io_time < 0 || burst->io_time > 1000000
                || (burst->next != NULL && burst->io_time == 0)
                || (burst->next == NULL && burst->io_time != 0)
                || !checked_add_i64(cpu_sum, burst->cpu_time, &cpu_sum)
                || !checked_add_i64(io_sum, burst->io_time, &io_sum)) {
                return 0;
            }
        }

        if (cpu_sum != process->total_cpu_original || io_sum != process->total_io_original
            || cpu_sum <= 0) {
            return 0;
        }
    }

    return 1;
}

static int count_queue_in_state(const ProcessQueue *queue, ProcessState expected_state, int process_count, int *out) {
    const ProcessNode *node = queue != NULL ? queue->head : NULL;
    int count = 0;

    if (out == NULL) return 0;
    while (node != NULL) {
        if (node->process == NULL || node->process->state != expected_state || count == process_count) {
            return 0;
        }
        count += 1;
        node = node->next;
    }
    *out = count;
    return 1;
}

static int check_invariants(Process **processes,
                            int process_count,
                            const ProcessQueue *future,
                            const ProcessQueue *blocked,
                            int ready_queue_count,
                            const Process *running,
                            const Process *context_target,
                            int finished_count,
                            int64_t current_time,
                            int64_t last_time,
                            FILE *diagnostic) {
    int state_counts[5] = {0};
    int future_count = 0;
    int blocked_count = 0;
    int i;

    if (processes == NULL || process_count <= 0 || ready_queue_count < 0
        || finished_count < 0 || (running != NULL && running == context_target)
        || !count_queue_in_state(future, PROCESS_NEW, process_count, &future_count)
        || !count_queue_in_state(blocked, PROCESS_BLOCKED, process_count, &blocked_count)) {
        if (diagnostic != NULL) {
            fprintf(diagnostic, "Invariant failed: invalid process container\n");
        }
        return 0;
    }

    for (i = 0; i < process_count; i += 1) {
        const Process *process = processes[i];

        if (process == NULL || process->state < PROCESS_NEW
            || process->state > PROCESS_FINISHED) {
            if (diagnostic != NULL) {
                fprintf(diagnostic, "Invariant failed: invalid state at process index %d\n", i);
            }
            return 0;
        }
        state_counts[process->state] += 1;
        if (process->state == PROCESS_FINISHED
            && process->finish_time < process->arrival_time) {
            if (diagnostic != NULL) {
                fprintf(diagnostic,
                        "Invariant failed: negative turnaround for pid %d\n",
                        process->pid);
            }
            return 0;
        }
    }

    if ((running == NULL && state_counts[PROCESS_RUNNING] != 0)
        || (running != NULL
            && (running->state != PROCESS_RUNNING
                || state_counts[PROCESS_RUNNING] != 1))) {
        if (diagnostic != NULL) {
            fprintf(diagnostic,
                    "Invariant failed: CPU ownership (running states: %d)\n",
                    state_counts[PROCESS_RUNNING]);
        }
        return 0;
    }
    if (context_target != NULL && context_target->state != PROCESS_READY) {
        if (diagnostic != NULL) {
            fprintf(diagnostic, "Invariant failed: invalid context-switch target\n");
        }
        return 0;
    }
    if (state_counts[PROCESS_NEW] != future_count
        || state_counts[PROCESS_READY]
            != ready_queue_count + (context_target != NULL ? 1 : 0)
        || state_counts[PROCESS_BLOCKED] != blocked_count
        || state_counts[PROCESS_FINISHED] != finished_count) {
        if (diagnostic != NULL) {
            fprintf(diagnostic,
                    "Invariant failed: process conservation "
                    "(new=%d/%d, ready=%d/%d, blocked=%d/%d, finished=%d/%d)\n",
                    state_counts[PROCESS_NEW], future_count,
                    state_counts[PROCESS_READY],
                    ready_queue_count + (context_target != NULL ? 1 : 0),
                    state_counts[PROCESS_BLOCKED], blocked_count,
                    state_counts[PROCESS_FINISHED], finished_count);
        }
        return 0;
    }
    if (current_time < last_time) {
        if (diagnostic != NULL) {
            fprintf(diagnostic,
                    "Invariant failed: monotonic time (current: %ld, last: %ld)\n",
                    (long)current_time, (long)last_time);
        }
        return 0;
    }
    return 1;
}

int simulation_invariants_self_test(void) {
    Process first = {0};
    Process second = {0};
    Process *processes[] = {&first, &second};
    ProcessNode second_node = {&second, NULL};
    ProcessNode first_node = {&first, &second_node};
    ProcessQueue future = {&first_node, NULL, QUEUE_FUTURE};
    ProcessQueue blocked = {NULL, NULL, QUEUE_BLOCKED};

    first.pid = 1;
    first.state = PROCESS_NEW;
    first.finish_time = -1;
    second.pid = 2;
    second.state = PROCESS_NEW;
    second.finish_time = -1;

    if (!check_invariants(processes, 2, &future, &blocked, 0, NULL, NULL,
                          0, 0, 0, NULL)) {
        return 1;
    }

    second.state = PROCESS_READY;
    if (check_invariants(processes, 2, &future, &blocked, 1, NULL, NULL,
                         0, 0, 0, NULL)) {
        return 1;
    }
    second.state = PROCESS_NEW;

    first.state = PROCESS_RUNNING;
    if (check_invariants(processes, 2, &future, &blocked, 0, NULL, NULL,
                         0, 0, 0, NULL)) {
        return 1;
    }
    first.state = PROCESS_NEW;

    first_node.process = &second;
    first_node.next = NULL;
    future.head = &first_node;
    first.state = PROCESS_FINISHED;
    first.arrival_time = 5;
    first.finish_time = 4;
    if (check_invariants(processes, 2, &future, &blocked, 0, NULL, NULL,
                         1, 5, 5, NULL)) {
        return 1;
    }

    first.state = PROCESS_NEW;
    first.arrival_time = 0;
    first.finish_time = -1;
    first_node.process = &first;
    first_node.next = &second_node;
    if (check_invariants(processes, 2, &future, &blocked, 0, NULL, NULL,
                         0, 4, 5, NULL)) {
        return 1;
    }
    return 0;
}

static int record_event(SimulationResult *result, int64_t time, SimulationEventType type, int pid) {
    SimulationEvent *events = NULL;
    size_t capacity = 0;

    if (result->event_count == result->event_capacity) {
        if (result->event_capacity > SIZE_MAX / 2) {
            return 0;
        }
        capacity = result->event_capacity == 0 ? 32 : result->event_capacity * 2;
        if (capacity > SIZE_MAX / sizeof(SimulationEvent)) {
            return 0;
        }
        events = (SimulationEvent *)realloc(result->events, capacity * sizeof(SimulationEvent));
        if (events == NULL) {
            return 0;
        }
        result->events = events;
        result->event_capacity = capacity;
    }

    result->events[result->event_count].time = time;
    result->events[result->event_count].type = type;
    result->events[result->event_count].pid = pid;
    result->event_count += 1;
    return 1;
}

static int add_ready_now(ReadyTransition *ready_now, int *ready_count,
                         Process *process, ReadyCause cause) {
    ready_now[*ready_count].process = process;
    ready_now[*ready_count].cause = cause;
    *ready_count += 1;
    return 1;
}

static int collect_io(ProcessQueue *blocked, int64_t time,
                      ReadyTransition *ready_now, int *ready_count,
                      SimulationResult *result) {
    while (!queue_is_empty(blocked) && queue_peek(blocked)->io_finish_time <= time) {
        Process *process = queue_pop(blocked);
        process->state = PROCESS_READY;
        process->ready_since = time;
        process->io_finish_time = -1;
        if (!record_event(result, time, SIM_EVENT_IO_END, process->pid)) {
            return 0;
        }
        add_ready_now(ready_now, ready_count, process, READY_FROM_IO);
    }

    return 1;
}

static int collect_arrivals(ProcessQueue *future, int64_t time,
                            ReadyTransition *ready_now, int *ready_count,
                            SimulationResult *result) {
    while (!queue_is_empty(future) && queue_peek(future)->arrival_time <= time) {
        Process *process = queue_pop(future);
        process->state = PROCESS_READY;
        process->ready_since = time;
        if (!record_event(result, time, SIM_EVENT_ARRIVAL, process->pid)) {
            return 0;
        }
        add_ready_now(ready_now, ready_count, process, READY_FROM_ARRIVAL);
    }

    return 1;
}

static int compare_ready_pid(const void *a, const void *b) {
    const ReadyTransition *left = (const ReadyTransition *)a;
    const ReadyTransition *right = (const ReadyTransition *)b;
    return (left->process->pid > right->process->pid)
        - (left->process->pid < right->process->pid);
}

static SchedulerProcessView scheduler_view(const Process *process) {
    SchedulerProcessView view;

    view.pid = process->pid;
    view.priority = process->priority;
    view.arrival = process->arrival_time;
    view.ready_since = process->ready_since;
    view.cpu_consumed = process->cpu_consumed;
    return view;
}

static int enqueue_ready_batch(Scheduler *scheduler, ReadyTransition *ready_now,
                               int ready_count) {
    int i;

    qsort(ready_now, (size_t)ready_count, sizeof(ReadyTransition), compare_ready_pid);
    for (i = 0; i < ready_count; i += 1) {
        SchedulerProcessView view = scheduler_view(ready_now[i].process);
        int accepted = ready_now[i].cause == READY_FROM_ARRIVAL
            ? scheduler_on_arrival(scheduler, &view)
            : scheduler_on_io_complete(scheduler, &view);

        if (!accepted) {
            return 0;
        }
    }

    return 1;
}

static int64_t next_external_event(ProcessQueue *future, ProcessQueue *blocked) {
    int64_t next = INT64_MAX;

    if (!queue_is_empty(future) && queue_peek(future)->arrival_time < next) {
        next = queue_peek(future)->arrival_time;
    }
    if (!queue_is_empty(blocked) && queue_peek(blocked)->io_finish_time < next) {
        next = queue_peek(blocked)->io_finish_time;
    }

    return next;
}

static int dispatch(Process *process, int64_t time, int *quantum_used, SimulationResult *result) {
    process->state = PROCESS_RUNNING;
    if (process->remaining_cpu <= 0) {
        process->remaining_cpu = process->current_burst->cpu_time;
    }
    if (process->start_time < 0) {
        process->start_time = time;
    }
    *quantum_used = 0;
    return record_event(result, time, SIM_EVENT_DISPATCH, process->pid);
}

static int start_next(Process *process,
                      int64_t time,
                      int context_switch_cost,
                      int last_pid,
                      int idle_since_last,
                      Process **context_target,
                      int64_t *context_end,
                      int *quantum_used,
                      uint64_t *context_switches,
                      Process **running,
                      SimulationResult *result) {
    int switches = last_pid > 0 && process->pid != last_pid && !idle_since_last;

    if (switches) {
        if (*context_switches == UINT64_MAX) {
            return 0;
        }
        *context_switches += 1;
        if (!record_event(result, time, SIM_EVENT_CONTEXT_SWITCH, process->pid)) {
            return 0;
        }
    }

    if (switches && context_switch_cost > 0) {
        *context_target = process;
        if (!checked_add_i64(time, (int64_t)context_switch_cost, context_end)) {
            return 0;
        }
        return 1;
    }

    *running = process;
    return dispatch(process, time, quantum_used, result);
}

static int process_cpu_result(Process **running,
                              ProcessQueue *blocked,
                              int64_t time,
                              int *finished_count,
                              int *last_pid,
                              int *idle_since_last,
                              Process **preempted,
                              Scheduler *scheduler,
                              int quantum_used,
                              SimulationResult *result) {
    Process *process = *running;
    int64_t io_time;

    if (process == NULL) {
        return 1;
    }

    if (process->remaining_cpu == 0) {
        *last_pid = process->pid;
        *idle_since_last = 0;
        *running = NULL;
        if (!record_event(result, time, SIM_EVENT_CPU_BURST_END, process->pid)) {
            return 0;
        }

        io_time = process->current_burst->io_time;
        if (io_time > 0 && process->current_burst->next != NULL) {
            process->current_burst = process->current_burst->next;
            process->state = PROCESS_BLOCKED;
            if (!checked_add_i64(time, (int64_t)io_time, &process->io_finish_time)) {
                return 0;
            }
            if (!record_event(result, time, SIM_EVENT_IO_START, process->pid)) {
                return 0;
            }
            return queue_insert(blocked, process);
        }

        process->state = PROCESS_FINISHED;
        process->finish_time = time;
        *finished_count += 1;
        return scheduler_on_finish(scheduler, process->pid)
            && record_event(result, time, SIM_EVENT_FINISH, process->pid);
    }

    if (scheduler_should_preempt(scheduler, quantum_used)) {
        *last_pid = process->pid;
        *idle_since_last = 0;
        *running = NULL;
        *preempted = process;
    }

    return 1;
}

static int compute_metrics(Process **processes, int count, SimulationResult *result) {
    int i;
    long double turnaround_sum = 0.0L;
    long double slowdown_sum = 0.0L;
    long double slowdown_sq_sum = 0.0L;
    double first_slowdown = 0.0;
    int all_slowdowns_equal = 1;

    if (processes == NULL || count <= 0 || result == NULL
        || (size_t)count > SIZE_MAX / sizeof(ProcessMetrics)) {
        return 0;
    }

    result->process_metrics = (ProcessMetrics *)calloc((size_t)count, sizeof(ProcessMetrics));
    if (result->process_metrics == NULL) {
        return 0;
    }
    result->process_metrics_count = (size_t)count;

    for (i = 0; i < count; i += 1) {
        Process *process = processes[i];
        ProcessMetrics *metrics = &result->process_metrics[i];
        int64_t ideal;
        int64_t turnaround;
        double slowdown;
        const Burst *burst;
        int io_requests = 0;

        if (process == NULL || process->finish_time < process->arrival_time
            || !checked_add_i64(process->total_cpu_original, process->total_io_original, &ideal)
            || ideal <= 0) {
            return 0;
        }
        turnaround = process->finish_time - process->arrival_time;
        slowdown = (double)turnaround / (double)ideal;
        if (!isfinite(slowdown) || slowdown < 1.0) {
            return 0;
        }

        metrics->pid = process->pid;
        metrics->arrival = process->arrival_time;
        metrics->completion = process->finish_time;
        metrics->turnaround = turnaround;
        metrics->ideal_time = ideal;
        metrics->slowdown = slowdown;
        metrics->priority = process->priority;
        metrics->total_cpu = process->total_cpu_original;
        metrics->total_io = process->total_io_original;
        for (burst = process->bursts; burst != NULL; burst = burst->next) {
            if (burst->io_time > 0) io_requests += 1;
        }
        metrics->io_requests = io_requests;

        if (i == 0) {
            first_slowdown = slowdown;
        } else if (slowdown != first_slowdown) {
            all_slowdowns_equal = 0;
        }

        turnaround_sum += (long double)turnaround;
        slowdown_sum += (long double)slowdown;
        slowdown_sq_sum += (long double)slowdown * (long double)slowdown;
        if (!isfinite(turnaround_sum) || !isfinite(slowdown_sum) || !isfinite(slowdown_sq_sum)) {
            return 0;
        }
    }

    result->process_count = count;
    result->mean_turnaround = (double)(turnaround_sum / (long double)count);
    if (!isfinite(result->mean_turnaround) || slowdown_sq_sum <= 0.0L) {
        return 0;
    }

    result->jain_slowdown_pct = all_slowdowns_equal
        ? 100.0
        : (double)((slowdown_sum * slowdown_sum)
                   / ((long double)count * slowdown_sq_sum) * 100.0L);
    if (!isfinite(result->jain_slowdown_pct) || result->jain_slowdown_pct <= 0.0) {
        return 0;
    }
    if (result->jain_slowdown_pct > 100.0) {
        result->jain_slowdown_pct = 100.0;
    }

    return 1;
}

static void destroy_processes(Process **processes, int count) {
    int i;

    for (i = 0; i < count; i += 1) {
        process_destroy(processes[i]);
    }
    free(processes);
}

static Process **index_processes_by_pid(Process **processes, int count) {
    Process **by_pid;
    int i;

    if (processes == NULL || count <= 0
        || (size_t)(count + 1) > SIZE_MAX / sizeof(Process *)) {
        return NULL;
    }
    by_pid = (Process **)calloc((size_t)count + 1, sizeof(Process *));
    if (by_pid == NULL) return NULL;

    for (i = 0; i < count; i += 1) {
        int pid = processes[i]->pid;
        if (pid < 1 || pid > count || by_pid[pid] != NULL) {
            free(by_pid);
            return NULL;
        }
        by_pid[pid] = processes[i];
    }
    return by_pid;
}

static Process *selected_process(Process **by_pid, int process_count, int pid) {
    Process *process;

    if (by_pid == NULL || pid < 1 || pid > process_count) return NULL;
    process = by_pid[pid];
    return process != NULL && process->state == PROCESS_READY ? process : NULL;
}

int simulation_run(ProcessQueue *workload,
                   const char *algorithm,
                   int context_switch_cost,
                   int rr_quantum,
                   SimulationResult *result) {
    Scheduler *scheduler = NULL;
    ProcessQueue *blocked = NULL;
    Process **processes = NULL;
    Process **process_by_pid = NULL;
    ReadyTransition *ready_now = NULL;
    Process *running = NULL;
    Process *context_target = NULL;
    Process *preempted = NULL;
    int64_t context_end = -1;
    int quantum_used = 0;
    int finished_count = 0;
    int ready_queue_count = 0;
    int process_count = 0;
    int64_t time = 0;
    int64_t last_time = 0;
    int last_pid = 0;
    int idle_since_last = 1;
    int ok = 0;

    if (workload == NULL || result == NULL || context_switch_cost < 0
        || context_switch_cost > 1000000 || rr_quantum <= 0 || rr_quantum > 1000000) {
        return 0;
    }

    *result = (SimulationResult){0};
    if (!remember_processes(workload, &processes, &process_count)) {
        return 0;
    }
    if (!validate_processes(processes, process_count)) {
        goto cleanup;
    }
    process_by_pid = index_processes_by_pid(processes, process_count);
    if (process_by_pid == NULL) goto cleanup;

    ready_now = (ReadyTransition *)malloc(sizeof(ReadyTransition) * (size_t)process_count);
    scheduler = scheduler_create(algorithm, rr_quantum);
    blocked = queue_create(QUEUE_BLOCKED, compare_io_finish);
    if ((ready_now == NULL && process_count > 0) || scheduler == NULL || blocked == NULL) {
        goto cleanup;
    }

    while (finished_count < process_count) {
        int ready_count = 0;
        int64_t next;
        int dispatched_from_context = 0;
        Process *selected = NULL;

        if (context_target != NULL && context_end == time) {
            running = context_target;
            context_target = NULL;
            context_end = -1;
            dispatched_from_context = 1;
            if (!dispatch(running, time, &quantum_used, result)) {
                goto cleanup;
            }
        }

        if (!dispatched_from_context
            && !process_cpu_result(&running, blocked, time, &finished_count,
                                   &last_pid, &idle_since_last, &preempted,
                                   scheduler, quantum_used, result)) {
            goto cleanup;
        }

        if (!collect_io(blocked, time, ready_now, &ready_count, result)) {
            goto cleanup;
        }
        if (!collect_arrivals(workload, time, ready_now, &ready_count, result)) {
            goto cleanup;
        }
        if (!enqueue_ready_batch(scheduler, ready_now, ready_count)) {
            goto cleanup;
        }

        if (preempted != NULL) {
            int selected_pid = 0;
            SchedulerSelectResult selection =
                scheduler_select_next(scheduler, time, &selected_pid);

            if (selection == SCHEDULER_SELECT_ERROR) goto cleanup;
            if (selection == SCHEDULER_SELECT_EMPTY) {
                running = preempted;
                running->state = PROCESS_RUNNING;
                quantum_used = 0;
            } else {
                SchedulerProcessView view;

                selected = selected_process(process_by_pid, process_count, selected_pid);
                if (selected == NULL) goto cleanup;
                preempted->state = PROCESS_READY;
                preempted->ready_since = time;
                view = scheduler_view(preempted);
                if (!record_event(result, time, SIM_EVENT_PREEMPT, preempted->pid)
                    || !scheduler_on_preempted(scheduler, &view)) {
                    goto cleanup;
                }
            }
            preempted = NULL;
        }

        if (running == NULL && context_target == NULL) {
            Process *next_process = selected;

            if (next_process == NULL) {
                int selected_pid = 0;
                SchedulerSelectResult selection =
                    scheduler_select_next(scheduler, time, &selected_pid);

                if (selection == SCHEDULER_SELECT_ERROR) goto cleanup;
                if (selection == SCHEDULER_SELECT_OK) {
                    next_process = selected_process(process_by_pid, process_count,
                                                    selected_pid);
                    if (next_process == NULL) goto cleanup;
                }
            }
            if (next_process != NULL) {
                if (!start_next(next_process, time, context_switch_cost, last_pid, idle_since_last,
                                &context_target, &context_end, &quantum_used, &result->context_switches,
                                &running, result)) {
                    goto cleanup;
                }
            }
        }

        {
            size_t event_index = result->event_count;
            int preemptions_enqueued = 0;
            int selected_from_ready = 0;

            while (event_index > 0 && result->events[event_index - 1].time == time) {
                SimulationEventType type = result->events[event_index - 1].type;

                if (type == SIM_EVENT_PREEMPT) preemptions_enqueued += 1;
                if (!dispatched_from_context
                    && (type == SIM_EVENT_DISPATCH || type == SIM_EVENT_CONTEXT_SWITCH)) {
                    selected_from_ready = 1;
                }
                event_index -= 1;
            }
            ready_queue_count += ready_count + preemptions_enqueued - selected_from_ready;
        }

        if (running != NULL) {
            running->remaining_cpu -= 1;
            if (!checked_add_i64(running->cpu_consumed, 1,
                                 &running->cpu_consumed)) {
                goto cleanup;
            }
            quantum_used += 1;
            if (!checked_add_i64(time, 1, &time)) {
                goto cleanup;
            }
        } else if (context_target != NULL) {
            next = next_external_event(workload, blocked);
            if (next > time && next < context_end) {
                time = next;
            } else {
                time = context_end;
            }
        } else {
            next = next_external_event(workload, blocked);
            if (next == INT64_MAX) {
                break;
            }
            if (next > time && !record_event(result, time, SIM_EVENT_IDLE, 0)) {
                goto cleanup;
            }
            idle_since_last = 1;
            if (next > time) {
                time = next;
            } else if (!checked_add_i64(time, 1, &time)) {
                goto cleanup;
            }
        }
        if (!check_invariants(processes, process_count, workload, blocked,
                              ready_queue_count, running, context_target,
                              finished_count, time, last_time, stderr)) {
            goto cleanup;
        }
        last_time = time;
    }

    if (finished_count != process_count) {
        fprintf(stderr,
                "Invariant failed: simulation stalled (%d of %d processes finished)\n",
                finished_count, process_count);
        goto cleanup;
    }
    if (!check_invariants(processes, process_count, workload, blocked,
                          ready_queue_count, running, context_target,
                          finished_count, time, last_time, stderr)) {
        goto cleanup;
    }
    result->makespan = time;
    if (!compute_metrics(processes, process_count, result)) {
        goto cleanup;
    }

    result->global_total_cpu = 0;
    result->global_total_io = 0;
    
    if (result->process_metrics != NULL) {
        for (size_t i = 0; i < result->process_metrics_count; i++) {
            result->global_total_cpu += result->process_metrics[i].total_cpu;
            result->global_total_io += result->process_metrics[i].total_io;
        }
    }

    ok = 1;

cleanup:
    queue_destroy(workload);
    queue_destroy(blocked);
    scheduler_destroy(scheduler);
    free(process_by_pid);
    free(ready_now);
    destroy_processes(processes, process_count);
    if (!ok) {
        simulation_result_destroy(result);
    }
    return ok;
}

void simulation_result_destroy(SimulationResult *result) {
    SimulationEvent *events;
    ProcessMetrics *process_metrics;

    if (result == NULL) {
        return;
    }
    events = result->events;
    process_metrics = result->process_metrics;
    *result = (SimulationResult){0};
    free(events);
    free(process_metrics);
}

const char *simulation_event_name(SimulationEventType type) {
    switch (type) {
        case SIM_EVENT_ARRIVAL: return "ARRIVAL";
        case SIM_EVENT_DISPATCH: return "DISPATCH";
        case SIM_EVENT_CPU_BURST_END: return "CPU_BURST_END";
        case SIM_EVENT_IO_START: return "IO_START";
        case SIM_EVENT_IO_END: return "IO_END";
        case SIM_EVENT_CONTEXT_SWITCH: return "CONTEXT_SWITCH";
        case SIM_EVENT_PREEMPT: return "PREEMPT";
        case SIM_EVENT_FINISH: return "FINISH";
        case SIM_EVENT_IDLE: return "IDLE";
    }

    return "UNKNOWN";
}
