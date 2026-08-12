#include "simulation.h"

#include "scheduler.h"

#include <limits.h>
#include <stdlib.h>

static int count_queue(ProcessQueue *queue) {
    int count = 0;
    ProcessNode *node = queue != NULL ? queue->head : NULL;

    while (node != NULL) {
        count += 1;
        node = node->next;
    }

    return count;
}

static int remember_processes(ProcessQueue *queue, Process ***out, int *count) {
    int i = 0;
    ProcessNode *node = NULL;

    *count = count_queue(queue);
    *out = (Process **)malloc(sizeof(Process *) * (size_t)*count);
    if (*out == NULL && *count > 0) {
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

static int record_event(SimulationResult *result, int time, SimulationEventType type, int pid) {
    SimulationEvent *events = NULL;
    size_t capacity = 0;

    if (result->event_count == result->event_capacity) {
        capacity = result->event_capacity == 0 ? 32 : result->event_capacity * 2;
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

static int compare_process_pid(const void *a, const void *b) {
    const Process *pa = *(const Process * const *)a;
    const Process *pb = *(const Process * const *)b;
    return (pa->pid > pb->pid) - (pa->pid < pb->pid);
}

static int add_ready_now(Process **ready_now, int *ready_count, Process *process) {
    ready_now[*ready_count] = process;
    *ready_count += 1;
    return 1;
}

static int collect_io(ProcessQueue *blocked, int time, Process **ready_now, int *ready_count, SimulationResult *result) {
    while (!queue_is_empty(blocked) && queue_peek(blocked)->io_finish_time <= time) {
        Process *process = queue_pop(blocked);
        process->state = PROCESS_READY;
        process->ready_since = time;
        process->io_finish_time = -1;
        if (!record_event(result, time, SIM_EVENT_IO_END, process->pid)) {
            return 0;
        }
        add_ready_now(ready_now, ready_count, process);
    }

    return 1;
}

static int collect_arrivals(ProcessQueue *future, int time, Process **ready_now, int *ready_count, SimulationResult *result) {
    while (!queue_is_empty(future) && queue_peek(future)->arrival_time <= time) {
        Process *process = queue_pop(future);
        process->state = PROCESS_READY;
        process->ready_since = time;
        if (!record_event(result, time, SIM_EVENT_ARRIVAL, process->pid)) {
            return 0;
        }
        add_ready_now(ready_now, ready_count, process);
    }

    return 1;
}

static int enqueue_ready_batch(Scheduler *scheduler, Process **ready_now, int ready_count) {
    int i;

    qsort(ready_now, (size_t)ready_count, sizeof(Process *), compare_process_pid);
    for (i = 0; i < ready_count; i += 1) {
        if (!scheduler_enqueue(scheduler, ready_now[i])) {
            return 0;
        }
    }

    return 1;
}

static int next_external_event(ProcessQueue *future, ProcessQueue *blocked) {
    int next = INT_MAX;

    if (!queue_is_empty(future) && queue_peek(future)->arrival_time < next) {
        next = queue_peek(future)->arrival_time;
    }
    if (!queue_is_empty(blocked) && queue_peek(blocked)->io_finish_time < next) {
        next = queue_peek(blocked)->io_finish_time;
    }

    return next;
}

static int dispatch(Process *process, int time, int *quantum_used, SimulationResult *result) {
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
                      int time,
                      int context_switch_cost,
                      int last_pid,
                      int idle_since_last,
                      Process **context_target,
                      int *context_end,
                      int *quantum_used,
                      int *context_switches,
                      Process **running,
                      SimulationResult *result) {
    int switches = last_pid > 0 && process->pid != last_pid && !idle_since_last;

    if (switches) {
        *context_switches += 1;
        if (!record_event(result, time, SIM_EVENT_CONTEXT_SWITCH, process->pid)) {
            return 0;
        }
    }

    if (switches && context_switch_cost > 0) {
        *context_target = process;
        *context_end = time + context_switch_cost;
        return 1;
    }

    *running = process;
    return dispatch(process, time, quantum_used, result);
}

static int process_cpu_result(Process **running,
                              ProcessQueue *blocked,
                              int time,
                              int *finished_count,
                              int *last_pid,
                              int *idle_since_last,
                              Process **preempted,
                              const Scheduler *scheduler,
                              int quantum_used,
                              SimulationResult *result) {
    Process *process = *running;
    int io_time;

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
            process->io_finish_time = time + io_time;
            if (!record_event(result, time, SIM_EVENT_IO_START, process->pid)) {
                return 0;
            }
            return queue_insert(blocked, process);
        }

        process->state = PROCESS_FINISHED;
        process->finish_time = time;
        *finished_count += 1;
        return record_event(result, time, SIM_EVENT_FINISH, process->pid);
    }

    if (scheduler_should_preempt(scheduler, quantum_used)) {
        *last_pid = process->pid;
        *idle_since_last = 0;
        *running = NULL;
        *preempted = process;
    }

    return 1;
}

static void compute_metrics(Process **processes, int count, SimulationResult *result) {
    int i;
    double turnaround_sum = 0.0;
    double slowdown_sum = 0.0;
    double slowdown_sq_sum = 0.0;

    for (i = 0; i < count; i += 1) {
        double turnaround = (double)(processes[i]->finish_time - processes[i]->arrival_time);
        double ideal = (double)(processes[i]->total_cpu_original + processes[i]->total_io_original);
        double slowdown = ideal > 0.0 ? turnaround / ideal : 0.0;

        turnaround_sum += turnaround;
        slowdown_sum += slowdown;
        slowdown_sq_sum += slowdown * slowdown;
    }

    result->process_count = count;
    result->mean_turnaround = count > 0 ? turnaround_sum / (double)count : 0.0;
    result->jain_slowdown_pct = slowdown_sq_sum > 0.0
        ? (slowdown_sum * slowdown_sum) / ((double)count * slowdown_sq_sum) * 100.0
        : 0.0;
}

static void destroy_processes(Process **processes, int count) {
    int i;

    for (i = 0; i < count; i += 1) {
        process_destroy(processes[i]);
    }
    free(processes);
}

int simulation_run(ProcessQueue *workload,
                   const char *algorithm,
                   int context_switch_cost,
                   int rr_quantum,
                   SimulationResult *result) {
    Scheduler *scheduler = NULL;
    ProcessQueue *blocked = NULL;
    Process **processes = NULL;
    Process **ready_now = NULL;
    Process *running = NULL;
    Process *context_target = NULL;
    Process *preempted = NULL;
    int context_end = -1;
    int quantum_used = 0;
    int finished_count = 0;
    int process_count = 0;
    int time = 0;
    int last_pid = 0;
    int idle_since_last = 1;
    int ok = 0;

    if (workload == NULL || result == NULL || context_switch_cost < 0 || rr_quantum <= 0) {
        return 0;
    }

    *result = (SimulationResult){0};
    if (!remember_processes(workload, &processes, &process_count)) {
        return 0;
    }

    ready_now = (Process **)malloc(sizeof(Process *) * (size_t)process_count);
    scheduler = scheduler_create(algorithm, rr_quantum);
    blocked = queue_create(QUEUE_BLOCKED, compare_io_finish);
    if ((ready_now == NULL && process_count > 0) || scheduler == NULL || blocked == NULL) {
        goto cleanup;
    }

    while (finished_count < process_count) {
        int ready_count = 0;
        int next;
        int dispatched_from_context = 0;

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
            && !process_cpu_result(&running, blocked, time, &finished_count, &last_pid,
                                   &idle_since_last, &preempted, scheduler, quantum_used, result)) {
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
            if (queue_is_empty(scheduler->ready)) {
                running = preempted;
                running->state = PROCESS_RUNNING;
                quantum_used = 0;
            } else {
                preempted->state = PROCESS_READY;
                preempted->ready_since = time;
                if (!record_event(result, time, SIM_EVENT_PREEMPT, preempted->pid)
                    || !scheduler_enqueue(scheduler, preempted)) {
                    goto cleanup;
                }
            }
            preempted = NULL;
        }

        if (running == NULL && context_target == NULL) {
            Process *next_process = scheduler_next(scheduler);
            if (next_process != NULL) {
                if (!start_next(next_process, time, context_switch_cost, last_pid, idle_since_last,
                                &context_target, &context_end, &quantum_used, &result->context_switches,
                                &running, result)) {
                    goto cleanup;
                }
            }
        }

        if (running != NULL) {
            running->remaining_cpu -= 1;
            quantum_used += 1;
            time += 1;
        } else if (context_target != NULL) {
            next = next_external_event(workload, blocked);
            if (next > time && next < context_end) {
                time = next;
            } else {
                time = context_end;
            }
        } else {
            next = next_external_event(workload, blocked);
            if (next == INT_MAX) {
                break;
            }
            if (next > time && !record_event(result, time, SIM_EVENT_IDLE, 0)) {
                goto cleanup;
            }
            idle_since_last = 1;
            time = next > time ? next : time + 1;
        }
    }

    result->makespan = time;
    compute_metrics(processes, process_count, result);
    ok = 1;

cleanup:
    queue_destroy(workload);
    queue_destroy(blocked);
    scheduler_destroy(scheduler);
    free(ready_now);
    destroy_processes(processes, process_count);
    if (!ok) {
        simulation_result_destroy(result);
    }
    return ok;
}

void simulation_result_destroy(SimulationResult *result) {
    if (result == NULL) {
        return;
    }
    free(result->events);
    result->events = NULL;
    result->event_count = 0;
    result->event_capacity = 0;
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
