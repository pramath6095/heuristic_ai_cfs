# cpu scheduling simulation - compares FCFS, SJF, SRTF, Priority, Round Robin, and Heuristic CFS

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.animation import FuncAnimation
import numpy as np
from dataclasses import dataclass, field
from typing import List, Tuple, Optional
from copy import deepcopy
import random
import time


@dataclass
class Process:
    """Process control block"""
    pid: int
    arrival_time: int
    burst_time: int
    priority: int = 0
    nice_value: int = 0

    remaining_time: int = field(init=False)
    start_time: int = -1
    finish_time: int = -1
    waiting_time: int = 0
    turnaround_time: int = 0
    response_time: int = -1

    # cfs specific
    vruntime: float = 0.0
    weight: int = 1024
    aging_boost: int = 0
    last_scheduled: int = 0

    def __post_init__(self):
        self.remaining_time = self.burst_time
        self.weight = self._nice_to_weight(self.nice_value)

    @staticmethod
    def _nice_to_weight(nice: int) -> int:
        weights = [
            88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
            9548, 7620, 6100, 4904, 3906, 3121, 2501, 1991, 1586, 1277,
            1024, 820, 655, 526, 423, 335, 272, 215, 172, 137,
            110, 87, 70, 56, 45, 36, 29, 23, 18, 15
        ]
        idx = nice + 20
        return weights[max(0, min(39, idx))]

    def reset(self):
        self.remaining_time = self.burst_time
        self.start_time = -1
        self.finish_time = -1
        self.waiting_time = 0
        self.turnaround_time = 0
        self.response_time = -1
        self.vruntime = 0.0
        self.aging_boost = 0
        self.last_scheduled = 0


@dataclass
class GanttEntry:
    pid: int
    start: int
    end: int


@dataclass
class SchedulerResult:
    name: str
    gantt_chart: List[GanttEntry]
    processes: List[Process]
    avg_waiting_time: float
    avg_turnaround_time: float
    avg_response_time: float
    throughput: float
    cpu_utilization: float
    total_time: int


class SchedulerBase:
    def __init__(self, name: str):
        self.name = name
        self.current_time = 0
        self.gantt_chart: List[GanttEntry] = []

    def schedule(self, processes: List[Process]) -> SchedulerResult:
        raise NotImplementedError

    def calculate_metrics(self, processes: List[Process]) -> SchedulerResult:
        total_waiting = sum(p.waiting_time for p in processes)
        total_turnaround = sum(p.turnaround_time for p in processes)
        total_response = sum(p.response_time for p in processes if p.response_time >= 0)

        n = len(processes)
        total_burst = sum(p.burst_time for p in processes)

        return SchedulerResult(
            name=self.name,
            gantt_chart=self.gantt_chart,
            processes=processes,
            avg_waiting_time=total_waiting / n,
            avg_turnaround_time=total_turnaround / n,
            avg_response_time=total_response / n,
            throughput=n / self.current_time if self.current_time > 0 else 0,
            cpu_utilization=(total_burst / self.current_time * 100) if self.current_time > 0 else 0,
            total_time=self.current_time
        )


class FCFSScheduler(SchedulerBase):
    def __init__(self):
        super().__init__("FCFS (First Come First Serve)")

    def schedule(self, processes: List[Process]) -> SchedulerResult:
        procs = deepcopy(processes)
        procs.sort(key=lambda p: (p.arrival_time, p.pid))

        self.current_time = 0
        self.gantt_chart = []

        for proc in procs:
            if self.current_time < proc.arrival_time:
                self.current_time = proc.arrival_time

            proc.start_time = self.current_time
            proc.response_time = self.current_time - proc.arrival_time

            self.gantt_chart.append(GanttEntry(
                pid=proc.pid,
                start=self.current_time,
                end=self.current_time + proc.burst_time
            ))

            self.current_time += proc.burst_time
            proc.remaining_time = 0
            proc.finish_time = self.current_time
            proc.turnaround_time = proc.finish_time - proc.arrival_time
            proc.waiting_time = proc.turnaround_time - proc.burst_time

        return self.calculate_metrics(procs)


class SJFScheduler(SchedulerBase):
    def __init__(self, preemptive: bool = False):
        self.preemptive = preemptive
        name = "SRTF (Shortest Remaining Time First)" if preemptive else "SJF (Shortest Job First)"
        super().__init__(name)

    def schedule(self, processes: List[Process]) -> SchedulerResult:
        procs = deepcopy(processes)
        self.current_time = 0
        self.gantt_chart = []
        completed = 0
        n = len(procs)
        last_proc = None
        last_start = 0

        while completed < n:
            available = [p for p in procs
                        if p.arrival_time <= self.current_time and p.remaining_time > 0]

            if not available:
                next_arrival = min(p.arrival_time for p in procs if p.remaining_time > 0)
                self.current_time = next_arrival
                continue

            current_proc = min(available, key=lambda p: (p.remaining_time, p.arrival_time, p.pid))

            if current_proc.response_time == -1:
                current_proc.response_time = self.current_time - current_proc.arrival_time
                current_proc.start_time = self.current_time

            if self.preemptive:
                next_arrival = float('inf')
                for p in procs:
                    if p.arrival_time > self.current_time and p.remaining_time > 0:
                        next_arrival = min(next_arrival, p.arrival_time)

                run_time = min(current_proc.remaining_time,
                              next_arrival - self.current_time if next_arrival != float('inf') else current_proc.remaining_time)

                if last_proc != current_proc.pid:
                    if last_proc is not None and last_start < self.current_time:
                        self.gantt_chart.append(GanttEntry(last_proc, last_start, self.current_time))
                    last_proc = current_proc.pid
                    last_start = self.current_time

                self.current_time += run_time
                current_proc.remaining_time -= run_time

                if current_proc.remaining_time == 0:
                    current_proc.finish_time = self.current_time
                    current_proc.turnaround_time = current_proc.finish_time - current_proc.arrival_time
                    current_proc.waiting_time = current_proc.turnaround_time - current_proc.burst_time
                    completed += 1
                    if last_start < self.current_time:
                        self.gantt_chart.append(GanttEntry(current_proc.pid, last_start, self.current_time))
                    last_proc = None
            else:
                self.gantt_chart.append(GanttEntry(
                    pid=current_proc.pid,
                    start=self.current_time,
                    end=self.current_time + current_proc.remaining_time
                ))
                self.current_time += current_proc.remaining_time
                current_proc.remaining_time = 0
                current_proc.finish_time = self.current_time
                current_proc.turnaround_time = current_proc.finish_time - current_proc.arrival_time
                current_proc.waiting_time = current_proc.turnaround_time - current_proc.burst_time
                completed += 1

        return self.calculate_metrics(procs)


class PriorityScheduler(SchedulerBase):
    """lower number = higher priority"""

    def __init__(self, preemptive: bool = False):
        self.preemptive = preemptive
        name = "Priority (Preemptive)" if preemptive else "Priority (Non-Preemptive)"
        super().__init__(name)

    def schedule(self, processes: List[Process]) -> SchedulerResult:
        procs = deepcopy(processes)
        self.current_time = 0
        self.gantt_chart = []
        completed = 0
        n = len(procs)
        last_proc = None
        last_start = 0

        while completed < n:
            available = [p for p in procs
                        if p.arrival_time <= self.current_time and p.remaining_time > 0]

            if not available:
                next_arrival = min(p.arrival_time for p in procs if p.remaining_time > 0)
                self.current_time = next_arrival
                continue

            current_proc = min(available, key=lambda p: (p.priority, p.arrival_time, p.pid))

            if current_proc.response_time == -1:
                current_proc.response_time = self.current_time - current_proc.arrival_time
                current_proc.start_time = self.current_time

            if self.preemptive:
                next_arrival = float('inf')
                for p in procs:
                    if p.arrival_time > self.current_time and p.remaining_time > 0:
                        next_arrival = min(next_arrival, p.arrival_time)

                run_time = min(current_proc.remaining_time,
                              next_arrival - self.current_time if next_arrival != float('inf') else current_proc.remaining_time)

                if last_proc != current_proc.pid:
                    if last_proc is not None and last_start < self.current_time:
                        self.gantt_chart.append(GanttEntry(last_proc, last_start, self.current_time))
                    last_proc = current_proc.pid
                    last_start = self.current_time

                self.current_time += run_time
                current_proc.remaining_time -= run_time

                if current_proc.remaining_time == 0:
                    current_proc.finish_time = self.current_time
                    current_proc.turnaround_time = current_proc.finish_time - current_proc.arrival_time
                    current_proc.waiting_time = current_proc.turnaround_time - current_proc.burst_time
                    completed += 1
                    if last_start < self.current_time:
                        self.gantt_chart.append(GanttEntry(current_proc.pid, last_start, self.current_time))
                    last_proc = None
            else:
                self.gantt_chart.append(GanttEntry(
                    pid=current_proc.pid,
                    start=self.current_time,
                    end=self.current_time + current_proc.remaining_time
                ))
                self.current_time += current_proc.remaining_time
                current_proc.remaining_time = 0
                current_proc.finish_time = self.current_time
                current_proc.turnaround_time = current_proc.finish_time - current_proc.arrival_time
                current_proc.waiting_time = current_proc.turnaround_time - current_proc.burst_time
                completed += 1

        return self.calculate_metrics(procs)


class RoundRobinScheduler(SchedulerBase):
    def __init__(self, time_quantum: int = 4):
        super().__init__(f"Round Robin (TQ={time_quantum})")
        self.time_quantum = time_quantum

    def schedule(self, processes: List[Process]) -> SchedulerResult:
        procs = deepcopy(processes)
        procs.sort(key=lambda p: (p.arrival_time, p.pid))

        self.current_time = 0
        self.gantt_chart = []
        ready_queue = []
        completed = 0
        n = len(procs)
        proc_index = 0

        while proc_index < n and procs[proc_index].arrival_time <= self.current_time:
            ready_queue.append(procs[proc_index])
            proc_index += 1

        while completed < n:
            if not ready_queue:
                if proc_index < n:
                    self.current_time = procs[proc_index].arrival_time
                    while proc_index < n and procs[proc_index].arrival_time <= self.current_time:
                        ready_queue.append(procs[proc_index])
                        proc_index += 1
                continue

            current_proc = ready_queue.pop(0)

            if current_proc.response_time == -1:
                current_proc.response_time = self.current_time - current_proc.arrival_time
                current_proc.start_time = self.current_time

            exec_time = min(self.time_quantum, current_proc.remaining_time)

            self.gantt_chart.append(GanttEntry(
                pid=current_proc.pid,
                start=self.current_time,
                end=self.current_time + exec_time
            ))

            self.current_time += exec_time
            current_proc.remaining_time -= exec_time

            while proc_index < n and procs[proc_index].arrival_time <= self.current_time:
                ready_queue.append(procs[proc_index])
                proc_index += 1

            if current_proc.remaining_time > 0:
                ready_queue.append(current_proc)
            else:
                current_proc.finish_time = self.current_time
                current_proc.turnaround_time = current_proc.finish_time - current_proc.arrival_time
                current_proc.waiting_time = current_proc.turnaround_time - current_proc.burst_time
                completed += 1

        return self.calculate_metrics(procs)


class HeuristicCFSScheduler(SchedulerBase):
    """CFS with heuristic enhancements - aging, interactivity detection, burst estimation"""

    def __init__(self, time_quantum: int = 4):
        super().__init__("Heuristic AI CFS")
        self.time_quantum = time_quantum
        self.min_vruntime = 0.0
        self.WEIGHT_NICE_0 = 1024
        self.MAX_WAIT_THRESHOLD = 50
        self.INTERACTIVE_THRESHOLD = 20

    def _compute_heuristic_metrics(self, proc: Process, current_time: int):
        # aging boost for starvation prevention
        wait_time = current_time - proc.last_scheduled
        if wait_time > self.MAX_WAIT_THRESHOLD:
            proc.aging_boost = min(10, (wait_time - self.MAX_WAIT_THRESHOLD) // 5)
        else:
            proc.aging_boost = 0
        proc.last_scheduled = current_time

    def _update_vruntime(self, proc: Process, executed_time: int):
        # vruntime += (exec_time * 1024) / weight
        delta_vruntime = (executed_time * self.WEIGHT_NICE_0) / proc.weight
        proc.vruntime += delta_vruntime

    def _select_next_process(self, available: List[Process], current_time: int) -> Optional[Process]:
        if not available:
            return None

        best_proc = None
        best_score = float('inf')

        for proc in available:
            self._compute_heuristic_metrics(proc, current_time)

            score = proc.vruntime
            score -= proc.aging_boost * 100          # aging bonus
            if proc.remaining_time < self.INTERACTIVE_THRESHOLD:
                score -= 50                          # interactive bonus
            if proc.remaining_time > 50:
                score += 10                          # long process penalty

            if score < best_score:
                best_score = score
                best_proc = proc

        return best_proc

    def schedule(self, processes: List[Process]) -> SchedulerResult:
        procs = deepcopy(processes)

        for p in procs:
            p.vruntime = self.min_vruntime
            p.last_scheduled = p.arrival_time

        self.current_time = 0
        self.gantt_chart = []
        completed = 0
        n = len(procs)
        last_proc_pid = None
        last_start = 0

        while completed < n:
            available = [p for p in procs
                        if p.arrival_time <= self.current_time and p.remaining_time > 0]

            if not available:
                next_arrival = min(p.arrival_time for p in procs if p.remaining_time > 0)
                self.current_time = next_arrival
                continue

            current_proc = self._select_next_process(available, self.current_time)

            if current_proc.response_time == -1:
                current_proc.response_time = self.current_time - current_proc.arrival_time
                current_proc.start_time = self.current_time

            # time slice from weight
            time_slice = max(2, (self.time_quantum * self.WEIGHT_NICE_0) // current_proc.weight)

            next_arrival = float('inf')
            for p in procs:
                if p.arrival_time > self.current_time and p.remaining_time > 0:
                    next_arrival = min(next_arrival, p.arrival_time)

            exec_time = min(time_slice, current_proc.remaining_time,
                           int(next_arrival - self.current_time) if next_arrival != float('inf') else time_slice)
            exec_time = max(1, exec_time)

            if last_proc_pid != current_proc.pid:
                if last_proc_pid is not None and last_start < self.current_time:
                    self.gantt_chart.append(GanttEntry(last_proc_pid, last_start, self.current_time))
                last_proc_pid = current_proc.pid
                last_start = self.current_time

            self.current_time += exec_time
            current_proc.remaining_time -= exec_time
            self._update_vruntime(current_proc, exec_time)

            active_vruntimes = [p.vruntime for p in procs if p.remaining_time > 0]
            if active_vruntimes:
                self.min_vruntime = min(active_vruntimes)

            if current_proc.remaining_time == 0:
                current_proc.finish_time = self.current_time
                current_proc.turnaround_time = current_proc.finish_time - current_proc.arrival_time
                current_proc.waiting_time = current_proc.turnaround_time - current_proc.burst_time
                completed += 1
                if last_start < self.current_time:
                    self.gantt_chart.append(GanttEntry(current_proc.pid, last_start, self.current_time))
                last_proc_pid = None

        return self.calculate_metrics(procs)


# visualization stuff

class SchedulerVisualizer:
    COLORS = [
        '#FF6B6B', '#4ECDC4', '#45B7D1', '#96CEB4', '#FFEAA7',
        '#DDA0DD', '#98D8C8', '#F7DC6F', '#BB8FCE', '#85C1E9',
        '#F8B500', '#00CED1', '#FF69B4', '#32CD32', '#FFD700'
    ]

    def __init__(self, results: List[SchedulerResult]):
        self.results = results
        self.process_colors = {}
        all_pids = set()
        for result in results:
            for entry in result.gantt_chart:
                all_pids.add(entry.pid)
        for i, pid in enumerate(sorted(all_pids)):
            self.process_colors[pid] = self.COLORS[i % len(self.COLORS)]

    def draw_gantt_chart(self, ax, result: SchedulerResult, title: str = None):
        ax.clear()
        if title:
            ax.set_title(title, fontsize=12, fontweight='bold', pad=10)
        else:
            ax.set_title(result.name, fontsize=12, fontweight='bold', pad=10)

        y_pos = 0.5
        height = 0.6

        for entry in result.gantt_chart:
            color = self.process_colors.get(entry.pid, '#808080')
            rect = mpatches.FancyBboxPatch(
                (entry.start, y_pos - height/2),
                entry.end - entry.start,
                height,
                boxstyle="round,pad=0.02",
                facecolor=color,
                edgecolor='black',
                linewidth=1.5
            )
            ax.add_patch(rect)

            mid_x = (entry.start + entry.end) / 2
            if entry.end - entry.start >= 3:
                ax.text(mid_x, y_pos, f'P{entry.pid}',
                       ha='center', va='center',
                       fontsize=9, fontweight='bold', color='white')

        ax.set_xlim(-1, result.total_time + 1)
        ax.set_ylim(0, 1)
        ax.set_xlabel('Time', fontsize=10)
        ax.set_yticks([])

        max_time = result.total_time
        step = max(1, max_time // 10)
        ax.set_xticks(range(0, max_time + 1, step))
        ax.grid(axis='x', linestyle='--', alpha=0.5)

        metrics_text = f"Avg Wait: {result.avg_waiting_time:.1f}  |  Avg TAT: {result.avg_turnaround_time:.1f}  |  CPU: {result.cpu_utilization:.1f}%"
        ax.text(0.5, -0.15, metrics_text, transform=ax.transAxes,
               ha='center', fontsize=9, style='italic')

    def plot_all_gantt_charts(self, animate: bool = True):
        n = len(self.results)
        fig, axes = plt.subplots(n, 1, figsize=(14, 3 * n))
        fig.suptitle('CPU Scheduling Algorithms - Gantt Charts', fontsize=14, fontweight='bold')

        if n == 1:
            axes = [axes]

        for ax, result in zip(axes, self.results):
            self.draw_gantt_chart(ax, result)

        legend_patches = [mpatches.Patch(color=color, label=f'P{pid}')
                         for pid, color in sorted(self.process_colors.items())]
        fig.legend(handles=legend_patches, loc='upper right', ncol=min(8, len(legend_patches)))

        plt.tight_layout(rect=[0, 0, 1, 0.96])
        return fig, axes

    def plot_performance_comparison(self):
        fig, axes = plt.subplots(2, 2, figsize=(14, 10))
        fig.suptitle('Scheduling Algorithm Performance Comparison', fontsize=14, fontweight='bold')

        names = [r.name.split('(')[0].strip() for r in self.results]
        colors = plt.cm.viridis(np.linspace(0.2, 0.8, len(self.results)))

        # waiting time
        ax1 = axes[0, 0]
        waiting_times = [r.avg_waiting_time for r in self.results]
        bars = ax1.bar(names, waiting_times, color=colors, edgecolor='black')
        ax1.set_ylabel('Average Waiting Time')
        ax1.set_title('Average Waiting Time Comparison', fontweight='bold')
        ax1.tick_params(axis='x', rotation=45)
        for bar, val in zip(bars, waiting_times):
            ax1.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5,
                    f'{val:.1f}', ha='center', va='bottom', fontsize=9)

        # turnaround time
        ax2 = axes[0, 1]
        tat = [r.avg_turnaround_time for r in self.results]
        bars = ax2.bar(names, tat, color=colors, edgecolor='black')
        ax2.set_ylabel('Average Turnaround Time')
        ax2.set_title('Average Turnaround Time Comparison', fontweight='bold')
        ax2.tick_params(axis='x', rotation=45)
        for bar, val in zip(bars, tat):
            ax2.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5,
                    f'{val:.1f}', ha='center', va='bottom', fontsize=9)

        # cpu utilization
        ax3 = axes[1, 0]
        cpu_util = [r.cpu_utilization for r in self.results]
        bars = ax3.bar(names, cpu_util, color=colors, edgecolor='black')
        ax3.set_ylabel('CPU Utilization (%)')
        ax3.set_title('CPU Utilization Comparison', fontweight='bold')
        ax3.tick_params(axis='x', rotation=45)
        ax3.set_ylim(0, 110)
        for bar, val in zip(bars, cpu_util):
            ax3.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 1,
                    f'{val:.1f}%', ha='center', va='bottom', fontsize=9)

        # response time
        ax4 = axes[1, 1]
        response = [r.avg_response_time for r in self.results]
        bars = ax4.bar(names, response, color=colors, edgecolor='black')
        ax4.set_ylabel('Average Response Time')
        ax4.set_title('Average Response Time Comparison', fontweight='bold')
        ax4.tick_params(axis='x', rotation=45)
        for bar, val in zip(bars, response):
            ax4.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5,
                    f'{val:.1f}', ha='center', va='bottom', fontsize=9)

        plt.tight_layout()
        return fig, axes

    def plot_performance_vs_load(self, load_levels: List[int], metrics_by_load: dict):
        fig, axes = plt.subplots(2, 2, figsize=(14, 10))
        fig.suptitle('Performance vs System Load', fontsize=14, fontweight='bold')

        markers = ['o', 's', '^', 'D', 'v', '<', '>', 'p']
        colors = plt.cm.tab10(np.linspace(0, 1, len(metrics_by_load.keys())))
        scheduler_names = list(metrics_by_load.keys())

        ax1 = axes[0, 0]
        for i, name in enumerate(scheduler_names):
            data = metrics_by_load[name]
            ax1.plot(load_levels, [d['avg_waiting'] for d in data],
                    marker=markers[i % len(markers)], color=colors[i],
                    label=name.split('(')[0].strip(), linewidth=2, markersize=8)
        ax1.set_xlabel('Number of Processes (Load)')
        ax1.set_ylabel('Average Waiting Time')
        ax1.set_title('Waiting Time vs Load', fontweight='bold')
        ax1.legend(fontsize=8)
        ax1.grid(True, alpha=0.3)

        ax2 = axes[0, 1]
        for i, name in enumerate(scheduler_names):
            data = metrics_by_load[name]
            ax2.plot(load_levels, [d['avg_tat'] for d in data],
                    marker=markers[i % len(markers)], color=colors[i],
                    label=name.split('(')[0].strip(), linewidth=2, markersize=8)
        ax2.set_xlabel('Number of Processes (Load)')
        ax2.set_ylabel('Average Turnaround Time')
        ax2.set_title('Turnaround Time vs Load', fontweight='bold')
        ax2.legend(fontsize=8)
        ax2.grid(True, alpha=0.3)

        ax3 = axes[1, 0]
        for i, name in enumerate(scheduler_names):
            data = metrics_by_load[name]
            ax3.plot(load_levels, [d['throughput'] for d in data],
                    marker=markers[i % len(markers)], color=colors[i],
                    label=name.split('(')[0].strip(), linewidth=2, markersize=8)
        ax3.set_xlabel('Number of Processes (Load)')
        ax3.set_ylabel('Throughput (processes/time)')
        ax3.set_title('Throughput vs Load', fontweight='bold')
        ax3.legend(fontsize=8)
        ax3.grid(True, alpha=0.3)

        ax4 = axes[1, 1]
        for i, name in enumerate(scheduler_names):
            data = metrics_by_load[name]
            ax4.plot(load_levels, [d['avg_response'] for d in data],
                    marker=markers[i % len(markers)], color=colors[i],
                    label=name.split('(')[0].strip(), linewidth=2, markersize=8)
        ax4.set_xlabel('Number of Processes (Load)')
        ax4.set_ylabel('Average Response Time')
        ax4.set_title('Response Time vs Load', fontweight='bold')
        ax4.legend(fontsize=8)
        ax4.grid(True, alpha=0.3)

        plt.tight_layout()
        return fig, axes


class RealTimeVisualizer:
    COLORS = SchedulerVisualizer.COLORS

    def __init__(self, results: List[SchedulerResult]):
        self.results = results
        self.process_colors = {}
        all_pids = set()
        for result in results:
            for entry in result.gantt_chart:
                all_pids.add(entry.pid)
        for i, pid in enumerate(sorted(all_pids)):
            self.process_colors[pid] = self.COLORS[i % len(self.COLORS)]

    def animate_gantt_charts(self, interval: int = 100):
        n = len(self.results)
        fig, axes = plt.subplots(n, 1, figsize=(14, 3 * n))
        fig.suptitle('CPU Scheduling Algorithms - Real-Time Simulation',
                    fontsize=14, fontweight='bold')

        if n == 1:
            axes = [axes]

        max_time = max(r.total_time for r in self.results)
        current_times = [0] * n

        def init():
            for ax in axes:
                ax.clear()
            return []

        def update(frame):
            time = frame
            patches = []

            for idx, (ax, result) in enumerate(zip(axes, self.results)):
                ax.clear()
                ax.set_title(result.name, fontsize=11, fontweight='bold')
                y_pos = 0.5
                height = 0.6

                for entry in result.gantt_chart:
                    if entry.start > time:
                        continue
                    color = self.process_colors.get(entry.pid, '#808080')
                    actual_end = min(entry.end, time)

                    if actual_end > entry.start:
                        rect = mpatches.FancyBboxPatch(
                            (entry.start, y_pos - height/2),
                            actual_end - entry.start,
                            height,
                            boxstyle="round,pad=0.02",
                            facecolor=color,
                            edgecolor='black',
                            linewidth=1.5
                        )
                        ax.add_patch(rect)
                        patches.append(rect)

                        mid_x = (entry.start + actual_end) / 2
                        if actual_end - entry.start >= 3:
                            ax.text(mid_x, y_pos, f'P{entry.pid}',
                                   ha='center', va='center',
                                   fontsize=9, fontweight='bold', color='white')

                ax.set_xlim(-1, max_time + 1)
                ax.set_ylim(0, 1)
                ax.set_xlabel('Time', fontsize=10)
                ax.set_yticks([])
                ax.axvline(x=time, color='red', linestyle='--', linewidth=2, alpha=0.7)

                step = max(1, max_time // 10)
                ax.set_xticks(range(0, max_time + 1, step))
                ax.grid(axis='x', linestyle='--', alpha=0.5)
                ax.text(time + 0.5, 0.9, f't={time}', fontsize=10,
                       color='red', fontweight='bold')

            legend_patches = [mpatches.Patch(color=color, label=f'P{pid}')
                             for pid, color in sorted(self.process_colors.items())]
            fig.legend(handles=legend_patches, loc='upper right',
                      ncol=min(8, len(legend_patches)))

            plt.tight_layout(rect=[0, 0, 0.95, 0.96])
            return patches

        anim = FuncAnimation(fig, update, frames=range(0, max_time + 1),
                            init_func=init, interval=interval, blit=False, repeat=False)

        return fig, anim


def generate_random_processes(n: int, max_arrival: int = 50,
                              max_burst: int = 30, seed: int = None) -> List[Process]:
    if seed is not None:
        random.seed(seed)

    processes = []
    for i in range(n):
        processes.append(Process(
            pid=i,
            arrival_time=random.randint(0, max_arrival),
            burst_time=random.randint(1, max_burst),
            priority=random.randint(1, 5),
            nice_value=random.randint(-10, 10)
        ))
    return processes


def run_all_schedulers(processes: List[Process]) -> List[SchedulerResult]:
    schedulers = [
        FCFSScheduler(),
        SJFScheduler(preemptive=False),
        SJFScheduler(preemptive=True),
        PriorityScheduler(preemptive=False),
        RoundRobinScheduler(time_quantum=4),
        HeuristicCFSScheduler(time_quantum=4)
    ]

    results = []
    for scheduler in schedulers:
        proc_copy = deepcopy(processes)
        result = scheduler.schedule(proc_copy)
        results.append(result)
        print(f"  {scheduler.name} completed")

    return results


def run_load_analysis(load_levels: List[int], seed: int = 42) -> Tuple[List[int], dict]:
    schedulers = [
        ("FCFS", FCFSScheduler),
        ("SJF", lambda: SJFScheduler(preemptive=False)),
        ("SRTF", lambda: SJFScheduler(preemptive=True)),
        ("Priority", lambda: PriorityScheduler(preemptive=False)),
        ("Round Robin", lambda: RoundRobinScheduler(time_quantum=4)),
        ("Heuristic CFS", lambda: HeuristicCFSScheduler(time_quantum=4))
    ]

    metrics_by_scheduler = {name: [] for name, _ in schedulers}

    for load in load_levels:
        print(f"  Running for {load} processes...")
        processes = generate_random_processes(load, max_arrival=load*2,
                                             max_burst=20, seed=seed+load)

        for name, scheduler_class in schedulers:
            scheduler = scheduler_class()
            result = scheduler.schedule(deepcopy(processes))

            metrics_by_scheduler[name].append({
                'avg_waiting': result.avg_waiting_time,
                'avg_tat': result.avg_turnaround_time,
                'avg_response': result.avg_response_time,
                'throughput': result.throughput,
                'cpu_util': result.cpu_utilization
            })

    return load_levels, metrics_by_scheduler


def print_comparison_table(results: List[SchedulerResult]):
    print("\n" + "="*90)
    print("                    SCHEDULING ALGORITHM COMPARISON")
    print("="*90)
    print(f"{'Algorithm':<25} {'Avg Wait':>10} {'Avg TAT':>10} {'Avg Resp':>10} {'CPU Util':>10} {'Throughput':>12}")
    print("-"*90)

    for r in results:
        name = r.name.split('(')[0].strip()[:24]
        print(f"{name:<25} {r.avg_waiting_time:>10.2f} {r.avg_turnaround_time:>10.2f} "
              f"{r.avg_response_time:>10.2f} {r.cpu_utilization:>9.1f}% {r.throughput:>12.4f}")

    print("="*90)

    best_wait = min(results, key=lambda r: r.avg_waiting_time)
    best_tat = min(results, key=lambda r: r.avg_turnaround_time)
    best_resp = min(results, key=lambda r: r.avg_response_time)
    best_cpu = max(results, key=lambda r: r.cpu_utilization)

    print(f"\nBest Average Waiting Time:    {best_wait.name.split('(')[0].strip()} ({best_wait.avg_waiting_time:.2f})")
    print(f"Best Average Turnaround Time: {best_tat.name.split('(')[0].strip()} ({best_tat.avg_turnaround_time:.2f})")
    print(f"Best Average Response Time:   {best_resp.name.split('(')[0].strip()} ({best_resp.avg_response_time:.2f})")
    print(f"Best CPU Utilization:         {best_cpu.name.split('(')[0].strip()} ({best_cpu.cpu_utilization:.1f}%)")


def main():
    print("="*70)
    print("   CPU SCHEDULING ALGORITHMS SIMULATION WITH VISUALIZATION")
    print("="*70)
    print("\nAlgorithms: FCFS, SJF, SRTF, Priority, Round Robin, Heuristic AI CFS")
    print("-"*70)

    # sample processes
    processes = [
        Process(pid=0, arrival_time=0, burst_time=8, priority=3, nice_value=0),
        Process(pid=1, arrival_time=1, burst_time=4, priority=1, nice_value=-5),
        Process(pid=2, arrival_time=2, burst_time=9, priority=4, nice_value=5),
        Process(pid=3, arrival_time=3, burst_time=5, priority=2, nice_value=0),
        Process(pid=4, arrival_time=4, burst_time=2, priority=5, nice_value=-10),
        Process(pid=5, arrival_time=6, burst_time=6, priority=3, nice_value=0),
    ]

    print("\nProcess Table:")
    print("-"*70)
    print(f"{'PID':<6} {'Arrival':<10} {'Burst':<10} {'Priority':<10} {'Nice':<10}")
    print("-"*70)
    for p in processes:
        print(f"P{p.pid:<5} {p.arrival_time:<10} {p.burst_time:<10} {p.priority:<10} {p.nice_value:<10}")
    print("-"*70)

    print("\nRunning scheduling algorithms...")
    results = run_all_schedulers(processes)

    print_comparison_table(results)

    print("\nGenerating visualizations...")
    visualizer = SchedulerVisualizer(results)
    fig1, _ = visualizer.plot_all_gantt_charts()
    fig2, _ = visualizer.plot_performance_comparison()

    print("\nRunning load analysis...")
    load_levels = [5, 10, 15, 20, 25, 30]
    load_levels, metrics = run_load_analysis(load_levels)
    fig3, _ = visualizer.plot_performance_vs_load(load_levels, metrics)

    print("\nCreating animation...")
    rt_visualizer = RealTimeVisualizer(results)
    fig4, anim = rt_visualizer.animate_gantt_charts(interval=200)

    print("\nDone! Close plot windows to exit.")
    plt.show()


if __name__ == "__main__":
    main()
