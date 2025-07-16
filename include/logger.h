#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include "job.h"

namespace threadshell {

class Logger {
public:
    static Logger& instance();

    // Initialize the logger with a log file path
    void init(const std::string& log_file = "logs/job_log.csv");
    
    // Log job status changes
    void log_job_submitted(const JobPtr& job);
    void log_job_started(const JobPtr& job);
    void log_job_completed(const JobPtr& job);
    void log_job_failed(const JobPtr& job);
    void log_job_killed(const JobPtr& job);

    // Write CSV header
    void write_header();

private:
    Logger() = default;
    ~Logger();
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::ofstream log_file_;
    mutable std::mutex mutex_;
    bool initialized_ = false;

    // Helper function to write a log entry
    void write_log_entry(const JobPtr& job, const std::string& event);
    
    // Format timestamp for logging
    std::string format_timestamp(const std::chrono::system_clock::time_point& tp) const;
    
    // Calculate job duration
    std::string calculate_duration(const JobPtr& job) const;
};

} // namespace threadshell 