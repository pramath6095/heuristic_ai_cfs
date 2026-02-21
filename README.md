# CFS Scheduler with Heuristic AI

A user-space CPU scheduler inspired by Linux's CFS (Completely Fair Scheduler), enhanced with simple heuristic-based optimizations. Also includes a Python simulation that compares multiple scheduling algorithms side-by-side with visualizations.

## What's in here

- `CFS_Heuristic_upgrade.c` — C implementation of a CFS-inspired scheduler that manages real Linux processes using POSIX signals (SIGSTOP/SIGCONT). Includes heuristic enhancements like aging boost, interactivity detection, and burst estimation.
- `scheduler_simulation.py` — Python simulation comparing FCFS, SJF, SRTF, Priority, Round Robin, and Heuristic AI CFS. Generates Gantt charts, performance comparison graphs, and animated visualizations using matplotlib.

## How CFS + Heuristics work

The scheduler tracks virtual runtime (vruntime) for each process — lower vruntime means higher scheduling priority. Weights derived from nice values control how fast vruntime grows.

On top of standard CFS, three heuristics adjust the selection:
1. **Aging boost** — processes waiting too long get a priority bump to prevent starvation
2. **Interactivity detection** — short-burst processes get a small bonus for responsiveness
3. **Burst estimation** — predicts next CPU burst using exponential moving average

## Setup

### C Scheduler (Linux only)

```bash
gcc -o cfs_scheduler CFS_Heuristic_upgrade.c -lm -Wall -Wextra
./cfs_scheduler
```

This forks real child processes and schedules them using signals. Needs to be run on Linux.

### Python Simulation

```bash
pip install -r requirements.txt
python scheduler_simulation.py
```

Shows comparison tables in terminal and opens matplotlib windows with Gantt charts and performance graphs.

## Dependencies

- GCC (for the C part)
- Python 3.8+ with matplotlib and numpy (for the simulation)
