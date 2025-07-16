# ThreadShell

A multi-threaded shell with intelligent job scheduling and real-time monitoring capabilities. ThreadShell provides a powerful command-line interface for managing concurrent processes with advanced scheduling algorithms, resource monitoring, and comprehensive visualization tools.

## ✨ Features

### Core Functionality
- **Multi-threaded Job Execution**: Run multiple commands concurrently with automatic resource management
- **Intelligent Job Scheduling**: Support for multiple scheduling policies (Priority, Shortest Job First, Round Robin, Fair Share)
- **Real-time Monitoring**: Live system statistics, CPU core utilization, and performance metrics
- **Job Management**: Start, stop, suspend, resume, and prioritize jobs with fine-grained control
- **Interactive Shell**: Rich command-line interface with history, completion, and colored output

### Advanced Features
- **Job Scripts**: Submit complex job configurations with resource limits and dependencies
- **Resource Monitoring**: Track memory usage, CPU utilization, and runtime statistics
- **Data Export**: Export job data to CSV format for external analysis
- **Visualization**: Generate Gantt charts, performance dashboards, and summary reports
- **Configurable Policies**: Customize scheduling behavior and system limits

## 🚀 Quick Start

### Prerequisites
- C++17 compatible compiler (g++, clang++)
- GNU Make
- GNU Readline library
- Python 3 (optional, for visualization features)

### Installation

1. **Clone the repository**
   ```bash
   git clone <repository-url>
   cd ThreadShell
   ```

2. **Install dependencies**
   ```bash
   # Ubuntu/Debian
   sudo apt-get install build-essential libreadline-dev

   # Fedora/RHEL
   sudo dnf install gcc-c++ readline-devel

   # macOS
   brew install readline
   ```

3. **Build the project**
   ```bash
   make
   ```

4. **Run ThreadShell**
   ```bash
   ./build/threadshell
   ```

### Optional: Python Visualization
```bash
pip install pandas matplotlib seaborn
```

## 📋 Usage

### Basic Commands

```bash
# Job execution
command                    # Run in foreground
command &                  # Run in background
cmd1 && cmd2              # Sequential execution

# Job management
jobs                      # List all jobs
jobs active               # List active jobs
jobs detailed             # Detailed job information
jobinfo <id>              # Show job details
kill <id>                 # Terminate job
suspend <id>              # Suspend job
resume <id>               # Resume suspended job
priority <id> <level>     # Change job priority

# System monitoring
stats                     # System statistics
cores                     # CPU core utilization
queue                     # Job queue status
perf                      # Performance summary

# Configuration
policy <type>             # Set scheduling policy
config <setting> <value>  # Configure system settings
```

### Job Scripts

Create job scripts with advanced configurations:

```bash
# example_job.sh
# JOB_NAME: Data Processing
# PRIORITY: HIGH
# MEMORY_LIMIT: 2048
# RUNTIME_LIMIT: 300
# CORES: 4

python3 process_data.py --input data.csv --output results.json
```

Submit the job:
```bash
submit example_job.sh
```

### Visualization

```bash
# Built-in visualization command
visualize                 # Generate all visualizations
visualize --gantt         # Generate Gantt chart only
visualize --dashboard     # Generate performance dashboard

# Direct Python script usage
python3 visualize_jobs.py --all
python3 visualize_jobs.py --gantt -o ./charts
```

## 🔧 Configuration

### Scheduling Policies
- **priority**: Jobs scheduled by priority level
- **shortest**: Shortest Job First algorithm
- **roundrobin**: Round-robin scheduling
- **fairshare**: Fair share scheduling

### System Settings
- **max_jobs**: Maximum concurrent jobs
- **cpu_affinity**: Enable/disable CPU affinity

Example:
```bash
policy priority
config max_jobs 8
config cpu_affinity true
```

## 📊 Monitoring & Visualization

ThreadShell provides comprehensive monitoring capabilities:

### Real-time Statistics
- Job completion rates and success ratios
- Average turnaround and wait times
- Memory and CPU utilization
- System throughput metrics

### Visualization Tools
- **Gantt Charts**: Job execution timelines
- **Performance Dashboards**: System metrics and trends
- **Summary Reports**: Detailed execution statistics

## 🛠️ Help System

ThreadShell includes a comprehensive help system:

```bash
help                      # General help
help jobs                 # Job management help
help monitoring           # Monitoring help
help advanced             # Advanced features
help visualization        # Visualization help
```

## 📁 Project Structure

```
ThreadShell/
├── src/                  # Source code
│   ├── main.cpp         # Main entry point
│   ├── shell.cpp        # Shell implementation
│   ├── scheduler.cpp    # Job scheduler
│   ├── job.cpp          # Job management
│   └── logger.cpp       # Logging system
├── include/             # Header files
├── build/               # Build output
├── logs/                # Log files
├── jobs/                # Example job scripts
├── visualize_jobs.py    # Visualization tool
├── Makefile            # Build configuration
└── README.md           # This file
```

## 🔨 Building from Source

### Debug Build
```bash
make debug
```

### Release Build
```bash
make
```

### Clean Build
```bash
make clean
make
```

## 🚀 Examples

### Basic Usage
```bash
# Start ThreadShell
./build/threadshell

# Run some background jobs
sleep 10 &
echo "Processing data..." &
ls -la /tmp &

# Monitor jobs
jobs
stats
cores

# Generate visualization
visualize --all
```

### Advanced Job Management
```bash
# Submit a job script
submit jobs/example_job.sh

# Change scheduling policy
policy shortest

# Configure system limits
config max_jobs 16

# Monitor performance
perf
export csv
```

## 📝 License

This project is open source. Feel free to use, modify, and distribute.

## 🤝 Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs and feature requests.


**ThreadShell** - Multi-threaded job scheduling made simple and powerful. 