#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { PROCESS_NEW, PROCESS_READY, PROCESS_RUNNING, PROCESS_BLOCKED, PROCESS_FINISHED } ProcessState;

typedef struct Burst {
    int cpu_time;
    int io_time;       // 0 se for a última rajada
    struct Burst *next;
} Burst;

typedef struct Process {
    int pid;
    int arrival_time;
    int priority;
    Burst *bursts;           // lista ligada de rajadas
    Burst *current_burst;    // posição atual
    int remaining_cpu;       // tempo restante na rajada atual
    int io_finish_time;      // quando a E/S termina
    ProcessState state;
    // Acumuladores para métricas
    int total_cpu_original;
    int total_io_original;
    int start_time;          // para turnaround
    int finish_time;         // para turnaround
    
    int ready_since;         // instante de entrada atual na fila de prontos
} Process;

Process* process_create(int pid, int arrival_time, int priority);
void process_destroy(Process *p);
bool process_add_burst(Process *p, int cpu_time, int io_time);

#endif // PROCESS_H
