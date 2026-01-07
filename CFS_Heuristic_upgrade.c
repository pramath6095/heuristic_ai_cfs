/*
 * CFS-Inspired User-Space Scheduler with Heuristic Enhancements
 * 
 * This is a user-space implementation that demonstrates CFS concepts using
 * real Linux processes. It does NOT replace the kernel scheduler but instead
 * coordinates process execution using POSIX signals.
 * 
 * Author: Senior Linux Kernel Engineer
 * Target: Ubuntu Linux with GCC
 * Compilation: gcc -o cfs_scheduler cfs_scheduler.c -lm -Wall -Wextra
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <sys/time.h>

/* Configuration constants */
#define MAX_PROCESSES 10
#define TIME_QUANTUM_MS 10          /* Base time quantum in milliseconds */
#define MIN_GRANULARITY_MS 5        /* Minimum granularity (CFS concept) */
#define SCHEDULER_TICK_US 1000      /* Scheduler tick interval in microseconds */
#define CFS_WEIGHT_NICE_0 1024      /* Base weight for nice value 0 */
#define MAX_WAIT_THRESHOLD_MS 100   /* Threshold for aging boost */
#define INTERACTIVE_THRESHOLD_MS 50 /* Threshold for interactivity detection */

/* Process states */
typedef enum {
    PROC_READY,
    PROC_RUNNING,
    PROC_STOPPED,
    PROC_COMPLETED,
    PROC_WAITING_ARRIVAL
} proc_state_t;

/* Process control block */
typedef struct {
    pid_t pid;                    /* Real Linux process ID */
    int task_id;                  /* Our logical task ID */
    int arrival_time_ms;          /* Arrival time (simulated) */
    int burst_time_ms;            /* Total CPU burst needed */
    int remaining_time_ms;        /* Remaining execution time */
    
    /* CFS-related fields */
    unsigned long vruntime_ns;    /* Virtual runtime in nanoseconds */
    int weight;                   /* Scheduling weight (CFS concept) */
    int nice_value;               /* Nice value (-20 to 19) */
    
    /* Timing statistics */
    long start_time_ms;           /* Actual start time (clock) */
    long finish_time_ms;          /* Actual finish time */
    long wait_time_ms;            /* Total waiting time */
    long response_time_ms;        /* Time to first execution */
    int first_run;                /* Flag for first execution */
    
    /* Heuristic AI fields */
    long last_schedule_time_ms;   /* Last time scheduled */
    long total_wait_time_ms;      /* Accumulated waiting time */
    int estimated_burst_ms;       /* Estimated next CPU burst */
    int interactivity_score;      /* Heuristic interactivity metric */
    int aging_boost;              /* Aging priority boost */
    
    /* State management */
    proc_state_t state;
    int time_slice_remaining_ms;  /* Current time slice remaining */
} process_t;

/* Scheduler state */
typedef struct {
    process_t processes[MAX_PROCESSES];
    int num_processes;
    int current_process_idx;
    unsigned long min_vruntime_ns; /* CFS min_vruntime concept */
    long scheduler_start_time_ms;
    long current_time_ms;
    int completed_count;
} scheduler_t;

scheduler_t scheduler;

/* Forward declarations */
void child_worker(int task_id, int burst_time_ms);
long get_time_ms(void);
void stop_process(pid_t pid);
void continue_process(pid_t pid);
void initialize_scheduler(void);
void compute_heuristic_metrics(process_t *proc, long current_time);
int select_next_process_cfs_heuristic(void);
void update_vruntime(process_t *proc, long executed_time_ms);
void schedule_processes(void);
void print_process_table(void);
void print_scheduling_trace(void);
void print_final_statistics(void);

/*
 * Get current time in milliseconds using monotonic clock
 */
long get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);
}

/*
 * Stop a process using SIGSTOP
 */
void stop_process(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGSTOP);
        /* Small delay to ensure signal is processed */
        usleep(100);
    }
}

/*
 * Resume a process using SIGCONT
 */
void continue_process(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGCONT);
        usleep(100);
    }
}

/*
 * Child process worker - performs CPU-bound work
 * Uses busy-wait loops instead of sleep to simulate actual CPU work
 */
void child_worker(int task_id, int burst_time_ms) {
    long start = get_time_ms();
    long target_end = start + burst_time_ms;
    volatile long counter = 0;
    
    /* Busy-wait to simulate CPU-bound work */
    while (get_time_ms() < target_end) {
        /* Perform some work to consume CPU */
        for (int i = 0; i < 10000; i++) {
            counter += i;
        }
    }
    
    /* Exit cleanly */
    exit(0);
}

/*
 * Initialize scheduler state
 */
void initialize_scheduler(void) {
    memset(&scheduler, 0, sizeof(scheduler_t));
    scheduler.current_process_idx = -1;
    scheduler.min_vruntime_ns = 0;
    scheduler.scheduler_start_time_ms = get_time_ms();
}

/*
 * Compute nice value to weight conversion (CFS formula)
 * weight = 1024 / (1.25 ^ nice)
 */
int nice_to_weight(int nice) {
    /* Simplified CFS weight table */
    static const int weights[] = {
        /* Nice -20 to -10 */
        88761, 71755, 56483, 46273, 36291,
        29154, 23254, 18705, 14949, 11916,
        /* Nice -9 to +1 */
        9548, 7620, 6100, 4904, 3906,
        3121, 2501, 1991, 1586, 1277,
        /* Nice +2 to +11 */
        1024, 820, 655, 526, 423,
        335, 272, 215, 172, 137,
        /* Nice +12 to +19 */
        110, 87, 70, 56, 45, 36, 29, 23
    };
    
    int idx = nice + 20; /* Map -20..19 to 0..39 */
    if (idx < 0) idx = 0;
    if (idx > 39) idx = 39;
    
    return weights[idx];
}

/*
 * Heuristic AI Layer: Compute dynamic metrics for intelligent scheduling
 * 
 * This function implements deterministic heuristics (not ML) to enhance
 * fairness and responsiveness:
 * 1. Aging boost: Increase priority for long-waiting processes
 * 2. Interactivity detection: Favor processes with shorter bursts
 * 3. Burst estimation: Predict next CPU burst length
 */
void compute_heuristic_metrics(process_t *proc, long current_time) {
    /* Update total waiting time */
    if (proc->state == PROC_READY || proc->state == PROC_STOPPED) {
        long wait_delta = current_time - proc->last_schedule_time_ms;
        if (wait_delta > 0) {
            proc->total_wait_time_ms += wait_delta;
        }
    }
    
    /* Heuristic 1: Aging boost to prevent starvation */
    if (proc->total_wait_time_ms > MAX_WAIT_THRESHOLD_MS) {
        /* Linear aging: boost increases with wait time */
        proc->aging_boost = (proc->total_wait_time_ms - MAX_WAIT_THRESHOLD_MS) / 10;
        if (proc->aging_boost > 10) proc->aging_boost = 10; /* Cap boost */
    } else {
        proc->aging_boost = 0;
    }
    
    /* Heuristic 2: Estimate next CPU burst (exponential moving average) */
    if (proc->estimated_burst_ms == 0) {
        /* Initial estimate based on remaining time */
        proc->estimated_burst_ms = proc->remaining_time_ms / 4;
        if (proc->estimated_burst_ms < TIME_QUANTUM_MS) {
            proc->estimated_burst_ms = TIME_QUANTUM_MS;
        }
    }
    
    /* Heuristic 3: Interactivity score (favor shorter, more responsive tasks) */
    /* Score = (remaining_time / total_burst) * 100 */
    /* Lower score = more complete = less interactive */
    if (proc->burst_time_ms > 0) {
        proc->interactivity_score = 
            (proc->remaining_time_ms * 100) / proc->burst_time_ms;
        
        /* Boost interactive processes (short bursts) */
        if (proc->estimated_burst_ms < INTERACTIVE_THRESHOLD_MS) {
            proc->interactivity_score += 20;
        }
    }
    
    proc->last_schedule_time_ms = current_time;
}

/*
 * Update virtual runtime (vruntime) for a process
 * This is the core CFS concept: vruntime = physical_time * (weight_0 / weight)
 */
void update_vruntime(process_t *proc, long executed_time_ms) {
    /* Convert ms to ns for precision */
    unsigned long executed_ns = executed_time_ms * 1000000UL;
    
    /* CFS formula: vruntime += (executed_time * NICE_0_WEIGHT) / weight */
    unsigned long delta_vruntime = 
        (executed_ns * CFS_WEIGHT_NICE_0) / proc->weight;
    
    proc->vruntime_ns += delta_vruntime;
    
    /* Update global min_vruntime */
    if (proc->vruntime_ns < scheduler.min_vruntime_ns || 
        scheduler.min_vruntime_ns == 0) {
        scheduler.min_vruntime_ns = proc->vruntime_ns;
    }
}

/*
 * CFS + Heuristic Selection Algorithm
 * 
 * This combines CFS's vruntime-based fairness with heuristic enhancements:
 * 1. Start with CFS: select process with minimum vruntime
 * 2. Apply aging boost: reduce effective vruntime for long-waiting processes
 * 3. Apply interactivity boost: slightly favor interactive tasks
 * 4. Break ties deterministically
 */
int select_next_process_cfs_heuristic(void) {
    int best_idx = -1;
    long long best_score = LLONG_MAX; /* Lower score = higher priority */
    long current_time = get_time_ms();
    
    for (int i = 0; i < scheduler.num_processes; i++) {
        process_t *proc = &scheduler.processes[i];
        
        /* Skip if not ready or not yet arrived */
        if (proc->state != PROC_READY && proc->state != PROC_STOPPED) {
            continue;
        }
        
        /* Check arrival time */
        long elapsed = current_time - scheduler.scheduler_start_time_ms;
        if (elapsed < proc->arrival_time_ms) {
            continue; /* Not yet arrived */
        }
        
        /* Update heuristic metrics */
        compute_heuristic_metrics(proc, current_time);
        
        /* Compute selection score (lower is better) */
        long long score = proc->vruntime_ns;
        
        /* Apply aging boost: reduce score for long-waiting processes */
        score -= (proc->aging_boost * 100000000LL); /* 100ms per boost level */
        
        /* Apply interactivity boost: slightly reduce score for interactive tasks */
        if (proc->estimated_burst_ms < INTERACTIVE_THRESHOLD_MS) {
            score -= 50000000LL; /* 50ms bonus */
        }
        
        /* Penalize very long processes slightly to promote fairness */
        if (proc->remaining_time_ms > 100) {
            score += 10000000LL; /* 10ms penalty */
        }
        
        /* Select process with lowest score */
        if (score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    
    return best_idx;
}

/*
 * Main scheduling loop
 * Coordinates process execution using signals and CFS+heuristic algorithm
 */
void schedule_processes(void) {
    printf("\n=== Starting CFS + Heuristic Scheduler ===\n\n");
    
    while (scheduler.completed_count < scheduler.num_processes) {
        long current_time = get_time_ms();
        scheduler.current_time_ms = current_time;
        
        /* Select next process using CFS + Heuristic algorithm */
        int next_idx = select_next_process_cfs_heuristic();
        
        if (next_idx == -1) {
            /* No process ready - CPU idle or waiting for arrivals */
            usleep(SCHEDULER_TICK_US);
            continue;
        }
        
        process_t *proc = &scheduler.processes[next_idx];
        
        /* Check if process has arrived */
        long elapsed = current_time - scheduler.scheduler_start_time_ms;
        if (elapsed < proc->arrival_time_ms) {
            /* Wait for next arrival */
            usleep(SCHEDULER_TICK_US);
            continue;
        }
        
        /* Context switch: stop current, start next */
        if (scheduler.current_process_idx != -1 && 
            scheduler.current_process_idx != next_idx) {
            process_t *prev = &scheduler.processes[scheduler.current_process_idx];
            if (prev->state == PROC_RUNNING) {
                stop_process(prev->pid);
                prev->state = PROC_STOPPED;
            }
        }
        
        /* Start/resume selected process */
        if (proc->state == PROC_READY || proc->state == PROC_STOPPED) {
            /* Record first run time for response time calculation */
            if (proc->first_run == 0) {
                proc->first_run = 1;
                proc->response_time_ms = current_time - scheduler.scheduler_start_time_ms - proc->arrival_time_ms;
                proc->start_time_ms = current_time;
            }
            
            continue_process(proc->pid);
            proc->state = PROC_RUNNING;
            scheduler.current_process_idx = next_idx;
            
            /* Calculate time slice based on weight (CFS-like) */
            int time_slice = (TIME_QUANTUM_MS * CFS_WEIGHT_NICE_0) / proc->weight;
            if (time_slice < MIN_GRANULARITY_MS) {
                time_slice = MIN_GRANULARITY_MS;
            }
            
            proc->time_slice_remaining_ms = time_slice;
            
            printf("[T=%4ld ms] Scheduled P%d (PID=%d) | vruntime=%lu ns | remaining=%d ms | aging=%d\n",
                   elapsed, proc->task_id, proc->pid, 
                   proc->vruntime_ns, proc->remaining_time_ms, proc->aging_boost);
        }
        
        /* Let process run for its time slice */
        long exec_start = get_time_ms();
        usleep(proc->time_slice_remaining_ms * 1000);
        long exec_end = get_time_ms();
        long executed_time = exec_end - exec_start;
        
        /* Update process state */
        proc->remaining_time_ms -= executed_time;
        if (proc->remaining_time_ms <= 0) {
            proc->remaining_time_ms = 0;
        }
        
        /* Update vruntime (CFS core concept) */
        update_vruntime(proc, executed_time);
        
        /* Check if process completed */
        int status;
        pid_t result = waitpid(proc->pid, &status, WNOHANG);
        
        if (result == proc->pid || proc->remaining_time_ms == 0) {
            /* Process completed */
            proc->state = PROC_COMPLETED;
            proc->finish_time_ms = get_time_ms();
            scheduler.completed_count++;
            
            long turnaround = proc->finish_time_ms - scheduler.scheduler_start_time_ms - proc->arrival_time_ms;
            proc->wait_time_ms = turnaround - proc->burst_time_ms;
            
            printf("[T=%4ld ms] Completed P%d | turnaround=%ld ms | wait=%ld ms | vruntime=%lu ns\n",
                   get_time_ms() - scheduler.scheduler_start_time_ms,
                   proc->task_id, turnaround, proc->wait_time_ms, proc->vruntime_ns);
        } else {
            /* Process preempted - will be rescheduled */
            stop_process(proc->pid);
            proc->state = PROC_STOPPED;
        }
    }
    
    printf("\n=== All processes completed ===\n");
}

/*
 * Print initial process table
 */
void print_process_table(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║              PROCESS TABLE - INITIAL CONFIGURATION                 ║\n");
    printf("╠════════╦═══════╦═══════════╦════════════╦══════════╦══════════════╣\n");
    printf("║ Task   ║  PID  ║  Arrival  ║   Burst    ║  Nice    ║    Weight    ║\n");
    printf("║   ID   ║       ║   (ms)    ║    (ms)    ║  Value   ║     (CFS)    ║\n");
    printf("╠════════╬═══════╬═══════════╬════════════╬══════════╬══════════════╣\n");
    
    for (int i = 0; i < scheduler.num_processes; i++) {
        process_t *proc = &scheduler.processes[i];
        printf("║   P%-2d  ║ %5d ║    %4d   ║    %4d    ║   %3d    ║     %4d     ║\n",
               proc->task_id, proc->pid, proc->arrival_time_ms,
               proc->burst_time_ms, proc->nice_value, proc->weight);
    }
    
    printf("╚════════╩═══════╩═══════════╩════════════╩══════════╩══════════════╝\n");
}

/*
 * Print scheduling trace showing vruntime evolution
 */
void print_scheduling_trace(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║                    SCHEDULING TRACE (VRUNTIME)                     ║\n");
    printf("╠════════╦═══════════════╦════════════════╦═════════════════════════╣\n");
    printf("║ Task   ║   Response    ║   Virtual      ║   Interactivity Score   ║\n");
    printf("║   ID   ║   Time (ms)   ║   Runtime (ns) ║   (Heuristic)           ║\n");
    printf("╠════════╬═══════════════╬════════════════╬═════════════════════════╣\n");
    
    for (int i = 0; i < scheduler.num_processes; i++) {
        process_t *proc = &scheduler.processes[i];
        printf("║   P%-2d  ║      %4ld     ║   %10lu   ║          %3d            ║\n",
               proc->task_id, proc->response_time_ms, 
               proc->vruntime_ns, proc->interactivity_score);
    }
    
    printf("╚════════╩═══════════════╩════════════════╩═════════════════════════╝\n");
}

/*
 * Print final statistics
 */
void print_final_statistics(void) {
    long total_wait = 0;
    long total_turnaround = 0;
    long max_wait = 0;
    long min_wait = LONG_MAX;
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║                   FINAL SCHEDULING STATISTICS                      ║\n");
    printf("╠════════╦═══════════════╦═══════════════╦════════════════╦═════════╣\n");
    printf("║ Task   ║   Wait Time   ║  Turnaround   ║   Virtual      ║  Aging  ║\n");
    printf("║   ID   ║     (ms)      ║   Time (ms)   ║   Runtime (ns) ║  Boost  ║\n");
    printf("╠════════╬═══════════════╬═══════════════╬════════════════╬═════════╣\n");
    
    for (int i = 0; i < scheduler.num_processes; i++) {
        process_t *proc = &scheduler.processes[i];
        long turnaround = proc->finish_time_ms - scheduler.scheduler_start_time_ms - proc->arrival_time_ms;
        long wait = turnaround - proc->burst_time_ms;
        
        total_wait += wait;
        total_turnaround += turnaround;
        
        if (wait > max_wait) max_wait = wait;
        if (wait < min_wait) min_wait = wait;
        
        printf("║   P%-2d  ║      %4ld     ║      %4ld     ║   %10lu   ║    %2d   ║\n",
               proc->task_id, wait, turnaround, 
               proc->vruntime_ns, proc->aging_boost);
    }
    
    printf("╠════════╩═══════════════╩═══════════════╩════════════════╩═════════╣\n");
    printf("║                        AGGREGATE METRICS                           ║\n");
    printf("╠════════════════════════════════════════════════════════════════════╣\n");
    printf("║  Average Wait Time       : %8.2f ms                             ║\n",
           (double)total_wait / scheduler.num_processes);
    printf("║  Average Turnaround Time : %8.2f ms                             ║\n",
           (double)total_turnaround / scheduler.num_processes);
    printf("║  Min Wait Time           : %8ld ms                             ║\n", min_wait);
    printf("║  Max Wait Time           : %8ld ms                             ║\n", max_wait);
    printf("║  Total Processes         : %8d                                  ║\n",
           scheduler.num_processes);
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
}

/*
 * Main function: Initialize processes and run scheduler
 */
int main(void) {
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║     CFS-INSPIRED USER-SPACE SCHEDULER WITH HEURISTIC AI LAYER     ║\n");
    printf("║                                                                    ║\n");
    printf("║  This scheduler demonstrates CFS concepts using real processes    ║\n");
    printf("║  and POSIX signals. It does NOT replace the kernel scheduler.     ║\n");
    printf("║                                                                    ║\n");
    printf("║  Features:                                                         ║\n");
    printf("║  • Virtual runtime (vruntime) tracking                            ║\n");
    printf("║  • Weight-based fair scheduling                                   ║\n");
    printf("║  • Aging prevention (heuristic)                                   ║\n");
    printf("║  • Interactivity detection (heuristic)                            ║\n");
    printf("║  • Burst estimation (heuristic)                                   ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
    
    initialize_scheduler();
    
    /* Define test workload with varied arrival times and burst lengths */
    struct {
        int arrival_ms;
        int burst_ms;
        int nice;
    } workload[] = {
        {0,   60,  0},   /* P0: CPU-bound, normal priority */
        {10,  20, -5},   /* P1: Short burst, higher priority */
        {15,  80,  5},   /* P2: Long burst, lower priority */
        {20,  30,  0},   /* P3: Medium burst, normal priority */
        {30,  15, -10},  /* P4: Very short, highest priority */
        {35,  50,  0},   /* P5: Medium burst, normal priority */
    };
    
    int num_tasks = sizeof(workload) / sizeof(workload[0]);
    scheduler.num_processes = num_tasks;
    
    /* Fork child processes */
    for (int i = 0; i < num_tasks; i++) {
        process_t *proc = &scheduler.processes[i];
        
        proc->task_id = i;
        proc->arrival_time_ms = workload[i].arrival_ms;
        proc->burst_time_ms = workload[i].burst_ms;
        proc->remaining_time_ms = workload[i].burst_ms;
        proc->nice_value = workload[i].nice;
        proc->weight = nice_to_weight(workload[i].nice);
        proc->vruntime_ns = scheduler.min_vruntime_ns; /* Start at min */
        proc->state = PROC_READY;
        proc->first_run = 0;
        proc->estimated_burst_ms = 0;
        proc->aging_boost = 0;
        proc->interactivity_score = 100;
        proc->last_schedule_time_ms = scheduler.scheduler_start_time_ms;
        
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        } else if (pid == 0) {
            /* Child process */
            child_worker(i, workload[i].burst_ms);
            /* Never reached */
            exit(0);
        } else {
            /* Parent process */
            proc->pid = pid;
            /* Immediately stop child to prevent it from running */
            usleep(1000); /* Let child start */
            stop_process(pid);
        }
    }
    
    /* Print initial configuration */
    print_process_table();
    
    /* Run the scheduler */
    schedule_processes();
    
    /* Wait for all children to exit */
    for (int i = 0; i < scheduler.num_processes; i++) {
        int status;
        waitpid(scheduler.processes[i].pid, &status, 0);
    }
    
    /* Print results */
    print_scheduling_trace();
    print_final_statistics();
    
    /* Explanatory output */
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║                        SCHEDULER EXPLANATION                       ║\n");
    printf("╠════════════════════════════════════════════════════════════════════╣\n");
    printf("║  CFS CONCEPTS DEMONSTRATED:                                        ║\n");
    printf("║  • vruntime: Processes with lower vruntime are scheduled first     ║\n");
    printf("║  • Weight-based fairness: Nice values affect CPU share             ║\n");
    printf("║  • Time slicing: Based on weight and number of processes           ║\n");
    printf("║                                                                    ║\n");
    printf("║  HEURISTIC AI ENHANCEMENTS:                                        ║\n");
    printf("║  • Aging boost: Long-waiting processes get priority                ║\n");
    printf("║  • Interactivity detection: Short bursts favored                   ║\n");
    printf("║  • Burst estimation: Predicts CPU needs (not learned)              ║\n");
    printf("║                                                                    ║\n");
    printf("║  LIMITATIONS:                                                      ║\n");
    printf("║  • Kernel scheduler still performs time-slicing                    ║\n");
    printf("║  • Signal overhead reduces precision                               ║\n");
    printf("║  • User-space context switches are slower                          ║\n");
    printf("║  • Cannot preempt kernel-level operations                          ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
    
    return 0;
}