#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <vector>
#include <thread>
#include <unordered_set>

namespace threadshell {

enum class JobPriority {
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2,
    CRITICAL = 3
};

enum class JobStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    KILLED,
    SUSPENDED,
    WAITING_DEPS  // Waiting for dependencies
};

enum class JobType {
    INTERACTIVE,
    BATCH,
    ARRAY_JOB
};

struct ResourceLimits {
    size_t max_memory_mb = 1024;  // Simulated memory limit
    std::chrono::seconds max_runtime{3600};  // Max runtime
    int max_cpu_cores = 1;  // Max cores this job can use
};

struct JobMetadata {
    int job_id;
    std::string job_name;
    std::string command;
    JobPriority priority;
    JobStatus status;
    JobType type;
    
    // Resource management
    int assigned_core_id;
    std::vector<int> assigned_cores;  // For multi-core jobs
    ResourceLimits limits;
    size_t memory_usage_mb = 0;  // Simulated memory usage
    
    // Threading info
    std::thread::id thread_id;
    
    // Timing
    std::chrono::system_clock::time_point submit_time;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::chrono::milliseconds actual_runtime{0};
    
    // Job dependencies
    std::unordered_set<int> dependencies;  // Job IDs this job depends on
    std::unordered_set<int> dependents;    // Job IDs that depend on this job
    
    // For chained commands (cmd1 && cmd2)
    std::vector<std::string> chained_commands;
    int current_command_index = 0;
    
    // Array job support
    int array_job_id = -1;  // Parent array job ID
    int array_task_id = -1; // Task ID within array
    
    // Performance metrics
    double cpu_utilization = 0.0;  // Simulated CPU usage %
    int context_switches = 0;       // Simulated context switches
    
    // Process info
    pid_t process_id = -1;
    int exit_code = 0;
    
    JobMetadata(int id, const std::string& cmd, JobPriority p = JobPriority::MEDIUM)
        : job_id(id)
        , command(cmd)
        , priority(p)
        , status(JobStatus::PENDING)
        , type(JobType::INTERACTIVE)
        , assigned_core_id(-1)
        , submit_time(std::chrono::system_clock::now()) {}
        
    // Check if all dependencies are satisfied
    bool dependencies_satisfied(const std::vector<std::shared_ptr<JobMetadata>>& all_jobs) const;
    
    // Get estimated runtime based on command complexity
    std::chrono::seconds get_estimated_runtime() const;
    
    // Calculate priority score (higher = more priority)
    double calculate_priority_score() const;
};

using JobPtr = std::shared_ptr<JobMetadata>;

// Job comparison for priority queue (higher priority first)
struct JobCompare {
    bool operator()(const JobPtr& a, const JobPtr& b) const {
        return a->calculate_priority_score() < b->calculate_priority_score();
    }
};

} // namespace threadshell 