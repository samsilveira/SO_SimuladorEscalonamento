#include "test_simulation.h"

#include "process.h"
#include "simulation.h"

#include <stdio.h>

static Process *make_process(int pid, int arrival, int priority) {
    return process_create(pid, arrival, priority);
}

static int add_process(ProcessQueue *queue, Process *process) {
    return process != NULL && queue_insert(queue, process);
}

static int has_event(const SimulationResult *result, int time, SimulationEventType type, int pid) {
    size_t i;

    for (i = 0; i < result->event_count; i += 1) {
        if (result->events[i].time == time
            && result->events[i].type == type
            && result->events[i].pid == pid) {
            return 1;
        }
    }

    return 0;
}

static int test_manual_timeline_with_io_and_context_switch(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    SimulationResult result = {0};
    Process *p1 = make_process(1, 0, 0);
    Process *p2 = make_process(2, 1, 0);

    if (workload == NULL) return 1;
    if (!process_add_burst(p1, 2, 3) || !process_add_burst(p1, 1, 0)) return 1;
    if (!process_add_burst(p2, 2, 0)) return 1;
    if (!add_process(workload, p1) || !add_process(workload, p2)) return 1;

    if (!simulation_run(workload, "fcfs", 1, 4, &result)) return 1;

    if (result.makespan != 7) return 1;
    if (result.context_switches != 2) return 1;
    if (!has_event(&result, 2, SIM_EVENT_IO_START, 1)) return 1;
    if (!has_event(&result, 5, SIM_EVENT_IO_END, 1)) return 1;
    if (!has_event(&result, 5, SIM_EVENT_CONTEXT_SWITCH, 1)) return 1;
    if (!has_event(&result, 7, SIM_EVENT_FINISH, 1)) return 1;

    simulation_result_destroy(&result);
    return 0;
}

static int test_multiple_io_and_simultaneous_completions(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    SimulationResult result = {0};
    Process *p1 = make_process(1, 0, 0);
    Process *p2 = make_process(2, 0, 0);

    if (workload == NULL) return 1;
    if (!process_add_burst(p1, 1, 3) || !process_add_burst(p1, 1, 0)) return 1;
    if (!process_add_burst(p2, 1, 2) || !process_add_burst(p2, 1, 0)) return 1;
    if (!add_process(workload, p1) || !add_process(workload, p2)) return 1;

    if (!simulation_run(workload, "fcfs", 0, 4, &result)) return 1;

    if (result.makespan != 6) return 1;
    if (!has_event(&result, 4, SIM_EVENT_IO_END, 1)) return 1;
    if (!has_event(&result, 4, SIM_EVENT_IO_END, 2)) return 1;
    if (!has_event(&result, 5, SIM_EVENT_FINISH, 1)) return 1;
    if (!has_event(&result, 6, SIM_EVENT_FINISH, 2)) return 1;

    simulation_result_destroy(&result);
    return 0;
}

static int test_process_with_multiple_io_requests(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    SimulationResult result = {0};
    Process *p = make_process(1, 0, 0);

    if (workload == NULL) return 1;
    if (!process_add_burst(p, 1, 1)
        || !process_add_burst(p, 1, 1)
        || !process_add_burst(p, 1, 0)) return 1;
    if (!add_process(workload, p)) return 1;

    if (!simulation_run(workload, "fcfs", 0, 4, &result)) return 1;

    if (result.makespan != 5) return 1;
    if (!has_event(&result, 1, SIM_EVENT_IO_START, 1)) return 1;
    if (!has_event(&result, 3, SIM_EVENT_IO_START, 1)) return 1;
    if (!has_event(&result, 5, SIM_EVENT_FINISH, 1)) return 1;

    simulation_result_destroy(&result);
    return 0;
}

static int test_idle_jump_and_global_finish(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    SimulationResult result = {0};
    Process *p1 = make_process(1, 0, 0);
    Process *p2 = make_process(2, 10, 0);

    if (workload == NULL) return 1;
    if (!process_add_burst(p1, 1, 0) || !process_add_burst(p2, 1, 0)) return 1;
    if (!add_process(workload, p1) || !add_process(workload, p2)) return 1;

    if (!simulation_run(workload, "fcfs", 1, 4, &result)) return 1;

    if (result.makespan != 11) return 1;
    if (result.context_switches != 0) return 1;
    if (!has_event(&result, 1, SIM_EVENT_IDLE, 0)) return 1;
    if (!has_event(&result, 11, SIM_EVENT_FINISH, 2)) return 1;

    simulation_result_destroy(&result);
    return 0;
}

static int test_round_robin_preemption(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    SimulationResult result = {0};
    Process *p1 = make_process(1, 0, 0);
    Process *p2 = make_process(2, 0, 0);

    if (workload == NULL) return 1;
    if (!process_add_burst(p1, 3, 0) || !process_add_burst(p2, 1, 0)) return 1;
    if (!add_process(workload, p1) || !add_process(workload, p2)) return 1;

    if (!simulation_run(workload, "rr", 0, 2, &result)) return 1;

    if (result.makespan != 4) return 1;
    if (!has_event(&result, 2, SIM_EVENT_PREEMPT, 1)) return 1;
    if (!has_event(&result, 3, SIM_EVENT_FINISH, 2)) return 1;
    if (!has_event(&result, 4, SIM_EVENT_FINISH, 1)) return 1;

    simulation_result_destroy(&result);
    return 0;
}

int simulation_run_all_tests(void) {
    if (test_manual_timeline_with_io_and_context_switch()) {
        fprintf(stderr, "test_manual_timeline_with_io_and_context_switch failed\n");
        return 1;
    }
    if (test_multiple_io_and_simultaneous_completions()) {
        fprintf(stderr, "test_multiple_io_and_simultaneous_completions failed\n");
        return 1;
    }
    if (test_process_with_multiple_io_requests()) {
        fprintf(stderr, "test_process_with_multiple_io_requests failed\n");
        return 1;
    }
    if (test_idle_jump_and_global_finish()) {
        fprintf(stderr, "test_idle_jump_and_global_finish failed\n");
        return 1;
    }
    if (test_round_robin_preemption()) {
        fprintf(stderr, "test_round_robin_preemption failed\n");
        return 1;
    }

    return 0;
}
