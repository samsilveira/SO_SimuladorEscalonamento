#include "queue.h"
#include <stdlib.h>

ProcessQueue* queue_create(QueueType type, bool (*comparator)(const Process *a, const Process *b)) {
    ProcessQueue *q = (ProcessQueue*)malloc(sizeof(ProcessQueue));
    if (!q) return NULL;
    q->head = NULL;
    q->comparator = comparator;
    q->type = type;
    return q;
}

void queue_destroy(ProcessQueue *q) {
    if (!q) return;
    ProcessNode *curr = q->head;
    while (curr) {
        ProcessNode *next = curr->next;
        free(curr); // Don't free the process itself
        curr = next;
    }
    free(q);
}

bool queue_contains_pid(ProcessQueue *q, int pid) {
    if (!q) return false;
    ProcessNode *curr = q->head;
    while (curr) {
        if (curr->process->pid == pid) return true;
        curr = curr->next;
    }
    return false;
}

bool queue_insert(ProcessQueue *q, Process *p) {
    if (!q || !p) return false;
    
    if (queue_contains_pid(q, p->pid)) return false;
    
    // Validate states
    if (q->type == QUEUE_READY) {
        if (p->state == PROCESS_NEW || p->state == PROCESS_BLOCKED || p->state == PROCESS_FINISHED) {
            return false;
        }
    } else if (q->type == QUEUE_FUTURE) {
        if (p->state != PROCESS_NEW) {
            return false;
        }
    } else if (q->type == QUEUE_BLOCKED) {
        if (p->state != PROCESS_BLOCKED) {
            return false;
        }
    }
    
    ProcessNode *node = (ProcessNode*)malloc(sizeof(ProcessNode));
    if (!node) return false;
    node->process = p;
    node->next = NULL;
    
    if (q->head == NULL) {
        q->head = node;
        return true;
    }
    
    if (q->comparator) {
        if (q->comparator(p, q->head->process)) {
            node->next = q->head;
            q->head = node;
            return true;
        }
        
        ProcessNode *curr = q->head;
        while (curr->next != NULL && !q->comparator(p, curr->next->process)) {
            curr = curr->next;
        }
        node->next = curr->next;
        curr->next = node;
    } else {
        ProcessNode *curr = q->head;
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = node;
    }
    return true;
}

Process* queue_pop(ProcessQueue *q) {
    if (!q || !q->head) return NULL;
    ProcessNode *node = q->head;
    Process *p = node->process;
    q->head = node->next;
    free(node);
    return p;
}

Process* queue_peek(ProcessQueue *q) {
    if (!q || !q->head) return NULL;
    return q->head->process;
}

bool queue_is_empty(ProcessQueue *q) {
    return q == NULL || q->head == NULL;
}

// FCFS: (ready_since, PID)
bool compare_fcfs(const Process *a, const Process *b) {
    if (a->ready_since != b->ready_since) {
        return a->ready_since < b->ready_since;
    }
    return a->pid < b->pid;
}

// Priority: (priority, ready_since, PID)
bool compare_priority(const Process *a, const Process *b) {
    if (a->priority != b->priority) {
        return a->priority < b->priority;
    }
    if (a->ready_since != b->ready_since) {
        return a->ready_since < b->ready_since;
    }
    return a->pid < b->pid;
}

// Blocked: (io_finish_time, PID)
bool compare_io_finish(const Process *a, const Process *b) {
    if (a->io_finish_time != b->io_finish_time) {
        return a->io_finish_time < b->io_finish_time;
    }
    return a->pid < b->pid;
}

// Future: (arrival_time, PID)
bool compare_arrival(const Process *a, const Process *b) {
    if (a->arrival_time != b->arrival_time) {
        return a->arrival_time < b->arrival_time;
    }
    return a->pid < b->pid;
}
