#include "workload.h"
#include "rng.h"
#include "process.h"
#include <stdio.h>
#include <string.h>

static int get_scenario_id(const char *scenario) {
    if (strcmp(scenario, "equilibrado") == 0) return 1;
    if (strcmp(scenario, "io_bound") == 0) return 2;
    if (strcmp(scenario, "cpu_bound") == 0) return 3;
    if (strcmp(scenario, "prioridades_desbalanceadas") == 0) return 4;
    return 1; 
}

ProcessQueue* workload_generate(int process_count, const char *scenario, uint64_t seed) {
    ProcessQueue *q = queue_create(QUEUE_FUTURE, compare_arrival);
    if (!q) return NULL;

    int scenario_id = get_scenario_id(scenario);
    
    rng_init(seed, (uint64_t)scenario_id);
    
    int arrival = 0;

    for (int pid = 1; pid <= process_count; pid++) {
        if (pid == 1) {
            arrival = 0;
        } else {
            arrival += rng_next_range(0, 3);
        }

        int priority = 0;
        if (scenario_id == 4) {
            if (rng_next_range(0, 99) < 80) {
                priority = rng_next_range(0, 2);
            } else {
                priority = rng_next_range(7, 9);
            }
        } else {
            priority = rng_next_range(0, 9);
        }

        int is_pouca_es = 0;
        int is_curto_cpu = 0;
        if (scenario_id == 1 || scenario_id == 4) {
            is_pouca_es = rng_next_range(0, 1);
            is_curto_cpu = rng_next_range(0, 1);
        }


        int num_bursts = 1;
        if (scenario_id == 2) { 
            num_bursts = rng_next_range(5, 8);
        } else if (scenario_id == 3) { 
            num_bursts = rng_next_range(1, 3);
        } else { 
            if (is_pouca_es) num_bursts = rng_next_range(1, 2);
            else num_bursts = rng_next_range(4, 7);
        }

        Process *p = process_create(pid, arrival, priority);

        for(int i = 0; i < num_bursts; i++) {
            int cpu = 1, io = 0;

            if (scenario_id == 2) {
                cpu = rng_next_range(1, 5);
            } else if (scenario_id == 3) {
                cpu = rng_next_range(20, 60);
            } else {
                if (is_curto_cpu) cpu = rng_next_range(1, 6);
                else cpu = rng_next_range(15, 40);
            }

            if (i < num_bursts - 1) {
                if (scenario_id == 2) {
                    io = rng_next_range(10, 30);
                } else if (scenario_id == 3) {
                    io = rng_next_range(5, 15);
                } else {
                    io = rng_next_range(5, 20);
                }
            }

            process_add_burst(p, cpu, io);
        }

        queue_insert(q, p);
    }

    return q;
}

int workload_export_csv(ProcessQueue *q, const char *filename) {
    if (!q || !filename) return 0;
    
    FILE *f = fopen(filename, "w");
    if (!f) return 0;
    
    fprintf(f, "pid,arrival,priority,burst_index,burst_type,duration\n");
    
    ProcessNode *curr = q->head;
    while(curr) {
        Process *p = curr->process;
        Burst *b = p->bursts;
        int burst_idx = 1;
        
        while(b) {
            fprintf(f, "%d,%d,%d,%d,CPU,%d\n", p->pid, p->arrival_time, p->priority, burst_idx++, b->cpu_time);
            
            if (b->io_time > 0) {
                fprintf(f, "%d,%d,%d,%d,IO,%d\n", p->pid, p->arrival_time, p->priority, burst_idx++, b->io_time);
            }
            b = b->next;
        }
        curr = curr->next;
    }
    
    fclose(f);
    return 1;
}