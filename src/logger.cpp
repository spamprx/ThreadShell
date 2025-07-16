#include <iomanip>
#include <sstream>
#include <filesystem>
#include "../include/logger.h"

namespace threadshell {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void Logger::init(const std::string& log_file) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return;
    }
    
    // Create logs directory if it doesn't exist
    std::filesystem::create_directories(std::filesystem::path(log_file).parent_path());
    
    log_file_.open(log_file, std::ios::out | std::ios::app);
    if (!log_file_) {
        throw std::runtime_error("Failed to open log file: " + log_file);
    }
    
    write_header();
    initialized_ = true;
}

void Logger::write_header() {
    log_file_ << "Timestamp,JobID,JobName,Command,Priority,Status,ThreadID,CoreID,Duration(ms),Event\n";
    log_file_.flush();
}

void Logger::log_job_submitted(const JobPtr& job) {
    write_log_entry(job, "SUBMITTED");
}

void Logger::log_job_started(const JobPtr& job) {
    write_log_entry(job, "STARTED");
}

void Logger::log_job_completed(const JobPtr& job) {
    write_log_entry(job, "COMPLETED");
}

void Logger::log_job_failed(const JobPtr& job) {
    write_log_entry(job, "FAILED");
}

void Logger::log_job_killed(const JobPtr& job) {
    write_log_entry(job, "KILLED");
}

void Logger::write_log_entry(const JobPtr& job, const std::string& event) {
    if (!initialized_) {
        throw std::runtime_error("Logger not initialized");
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    
    log_file_ << format_timestamp(now) << ","
              << job->job_id << ","
              << (job->job_name.empty() ? "-" : job->job_name) << ","
              << "\"" << job->command << "\","
              << static_cast<int>(job->priority) << ","
              << static_cast<int>(job->status) << ","
              << job->thread_id << ","
              << job->assigned_core_id << ","
              << calculate_duration(job) << ","
              << event << "\n";
    
    log_file_.flush();
}

std::string Logger::format_timestamp(const std::chrono::system_clock::time_point& tp) const {
    auto time = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()).count() % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms;
    
    return ss.str();
}

std::string Logger::calculate_duration(const JobPtr& job) const {
    if (job->start_time == std::chrono::system_clock::time_point() ||
        job->status == JobStatus::PENDING) {
        return "0";
    }
    
    auto end = (job->end_time != std::chrono::system_clock::time_point())
        ? job->end_time
        : std::chrono::system_clock::now();
    
    return std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            end - job->start_time).count());
}

} // namespace threadshell 