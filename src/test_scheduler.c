#include "test_scheduler.h"

#include "scheduler.h"

#include <stdio.h>

static SchedulerProcessView view(int pid, int priority,
                                 int64_t arrival, int64_t ready_since) {
    SchedulerProcessView process = {pid, priority, arrival, ready_since};
    return process;
}

static int expect_pid(Scheduler *scheduler, int expected) {
    int pid = -1;
    return scheduler_select_next(scheduler, &pid) == SCHEDULER_SELECT_OK
        && pid == expected;
}

static int test_fcfs_empty_and_validation(void) {
    Scheduler *scheduler = scheduler_create("fcfs", 4);
    SchedulerProcessView valid = view(1, 0, 0, 0);
    SchedulerProcessView invalid = view(0, 0, 0, 0);
    int pid = -1;

    if (scheduler == NULL || scheduler_select_next(scheduler, &pid)
        != SCHEDULER_SELECT_EMPTY || pid != 0) return 1;
    if (scheduler_on_arrival(scheduler, &invalid)) return 1;
    if (!scheduler_on_arrival(scheduler, &valid)) return 1;
    if (scheduler_on_io_complete(scheduler, &valid)) return 1;
    if (!expect_pid(scheduler, 1)) return 1;
    if (!scheduler_on_finish(scheduler, 1)) return 1;
    if (scheduler_should_preempt(scheduler, 1000000)) return 1;

    scheduler_destroy(scheduler);
    return 0;
}

static int test_fcfs_ready_since_and_pid_order(void) {
    Scheduler *scheduler = scheduler_create("fcfs", 4);
    SchedulerProcessView later_pid = view(3, 9, 0, 10);
    SchedulerProcessView older = view(2, 0, 2, 5);
    SchedulerProcessView lower_pid = view(1, 5, 0, 10);

    if (scheduler == NULL) return 1;
    if (!scheduler_on_arrival(scheduler, &later_pid)
        || !scheduler_on_io_complete(scheduler, &older)
        || !scheduler_on_arrival(scheduler, &lower_pid)) return 1;
    if (!expect_pid(scheduler, 2)
        || !expect_pid(scheduler, 1)
        || !expect_pid(scheduler, 3)) return 1;

    scheduler_destroy(scheduler);
    return 0;
}

static int test_rr_fifo_quantum_and_reinsertion(void) {
    Scheduler *rr = scheduler_create("rr", 2);
    SchedulerProcessView p1 = view(1, 7, 0, 0);
    SchedulerProcessView p2 = view(2, 1, 0, 0);
    SchedulerProcessView p3 = view(3, 4, 1, 3);

    if (rr == NULL || scheduler_create("rr", 0) != NULL) return 1;
    if (!scheduler_on_arrival(rr, &p1) || !scheduler_on_arrival(rr, &p2)
        || scheduler_on_io_complete(rr, &p2)
        || !expect_pid(rr, 1)
        || scheduler_should_preempt(rr, 1)
        || !scheduler_should_preempt(rr, 2)
        || !scheduler_should_preempt(rr, 3)
        || !scheduler_on_io_complete(rr, &p3)
        || !scheduler_on_preempted(rr, &p1)
        || !expect_pid(rr, 2)
        || !expect_pid(rr, 3)
        || !expect_pid(rr, 1)) return 1;

    scheduler_destroy(rr);
    return 0;
}

static int test_priority_order_and_non_preemption(void) {
    Scheduler *priority = scheduler_create("prioridade", 4);
    SchedulerProcessView lower_priority = view(1, 7, 0, 0);
    SchedulerProcessView later = view(4, 1, 0, 8);
    SchedulerProcessView higher_pid = view(3, 1, 0, 5);
    SchedulerProcessView lower_pid = view(2, 1, 0, 5);

    if (priority == NULL) return 1;
    if (!scheduler_on_arrival(priority, &lower_priority)
        || !scheduler_on_arrival(priority, &later)
        || !scheduler_on_io_complete(priority, &higher_pid)
        || !scheduler_on_arrival(priority, &lower_pid)
        || scheduler_should_preempt(priority, 1000000)
        || !expect_pid(priority, 2)
        || !expect_pid(priority, 3)
        || !expect_pid(priority, 4)
        || !expect_pid(priority, 1)) return 1;

    scheduler_destroy(priority);
    return 0;
}

static int test_own_policy_still_uses_common_contract(void) {
    Scheduler *own = scheduler_create("proprio", 4);
    SchedulerProcessView p1 = view(1, 7, 0, 0);
    SchedulerProcessView p2 = view(2, 1, 0, 0);

    if (own == NULL) return 1;
    if (!scheduler_on_arrival(own, &p1) || !scheduler_on_arrival(own, &p2)
        || !expect_pid(own, 2)) return 1;

    scheduler_destroy(own);
    return 0;
}

int scheduler_run_all_tests(void) {
    if (test_fcfs_empty_and_validation()) {
        fprintf(stderr, "test_fcfs_empty_and_validation failed\n");
        return 1;
    }
    if (test_fcfs_ready_since_and_pid_order()) {
        fprintf(stderr, "test_fcfs_ready_since_and_pid_order failed\n");
        return 1;
    }
    if (test_rr_fifo_quantum_and_reinsertion()) {
        fprintf(stderr, "test_rr_fifo_quantum_and_reinsertion failed\n");
        return 1;
    }
    if (test_priority_order_and_non_preemption()) {
        fprintf(stderr, "test_priority_order_and_non_preemption failed\n");
        return 1;
    }
    if (test_own_policy_still_uses_common_contract()) {
        fprintf(stderr, "test_own_policy_still_uses_common_contract failed\n");
        return 1;
    }
    return 0;
}
