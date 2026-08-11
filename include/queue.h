#ifndef QUEUE_H
#define QUEUE_H

#include "process.h"
#include <stdbool.h>

typedef struct ProcessNode {
    Process *process;
    struct ProcessNode *next;
} ProcessNode;

// Definimos o tipo de fila que queremos para adicionar invariantes
typedef enum { QUEUE_FUTURE, QUEUE_READY, QUEUE_BLOCKED } QueueType;

typedef struct ProcessQueue {
    ProcessNode *head;
    bool (*comparator)(const Process *a, const Process *b);
    QueueType type;
} ProcessQueue;

ProcessQueue* queue_create(QueueType type, bool (*comparator)(const Process *a, const Process *b));
void queue_destroy(ProcessQueue *q);
bool queue_insert(ProcessQueue *q, Process *p); // returns false if duplicate PID or invalid state
Process* queue_pop(ProcessQueue *q);
Process* queue_peek(ProcessQueue *q);
bool queue_is_empty(ProcessQueue *q);
bool queue_contains_pid(ProcessQueue *q, int pid);

// Standard comparators
bool compare_fcfs(const Process *a, const Process *b);
bool compare_priority(const Process *a, const Process *b);
bool compare_io_finish(const Process *a, const Process *b);
bool compare_arrival(const Process *a, const Process *b);

#endif // QUEUE_H
