# CPU Scheduling Algorithms Simulation

A comprehensive Python simulation that compares various CPU scheduling algorithms with real-time visualization, animated Gantt charts, and performance analysis.

## 🎯 Features

- **Multiple Scheduling Algorithms**: Compare 6 different scheduling algorithms side-by-side
- **Real-time Animated Gantt Charts**: Visualize process execution over time
- **Performance Comparison Graphs**: Bar charts comparing key metrics
- **Load Analysis**: See how algorithms perform as system load increases
- **Two Visualization Options**: 
  - Matplotlib (default) - Cross-platform with animation support
  - Dear PyGui - Interactive GUI with tabs

## 📊 Scheduling Algorithms Implemented

| Algorithm | Type | Description |
|-----------|------|-------------|
| **FCFS** | Non-preemptive | First Come First Serve - Processes executed in arrival order |
| **SJF** | Non-preemptive | Shortest Job First - Shortest burst time gets priority |
| **SRTF** | Preemptive | Shortest Remaining Time First - Preemptive version of SJF |
| **Priority** | Non-preemptive | Lower priority number = higher priority |
| **Round Robin** | Preemptive | Time-sliced execution with configurable quantum |
| **Heuristic AI CFS** | Preemptive | CFS with AI-enhanced heuristics |

## 🧠 Heuristic AI CFS (Completely Fair Scheduler)

The Heuristic AI CFS is an enhanced version of Linux's Completely Fair Scheduler with AI-inspired optimizations:

### Core CFS Concepts:
- **Virtual Runtime (vruntime)**: Weighted execution time for fairness
- **Weight-based scheduling**: Nice values (-20 to 19) determine process weights

### AI Heuristics:
1. **Aging Boost**: Prevents starvation by boosting priority of long-waiting processes
2. **Interactivity Detection**: Favors short, interactive tasks for better responsiveness
3. **Burst Estimation**: Penalizes very long processes to balance system load

## 🚀 Installation

1. Clone the repository:
```bash
git clone https://github.com/harihara-1869/CFS_Scheduler.git
cd CFS_Scheduler
```

2. Install dependencies:
```bash
pip install -r requirements.txt
```

## 💻 Usage

### Matplotlib Visualization (Recommended)
```bash
python scheduler_simulation.py
```

This will:
- Generate sample processes
- Run all scheduling algorithms
- Display comparison table in terminal
- Show Gantt charts, performance graphs, and animated visualization

### Dear PyGui Visualization (Alternative)
```bash
python dearpygui_simulation
```

Features a tabbed interface with:
- Gantt Charts tab
- Performance Comparison tab
- Performance vs Load tab
- Real-time Animation tab

## 📈 Performance Metrics

The simulation calculates and compares:

| Metric | Description |
|--------|-------------|
| **Average Waiting Time** | Time process spends waiting in ready queue |
| **Average Turnaround Time** | Total time from arrival to completion |
| **Average Response Time** | Time from arrival to first execution |
| **CPU Utilization** | Percentage of time CPU is busy |
| **Throughput** | Number of processes completed per unit time |

## 📁 Project Structure

```
CFS_Scheduler/
├── scheduler_simulation.py    # Main simulation with Matplotlib visualization
├── dearpygui_simulation       # Alternative Dear PyGui visualization
├── CFS_Heuristic_upgrade.c    # C implementation reference
├── requirements.txt           # Python dependencies
└── README.md                  # This file
```

## 🖼️ Sample Output

```
==========================================================================================
                    SCHEDULING ALGORITHM COMPARISON
==========================================================================================
Algorithm                   Avg Wait    Avg TAT   Avg Resp   CPU Util   Throughput
------------------------------------------------------------------------------------------
FCFS                           13.17      18.83      13.17     100.0%       0.1765
SJF                            10.00      15.67      10.00     100.0%       0.1765
SRTF                            8.50      14.17       5.67     100.0%       0.1765
Priority                       12.67      18.33      12.67     100.0%       0.1765
Round Robin                    16.00      21.67       7.67     100.0%       0.1765
Heuristic AI CFS               14.00      19.67       0.00     100.0%       0.1765
==========================================================================================

🏆 Best Average Waiting Time:    SRTF (8.50)
🏆 Best Average Turnaround Time: SRTF (14.17)
🏆 Best Average Response Time:   Heuristic AI CFS (0.00)
🏆 Best CPU Utilization:         FCFS (100.0%)
```

## 🔧 Customization

### Modify Process Set
Edit the `processes` list in `main()`:
```python
processes = [
    Process(pid=0, arrival_time=0, burst_time=8, priority=3, nice_value=0),
    Process(pid=1, arrival_time=1, burst_time=4, priority=1, nice_value=-5),
    # Add more processes...
]
```

### Change Time Quantum
```python
RoundRobinScheduler(time_quantum=2)  # Default is 4
HeuristicCFSScheduler(time_quantum=2)
```

### Generate Random Processes
```python
processes = generate_random_processes(n=10, max_arrival=30, max_burst=15, seed=42)
```

## 📚 References

- Linux CFS Scheduler Documentation
- Operating System Concepts by Silberschatz, Galvin, and Gagne
- Understanding the Linux Kernel by Bovet and Cesati

## 📄 License

This project is for educational purposes as part of the Operating Systems Lab coursework.

---
*Developed for OS Lab - RV College of Engineering*
