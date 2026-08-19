#include "test_scheduler.h"

#include "scheduler.h"

#include <stdio.h>

static SchedulerProcessView view(int pid, int priority,
                                 int64_t arrival, int64_t ready_since) {
    SchedulerProcessView process = {pid, priority, arrival, ready_since, 0};
    return process;
}

static int expect_pid_at(Scheduler *scheduler, int64_t time, int expected) {
    int pid = -1;
    return scheduler_select_next(scheduler, time, &pid) == SCHEDULER_SELECT_OK
        && pid == expected;
}

static int expect_pid(Scheduler *scheduler, int expected) {
    return expect_pid_at(scheduler, 100, expected);
}

static int test_fcfs_empty_and_validation(void) {
    Scheduler *scheduler = scheduler_create("fcfs", 4);
    SchedulerProcessView valid = view(1, 0, 0, 0);
    SchedulerProcessView invalid = view(0, 0, 0, 0);
    int pid = -1;

    if (scheduler == NULL || scheduler_select_next(scheduler, 0, &pid)
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

static int test_sjf_initial_estimate_history_and_ties(void) {
    Scheduler *sjf = scheduler_create("sjf", 4);
    SchedulerProcessView p1 = view(1, 9, 0, 0);
    SchedulerProcessView p2 = view(2, 0, 0, 1);
    SchedulerProcessView p3 = view(3, 5, 0, 0);

    if (sjf == NULL
        || !scheduler_on_arrival(sjf, &p3)
        || !scheduler_on_arrival(sjf, &p2)
        || !scheduler_on_arrival(sjf, &p1)
        || !expect_pid(sjf, 1)
        || !expect_pid(sjf, 3)
        || !expect_pid(sjf, 2)) return 1;

    p1.ready_since = 20;
    p2.ready_since = 20;
    p3.ready_since = 20;
    p1.cpu_consumed = 2; /* tau = 0,5 * 2 + 0,5 * 10 = 6 */
    p2.cpu_consumed = 8; /* tau = 9 */
    p3.cpu_consumed = 4; /* tau = 7 */
    if (!scheduler_on_io_complete(sjf, &p2)
        || !scheduler_on_io_complete(sjf, &p3)
        || !scheduler_on_io_complete(sjf, &p1)
        || scheduler_on_io_complete(sjf, &p1)
        || !expect_pid(sjf, 1)
        || !expect_pid(sjf, 3)
        || !expect_pid(sjf, 2)) return 1;

    p1.ready_since = 30;
    p2.ready_since = 30;
    p1.cpu_consumed = 6;  /* t_n = 4; tau = 5 */
    p2.cpu_consumed = 10; /* t_n = 2; tau = 5,5 */
    if (!scheduler_on_io_complete(sjf, &p2)
        || !scheduler_on_io_complete(sjf, &p1)
        || !expect_pid(sjf, 1)
        || !expect_pid(sjf, 2)
        || scheduler_should_preempt(sjf, 1000000)) return 1;

    scheduler_destroy(sjf);
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

static int test_pdbh_selection_and_functional_difference(void) {
    Scheduler *own = scheduler_create("proprio", 4);
    SchedulerProcessView cpu_heavy = view(1, 2, 0, 35);
    SchedulerProcessView waiting = view(3, 4, 6, 6);

    cpu_heavy.cpu_consumed = 20;
    if (own == NULL || !scheduler_on_io_complete(own, &cpu_heavy)
        || !scheduler_on_arrival(own, &waiting)
        || !expect_pid_at(own, 36, 3)) return 1;
    scheduler_destroy(own);
    return 0;
}

static int test_pdbh_ties_and_factor_boundaries(void) {
    Scheduler *own = scheduler_create("proprio", 4);
    SchedulerProcessView newer = view(8, 4, 0, 52);
    SchedulerProcessView older = view(9, 4, 0, 48);
    SchedulerProcessView pid7 = view(7, 5, 0, 50);
    SchedulerProcessView pid6 = view(6, 5, 0, 50);

    /* Em t=60, older e newer tem score 4; ready_since desempata. */
    older.priority = 5;
    if (own == NULL || !scheduler_on_arrival(own, &newer)
        || !scheduler_on_arrival(own, &older)
        || !expect_pid_at(own, 60, 9)) return 1;
    scheduler_destroy(own);

    own = scheduler_create("proprio", 4);
    if (own == NULL || !scheduler_on_arrival(own, &pid7)
        || !scheduler_on_arrival(own, &pid6)
        || !expect_pid_at(own, 60, 6)) return 1;
    scheduler_destroy(own);

    own = scheduler_create("proprio", 4);
    older = view(1, 1, 0, 0);       /* espera 10: score 0 */
    newer = view(2, 0, 0, 1);       /* espera 9: score 0 */
    if (own == NULL || !scheduler_on_arrival(own, &older)
        || !scheduler_on_arrival(own, &newer)
        || !expect_pid_at(own, 10, 1)) return 1;
    scheduler_destroy(own);

    own = scheduler_create("proprio", 4);
    older = view(2, 0, 0, 0);
    newer = view(1, 0, 0, 0);
    newer.cpu_consumed = 4;         /* penalidade 0: PID desempata */
    if (own == NULL || !scheduler_on_arrival(own, &newer)
        || !scheduler_on_arrival(own, &older)
        || !expect_pid_at(own, 0, 1)) return 1;
    scheduler_destroy(own);

    own = scheduler_create("proprio", 4);
    newer.cpu_consumed = 5;         /* penalidade 1: perde para score 0 */
    if (own == NULL || !scheduler_on_arrival(own, &older)
        || !scheduler_on_arrival(own, &newer)
        || !expect_pid_at(own, 0, 2)) return 1;
    scheduler_destroy(own);
    return 0;
}

static int test_pdbh_io_cpu_history_extremes_and_no_preemption(void) {
    Scheduler *own = scheduler_create("proprio", 4);
    SchedulerProcessView returned = view(1, 0, 0, 100);
    SchedulerProcessView long_wait = view(2, 9, 0, 0);
    SchedulerProcessView penalty_edge = view(3, 0, 0, 100);

    returned.cpu_consumed = 1000000000000LL;
    penalty_edge.cpu_consumed = 4;
    if (own == NULL || !scheduler_on_io_complete(own, &returned)
        || !scheduler_on_arrival(own, &long_wait)
        || !scheduler_on_arrival(own, &penalty_edge)
        || !expect_pid_at(own, 100, 2)
        || scheduler_should_preempt(own, 1000000)) return 1;
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
    if (test_sjf_initial_estimate_history_and_ties()) {
        fprintf(stderr, "test_sjf_initial_estimate_history_and_ties failed\n");
        return 1;
    }
    if (test_own_policy_still_uses_common_contract()) {
        fprintf(stderr, "test_own_policy_still_uses_common_contract failed\n");
        return 1;
    }
    if (test_pdbh_selection_and_functional_difference()) {
        fprintf(stderr, "test_pdbh_selection_and_functional_difference failed\n");
        return 1;
    }
    if (test_pdbh_ties_and_factor_boundaries()) {
        fprintf(stderr, "test_pdbh_ties_and_factor_boundaries failed\n");
        return 1;
    }
    if (test_pdbh_io_cpu_history_extremes_and_no_preemption()) {
        fprintf(stderr, "test_pdbh_io_cpu_history_extremes_and_no_preemption failed\n");
        return 1;
    }
    return 0;
}
