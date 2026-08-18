#include "test_process.h"
#include "process.h"
#include "queue.h"
#include <stdio.h>

static int test_process_states_and_bursts(void) {
    // Invalid creation
    if (process_create(0, 0, 0) != NULL) return 1;
    if (process_create(1, -1, 0) != NULL) return 1;
    if (process_create(1, 0, -1) != NULL) return 1;
    if (process_create(1, 0, 10) != NULL) return 1;

    Process *p = process_create(1, 0, 0);
    if (!p) return 1;
    
    // Invalid burst
    if (process_add_burst(p, -1, 10)) return 1;
    if (process_add_burst(p, 10, -5)) return 1;
    if (process_add_burst(p, 0, 10)) return 1;
    
    if (!process_add_burst(p, 5, 10)) return 1;
    if (!process_add_burst(p, 3, 0)) return 1;
    
    // Test rejection of burst after io_time == 0
    if (process_add_burst(p, 4, 0)) return 1;
    
    if (p->total_cpu_original != 8 || p->total_io_original != 10) return 1;
    
    // Test traverses states
    if (p->state != PROCESS_NEW) return 1;
    p->state = PROCESS_READY;
    if (p->state != PROCESS_READY) return 1;
    p->state = PROCESS_RUNNING;
    if (p->state != PROCESS_RUNNING) return 1;
    p->state = PROCESS_BLOCKED;
    if (p->state != PROCESS_BLOCKED) return 1;
    p->state = PROCESS_READY;
    if (p->state != PROCESS_READY) return 1;
    p->state = PROCESS_RUNNING;
    if (p->state != PROCESS_RUNNING) return 1;
    p->state = PROCESS_FINISHED;
    if (p->state != PROCESS_FINISHED) return 1;
    
    process_destroy(p);
    return 0;
}

static int test_ready_queue_validation(void) {
    ProcessQueue *q = queue_create(QUEUE_READY, compare_fcfs);
    
    Process *p_new = process_create(1, 0, 0);
    if (queue_insert(q, p_new)) return 1; // Should fail because it's NEW
    
    p_new->state = PROCESS_RUNNING;
    if (queue_insert(q, p_new)) return 1; // Should fail because it's RUNNING
    
    p_new->state = PROCESS_BLOCKED;
    if (queue_insert(q, p_new)) return 1; // Should fail
    
    p_new->state = PROCESS_FINISHED;
    if (queue_insert(q, p_new)) return 1; // Should fail
    
    p_new->state = PROCESS_READY;
    if (!queue_insert(q, p_new)) return 1; // Should succeed
    
    // Test duplicate PID rejection
    Process *p_dup = process_create(1, 5, 0);
    p_dup->state = PROCESS_READY;
    if (queue_insert(q, p_dup)) return 1; // Should fail due to duplicate PID
    
    process_destroy(p_dup);
    process_destroy(queue_pop(q)); // This destroys p_new
    queue_destroy(q);
    
    return 0;
}

static int test_tiebreakers(void) {
    ProcessQueue *q = queue_create(QUEUE_READY, compare_fcfs);
    
    Process *p1 = process_create(1, 0, 0); p1->state = PROCESS_READY; p1->ready_since = 10;
    Process *p2 = process_create(2, 0, 0); p2->state = PROCESS_READY; p2->ready_since = 5;
    Process *p3 = process_create(3, 0, 0); p3->state = PROCESS_READY; p3->ready_since = 10;
    
    queue_insert(q, p1);
    queue_insert(q, p2);
    queue_insert(q, p3);
    
    Process *out1 = queue_pop(q);
    Process *out2 = queue_pop(q);
    Process *out3 = queue_pop(q);
    
    // Expected order: p2 (ready_since=5), p1 (ready_since=10, pid=1), p3 (ready_since=10, pid=3)
    if (out1 != p2 || out2 != p1 || out3 != p3) return 1;
    
    process_destroy(p1);
    process_destroy(p2);
    process_destroy(p3);
    queue_destroy(q);
    return 0;
}

static int test_all_comparators(void) {
    // 1. Priority comparator: (priority, ready_since, PID)
    {
        ProcessQueue *q = queue_create(QUEUE_READY, compare_priority);
        Process *p1 = process_create(1, 0, 5); p1->state = PROCESS_READY; p1->ready_since = 10;
        Process *p2 = process_create(2, 0, 2); p2->state = PROCESS_READY; p2->ready_since = 10;
        Process *p3 = process_create(3, 0, 5); p3->state = PROCESS_READY; p3->ready_since = 5;
        
        queue_insert(q, p1);
        queue_insert(q, p2);
        queue_insert(q, p3);
        
        if (queue_pop(q) != p2 || queue_pop(q) != p3 || queue_pop(q) != p1) return 1;
        
        process_destroy(p1); process_destroy(p2); process_destroy(p3);
        queue_destroy(q);
    }
    
    // 2. IO Finish comparator: (io_finish_time, PID)
    {
        ProcessQueue *q = queue_create(QUEUE_BLOCKED, compare_io_finish);
        Process *p1 = process_create(1, 0, 0); p1->state = PROCESS_BLOCKED; p1->io_finish_time = 20;
        Process *p2 = process_create(2, 0, 0); p2->state = PROCESS_BLOCKED; p2->io_finish_time = 10;
        Process *p3 = process_create(3, 0, 0); p3->state = PROCESS_BLOCKED; p3->io_finish_time = 20;
        
        queue_insert(q, p1);
        queue_insert(q, p2);
        queue_insert(q, p3);
        
        if (queue_pop(q) != p2 || queue_pop(q) != p1 || queue_pop(q) != p3) return 1;
        
        process_destroy(p1); process_destroy(p2); process_destroy(p3);
        queue_destroy(q);
    }
    
    // 3. Arrival comparator: (arrival_time, PID)
    {
        ProcessQueue *q = queue_create(QUEUE_FUTURE, compare_arrival);
        Process *p1 = process_create(1, 15, 0); p1->state = PROCESS_NEW;
        Process *p2 = process_create(2, 5, 0);  p2->state = PROCESS_NEW;
        Process *p3 = process_create(3, 15, 0); p3->state = PROCESS_NEW;
        
        queue_insert(q, p1);
        queue_insert(q, p2);
        queue_insert(q, p3);
        
        if (queue_pop(q) != p2 || queue_pop(q) != p1 || queue_pop(q) != p3) return 1;
        
        process_destroy(p1); process_destroy(p2); process_destroy(p3);
        queue_destroy(q);
    }
    
    return 0;
}

static int test_queue_operations(void) {
    ProcessQueue *q = queue_create(QUEUE_READY, NULL); // FIFO fallback
    if (!queue_is_empty(q)) return 1;
    
    Process *p1 = process_create(1, 0, 0); p1->state = PROCESS_READY;
    Process *p2 = process_create(2, 0, 0); p2->state = PROCESS_READY;
    
    queue_insert(q, p1);
    queue_insert(q, p2);
    
    if (queue_is_empty(q)) return 1;
    if (queue_peek(q) != p1) return 1;
    
    Process *out1 = queue_pop(q);
    if (out1 != p1) return 1;
    
    Process *out2 = queue_pop(q);
    if (out2 != p2) return 1;
    
    if (!queue_is_empty(q)) return 1;
    
    process_destroy(p1);
    process_destroy(p2);
    queue_destroy(q);
    
    return 0;
}

int process_run_all_tests(void) {
    if (test_process_states_and_bursts()) {
        fprintf(stderr, "test_process_states_and_bursts failed\n");
        return 1;
    }
    if (test_ready_queue_validation()) {
        fprintf(stderr, "test_ready_queue_validation failed\n");
        return 1;
    }
    if (test_tiebreakers()) {
        fprintf(stderr, "test_tiebreakers failed\n");
        return 1;
    }
    if (test_all_comparators()) {
        fprintf(stderr, "test_all_comparators failed\n");
        return 1;
    }
    if (test_queue_operations()) {
        fprintf(stderr, "test_queue_operations failed\n");
        return 1;
    }
    return 0;
}
