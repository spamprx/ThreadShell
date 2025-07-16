#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <vector>
#include <functional>
#include <unordered_map>
#include <chrono>
#include "job.h"

namespace threadshell {

enum class SchedulingPolicy {
    PRIORITY_FIRST,     // Priority-based scheduling
    SHORTEST_JOB_FIRST, // Shortest job first
    ROUND_ROBIN,        // Round-robin for same priority
    FAIR_SHARE         // Fair share scheduling
};

struct SystemStats {
    size_t total_jobs_submitted = 0;
    size_t total_jobs_completed = 0;
    size_t total_jobs_failed = 0;
    size_t total_jobs_killed = 0;
    double average_turnaround_time = 0.0;
    double average_wait_time = 0.0;
    double system_throughput = 0.0;  // Jobs per minute
    size_t current_memory_usage_mb = 0;
    std::chrono::system_clock::time_point start_time;
};

class JobScheduler {
public:
    JobScheduler(size_t num_cores = std::thread::hardware_concurrency());
    ~JobScheduler();

    // Job submission methods
    JobPtr submit_job(const std::string& command, JobPriority priority = JobPriority::MEDIUM);
    JobPtr submit_job_with_deps(const std::string& command, 
                               const std::vector<int>& dependencies,
                               JobPriority priority = JobPriority::MEDIUM);
    JobPtr submit_job_script(const std::string& script_path);
    std::vector<JobPtr> submit_job_array(const std::string& command_template, 
                                        int array_size, 
                                        JobPriority priority = JobPriority::MEDIUM);
    
    // Job control methods
    std::vector<JobPtr> get_jobs() const;
    std::vector<JobPtr> get_active_jobs() const;
    std::vector<JobPtr> get_completed_jobs() const;
    bool kill_job(int job_id);
    bool suspend_job(int job_id);
    bool resume_job(int job_id);
    bool change_job_priority(int job_id, JobPriority new_priority);
    
    // Scheduler control
    void start();
    void stop();
    void set_scheduling_policy(SchedulingPolicy policy);
    
    // System monitoring
    SystemStats get_system_stats() const;
    std::vector<int> get_core_utilization() const;
    size_t get_queue_length() const;
    
    // Configuration
    void set_max_concurrent_jobs(size_t max_jobs);
    void enable_cpu_affinity(bool enable);

private:
    struct JobCompare {
        bool operator()(const JobPtr& a, const JobPtr& b) const {
            return static_cast<int>(a->priority) < static_cast<int>(b->priority);
        }
    };

    // Thread pool and job queue
    std::vector<std::thread> worker_threads_;
    std::priority_queue<JobPtr, std::vector<JobPtr>, JobCompare> job_queue_;
    
    // Job tracking
    std::vector<JobPtr> all_jobs_;          // All jobs ever submitted
    std::vector<JobPtr> active_jobs_;       // Currently running jobs
    std::vector<JobPtr> completed_jobs_;    // Completed jobs
    std::unordered_map<int, JobPtr> job_lookup_; // Quick job lookup by ID
    
    // Thread synchronization
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> running_{false};
    
    // Resource management
    size_t num_cores_;
    std::vector<bool> core_availability_;
    std::vector<std::chrono::system_clock::time_point> core_last_used_;
    std::atomic<size_t> max_concurrent_jobs_;
    std::atomic<bool> cpu_affinity_enabled_{false};
    
    // Scheduling
    std::atomic<int> next_job_id_{1};
    SchedulingPolicy current_policy_ = SchedulingPolicy::PRIORITY_FIRST;
    
    // Performance tracking
    mutable SystemStats stats_;
    std::chrono::system_clock::time_point last_stats_update_;
    
    // Worker thread functions
    void worker_function();
    void dependency_manager_thread();
    void stats_updater_thread();
    
    // Job execution
    void execute_job(JobPtr job);
    void execute_command_chain(JobPtr job);
    
    // Resource management
    int assign_core();
    std::vector<int> assign_multiple_cores(int count);
    void release_core(int core_id);
    void release_cores(const std::vector<int>& core_ids);
    bool can_schedule_job(const JobPtr& job) const;
    
    // Dependency management
    void check_and_schedule_dependencies();
    void update_job_dependencies(JobPtr completed_job);
    
    // Job parsing and creation
    JobPtr parse_job_script(const std::string& script_path);
    JobPtr create_job_from_command(const std::string& command, JobPriority priority);
    
    // Scheduling algorithms
    JobPtr get_next_job_priority_first();
    JobPtr get_next_job_shortest_first();
    JobPtr get_next_job_round_robin();
    JobPtr get_next_job_fair_share();
    
    // Performance monitoring
    void update_system_stats();
    void simulate_cpu_usage(JobPtr job);
    void simulate_memory_usage(JobPtr job);
    
    // Utility functions
    bool is_job_ready_to_run(const JobPtr& job) const;
    void cleanup_completed_jobs();
    void log_scheduler_event(const std::string& event, const JobPtr& job = nullptr);
};

} // namespace threadshell 