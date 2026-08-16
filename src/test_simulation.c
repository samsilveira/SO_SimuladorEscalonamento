#include "test_simulation.h"

#include "process.h"
#include "simulation.h"
#include "workload.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

static Process *make_process(int pid, int64_t arrival, int priority) {
    return process_create(pid, arrival, priority);
}

static int add_process(ProcessQueue *queue, Process *process) {
    return process != NULL && queue_insert(queue, process);
}

static int has_event(const SimulationResult *result, int64_t time, SimulationEventType type, int pid) {
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

static const ProcessMetrics *metrics_for(const SimulationResult *result, int pid) {
    size_t i;

    for (i = 0; i < result->process_metrics_count; i += 1) {
        if (result->process_metrics[i].pid == pid) {
            return &result->process_metrics[i];
        }
    }
    return NULL;
}

static int nearly_equal(double left, double right) {
    return fabs(left - right) <= 1e-12;
}

static int results_equal(const SimulationResult *left, const SimulationResult *right) {
    size_t i;

    if (left->makespan != right->makespan
        || !nearly_equal(left->mean_turnaround, right->mean_turnaround)
        || left->context_switches != right->context_switches
        || !nearly_equal(left->jain_slowdown_pct, right->jain_slowdown_pct)
        || left->process_count != right->process_count
        || left->process_metrics_count != right->process_metrics_count
        || left->event_count != right->event_count) {
        return 0;
    }
    for (i = 0; i < left->event_count; i += 1) {
        if (left->events[i].time != right->events[i].time
            || left->events[i].type != right->events[i].type
            || left->events[i].pid != right->events[i].pid) {
            return 0;
        }
    }
    for (i = 0; i < left->process_metrics_count; i += 1) {
        const ProcessMetrics *a = &left->process_metrics[i];
        const ProcessMetrics *b = &right->process_metrics[i];

        if (a->pid != b->pid || a->arrival != b->arrival
            || a->completion != b->completion || a->turnaround != b->turnaround
            || a->ideal_time != b->ideal_time || !nearly_equal(a->slowdown, b->slowdown)
            || a->priority != b->priority || a->total_cpu != b->total_cpu
            || a->total_io != b->total_io || a->io_requests != b->io_requests) {
            return 0;
        }
    }
    return 1;
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
    if (has_event(&result, 0, SIM_EVENT_CONTEXT_SWITCH, 1)) return 1;
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
    if (has_event(&result, 0, SIM_EVENT_CONTEXT_SWITCH, 1)) return 1;
    if (!has_event(&result, 1, SIM_EVENT_IDLE, 0)) return 1;
    if (has_event(&result, 10, SIM_EVENT_CONTEXT_SWITCH, 2)) return 1;
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
    if (result.context_switches != 2) return 1;
    if (!has_event(&result, 2, SIM_EVENT_PREEMPT, 1)) return 1;
    if (!has_event(&result, 3, SIM_EVENT_FINISH, 2)) return 1;
    if (!has_event(&result, 4, SIM_EVENT_FINISH, 1)) return 1;

    simulation_result_destroy(&result);
    return 0;
}

static int test_context_switch_cost_and_cpu_unavailability(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    SimulationResult result = {0};
    Process *p1 = make_process(1, 0, 0);
    Process *p2 = make_process(2, 1, 0);

    if (workload == NULL) return 1;
    if (!process_add_burst(p1, 1, 0) || !process_add_burst(p2, 1, 0)) return 1;
    if (!add_process(workload, p1) || !add_process(workload, p2)) return 1;

    if (!simulation_run(workload, "fcfs", 2, 4, &result)) return 1;

    if (result.makespan != 4 || result.context_switches != 1) return 1;
    if (has_event(&result, 0, SIM_EVENT_CONTEXT_SWITCH, 1)) return 1;
    if (!has_event(&result, 1, SIM_EVENT_ARRIVAL, 2)) return 1;
    if (!has_event(&result, 0, SIM_EVENT_DISPATCH, 1)) return 1;
    if (!has_event(&result, 1, SIM_EVENT_CONTEXT_SWITCH, 2)) return 1;
    if (!has_event(&result, 3, SIM_EVENT_DISPATCH, 2)) return 1;
    if (!has_event(&result, 4, SIM_EVENT_FINISH, 2)) return 1;

    simulation_result_destroy(&result);
    return 0;
}

static int test_rr_single_process_renews_without_switch(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    SimulationResult result = {0};
    Process *process = make_process(1, 0, 0);

    if (workload == NULL) return 1;
    if (!process_add_burst(process, 5, 0) || !add_process(workload, process)) return 1;
    if (!simulation_run(workload, "rr", 1, 2, &result)) return 1;

    if (result.makespan != 5 || result.context_switches != 0) return 1;
    if (has_event(&result, 2, SIM_EVENT_PREEMPT, 1)) return 1;
    if (has_event(&result, 4, SIM_EVENT_PREEMPT, 1)) return 1;

    simulation_result_destroy(&result);
    return 0;
}

static int test_individual_and_aggregate_metrics(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    SimulationResult result = {0};
    Process *p1 = make_process(1, 0, 0);
    Process *p2 = make_process(2, 0, 0);
    const ProcessMetrics *m1;
    const ProcessMetrics *m2;

    if (workload == NULL) return 1;
    if (!process_add_burst(p1, 1, 0) || !process_add_burst(p2, 1, 0)) return 1;
    if (!add_process(workload, p1) || !add_process(workload, p2)) return 1;
    if (!simulation_run(workload, "fcfs", 0, 4, &result)) return 1;

    m1 = metrics_for(&result, 1);
    m2 = metrics_for(&result, 2);
    if (m1 == NULL || m2 == NULL || result.process_metrics_count != 2) return 1;
    if (m1->arrival != 0 || m1->completion != 1 || m1->turnaround != 1
        || m1->ideal_time != 1 || !nearly_equal(m1->slowdown, 1.0)
        || m1->priority != 0 || m1->io_requests != 0) return 1;
    if (m2->arrival != 0 || m2->completion != 2 || m2->turnaround != 2
        || m2->ideal_time != 1 || !nearly_equal(m2->slowdown, 2.0)) return 1;
    if (!nearly_equal(result.mean_turnaround, 1.5)) return 1;
    if (!nearly_equal(result.jain_slowdown_pct, 90.0)) return 1;
    if (result.jain_slowdown_pct <= 0.0 || result.jain_slowdown_pct > 100.0) return 1;

    simulation_result_destroy(&result);
    return 0;
}

static int test_jain_is_exactly_100_for_equal_slowdowns(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    SimulationResult result = {0};
    Process *p1 = make_process(1, 0, 0);
    Process *p2 = make_process(2, 1, 0);

    if (workload == NULL) return 1;
    if (!process_add_burst(p1, 1, 0) || !process_add_burst(p2, 1, 0)) return 1;
    if (!add_process(workload, p1) || !add_process(workload, p2)) return 1;
    if (!simulation_run(workload, "fcfs", 0, 4, &result)) return 1;

    if (result.jain_slowdown_pct != 100.0 || !nearly_equal(result.mean_turnaround, 1.0)) return 1;

    simulation_result_destroy(&result);
    return 0;
}

static int test_invalid_empty_zero_ideal_and_overflow_inputs(void) {
    ProcessQueue *empty = queue_create(QUEUE_FUTURE, compare_arrival);
    ProcessQueue *zero_ideal = queue_create(QUEUE_FUTURE, compare_arrival);
    ProcessQueue *overflow = queue_create(QUEUE_FUTURE, compare_arrival);
    SimulationResult result = {0};
    Process *without_burst = make_process(1, 0, 0);
    Process *late = make_process(2, INT64_MAX, 0);

    if (empty == NULL || zero_ideal == NULL || overflow == NULL
        || without_burst == NULL || late == NULL) return 1;
    if (simulation_run(empty, "fcfs", 1, 4, &result)) return 1;
    if (!add_process(zero_ideal, without_burst)) return 1;
    if (simulation_run(zero_ideal, "fcfs", 1, 4, &result)) return 1;
    if (!process_add_burst(late, 1, 0) || !add_process(overflow, late)) return 1;
    if (simulation_run(overflow, "fcfs", 1, 4, &result)) return 1;
    if (result.events != NULL || result.process_metrics != NULL) return 1;

    return 0;
}

static ProcessQueue *make_io_fixture(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    Process *p1 = make_process(1, 0, 1);
    Process *p2 = make_process(2, 1, 2);

    if (workload == NULL || p1 == NULL || p2 == NULL
        || !process_add_burst(p1, 2, 3) || !process_add_burst(p1, 1, 0)
        || !process_add_burst(p2, 2, 0)
        || !add_process(workload, p1) || !add_process(workload, p2)) {
        queue_destroy(workload);
        process_destroy(p1);
        process_destroy(p2);
        return NULL;
    }
    return workload;
}

static ProcessQueue *make_contention_fixture(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    Process *p1 = make_process(1, 0, 5);
    Process *p2 = make_process(2, 0, 0);

    if (workload == NULL || p1 == NULL || p2 == NULL
        || !process_add_burst(p1, 4, 0) || !process_add_burst(p2, 1, 0)
        || !add_process(workload, p1) || !add_process(workload, p2)) {
        queue_destroy(workload);
        process_destroy(p1);
        process_destroy(p2);
        return NULL;
    }
    return workload;
}

static int test_all_algorithms_on_manual_fixtures(void) {
    const char *algorithms[] = {"fcfs", "rr", "prioridade", "proprio"};
    const int64_t contention_p1_completion[] = {4, 5, 5, 5};
    const int64_t contention_p2_completion[] = {5, 3, 1, 1};
    const uint64_t contention_switches[] = {1, 2, 1, 1};
    int i;

    for (i = 0; i < 4; i += 1) {
        ProcessQueue *workload = make_io_fixture();
        SimulationResult result = {0};
        const ProcessMetrics *p1;
        const ProcessMetrics *p2;

        if (workload == NULL || !simulation_run(workload, algorithms[i], 1, 4, &result)) {
            return 1;
        }
        p1 = metrics_for(&result, 1);
        p2 = metrics_for(&result, 2);
        if (result.makespan != 7 || result.context_switches != 2
            || p1 == NULL || p1->completion != 7 || p1->turnaround != 7
            || p2 == NULL || p2->completion != 5 || p2->turnaround != 4
            || !has_event(&result, 2, SIM_EVENT_IO_START, 1)
            || !has_event(&result, 5, SIM_EVENT_IO_END, 1)) {
            simulation_result_destroy(&result);
            return 1;
        }
        simulation_result_destroy(&result);

        workload = make_contention_fixture();
        if (workload == NULL || !simulation_run(workload, algorithms[i], 0, 2, &result)) {
            return 1;
        }
        p1 = metrics_for(&result, 1);
        p2 = metrics_for(&result, 2);
        if (result.makespan != 5 || result.context_switches != contention_switches[i]
            || p1 == NULL || p1->completion != contention_p1_completion[i]
            || p2 == NULL || p2->completion != contention_p2_completion[i]
            || has_event(&result, 2, SIM_EVENT_PREEMPT, 1) != (i == 1)) {
            simulation_result_destroy(&result);
            return 1;
        }
        simulation_result_destroy(&result);
    }
    return 0;
}

static int test_extreme_inputs(void) {
    ProcessQueue *workload = queue_create(QUEUE_FUTURE, compare_arrival);
    SimulationResult result = {0};
    int i;

    if (workload == NULL) return 1;
    for (i = 1; i <= 1000; i++) {
        Process *p = make_process(i, i % 10, i % 4);
        if (!process_add_burst(p, 1, 1) || !process_add_burst(p, 1, 0)) return 1;
        if (!add_process(workload, p)) return 1;
    }

    if (!simulation_run(workload, "rr", 1, 2, &result)) return 1;
    if (result.process_count != 1000) return 1;
    simulation_result_destroy(&result);
    return 0;
}

static int test_seeded_determinism(void) {
    const char *algorithms[] = {"fcfs", "rr", "prioridade", "proprio"};
    int i;

    for (i = 0; i < 4; i += 1) {
        ProcessQueue *workload1 = workload_generate(25, "equilibrado", 42);
        ProcessQueue *workload2 = workload_generate(25, "equilibrado", 42);
        SimulationResult result1 = {0};
        SimulationResult result2 = {0};
        int equal;

        if (workload1 == NULL || workload2 == NULL) {
            workload_destroy(workload1);
            workload_destroy(workload2);
            return 1;
        }
        if (!simulation_run(workload1, algorithms[i], 1, 4, &result1)) {
            workload_destroy(workload2);
            return 1;
        }
        if (!simulation_run(workload2, algorithms[i], 1, 4, &result2)) {
            simulation_result_destroy(&result1);
            return 1;
        }
        equal = results_equal(&result1, &result2);
        simulation_result_destroy(&result1);
        simulation_result_destroy(&result2);
        if (!equal) return 1;
    }
    return 0;
}

int simulation_run_all_tests(void) {
    if (simulation_invariants_self_test()) {
        fprintf(stderr, "simulation_invariants_self_test failed\n");
        return 1;
    }
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
    if (test_context_switch_cost_and_cpu_unavailability()) {
        fprintf(stderr, "test_context_switch_cost_and_cpu_unavailability failed\n");
        return 1;
    }
    if (test_rr_single_process_renews_without_switch()) {
        fprintf(stderr, "test_rr_single_process_renews_without_switch failed\n");
        return 1;
    }
    if (test_individual_and_aggregate_metrics()) {
        fprintf(stderr, "test_individual_and_aggregate_metrics failed\n");
        return 1;
    }
    if (test_jain_is_exactly_100_for_equal_slowdowns()) {
        fprintf(stderr, "test_jain_is_exactly_100_for_equal_slowdowns failed\n");
        return 1;
    }
    if (test_invalid_empty_zero_ideal_and_overflow_inputs()) {
        fprintf(stderr, "test_invalid_empty_zero_ideal_and_overflow_inputs failed\n");
        return 1;
    }

    if (test_all_algorithms_on_manual_fixtures()) {
        fprintf(stderr, "test_all_algorithms_on_manual_fixtures failed\n");
        return 1;
    }
    if (test_extreme_inputs()) {
        fprintf(stderr, "test_extreme_inputs failed\n");
        return 1;
    }
    if (test_seeded_determinism()) {
        fprintf(stderr, "test_seeded_determinism failed\n");
        return 1;
    }

    return 0;
}
