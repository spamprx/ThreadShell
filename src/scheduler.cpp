#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "../include/scheduler.h"
#include "../include/logger.h"

namespace threadshell {

JobScheduler::JobScheduler(size_t num_cores)
    : num_cores_(num_cores)
    , core_availability_(num_cores, true)
    , core_last_used_(num_cores)  // Initialize core_last_used_ vector
    , max_concurrent_jobs_(num_cores * 2) {
    stats_.start_time = std::chrono::system_clock::now();
}

JobScheduler::~JobScheduler() {
    stop();
}

void JobScheduler::start() {
    running_ = true;
    
    // Create worker threads
    for (size_t i = 0; i < num_cores_; ++i) {
        worker_threads_.emplace_back(&JobScheduler::worker_function, this);
    }
}

void JobScheduler::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    condition_.notify_all();
    
    // Wait for all worker threads to finish
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
    
    // Clean up any remaining active jobs
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& job : active_jobs_) {
        if (job->process_id > 0) {
            // Try to terminate any remaining processes
            kill(job->process_id, SIGTERM);
        }
    }
    active_jobs_.clear();
}

JobPtr JobScheduler::submit_job(const std::string& command, JobPriority priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto job = std::make_shared<JobMetadata>(next_job_id_++, command, priority);
    job_queue_.push(job);
    all_jobs_.push_back(job);
    
    stats_.total_jobs_submitted++;
    
    // Log job submission
    Logger::instance().log_job_submitted(job);
    
    condition_.notify_one();
    return job;
}

JobPtr JobScheduler::submit_job_with_deps(const std::string& command, 
                                         const std::vector<int>& dependencies,
                                         JobPriority priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto job = std::make_shared<JobMetadata>(next_job_id_++, command, priority);
    for (int dep_id : dependencies) {
        job->dependencies.insert(dep_id);
    }
    
    if (job->dependencies_satisfied(all_jobs_)) {
        job_queue_.push(job);
    } else {
        job->status = JobStatus::WAITING_DEPS;
    }
    
    all_jobs_.push_back(job);
    stats_.total_jobs_submitted++;
    
    Logger::instance().log_job_submitted(job);
    condition_.notify_one();
    return job;
}

std::vector<JobPtr> JobScheduler::submit_job_array(const std::string& command_template, 
                                                   int array_size, 
                                                   JobPriority priority) {
    std::vector<JobPtr> jobs;
    int array_job_id = next_job_id_.load();
    
    for (int i = 0; i < array_size; ++i) {
        std::string command = command_template;
        // Replace placeholders with array index
        size_t pos = command.find("$ARRAY_ID");
        if (pos != std::string::npos) {
            command.replace(pos, 9, std::to_string(i));
        }
        
        auto job = submit_job(command, priority);
        job->array_job_id = array_job_id;
        job->array_task_id = i;
        job->type = JobType::ARRAY_JOB;
        jobs.push_back(job);
    }
    
    return jobs;
}

JobPtr JobScheduler::submit_job_script(const std::string& script_path) {
    return parse_job_script(script_path);
}

std::vector<JobPtr> JobScheduler::get_jobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return all_jobs_;
}

std::vector<JobPtr> JobScheduler::get_active_jobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_jobs_;
}

std::vector<JobPtr> JobScheduler::get_completed_jobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return completed_jobs_;
}

bool JobScheduler::kill_job(int job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(active_jobs_.begin(), active_jobs_.end(),
        [job_id](const JobPtr& job) { return job->job_id == job_id; });
    
    if (it != active_jobs_.end()) {
        (*it)->status = JobStatus::KILLED;
        stats_.total_jobs_killed++;
        Logger::instance().log_job_killed(*it);
        return true;
    }
    return false;
}

bool JobScheduler::suspend_job(int job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(active_jobs_.begin(), active_jobs_.end(),
        [job_id](const JobPtr& job) { return job->job_id == job_id; });
    
    if (it != active_jobs_.end() && (*it)->status == JobStatus::RUNNING) {
        (*it)->status = JobStatus::SUSPENDED;
        // In a real implementation, we would send SIGSTOP to the process
        return true;
    }
    return false;
}

bool JobScheduler::resume_job(int job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(active_jobs_.begin(), active_jobs_.end(),
        [job_id](const JobPtr& job) { return job->job_id == job_id; });
    
    if (it != active_jobs_.end() && (*it)->status == JobStatus::SUSPENDED) {
        (*it)->status = JobStatus::RUNNING;
        // In a real implementation, we would send SIGCONT to the process
        return true;
    }
    return false;
}

bool JobScheduler::change_job_priority(int job_id, JobPriority new_priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(all_jobs_.begin(), all_jobs_.end(),
        [job_id](const JobPtr& job) { return job->job_id == job_id; });
    
    if (it != all_jobs_.end() && (*it)->status == JobStatus::PENDING) {
        (*it)->priority = new_priority;
        return true;
    }
    return false;
}

void JobScheduler::set_scheduling_policy(SchedulingPolicy policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_policy_ = policy;
}

SystemStats JobScheduler::get_system_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

std::vector<int> JobScheduler::get_core_utilization() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> utilization(num_cores_, 0);
    
    // Simulate core utilization based on active jobs
    for (const auto& job : active_jobs_) {
        if (job->assigned_core_id >= 0 && job->assigned_core_id < static_cast<int>(num_cores_)) {
            utilization[job->assigned_core_id] = static_cast<int>(job->cpu_utilization);
        }
    }
    
    return utilization;
}

size_t JobScheduler::get_queue_length() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return job_queue_.size();
}

void JobScheduler::set_max_concurrent_jobs(size_t max_jobs) {
    max_concurrent_jobs_ = max_jobs;
}

void JobScheduler::enable_cpu_affinity(bool enable) {
    cpu_affinity_enabled_ = enable;
}

void JobScheduler::worker_function() {
    while (running_) {
        JobPtr job;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { 
                return !running_ || !job_queue_.empty(); 
            });
            
            if (!running_) {
                return;
            }
            
            if (!job_queue_.empty() && active_jobs_.size() < max_concurrent_jobs_) {
                job = job_queue_.top();
                job_queue_.pop();
                active_jobs_.push_back(job);
            }
        }
        
        if (job) {
            execute_job(job);
            
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::find(active_jobs_.begin(), active_jobs_.end(), job);
            if (it != active_jobs_.end()) {
                active_jobs_.erase(it);
                if (job->status == JobStatus::COMPLETED || 
                    job->status == JobStatus::FAILED || 
                    job->status == JobStatus::KILLED) {
                    completed_jobs_.push_back(job);
                    if (job->status == JobStatus::COMPLETED) {
                        stats_.total_jobs_completed++;
                    } else if (job->status == JobStatus::FAILED) {
                        stats_.total_jobs_failed++;
                    }
                    update_job_dependencies(job);
                }
            }
        }
    }
}

void JobScheduler::execute_job(JobPtr job) {
    job->status = JobStatus::RUNNING;
    job->thread_id = std::this_thread::get_id();
    job->start_time = std::chrono::system_clock::now();
    job->assigned_core_id = assign_core();
    
    // Simulate resource usage
    simulate_cpu_usage(job);
    simulate_memory_usage(job);
    
    Logger::instance().log_job_started(job);
    
    // Fork and execute the command
    pid_t pid = fork();
    
    if (pid == 0) {  // Child process
        // Execute the command
        execl("/bin/sh", "sh", "-c", job->command.c_str(), nullptr);
        exit(1);  // If execl fails
    } else if (pid > 0) {  // Parent process
        job->process_id = pid;
        int status;
        waitpid(pid, &status, 0);
        
        job->end_time = std::chrono::system_clock::now();
        job->actual_runtime = std::chrono::duration_cast<std::chrono::milliseconds>(
            job->end_time - job->start_time);
        
        if (WIFEXITED(status)) {
            job->exit_code = WEXITSTATUS(status);
            job->status = (job->exit_code == 0) ? JobStatus::COMPLETED : JobStatus::FAILED;
        } else {
            job->status = JobStatus::FAILED;
            job->exit_code = -1;
        }
        
        if (job->status == JobStatus::COMPLETED) {
            Logger::instance().log_job_completed(job);
        } else {
            Logger::instance().log_job_failed(job);
        }
        
        release_core(job->assigned_core_id);
    } else {  // Fork failed
        job->status = JobStatus::FAILED;
        job->exit_code = -1;
        Logger::instance().log_job_failed(job);
        release_core(job->assigned_core_id);
    }
}

int JobScheduler::assign_core() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < core_availability_.size(); ++i) {
        if (core_availability_[i]) {
            core_availability_[i] = false;
            core_last_used_[i] = std::chrono::system_clock::now();
            return static_cast<int>(i);
        }
    }
    return -1;  // No cores available
}

std::vector<int> JobScheduler::assign_multiple_cores(int count) {
    std::vector<int> assigned_cores;
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (size_t i = 0; i < core_availability_.size() && assigned_cores.size() < static_cast<size_t>(count); ++i) {
        if (core_availability_[i]) {
            core_availability_[i] = false;
            core_last_used_[i] = std::chrono::system_clock::now();
            assigned_cores.push_back(static_cast<int>(i));
        }
    }
    
    return assigned_cores;
}

void JobScheduler::release_core(int core_id) {
    if (core_id >= 0 && core_id < static_cast<int>(core_availability_.size())) {
        std::lock_guard<std::mutex> lock(mutex_);
        core_availability_[core_id] = true;
    }
}

void JobScheduler::release_cores(const std::vector<int>& core_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int core_id : core_ids) {
        if (core_id >= 0 && core_id < static_cast<int>(core_availability_.size())) {
            core_availability_[core_id] = true;
        }
    }
}

bool JobScheduler::can_schedule_job(const JobPtr& job) const {
    return job->dependencies_satisfied(all_jobs_) && 
           active_jobs_.size() < max_concurrent_jobs_;
}

void JobScheduler::check_and_schedule_dependencies() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& job : all_jobs_) {
        if (job->status == JobStatus::WAITING_DEPS && 
            job->dependencies_satisfied(all_jobs_)) {
            job->status = JobStatus::PENDING;
            job_queue_.push(job);
        }
    }
}

void JobScheduler::update_job_dependencies(JobPtr completed_job) {
    // Check if any waiting jobs can now be scheduled
    for (auto& job : all_jobs_) {
        if (job->status == JobStatus::WAITING_DEPS) {
            if (job->dependencies.count(completed_job->job_id) > 0 && 
                job->dependencies_satisfied(all_jobs_)) {
                job->status = JobStatus::PENDING;
                job_queue_.push(job);
            }
        }
    }
}

JobPtr JobScheduler::parse_job_script(const std::string& script_path) {
    std::ifstream file(script_path);
    if (!file) {
        throw std::runtime_error("Failed to open job script: " + script_path);
    }
    
    std::string job_name;
    JobPriority priority = JobPriority::MEDIUM;
    std::string command;
    size_t memory_limit = 1024;
    int runtime_limit = 3600;
    int cores = 1;
    std::vector<int> dependencies;
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] != '#') {
            command = line;
            break;
        }
        
        if (line.find("JOB_NAME:") != std::string::npos) {
            job_name = line.substr(line.find(":") + 1);
            job_name.erase(0, job_name.find_first_not_of(" \t"));
            job_name.erase(job_name.find_last_not_of(" \t") + 1);
        } else if (line.find("PRIORITY:") != std::string::npos) {
            std::string pri = line.substr(line.find(":") + 1);
            pri.erase(0, pri.find_first_not_of(" \t"));
            pri.erase(pri.find_last_not_of(" \t") + 1);
            
            if (pri == "HIGH") priority = JobPriority::HIGH;
            else if (pri == "LOW") priority = JobPriority::LOW;
            else if (pri == "CRITICAL") priority = JobPriority::CRITICAL;
        } else if (line.find("MEMORY_LIMIT:") != std::string::npos) {
            std::string mem = line.substr(line.find(":") + 1);
            mem.erase(0, mem.find_first_not_of(" \t"));
            memory_limit = std::stoul(mem);
        } else if (line.find("RUNTIME_LIMIT:") != std::string::npos) {
            std::string time = line.substr(line.find(":") + 1);
            time.erase(0, time.find_first_not_of(" \t"));
            runtime_limit = std::stoi(time);
        } else if (line.find("CORES:") != std::string::npos) {
            std::string core_str = line.substr(line.find(":") + 1);
            core_str.erase(0, core_str.find_first_not_of(" \t"));
            cores = std::stoi(core_str);
        } else if (line.find("DEPENDENCIES:") != std::string::npos) {
            std::string deps = line.substr(line.find(":") + 1);
            deps.erase(0, deps.find_first_not_of(" \t"));
            
            std::stringstream ss(deps);
            std::string dep;
            while (std::getline(ss, dep, ',')) {
                dep.erase(0, dep.find_first_not_of(" \t"));
                dep.erase(dep.find_last_not_of(" \t") + 1);
                dependencies.push_back(std::stoi(dep));
            }
        }
    }
    
    if (command.empty()) {
        throw std::runtime_error("No command found in job script: " + script_path);
    }
    
    JobPtr job;
    if (!dependencies.empty()) {
        job = submit_job_with_deps(command, dependencies, priority);
    } else {
        job = submit_job(command, priority);
    }
    
    if (!job_name.empty()) {
        job->job_name = job_name;
    }
    
    job->limits.max_memory_mb = memory_limit;
    job->limits.max_runtime = std::chrono::seconds(runtime_limit);
    job->limits.max_cpu_cores = cores;
    job->type = JobType::BATCH;
    
    return job;
}

JobPtr JobScheduler::create_job_from_command(const std::string& command, JobPriority priority) {
    return submit_job(command, priority);
}

void JobScheduler::simulate_cpu_usage(JobPtr job) {
    // Simulate CPU usage based on command type
    std::string cmd = job->command;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
    
    if (cmd.find("sleep") != std::string::npos) {
        job->cpu_utilization = 5.0 + (rand() % 15);  // Low CPU for sleep
    } else if (cmd.find("find") != std::string::npos || cmd.find("grep") != std::string::npos) {
        job->cpu_utilization = 30.0 + (rand() % 40);  // I/O intensive
    } else if (cmd.find("make") != std::string::npos || cmd.find("compile") != std::string::npos) {
        job->cpu_utilization = 70.0 + (rand() % 30);  // CPU intensive
    } else {
        job->cpu_utilization = 25.0 + (rand() % 50);  // Default
    }
    
    job->context_switches = 100 + (rand() % 500);
}

void JobScheduler::simulate_memory_usage(JobPtr job) {
    // Simulate memory usage based on command complexity
    size_t base_memory = 10;  // Base 10MB
    
    std::string cmd = job->command;
    base_memory += cmd.length() / 10;  // Longer commands use more memory
    
    if (cmd.find("make") != std::string::npos) {
        base_memory *= 5;  // Compilation uses more memory
    }
    
    job->memory_usage_mb = std::min(base_memory, job->limits.max_memory_mb);
}

void JobScheduler::update_system_stats() {
    // Calculate average times and throughput
    if (completed_jobs_.empty()) return;
    
    double total_turnaround = 0.0;
    double total_wait = 0.0;
    
    for (const auto& job : completed_jobs_) {
        auto turnaround = std::chrono::duration_cast<std::chrono::milliseconds>(
            job->end_time - job->submit_time).count();
        auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(
            job->start_time - job->submit_time).count();
        
        total_turnaround += turnaround;
        total_wait += wait;
    }
    
    stats_.average_turnaround_time = total_turnaround / completed_jobs_.size();
    stats_.average_wait_time = total_wait / completed_jobs_.size();
    
    // Calculate throughput (jobs per minute)
    auto uptime = std::chrono::system_clock::now() - stats_.start_time;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(uptime).count();
    if (minutes > 0) {
        stats_.system_throughput = static_cast<double>(stats_.total_jobs_completed) / minutes;
    }
    
    // Update memory usage
    stats_.current_memory_usage_mb = 0;
    for (const auto& job : active_jobs_) {
        stats_.current_memory_usage_mb += job->memory_usage_mb;
    }
}

bool JobScheduler::is_job_ready_to_run(const JobPtr& job) const {
    return job->status == JobStatus::PENDING && 
           job->dependencies_satisfied(all_jobs_) &&
           active_jobs_.size() < max_concurrent_jobs_;
}

void JobScheduler::cleanup_completed_jobs() {
    // Keep only recent completed jobs to prevent memory growth
    const size_t max_completed = 1000;
    if (completed_jobs_.size() > max_completed) {
        completed_jobs_.erase(completed_jobs_.begin(), 
                            completed_jobs_.begin() + (completed_jobs_.size() - max_completed));
    }
}

void JobScheduler::log_scheduler_event(const std::string& event, const JobPtr& job) {
    // Log scheduler events for debugging
    std::cout << "[SCHEDULER] " << event;
    if (job) {
        std::cout << " (Job " << job->job_id << ")";
    }
    std::cout << std::endl;
}

} // namespace threadshell 