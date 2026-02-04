/*
 * User-Space Scheduler inspired by Linux CFS, enhanced with simple heuristics
 *
 * Build:
 *   gcc -o cfs_scheduler cfs_scheduler.c -lm -Wall -Wextra
 *
 * Purpose:
 *   Demonstrates how CFS ideas (vruntime, weights) can be approximated
 *   in user space using fork(), SIGSTOP/SIGCONT, and timing heuristics.
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

#define MAX_PROCESSES 10
#define TIME_QUANTUM_MS 10
#define MIN_GRANULARITY_MS 5
#define SCHEDULER_TICK_US 1000
#define CFS_WEIGHT_NICE_0 1024
#define MAX_WAIT_THRESHOLD_MS 100
#define INTERACTIVE_THRESHOLD_MS 50

/* Possible lifecycle states of a task */
typedef enum {
    PROC_READY,
    PROC_RUNNING,
    PROC_STOPPED,
    PROC_COMPLETED,
    PROC_WAITING_ARRIVAL
} proc_state_t;

/*
 * process_t
 * ----------
 * Represents one scheduled task.
 *
 * Sections:
 *
 * 1. Identity & timing
 *    - pid              : Actual Linux PID (from fork)
 *    - task_id          : Logical ID used by our scheduler
 *    - arrival_time_ms  : When the task becomes eligible
 *    - burst_time_ms    : Total CPU time required
 *    - remaining_time_ms: CPU time still needed
 *
 * 2. CFS-related fields
 *    - vruntime_ns : Virtual runtime (lower = higher priority)
 *    - weight      : Derived from nice value
 *    - nice_value  : User-friendly priority indicator
 *
 * 3. Heuristic extensions
 *    - aging_boost        : Priority boost for long wait times
 *    - estimated_burst_ms : Guess of next CPU burst length
 *    - interactivity_score: Higher means more interactive
 *
 * 4. Accounting & state
 *    - wait_time_ms, finish_time_ms, response_time_ms
 *    - state             : Current execution state
 */
typedef struct {
    pid_t pid;
    int task_id;
    int arrival_time_ms;
    int burst_time_ms;
    int remaining_time_ms;
    
    unsigned long vruntime_ns;
    int weight;
    int nice_value;
    
    long start_time_ms;
    long finish_time_ms;
    long wait_time_ms;
    long response_time_ms;
    int first_run;
    
    long last_schedule_time_ms;
    long total_wait_time_ms;
    int estimated_burst_ms;
    int interactivity_score;
    int aging_boost;
    
    proc_state_t state;
    int time_slice_remaining_ms;
} process_t;

/*
 * scheduler_t
 * -----------
 * Global scheduler state shared across all tasks.
 */
typedef struct {
    process_t processes[MAX_PROCESSES];
    int num_processes;
    int current_process_idx;
    unsigned long min_vruntime_ns;
    long scheduler_start_time_ms;
    long current_time_ms;
    int completed_count;
} scheduler_t;

scheduler_t scheduler;

/* Utility helpers */
long get_time_ms(void);
void stop_process(pid_t pid);
void continue_process(pid_t pid);
void child_worker(int task_id, int burst_time_ms);

/*
 * Returns monotonic time in milliseconds.
 * CLOCK_MONOTONIC is used to avoid issues with system time changes.
 */
long get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);
}

/* Suspend a process using SIGSTOP */
void stop_process(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGSTOP);
        usleep(100);
    }
}

/* Resume a stopped process using SIGCONT */
void continue_process(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGCONT);
        usleep(100);
    }
}

/*
 * Child workload:
 * Busy-spins for the requested duration to simulate CPU usage.
 * This intentionally wastes CPU to make scheduling effects visible.
 */
void child_worker(int task_id, int burst_time_ms) {
    (void)task_id;
    long start = get_time_ms();
    long target_end = start + burst_time_ms;
    volatile long counter = 0;
    
    while (get_time_ms() < target_end) {
        for (int i = 0; i < 10000; i++) {
            counter += i;
        }
    }
    exit(0);
}

/* Reset scheduler state */
void initialize_scheduler(void) {
    memset(&scheduler, 0, sizeof(scheduler_t));
    scheduler.current_process_idx = -1;
    scheduler.min_vruntime_ns = 0;
    scheduler.scheduler_start_time_ms = get_time_ms();
}

/*
 * Converts Linux nice values to CFS weights.
 * Lower nice → higher weight → more CPU share.
 */
int nice_to_weight(int nice) {
    static const int weights[] = {
        88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
        9548, 7620, 6100, 4904, 3906, 3121, 2501, 1991, 1586, 1277,
        1024, 820, 655, 526, 423, 335, 272, 215, 172, 137,
        110, 87, 70, 56, 45, 36, 29, 23
    };
    int idx = nice + 20;
    if (idx < 0) idx = 0;
    if (idx > 39) idx = 39;
    return weights[idx];
}

/*
 * Computes heuristic adjustments:
 * - Aging boost to avoid starvation
 * - Burst estimation to detect interactive tasks
 * - Interactivity score for responsiveness
 */
void compute_heuristic_metrics(process_t *proc, long current_time) {
    if (proc->state == PROC_READY || proc->state == PROC_STOPPED) {
        long wait_delta = current_time - proc->last_schedule_time_ms;
        if (wait_delta > 0) {
            proc->total_wait_time_ms += wait_delta;
        }
    }
    
    if (proc->total_wait_time_ms > MAX_WAIT_THRESHOLD_MS) {
        proc->aging_boost = (proc->total_wait_time_ms - MAX_WAIT_THRESHOLD_MS) / 10;
        if (proc->aging_boost > 10) proc->aging_boost = 10;
    } else {
        proc->aging_boost = 0;
    }
    
    if (proc->estimated_burst_ms == 0) {
        proc->estimated_burst_ms = proc->remaining_time_ms / 4;
        if (proc->estimated_burst_ms < TIME_QUANTUM_MS) {
            proc->estimated_burst_ms = TIME_QUANTUM_MS;
        }
    }
    
    if (proc->burst_time_ms > 0) {
        proc->interactivity_score =
            (proc->remaining_time_ms * 100) / proc->burst_time_ms;
        if (proc->estimated_burst_ms < INTERACTIVE_THRESHOLD_MS) {
            proc->interactivity_score += 20;
        }
    }
    
    proc->last_schedule_time_ms = current_time;
}

/*
 * Updates virtual runtime using the standard CFS formula:
 *
 *   vruntime += (actual_runtime × 1024) / weight
 */
void update_vruntime(process_t *proc, long executed_time_ms) {
    unsigned long executed_ns = executed_time_ms * 1000000UL;
    unsigned long delta_vruntime =
        (executed_ns * CFS_WEIGHT_NICE_0) / proc->weight;
    proc->vruntime_ns += delta_vruntime;
    
    if (proc->vruntime_ns < scheduler.min_vruntime_ns ||
        scheduler.min_vruntime_ns == 0) {
        scheduler.min_vruntime_ns = proc->vruntime_ns;
    }
}

/*
 * Selects the next task to run.
 *
 * Base metric:
 *   - Lowest vruntime wins (CFS principle)
 *
 * Heuristic tweaks:
 *   - Aging boost for long-waiting tasks
 *   - Bonus for short / interactive bursts
 *   - Small penalty for very long jobs
 */
int select_next_process_cfs_heuristic(void) {
    int best_idx = -1;
    long long best_score = LLONG_MAX;
    long current_time = get_time_ms();
    
    for (int i = 0; i < scheduler.num_processes; i++) {
        process_t *proc = &scheduler.processes[i];
        
        if (proc->state != PROC_READY &&
            proc->state != PROC_STOPPED) {
            continue;
        }
        
        long elapsed =
            current_time - scheduler.scheduler_start_time_ms;
        if (elapsed < proc->arrival_time_ms) {
            continue;
        }
        
        compute_heuristic_metrics(proc, current_time);
        
        long long score = proc->vruntime_ns;
        score -= (proc->aging_boost * 100000000LL);
        
        if (proc->estimated_burst_ms < INTERACTIVE_THRESHOLD_MS) {
            score -= 50000000LL;
        }
        
        if (proc->remaining_time_ms > 100) {
            score += 10000000LL;
        }
        
        if (score < best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    
    return best_idx;
}

/*
 * Main scheduling loop.
 *
 * Repeatedly:
 *   - Pick best runnable task
 *   - Context switch using SIGSTOP/SIGCONT
 *   - Let it run for a computed time slice
 *   - Update vruntime and accounting
 *   - Detect completion
 */
void schedule_processes(void) {
    printf("\n=== CFS + Heuristic Scheduler Started ===\n\n");
    
    while (scheduler.completed_count < scheduler.num_processes) {
        long current_time = get_time_ms();
        scheduler.current_time_ms = current_time;
        
        int next_idx = select_next_process_cfs_heuristic();
        
        if (next_idx == -1) {
            usleep(SCHEDULER_TICK_US);
            continue;
        }
        
        process_t *proc = &scheduler.processes[next_idx];
        
        long elapsed =
            current_time - scheduler.scheduler_start_time_ms;
        if (elapsed < proc->arrival_time_ms) {
            usleep(SCHEDULER_TICK_US);
            continue;
        }
        
        if (scheduler.current_process_idx != -1 &&
            scheduler.current_process_idx != next_idx) {
            process_t *prev =
                &scheduler.processes[scheduler.current_process_idx];
            if (prev->state == PROC_RUNNING) {
                stop_process(prev->pid);
                prev->state = PROC_STOPPED;
            }
        }
        
        if (proc->state == PROC_READY ||
            proc->state == PROC_STOPPED) {
            if (proc->first_run == 0) {
                proc->first_run = 1;
                proc->response_time_ms =
                    current_time -
                    scheduler.scheduler_start_time_ms -
                    proc->arrival_time_ms;
                proc->start_time_ms = current_time;
            }
            
            continue_process(proc->pid);
            proc->state = PROC_RUNNING;
            scheduler.current_process_idx = next_idx;
            
            int time_slice =
                (TIME_QUANTUM_MS * CFS_WEIGHT_NICE_0) / proc->weight;
            if (time_slice < MIN_GRANULARITY_MS) {
                time_slice = MIN_GRANULARITY_MS;
            }
            proc->time_slice_remaining_ms = time_slice;
            
            printf("[%4ld ms] P%d running "
                   "(vruntime=%lu, remaining=%d ms)\n",
                   elapsed, proc->task_id,
                   proc->vruntime_ns,
                   proc->remaining_time_ms);
        }
        
        long exec_start = get_time_ms();
        usleep(proc->time_slice_remaining_ms * 1000);
        long exec_end = get_time_ms();
        long executed_time = exec_end - exec_start;
        
        proc->remaining_time_ms -= executed_time;
        if (proc->remaining_time_ms <= 0) {
            proc->remaining_time_ms = 0;
        }
        
        update_vruntime(proc, executed_time);
        
        int status;
        pid_t result =
            waitpid(proc->pid, &status, WNOHANG);
        
        if (result == proc->pid ||
            proc->remaining_time_ms == 0) {
            proc->state = PROC_COMPLETED;
            proc->finish_time_ms = get_time_ms();
            scheduler.completed_count++;
            
            long turnaround =
                proc->finish_time_ms -
                scheduler.scheduler_start_time_ms -
                proc->arrival_time_ms;
            proc->wait_time_ms =
                turnaround - proc->burst_time_ms;
            
            printf("[%4ld ms] P%d completed "
                   "(wait=%ld ms, turnaround=%ld ms)\n",
                   get_time_ms() -
                   scheduler.scheduler_start_time_ms,
                   proc->task_id,
                   proc->wait_time_ms,
                   turnaround);
        } else {
            stop_process(proc->pid);
            proc->state = PROC_STOPPED;
        }
    }
    
    printf("\n=== All processes completed ===\n");
}

/* Prints per-task and average scheduling statistics */
void print_results(void) {
    long total_wait = 0;
    long total_turnaround = 0;
    
    printf("\n--- FINAL STATISTICS ---\n");
    printf("Task | Wait(ms) | Turnaround(ms) | vruntime(ns) | Aging\n");
    printf("-----|----------|----------------|--------------|------\n");
    
    for (int i = 0; i < scheduler.num_processes; i++) {
        process_t *proc = &scheduler.processes[i];
        long turnaround =
            proc->finish_time_ms -
            scheduler.scheduler_start_time_ms -
            proc->arrival_time_ms;
        long wait = turnaround - proc->burst_time_ms;
        
        total_wait += wait;
        total_turnaround += turnaround;
        
        printf("P%-3d | %8ld | %14ld | %12lu | %4d\n",
               proc->task_id,
               wait,
               turnaround,
               proc->vruntime_ns,
               proc->aging_boost);
    }
    
    printf("\nAverage Wait Time: %.2f ms\n",
           (double)total_wait / scheduler.num_processes);
    printf("Average Turnaround: %.2f ms\n",
           (double)total_turnaround / scheduler.num_processes);
}

int main(void) {
    printf("CFS-Inspired Scheduler with Heuristic AI\n");
    printf("=========================================\n");
    
    initialize_scheduler();
    
    /* Synthetic workload definition */
    struct {
        int arrival_ms;
        int burst_ms;
        int nice;
    } workload[] = {
        {0,   60,  0},
        {10,  20, -5},
        {15,  80,  5},
        {20,  30,  0},
        {30,  15, -10},
        {35,  50,  0},
    };
    
    int num_tasks =
        sizeof(workload) / sizeof(workload[0]);
    scheduler.num_processes = num_tasks;
    
    printf("\nInitial Process Configuration:\n");
    printf("Task | Arrival | Burst | Nice | Weight\n");
    printf("-----|---------|-------|------|-------\n");
    
    for (int i = 0; i < num_tasks; i++) {
        process_t *proc = &scheduler.processes[i];
        
        proc->task_id = i;
        proc->arrival_time_ms = workload[i].arrival_ms;
        proc->burst_time_ms = workload[i].burst_ms;
        proc->remaining_time_ms = workload[i].burst_ms;
        proc->nice_value = workload[i].nice;
        proc->weight = nice_to_weight(workload[i].nice);
        proc->vruntime_ns = scheduler.min_vruntime_ns;
        proc->state = PROC_READY;
        proc->first_run = 0;
        proc->estimated_burst_ms = 0;
        proc->aging_boost = 0;
        proc->interactivity_score = 100;
        proc->last_schedule_time_ms =
            scheduler.scheduler_start_time_ms;
        
        printf("P%-3d | %7d | %5d | %4d | %5d\n",
               i,
               workload[i].arrival_ms,
               workload[i].burst_ms,
               workload[i].nice,
               proc->weight);
        
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        } else if (pid == 0) {
            child_worker(i, workload[i].burst_ms);
            exit(0);
        } else {
            proc->pid = pid;
            usleep(1000);
            stop_process(pid);
        }
    }
    
    schedule_processes();
    
    for (int i = 0; i < scheduler.num_processes; i++) {
        int status;
        waitpid(scheduler.processes[i].pid, &status, 0);
    }
    
    print_results();
    
    printf("\nKey Concepts:\n");
    printf("• vruntime: core fairness metric from CFS\n");
    printf("• Aging: prevents starvation of long-waiting tasks\n");
    printf("• Weight: derived from nice value, controls CPU share\n");
    printf("• Kernel still schedules threads; this coordinates them\n");
    
    return 0;
}
