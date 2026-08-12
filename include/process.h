#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { PROCESS_NEW, PROCESS_READY, PROCESS_RUNNING, PROCESS_BLOCKED, PROCESS_FINISHED } ProcessState;

typedef struct Burst {
    int64_t cpu_time;
    int64_t io_time;       // 0 se for a última rajada
    struct Burst *next;
} Burst;

typedef struct Process {
    int pid;
    int64_t arrival_time;
    int priority;
    Burst *bursts;           // lista ligada de rajadas
    Burst *current_burst;    // posição atual
    int64_t remaining_cpu;       // tempo restante na rajada atual
    int64_t io_finish_time;      // quando a E/S termina
    ProcessState state;
    // Acumuladores para métricas
    int64_t total_cpu_original;
    int64_t total_io_original;
    int64_t start_time;
    int64_t finish_time;
    
    int64_t ready_since;         // instante de entrada atual na fila de prontos
} Process;

Process* process_create(int pid, int64_t arrival_time, int priority);
void process_destroy(Process *p);
bool process_add_burst(Process *p, int64_t cpu_time, int64_t io_time);

#endif // PROCESS_H
