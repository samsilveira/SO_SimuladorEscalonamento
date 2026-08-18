#include "process.h"

#include <stdint.h>
#include <stdlib.h>

Process* process_create(int pid, int64_t arrival_time, int priority) {
    if (pid <= 0 || arrival_time < 0 || priority < 0 || priority > 9) {
        return NULL; // Invalid process data
    }
    Process *p = (Process*)malloc(sizeof(Process));
    if (!p) return NULL;
    
    p->pid = pid;
    p->arrival_time = arrival_time;
    p->priority = priority;
    p->bursts = NULL;
    p->current_burst = NULL;
    p->remaining_cpu = 0;
    p->io_finish_time = -1;
    p->state = PROCESS_NEW;
    
    p->total_cpu_original = 0;
    p->total_io_original = 0;
    p->start_time = -1;
    p->finish_time = -1;
    p->cpu_consumed = 0;
    p->ready_since = -1;
    
    return p;
}

void process_destroy(Process *p) {
    if (!p) return;
    Burst *curr = p->bursts;
    while (curr) {
        Burst *next = curr->next;
        free(curr);
        curr = next;
    }
    free(p);
}

bool process_add_burst(Process *p, int64_t cpu_time, int64_t io_time) {
    if (!p || cpu_time <= 0 || cpu_time > 1000000 || io_time < 0 || io_time > 1000000) {
        return false;
    }

    if (p->total_cpu_original > INT64_MAX - cpu_time
        || p->total_io_original > INT64_MAX - io_time) {
        return false;
    }
    
    if (p->bursts != NULL) {
        Burst *curr = p->bursts;
        while (curr->next) {
            curr = curr->next;
        }
        if (curr->io_time == 0) {
            return false; // Cannot add burst after a burst with io_time == 0
        }
    }
    
    Burst *b = (Burst*)malloc(sizeof(Burst));
    if (!b) return false;
    
    b->cpu_time = cpu_time;
    b->io_time = io_time;
    b->next = NULL;
    
    if (p->bursts == NULL) {
        p->bursts = b;
        p->current_burst = b;
    } else {
        Burst *curr = p->bursts;
        while (curr->next) {
            curr = curr->next;
        }
        curr->next = b;
    }
    
    p->total_cpu_original += cpu_time;
    p->total_io_original += io_time;
    
    return true;
}
