#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include "scheduler.h"

namespace threadshell {

class Shell {
public:
    Shell();
    ~Shell();

    // Start the shell
    void run();

private:
    // Command parsing and execution
    void parse_and_execute(const std::string& input);
    std::vector<std::string> parse_command_chain(const std::string& input);
    bool is_background_job(const std::string& command);
    
    // Built-in commands
    void handle_builtin_command(const std::string& command);
    
    // Job management commands
    void show_jobs(const std::string& args = "");
    void show_job_details(const std::string& job_id);
    void kill_job(const std::string& job_id);
    void suspend_job(const std::string& job_id);
    void resume_job(const std::string& job_id);
    void change_job_priority(const std::string& args);
    
    // Job submission commands
    void submit_job_file(const std::string& filename);
    void submit_job_with_deps(const std::string& args);
    void submit_job_array(const std::string& args);
    
    // System monitoring commands
    void show_system_stats();
    void show_core_utilization();
    void show_queue_status();
    void show_performance_summary();
    
    // Configuration commands
    void set_scheduling_policy(const std::string& policy);
    void configure_system(const std::string& args);
    void show_help(const std::string& topic = "");
    
    // Interactive features
    void enable_watch_mode();
    void show_job_logs(const std::string& job_id);
    void export_job_data(const std::string& format);
    void visualize_jobs(const std::string& args);
    
    // Shell builtin commands
    void change_directory(const std::string& path);
    void print_working_directory();
    
    // Prompt generation
    std::string get_prompt();
    
    // Shell state
    bool running_ = true;
    std::unique_ptr<JobScheduler> scheduler_;
    bool watch_mode_ = false;
    std::string current_working_dir_;
    
    // Command history and completion
    std::vector<std::string> command_history_;
    void save_command_history();
    void load_command_history();
    
    // Built-in command handlers
    using CommandHandler = std::function<void(Shell*, const std::string&)>;
    std::unordered_map<std::string, CommandHandler> builtin_commands_;
    
    // Initialize built-in commands
    void init_builtin_commands();
    
    // Command string processing
    std::string trim_command(const std::string& command);
    bool is_builtin_command(const std::string& command);
    std::vector<std::string> split_args(const std::string& args);
    
    // Help system
    void show_general_help();
    void show_job_help();
    void show_monitoring_help();
    void show_advanced_help();
    void show_visualization_help();
    
    // Utility functions
    void print_banner();
    void print_job_table(const std::vector<JobPtr>& jobs, bool detailed = false);
    void print_colored_status(JobStatus status, int width = 0);
    void print_colored_status_fixed(JobStatus status, int width);
    std::string truncate_text(const std::string& text, int max_width);
    std::string center_text(const std::string& text, int width);
    std::string format_duration(std::chrono::milliseconds duration);
    std::string format_priority(JobPriority priority);
    
    // Auto-completion support
    std::vector<std::string> get_command_completions(const std::string& partial);
    std::vector<std::string> get_file_completions(const std::string& partial);

    void print_help_row(const std::string& col1, const std::string& col2, int col1_width, int table_width);
    void print_center_row(const std::string& text, int table_width);
    int visible_length(const std::string& text);
};

} // namespace threadshell 