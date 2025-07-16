#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "../include/shell.h"
#include "../include/logger.h"

namespace threadshell {

Shell::Shell() : scheduler_(std::make_unique<JobScheduler>()) {
    init_builtin_commands();
    Logger::instance().init();
    scheduler_->start();
    load_command_history();
    
    // Initialize current working directory
    char* cwd = getcwd(nullptr, 0);
    if (cwd) {
        current_working_dir_ = std::string(cwd);
        free(cwd);
    } else {
        current_working_dir_ = "/";
    }
    
    print_banner();
}

Shell::~Shell() {
    save_command_history();
    scheduler_->stop();
}

void Shell::run() {
    char* input;
    
    while (running_ && (input = readline(get_prompt().c_str())) != nullptr) {
        if (strlen(input) > 0) {
            add_history(input);
            
            // Clear any residual output from background jobs
            std::cout << "\033[2K\r"; // Clear current line
            
            parse_and_execute(input);
            
            // Add a clean separator after command execution
            std::string input_str(input);
            std::string first_word = input_str.substr(0, input_str.find(' '));
            if (!is_builtin_command(first_word)) {
                std::cout << std::flush;
            }
        }
        free(input);
    }
}

void Shell::init_builtin_commands() {
    builtin_commands_["help"] = [](Shell* shell, const std::string& args) {
        shell->show_help(args);
    };
    
    builtin_commands_["jobs"] = [](Shell* shell, const std::string& args) {
        shell->show_jobs(args);
    };
    
    builtin_commands_["jobinfo"] = [](Shell* shell, const std::string& args) {
        shell->show_job_details(args);
    };
    
    builtin_commands_["kill"] = [](Shell* shell, const std::string& args) {
        shell->kill_job(args);
    };
    
    builtin_commands_["suspend"] = [](Shell* shell, const std::string& args) {
        shell->suspend_job(args);
    };
    
    builtin_commands_["resume"] = [](Shell* shell, const std::string& args) {
        shell->resume_job(args);
    };
    
    builtin_commands_["priority"] = [](Shell* shell, const std::string& args) {
        shell->change_job_priority(args);
    };
    
    builtin_commands_["submit"] = [](Shell* shell, const std::string& args) {
        shell->submit_job_file(args);
    };
    
    builtin_commands_["stats"] = [](Shell* shell, const std::string&) {
        shell->show_system_stats();
    };
    
    builtin_commands_["cores"] = [](Shell* shell, const std::string&) {
        shell->show_core_utilization();
    };
    
    builtin_commands_["queue"] = [](Shell* shell, const std::string&) {
        shell->show_queue_status();
    };
    
    builtin_commands_["perf"] = [](Shell* shell, const std::string&) {
        shell->show_performance_summary();
    };
    
    builtin_commands_["policy"] = [](Shell* shell, const std::string& args) {
        shell->set_scheduling_policy(args);
    };
    
    builtin_commands_["config"] = [](Shell* shell, const std::string& args) {
        shell->configure_system(args);
    };
    
    builtin_commands_["export"] = [](Shell* shell, const std::string& args) {
        shell->export_job_data(args);
    };
    
    builtin_commands_["visualize"] = [](Shell* shell, const std::string& args) {
        shell->visualize_jobs(args);
    };
    
    // Add shell builtin commands
    builtin_commands_["cd"] = [](Shell* shell, const std::string& args) {
        shell->change_directory(args);
    };
    
    builtin_commands_["pwd"] = [](Shell* shell, const std::string&) {
        shell->print_working_directory();
    };
    
    builtin_commands_["exit"] = [](Shell* shell, const std::string&) {
        shell->running_ = false;
    };
    
    builtin_commands_["quit"] = builtin_commands_["exit"];
}

void Shell::parse_and_execute(const std::string& input) {
    std::string trimmed = trim_command(input);
    if (trimmed.empty()) {
        return;
    }
    
    // Check for built-in commands first
    std::string command = trimmed;
    std::string args;
    
    size_t space_pos = trimmed.find(' ');
    if (space_pos != std::string::npos) {
        command = trimmed.substr(0, space_pos);
        args = trim_command(trimmed.substr(space_pos + 1));
    }
    
    if (is_builtin_command(command)) {
        handle_builtin_command(trimmed);
        std::cout << std::endl; // Add spacing after builtin commands
        return;
    }
    
    // Check for command chains (cmd1 && cmd2)
    auto commands = parse_command_chain(trimmed);
    
    for (const auto& cmd : commands) {
        bool background = is_background_job(cmd);
        std::string actual_cmd = background ? 
            cmd.substr(0, cmd.length() - 1) : cmd;
        
        actual_cmd = trim_command(actual_cmd);
        if (!actual_cmd.empty()) {
            auto job = scheduler_->submit_job(actual_cmd, 
                background ? JobPriority::LOW : JobPriority::MEDIUM);
            
            // Visual feedback for job submission
            if (background) {
                std::cout << "\033[1;32m✓\033[0m Job submitted in background (ID: " 
                          << job->job_id << ")\n";
            } else {
                std::cout << "\033[1;34m→\033[0m Executing: " << actual_cmd << "\n";
                // Add a separator line for foreground jobs
                std::cout << "\033[2;37m" << std::string(50, '-') << "\033[0m\n";
                // Wait a brief moment for job execution output
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::cout << "\033[2;37m" << std::string(50, '-') << "\033[0m\n";
            }
        }
    }
}

std::vector<std::string> Shell::parse_command_chain(const std::string& input) {
    std::vector<std::string> commands;
    
    // Handle the simple case of a single command with &
    if (input.find("&&") == std::string::npos) {
        // No command chaining, just check for background
        commands.push_back(input);
        return commands;
    }
    
    // Handle command chaining with &&
    std::stringstream ss(input);
    std::string command;
    
    while (std::getline(ss, command, '&')) {
        if (command.empty()) continue;
        
        // Check if this was part of a && separator
        if (!commands.empty() && command[0] == '&') {
            command = command.substr(1);
        }
        
        command = trim_command(command);
        if (!command.empty()) {
            commands.push_back(command);
        }
    }
    
    return commands;
}

bool Shell::is_background_job(const std::string& command) {
    return !command.empty() && command.back() == '&';
}

void Shell::handle_builtin_command(const std::string& command) {
    std::string cmd_name = command;
    std::string args;
    
    size_t space_pos = command.find(' ');
    if (space_pos != std::string::npos) {
        cmd_name = command.substr(0, space_pos);
        args = trim_command(command.substr(space_pos + 1));
    }
    
    auto it = builtin_commands_.find(cmd_name);
    if (it != builtin_commands_.end()) {
        it->second(this, args);
    }
}

void Shell::show_jobs(const std::string& args) {
    std::vector<JobPtr> jobs;
    
    if (args == "active") {
        jobs = scheduler_->get_active_jobs();
    } else if (args == "completed") {
        jobs = scheduler_->get_completed_jobs();
    } else {
        jobs = scheduler_->get_jobs();
    }
    
    print_job_table(jobs, args == "detailed");
}

void Shell::show_job_details(const std::string& job_id) {
    try {
        int id = std::stoi(job_id);
        auto jobs = scheduler_->get_jobs();
        auto it = std::find_if(jobs.begin(), jobs.end(),
            [id](const JobPtr& job) { return job->job_id == id; });
        
        if (it != jobs.end()) {
            auto job = *it;
            std::cout << "\n=== Job Details ===\n";
            std::cout << "  ID: " << job->job_id << "\n";
            std::cout << "  Name: " << (job->job_name.empty() ? "N/A" : job->job_name) << "\n";
            std::cout << "  Command: " << job->command << "\n";
            std::cout << "  Status: ";
            print_colored_status(job->status, 0);
            std::cout << "\n";
            std::cout << "  Priority: " << format_priority(job->priority) << "\n";
            std::cout << "  Core: " << job->assigned_core_id << "\n";
            std::cout << "  Runtime: " << format_duration(job->actual_runtime) << "\n";
            std::cout << "  Memory Usage: " << job->memory_usage_mb << " MB\n";
            std::cout << "  CPU Utilization: " << job->cpu_utilization << "%\n";
            std::cout << "  Exit Code: " << job->exit_code << "\n";
            std::cout << std::endl;
        } else {
            std::cout << "Job " << id << " not found.\n";
        }
    } catch (const std::exception&) {
        std::cout << "Invalid job ID: " << job_id << "\n";
    }
}

void Shell::kill_job(const std::string& job_id) {
    try {
        int id = std::stoi(job_id);
        if (scheduler_->kill_job(id)) {
            std::cout << "Job " << id << " killed.\n";
        } else {
            std::cout << "Job " << id << " not found or cannot be killed.\n";
        }
    } catch (const std::exception&) {
        std::cout << "Invalid job ID: " << job_id << "\n";
    }
}

void Shell::suspend_job(const std::string& job_id) {
    try {
        int id = std::stoi(job_id);
        if (scheduler_->suspend_job(id)) {
            std::cout << "Job " << id << " suspended.\n";
        } else {
            std::cout << "Job " << id << " not found or cannot be suspended.\n";
        }
    } catch (const std::exception&) {
        std::cout << "Invalid job ID: " << job_id << "\n";
    }
}

void Shell::resume_job(const std::string& job_id) {
    try {
        int id = std::stoi(job_id);
        if (scheduler_->resume_job(id)) {
            std::cout << "Job " << id << " resumed.\n";
        } else {
            std::cout << "Job " << id << " not found or cannot be resumed.\n";
        }
    } catch (const std::exception&) {
        std::cout << "Invalid job ID: " << job_id << "\n";
    }
}

void Shell::change_job_priority(const std::string& args) {
    auto arg_list = split_args(args);
    if (arg_list.size() != 2) {
        std::cout << "Usage: priority <job_id> <priority>\n";
        std::cout << "Priorities: LOW, MEDIUM, HIGH, CRITICAL\n";
        return;
    }
    
    try {
        int id = std::stoi(arg_list[0]);
        JobPriority priority;
        
        if (arg_list[1] == "LOW") priority = JobPriority::LOW;
        else if (arg_list[1] == "MEDIUM") priority = JobPriority::MEDIUM;
        else if (arg_list[1] == "HIGH") priority = JobPriority::HIGH;
        else if (arg_list[1] == "CRITICAL") priority = JobPriority::CRITICAL;
        else {
            std::cout << "Invalid priority. Use: LOW, MEDIUM, HIGH, CRITICAL\n";
            return;
        }
        
        if (scheduler_->change_job_priority(id, priority)) {
            std::cout << "Job " << id << " priority changed to " << arg_list[1] << ".\n";
        } else {
            std::cout << "Job " << id << " not found or priority cannot be changed.\n";
        }
    } catch (const std::exception&) {
        std::cout << "Invalid job ID: " << arg_list[0] << "\n";
    }
}

void Shell::submit_job_file(const std::string& filename) {
    try {
        auto job = scheduler_->submit_job_script(filename);
        std::cout << "\033[1;32m✅ Job script submitted successfully!\033[0m\n";
        std::cout << "   📄 File: \033[1;36m" << filename << "\033[0m\n";
        std::cout << "   🆔 Job ID: \033[1;33m" << job->job_id << "\033[0m\n";
        if (!job->job_name.empty()) {
            std::cout << "   📝 Name: \033[1;37m" << job->job_name << "\033[0m\n";
        }
        std::cout << "   🔥 Priority: \033[1;35m" << format_priority(job->priority) << "\033[0m\n\n";
    } catch (const std::exception& e) {
        std::cout << "\033[1;31m❌ Error submitting job script:\033[0m " << e.what() << "\n\n";
    }
}

void Shell::submit_job_with_deps(const std::string& args) {
    std::cout << "Job dependencies feature: " << args << "\n";
    // TODO: Implement job dependencies
}

void Shell::submit_job_array(const std::string& args) {
    std::cout << "Job array feature: " << args << "\n";
    // TODO: Implement job arrays
}

void Shell::show_system_stats() {
    auto stats = scheduler_->get_system_stats();
    
    std::cout << "\n=== System Statistics ===\n";
    std::cout << "Total Jobs Submitted: " << stats.total_jobs_submitted << "\n";
    std::cout << "Total Jobs Completed: " << stats.total_jobs_completed << "\n";
    std::cout << "Total Jobs Failed: " << stats.total_jobs_failed << "\n";
    std::cout << "Total Jobs Killed: " << stats.total_jobs_killed << "\n";
    std::cout << "Average Turnaround Time: " << stats.average_turnaround_time << " ms\n";
    std::cout << "Average Wait Time: " << stats.average_wait_time << " ms\n";
    std::cout << "System Throughput: " << stats.system_throughput << " jobs/min\n";
    std::cout << "Current Memory Usage: " << stats.current_memory_usage_mb << " MB\n";
    std::cout << "Queue Length: " << scheduler_->get_queue_length() << "\n";
    std::cout << std::endl;
}

void Shell::show_core_utilization() {
    auto utilization = scheduler_->get_core_utilization();
    
    std::cout << "\n=== CPU Core Utilization ===\n";
    for (size_t i = 0; i < utilization.size(); ++i) {
        std::cout << "Core " << i << ": " << utilization[i] << "%\n";
    }
    std::cout << std::endl;
}

void Shell::show_queue_status() {
    std::cout << "\n=== Job Queue Status ===\n";
    std::cout << "Queue Length: " << scheduler_->get_queue_length() << "\n";
    std::cout << "Active Jobs: " << scheduler_->get_active_jobs().size() << "\n";
    std::cout << "Completed Jobs: " << scheduler_->get_completed_jobs().size() << "\n";
    std::cout << std::endl;
}

void Shell::show_performance_summary() {
    auto stats = scheduler_->get_system_stats();
    auto active_jobs = scheduler_->get_active_jobs();
    
    std::cout << "\n=== Performance Summary ===\n";
    std::cout << "System Uptime: ";
    auto uptime = std::chrono::system_clock::now() - stats.start_time;
    auto hours = std::chrono::duration_cast<std::chrono::hours>(uptime).count();
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(uptime).count() % 60;
    std::cout << hours << "h " << minutes << "m\n";
    
    std::cout << "Jobs/Hour: " << (stats.total_jobs_submitted * 3600.0) / 
                                  std::chrono::duration_cast<std::chrono::seconds>(uptime).count() << "\n";
    std::cout << "Success Rate: " << (stats.total_jobs_completed * 100.0) / 
                                     std::max(1UL, stats.total_jobs_submitted) << "%\n";
    std::cout << "Currently Running: " << active_jobs.size() << " jobs\n";
    std::cout << std::endl;
}

void Shell::set_scheduling_policy(const std::string& policy) {
    SchedulingPolicy new_policy;
    
    if (policy == "priority") new_policy = SchedulingPolicy::PRIORITY_FIRST;
    else if (policy == "shortest") new_policy = SchedulingPolicy::SHORTEST_JOB_FIRST;
    else if (policy == "roundrobin") new_policy = SchedulingPolicy::ROUND_ROBIN;
    else if (policy == "fairshare") new_policy = SchedulingPolicy::FAIR_SHARE;
    else {
        std::cout << "Invalid scheduling policy. Available: priority, shortest, roundrobin, fairshare\n";
        return;
    }
    
    scheduler_->set_scheduling_policy(new_policy);
    std::cout << "Scheduling policy set to: " << policy << "\n";
}

void Shell::configure_system(const std::string& args) {
    auto arg_list = split_args(args);
    if (arg_list.size() != 2) {
        std::cout << "Usage: config <setting> <value>\n";
        std::cout << "Settings: max_jobs, cpu_affinity\n";
        return;
    }
    
    if (arg_list[0] == "max_jobs") {
        try {
            size_t max_jobs = std::stoul(arg_list[1]);
            scheduler_->set_max_concurrent_jobs(max_jobs);
            std::cout << "Maximum concurrent jobs set to: " << max_jobs << "\n";
        } catch (const std::exception&) {
            std::cout << "Invalid value for max_jobs: " << arg_list[1] << "\n";
        }
    } else if (arg_list[0] == "cpu_affinity") {
        bool enable = (arg_list[1] == "true" || arg_list[1] == "1" || arg_list[1] == "on");
        scheduler_->enable_cpu_affinity(enable);
        std::cout << "CPU affinity " << (enable ? "enabled" : "disabled") << "\n";
    } else {
        std::cout << "Unknown setting: " << arg_list[0] << "\n";
    }
}

void Shell::show_help(const std::string& topic) {
    if (topic.empty() || topic == "general") {
        show_general_help();
    } else if (topic == "jobs") {
        show_job_help();
    } else if (topic == "monitoring") {
        show_monitoring_help();
    } else if (topic == "advanced") {
        show_advanced_help();
    } else if (topic == "visualization") {
        show_visualization_help();
    } else {
        std::cout << "Unknown help topic: " << topic << "\n";
        std::cout << "Available topics: general, jobs, monitoring, advanced, visualization\n";
    }
}

void Shell::enable_watch_mode() {
    watch_mode_ = !watch_mode_;
    std::cout << "Watch mode " << (watch_mode_ ? "enabled" : "disabled") << "\n";
}

void Shell::show_job_logs(const std::string& job_id) {
    std::cout << "Job logs for " << job_id << " would be displayed here.\n";
}

void Shell::export_job_data(const std::string& format) {
    if (format == "csv") {
        std::cout << "Job data exported to logs/job_log.csv\n";
    } else if (format == "json") {
        std::cout << "JSON export not yet implemented.\n";
    } else {
        std::cout << "Supported formats: csv, json\n";
    }
}

void Shell::visualize_jobs(const std::string& args) {
    // Build the command to run the Python visualization script
    std::string command = "python3 visualize_jobs.py ";
    
    if (args.empty()) {
        // Default to showing all visualizations
        command += "--all";
    } else {
        // Parse and validate arguments
        std::vector<std::string> valid_args = {
            "--all", "--gantt", "--dashboard", "--report", 
            "-l", "--log-file", "-o", "--output-dir"
        };
        
        // For simplicity, just pass through the arguments
        // In production, you might want to validate them
        command += args;
    }
    
    std::cout << "\033[1;36m📊 Running visualization: \033[1;37m" << command << "\033[0m\n";
    std::cout << "\033[1;33mNote:\033[0m Ensure Python 3 with pandas, matplotlib, and seaborn are installed.\n";
    std::cout << "Install with: \033[1;36mpip install pandas matplotlib seaborn\033[0m\n\n";
    
    // Execute the command
    int result = system(command.c_str());
    
    if (result == 0) {
        std::cout << "\033[1;32m✅ Visualization completed successfully!\033[0m\n";
        std::cout << "Check the current directory for generated files.\n";
    } else {
        std::cout << "\033[1;31m❌ Visualization failed.\033[0m\n";
        std::cout << "Make sure Python 3 and required packages are installed.\n";
        std::cout << "Also ensure the visualize_jobs.py script is in the current directory.\n";
    }
}

void Shell::change_directory(const std::string& path) {
    std::string target_path = path.empty() ? getenv("HOME") : path;
    
    if (chdir(target_path.c_str()) == 0) {
        char* cwd = getcwd(nullptr, 0);
        if (cwd) {
            current_working_dir_ = std::string(cwd);
            std::cout << "\033[1;32m📁 Changed to: \033[1;36m" << current_working_dir_ << "\033[0m\n";
            free(cwd);
        }
    } else {
        std::cout << "\033[1;31m❌ Cannot change to directory: \033[0m" << target_path << "\n";
        std::cout << "   Reason: " << strerror(errno) << "\n";
    }
}

void Shell::print_working_directory() {
    char* cwd = getcwd(nullptr, 0);
    if (cwd) {
        std::cout << "\033[1;36m📍 Current directory: \033[1;37m" << cwd << "\033[0m\n";
        free(cwd);
    } else {
        std::cout << "\033[1;31m❌ Unable to get current directory\033[0m\n";
    }
}

std::string Shell::get_prompt() {
    // Get current working directory
    char* cwd = getcwd(nullptr, 0);
    std::string current_dir = "/";
    
    if (cwd) {
        current_dir = cwd;
        free(cwd);
        
        // Get just the directory name (basename)
        size_t last_slash = current_dir.find_last_of('/');
        if (last_slash != std::string::npos && last_slash < current_dir.length() - 1) {
            current_dir = current_dir.substr(last_slash + 1);
        } else if (current_dir == "/") {
            current_dir = "root";
        }
    }
    
    // Build the colored prompt with current directory
    std::string prompt = "\033[1;36m┌─[\033[1;32mThreadShell\033[1;36m]-[\033[1;34m";
    prompt += current_dir;
    prompt += "\033[1;36m]\n└─\033[1;32m$\033[0m ";
    
    return prompt;
}

// Utility functions
void Shell::print_banner() {
    std::cout << "\n";
    std::cout << "████████╗██╗  ██╗██████╗ ███████╗ █████╗ ██████╗ ███████╗██╗  ██╗███████╗██╗     ██╗\n";
    std::cout << "╚══██╔══╝██║  ██║██╔══██╗██╔════╝██╔══██╗██╔══██╗██╔════╝██║  ██║██╔════╝██║     ██║\n";
    std::cout << "   ██║   ███████║██████╔╝█████╗  ███████║██║  ██║███████╗███████║█████╗  ██║     ██║\n";
    std::cout << "   ██║   ██╔══██║██╔══██╗██╔══╝  ██╔══██║██║  ██║╚════██║██╔══██║██╔══╝  ██║     ██║\n";
    std::cout << "   ██║   ██║  ██║██║  ██║███████╗██║  ██║██████╔╝███████║██║  ██║███████╗███████╗███████╗\n";
    std::cout << "   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═════╝ ╚══════╝╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝\n";
    std::cout << "\n";
    std::cout << "              🚀 Multi-threaded Job Scheduler 🚀\n";
    std::cout << "                     Type 'help' for available commands\n\n";
}

void Shell::print_job_table(const std::vector<JobPtr>& jobs, bool detailed) {
    if (jobs.empty()) {
        std::cout << "\033[1;33m⚠ No jobs to display.\033[0m\n";
        return;
    }

    // Define column widths (visible characters, no color codes)
    const int id_w = 4;
    const int cmd_w = 28;
    const int status_w = 10;
    const int prio_w = 9; // MEDIUM etc.
    const int core_w = 4;
    const int run_w = 11; // e.g., 999h59m
    const int mem_w = 9;  // e.g., 16384MB
    const int cpu_w = 6;  // e.g., 100%

    std::vector<int> widths = {id_w, cmd_w, status_w, prio_w, core_w, run_w};
    if (detailed) {
        widths.push_back(mem_w);
        widths.push_back(cpu_w);
    }

    // Build horizontal border
    std::string border = "\033[1;36m+";
    for (int w : widths) {
        border += std::string(w, '-') + "+";
    }
    border += "\033[0m";

    // Header labels (plain text, will be colored)
    std::vector<std::string> headers = {"ID", "Command", "Status", "Priority", "Core", "Runtime"};
    if (detailed) {
        headers.push_back("Memory");
        headers.push_back("CPU%");
    }

    auto delim = []() { std::cout << "\033[1;36m|\033[0m"; };

    // Print border, header, border
    std::cout << "\n" << border << "\n";

    delim();
    for (size_t i = 0; i < headers.size(); ++i) {
        std::string h = "\033[1;32m" + headers[i] + "\033[0m";
        int pad = widths[i] - visible_length(headers[i]);
        int left = pad / 2, right = pad - left;
        std::cout << std::string(left, ' ') << h << std::string(right, ' ');
        delim();
    }
    std::cout << "\n" << border << "\n";

    // Print each job row
    for (const auto& job : jobs) {
        delim();
        // ID
        std::cout << std::setw(id_w) << std::right << job->job_id;
        delim();
        // Command (truncate)
        std::string cmd = truncate_text(job->command, cmd_w);
        std::cout << std::setw(cmd_w) << std::left << cmd;
        delim();
        // Status
        print_colored_status_fixed(job->status, status_w);
        delim();
        // Priority
        std::string prio = truncate_text(format_priority(job->priority), prio_w);
        std::cout << std::setw(prio_w) << std::left << prio;
        delim();
        // Core
        std::cout << std::setw(core_w) << std::right << job->assigned_core_id;
        delim();
        // Runtime
        std::string run = truncate_text(format_duration(job->actual_runtime), run_w);
        std::cout << std::setw(run_w) << std::left << run;
        if (detailed) {
            delim();
            std::string mem = truncate_text(std::to_string(job->memory_usage_mb) + "MB", mem_w);
            std::cout << std::setw(mem_w) << std::right << mem;
            delim();
            std::string cpu = truncate_text(std::to_string(job->cpu_utilization) + "%", cpu_w);
            std::cout << std::setw(cpu_w) << std::right << cpu;
        }
        delim();
        std::cout << "\n";
    }

    std::cout << border << "\n\033[2;37m Total: " << jobs.size() << " jobs displayed\033[0m\n\n";
}

// Helper function to truncate text to fit within column width
std::string Shell::truncate_text(const std::string& text, int max_width) {
    if (text.length() <= static_cast<size_t>(max_width)) {
        return text;
    }
    
    if (max_width < 4) {
        return text.substr(0, max_width);
    }
    
    return text.substr(0, max_width - 3) + "...";
}

// Helper function to center text within a given width
std::string Shell::center_text(const std::string& text, int width) {
    if (text.length() >= static_cast<size_t>(width)) {
        return truncate_text(text, width);
    }
    
    int padding = width - text.length();
    int left_pad = padding / 2;
    int right_pad = padding - left_pad;
    
    return std::string(left_pad, ' ') + text + std::string(right_pad, ' ');
}

// Improved status printing with fixed width handling
void Shell::print_colored_status_fixed(JobStatus status, int width) {
    std::string status_text;
    std::string color_code;
    
    switch (status) {
        case JobStatus::PENDING:
            color_code = "\033[33m";  // Yellow
            status_text = "PENDING";
            break;
        case JobStatus::RUNNING:
            color_code = "\033[32m";  // Green
            status_text = "RUNNING";
            break;
        case JobStatus::COMPLETED:
            color_code = "\033[34m";  // Blue
            status_text = "DONE";
            break;
        case JobStatus::FAILED:
            color_code = "\033[31m";  // Red
            status_text = "FAILED";
            break;
        case JobStatus::KILLED:
            color_code = "\033[35m";  // Magenta
            status_text = "KILLED";
            break;
        case JobStatus::SUSPENDED:
            color_code = "\033[36m";  // Cyan
            status_text = "SUSPEND";
            break;
        case JobStatus::WAITING_DEPS:
            color_code = "\033[37m";  // White
            status_text = "WAITING";
            break;
    }
    
    // Truncate if necessary and center the text
    status_text = truncate_text(status_text, width);
    std::string centered = center_text(status_text, width);
    
    std::cout << color_code << centered << "\033[0m";
}

void Shell::print_colored_status(JobStatus status, int width) {
    std::string status_text;
    std::string color_code;
    
    switch (status) {
        case JobStatus::PENDING:
            color_code = "\033[33m";  // Yellow
            status_text = "PENDING";
            break;
        case JobStatus::RUNNING:
            color_code = "\033[32m";  // Green
            status_text = "RUNNING";
            break;
        case JobStatus::COMPLETED:
            color_code = "\033[34m";  // Blue
            status_text = "DONE";
            break;
        case JobStatus::FAILED:
            color_code = "\033[31m";  // Red
            status_text = "FAILED";
            break;
        case JobStatus::KILLED:
            color_code = "\033[35m";  // Magenta
            status_text = "KILLED";
            break;
        case JobStatus::SUSPENDED:
            color_code = "\033[36m";  // Cyan
            status_text = "SUSPEND";
            break;
        case JobStatus::WAITING_DEPS:
            color_code = "\033[37m";  // White
            status_text = "WAITING";
            break;
    }
    
    // Format with proper width and then apply color
    if (width > 0) {
        std::cout << color_code << std::setw(width) << std::right << status_text << "\033[0m";
    } else {
        std::cout << color_code << status_text << "\033[0m";
    }
}

std::string Shell::format_duration(std::chrono::milliseconds duration) {
    if (duration.count() == 0) return "0s";
    
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    if (seconds.count() < 60) {
        return std::to_string(seconds.count()) + "s";
    }
    
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    if (minutes.count() < 60) {
        return std::to_string(minutes.count()) + "m" + 
               std::to_string(seconds.count() % 60) + "s";
    }
    
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    return std::to_string(hours.count()) + "h" + 
           std::to_string(minutes.count() % 60) + "m";
}

std::string Shell::format_priority(JobPriority priority) {
    switch (priority) {
        case JobPriority::LOW: return "LOW";
        case JobPriority::MEDIUM: return "MEDIUM";
        case JobPriority::HIGH: return "HIGH";
        case JobPriority::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

std::string Shell::trim_command(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

bool Shell::is_builtin_command(const std::string& command) {
    return builtin_commands_.find(command) != builtin_commands_.end();
}

std::vector<std::string> Shell::split_args(const std::string& args) {
    std::vector<std::string> result;
    std::stringstream ss(args);
    std::string item;
    
    while (ss >> item) {
        result.push_back(item);
    }
    
    return result;
}

// Helper to compute visible length of string (excluding ANSI escape codes)
int Shell::visible_length(const std::string& text) {
    bool in_escape = false;
    int len = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (in_escape) {
            if (c == 'm') {
                in_escape = false;
            }
        } else {
            if (c == '\033') {
                in_escape = true;
            } else {
                ++len;
            }
        }
    }
    return len;
}

// Print a row with content centered across the entire table width (no columns)
void Shell::print_center_row(const std::string& text, int table_width) {
    int visible = visible_length(text);
    int padding = std::max(0, table_width - visible);
    int left_pad = padding / 2;
    int right_pad = padding - left_pad;
    std::cout << "\033[1;36m|\033[0m" << std::string(left_pad, ' ') << text << std::string(right_pad, ' ') << "\033[1;36m|\033[0m\n";
}

// Print a row with two columns (command + description) with wrapping support
void Shell::print_help_row(const std::string& col1, const std::string& col2, int col1_width, int table_width) {
    const int space_between = 2; // spaces between columns
    const int col2_width = table_width - col1_width - space_between - 1; // 1 for leading space after border

    std::string remaining = col2;
    bool first = true;
    while (true) {
        std::string part;
        if (static_cast<int>(remaining.size()) == 0) {
            if (first) part = ""; else break;
        } else if (visible_length(remaining) <= col2_width) {
            part = remaining;
            remaining.clear();
        } else {
            // find last space within col2_width visible chars
            int visible_count = 0;
            size_t idx = 0, last_space_idx = std::string::npos;
            for (; idx < remaining.size(); ++idx) {
                if (remaining[idx] == ' ') last_space_idx = idx;
                ++visible_count;
                if (visible_count >= col2_width) break;
            }
            if (last_space_idx == std::string::npos || last_space_idx == 0) {
                last_space_idx = idx;
            }
            part = remaining.substr(0, last_space_idx);
            remaining.erase(0, last_space_idx + 1);
        }

        // build row
        std::cout << "\033[1;36m|\033[0m ";
        if (first) {
            std::cout << col1;
            int pad = col1_width - visible_length(col1);
            if (pad > 0) std::cout << std::string(pad, ' ');
        } else {
            std::cout << std::string(col1_width, ' ');
        }
        std::cout << "  "; // two spaces between columns
        std::cout << part;
        int pad2 = col2_width - visible_length(part);
        if (pad2 > 0) std::cout << std::string(pad2, ' ');
        std::cout << "\033[1;36m|\033[0m\n";

        first = false;
        if (remaining.empty()) break;
    }
}

void Shell::show_general_help() {
    const int table_width = 78;
    const int cmd_width = 18;
    std::string border = "\n\033[1;36m+" + std::string(table_width, '-') + "+\033[0m\n";

    std::cout << border;
    print_center_row("\033[1;32mThreadShell General Commands\033[0m", table_width);
    std::cout << border;

    // General commands
    print_help_row("\033[1;33mhelp [topic]\033[0m", "Show help (topics: general, jobs, monitoring, advanced, visualization)", cmd_width, table_width);
    print_help_row("\033[1;33mexit, quit\033[0m", "Exit the shell", cmd_width, table_width);

    std::cout << border;
    print_center_row("\033[1;32mJob Execution\033[0m", table_width);
    std::cout << border;
    print_help_row("\033[1;33m<command>\033[0m", "Execute command in foreground", cmd_width, table_width);
    print_help_row("\033[1;33m<command> &\033[0m", "Execute command in background", cmd_width, table_width);
    print_help_row("\033[1;33mcmd1 && cmd2\033[0m", "Execute commands in sequence", cmd_width, table_width);

    std::cout << border;
    print_center_row("\033[1;32mShell Navigation\033[0m", table_width);
    std::cout << border;
    print_help_row("\033[1;33mcd [path]\033[0m", "Change directory", cmd_width, table_width);
    print_help_row("\033[1;33mpwd\033[0m", "Print working directory", cmd_width, table_width);
    std::cout << border << "\n";
}

void Shell::show_job_help() {
    const int table_width = 78;
    const int cmd_width = 32;
    std::string border = "\n\033[1;36m+" + std::string(table_width, '-') + "+\033[0m\n";

    std::cout << border;
    print_center_row("\033[1;32mThreadShell Job Management\033[0m", table_width);
    std::cout << border;

    // Job listing & control
    print_help_row("\033[1;33mjobs [active|completed|detailed]\033[0m", "List jobs", cmd_width, table_width);
    print_help_row("\033[1;33mjobinfo <id>\033[0m", "Show detailed job information", cmd_width, table_width);
    print_help_row("\033[1;33mkill <id>\033[0m", "Kill a running job", cmd_width, table_width);
    print_help_row("\033[1;33msuspend <id>\033[0m", "Suspend a running job", cmd_width, table_width);
    print_help_row("\033[1;33mresume <id>\033[0m", "Resume a suspended job", cmd_width, table_width);
    print_help_row("\033[1;33mpriority <id> <pri>\033[0m", "Change job priority (LOW/MEDIUM/HIGH/CRITICAL)", cmd_width, table_width);

    std::cout << border;
    print_center_row("\033[1;32mJob Submission\033[0m", table_width);
    std::cout << border;
    print_help_row("\033[1;33msubmit <script>\033[0m", "Submit a job script file", cmd_width, table_width);
    std::cout << border << "\n";
}

void Shell::show_monitoring_help() {
    const int table_width = 78;
    const int cmd_width = 20;
    std::string border = "\n\033[1;36m+" + std::string(table_width, '-') + "+\033[0m\n";

    std::cout << border;
    print_center_row("\033[1;32mThreadShell Monitoring\033[0m", table_width);
    std::cout << border;

    print_help_row("\033[1;33mstats\033[0m", "Show system statistics", cmd_width, table_width);
    print_help_row("\033[1;33mcores\033[0m", "Show CPU core utilization", cmd_width, table_width);
    print_help_row("\033[1;33mqueue\033[0m", "Show job queue status", cmd_width, table_width);
    print_help_row("\033[1;33mperf\033[0m", "Show performance summary", cmd_width, table_width);
    print_help_row("\033[1;33mexport <format>\033[0m", "Export job data (csv/json)", cmd_width, table_width);

    std::cout << border << "\n";
}

void Shell::show_advanced_help() {
    const int table_width = 78;
    const int cmd_width = 28;
    std::string border = "\n\033[1;36m+" + std::string(table_width, '-') + "+\033[0m\n";

    std::cout << border;
    print_center_row("\033[1;32mThreadShell Advanced Features\033[0m", table_width);
    std::cout << border;

    print_help_row("\033[1;33mpolicy <type>\033[0m", "Set scheduling policy (priority/shortest/roundrobin/fairshare)", cmd_width, table_width);
    print_help_row("\033[1;33mconfig <setting> <value>\033[0m", "Configure system settings (max_jobs, cpu_affinity)", cmd_width, table_width);

    std::cout << border;
    print_center_row("\033[1;32mJob Script Format\033[0m", table_width);
    std::cout << border;
    print_help_row("\033[1;33m# JOB_NAME: name\033[0m", "Set job name", cmd_width, table_width);
    print_help_row("\033[1;33m# PRIORITY: HIGH\033[0m", "Set priority (LOW/MEDIUM/HIGH/CRITICAL)", cmd_width, table_width);
    print_help_row("\033[1;33m# MEMORY_LIMIT: 1024\033[0m", "Set memory limit in MB", cmd_width, table_width);
    print_help_row("\033[1;33m# RUNTIME_LIMIT: 300\033[0m", "Set runtime limit in seconds", cmd_width, table_width);
    print_help_row("\033[1;33m# CORES: 2\033[0m", "Request multiple cores", cmd_width, table_width);
    print_help_row("\033[1;33m# DEPENDENCIES: 1,2\033[0m", "Depend on jobs 1 and 2", cmd_width, table_width);
    print_help_row("\033[1;33mcommand\033[0m", "Command to execute", cmd_width, table_width);
    std::cout << border << "\n";
}

void Shell::show_visualization_help() {
    const int table_width = 78;
    const int cmd_width = 32;
    std::string border = "\n\033[1;36m+" + std::string(table_width, '-') + "+\033[0m\n";

    std::cout << border;
    print_center_row("\033[1;32mThreadShell Visualization\033[0m", table_width);
    std::cout << border;

    // Built-in visualization commands
    print_help_row("\033[1;33mvisualize [args]\033[0m", "Run Python visualization script (default: --all)", cmd_width, table_width);
    print_help_row("\033[1;33mexport csv\033[0m", "Export job data to CSV format", cmd_width, table_width);
    print_help_row("\033[1;33mstats\033[0m", "Show system statistics and performance metrics", cmd_width, table_width);
    print_help_row("\033[1;33mperf\033[0m", "Show performance summary", cmd_width, table_width);

    std::cout << border;
    print_center_row("\033[1;32mPython Visualization Tool\033[0m", table_width);
    std::cout << border;

    print_help_row("\033[1;33mpython3 visualize_jobs.py --all\033[0m", "Generate all visualizations (Gantt chart, dashboard, report)", cmd_width, table_width);
    print_help_row("\033[1;33mpython3 visualize_jobs.py --gantt\033[0m", "Generate Gantt chart of job execution timeline", cmd_width, table_width);
    print_help_row("\033[1;33mpython3 visualize_jobs.py --dashboard\033[0m", "Generate performance dashboard with metrics", cmd_width, table_width);
    print_help_row("\033[1;33mpython3 visualize_jobs.py --report\033[0m", "Generate text summary report", cmd_width, table_width);
    print_help_row("\033[1;33m-l <logfile>\033[0m", "Specify custom log file (default: logs/job_log.csv)", cmd_width, table_width);
    print_help_row("\033[1;33m-o <directory>\033[0m", "Specify output directory for generated files", cmd_width, table_width);

    std::cout << border;
    print_center_row("\033[1;32mExample Usage\033[0m", table_width);
    std::cout << border;

    print_help_row("\033[1;37m# Quick visualization (builtin)\033[0m", "visualize", cmd_width, table_width);
    print_help_row("\033[1;37m# Gantt chart only (builtin)\033[0m", "visualize --gantt", cmd_width, table_width);
    print_help_row("\033[1;37m# Direct Python script usage\033[0m", "python3 visualize_jobs.py --all", cmd_width, table_width);
    print_help_row("\033[1;37m# Custom log file and output\033[0m", "python3 visualize_jobs.py --gantt -l mylogs.csv -o ./charts", cmd_width, table_width);

    std::cout << border << "\n";
    std::cout << "\033[1;33mNote:\033[0m Requires Python 3 with pandas, matplotlib, and seaborn packages.\n";
    std::cout << "Install with: \033[1;36mpip install pandas matplotlib seaborn\033[0m\n\n";
}

void Shell::save_command_history() {
    const char* home = getenv("HOME");
    if (!home) return;
    
    std::string history_file = std::string(home) + "/.threadshell_history";
    
    // Save readline history to file
    if (write_history(history_file.c_str()) != 0) {
        // Silent failure - not critical
    }
}

void Shell::load_command_history() {
    const char* home = getenv("HOME");
    if (!home) return;
    
    std::string history_file = std::string(home) + "/.threadshell_history";
    
    // Load readline history from file
    if (read_history(history_file.c_str()) != 0) {
        // File might not exist yet - that's okay
    }
    
    // Set maximum history size
    stifle_history(1000);
}

std::vector<std::string> Shell::get_command_completions(const std::string& partial) {
    std::vector<std::string> completions;
    
    // Match against builtin commands
    for (const auto& pair : builtin_commands_) {
        const std::string& command = pair.first;
        if (command.find(partial) == 0) {  // Starts with partial
            completions.push_back(command);
        }
    }
    
    // Add some common system commands
    std::vector<std::string> common_commands = {
        "ls", "cat", "grep", "echo", "ps", "top", "htop", "find", "which", "man"
    };
    
    for (const std::string& cmd : common_commands) {
        if (cmd.find(partial) == 0) {
            completions.push_back(cmd);
        }
    }
    
    // Sort completions alphabetically
    std::sort(completions.begin(), completions.end());
    
    return completions;
}

std::vector<std::string> Shell::get_file_completions(const std::string& partial) {
    std::vector<std::string> completions;
    
    // Parse directory and filename from partial path
    std::string dir_path = ".";
    std::string filename_prefix = partial;
    
    size_t last_slash = partial.find_last_of('/');
    if (last_slash != std::string::npos) {
        dir_path = partial.substr(0, last_slash);
        filename_prefix = partial.substr(last_slash + 1);
        if (dir_path.empty()) dir_path = "/";
    }
    
    // Open directory
    DIR* dir = opendir(dir_path.c_str());
    if (!dir) return completions;
    
    // Read directory entries
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        
        // Skip hidden files unless explicitly requested
        if (filename[0] == '.' && filename_prefix[0] != '.') {
            continue;
        }
        
        // Check if filename matches prefix
        if (filename.find(filename_prefix) == 0) {
            std::string full_path = (dir_path == ".") ? filename : dir_path + "/" + filename;
            
            // Check if it's a directory and add trailing slash
            struct stat st;
            if (stat(full_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                completions.push_back(full_path + "/");
            } else {
                completions.push_back(full_path);
            }
        }
    }
    
    closedir(dir);
    
    // Sort completions alphabetically
    std::sort(completions.begin(), completions.end());
    
    return completions;
}

} 