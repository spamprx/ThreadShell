#include <algorithm>
#include <sstream>
#include <regex>
#include "../include/job.h"

namespace threadshell {

bool JobMetadata::dependencies_satisfied(const std::vector<std::shared_ptr<JobMetadata>>& all_jobs) const {
    for (int dep_id : dependencies) {
        auto it = std::find_if(all_jobs.begin(), all_jobs.end(),
            [dep_id](const JobPtr& job) { return job->job_id == dep_id; });
        
        if (it == all_jobs.end() || (*it)->status != JobStatus::COMPLETED) {
            return false;
        }
    }
    return true;
}

std::chrono::seconds JobMetadata::get_estimated_runtime() const {
    // Estimate runtime based on command complexity
    std::string cmd = command;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
    
    // Base runtime estimation
    int base_seconds = 5;
    
    // Adjust based on command patterns
    if (cmd.find("sleep") != std::string::npos) {
        // Extract sleep duration if present
        std::regex sleep_regex(R"(sleep\s+(\d+))");
        std::smatch matches;
        if (std::regex_search(cmd, matches, sleep_regex)) {
            return std::chrono::seconds(std::stoi(matches[1].str()));
        }
        return std::chrono::seconds(10);
    }
    
    if (cmd.find("for") != std::string::npos || cmd.find("while") != std::string::npos) {
        base_seconds *= 3;  // Loops take longer
    }
    
    if (cmd.find("find") != std::string::npos || cmd.find("grep") != std::string::npos) {
        base_seconds *= 2;  // I/O intensive
    }
    
    if (cmd.find("make") != std::string::npos || cmd.find("compile") != std::string::npos) {
        base_seconds *= 5;  // Compilation takes time
    }
    
    if (cmd.find("download") != std::string::npos || cmd.find("wget") != std::string::npos || cmd.find("curl") != std::string::npos) {
        base_seconds *= 4;  // Network operations
    }
    
    // Adjust for command length (longer commands typically more complex)
    base_seconds += command.length() / 20;
    
    return std::chrono::seconds(base_seconds);
}

double JobMetadata::calculate_priority_score() const {
    double score = static_cast<double>(priority);
    
    // Boost score for shorter estimated jobs (shortest job first tie-breaker)
    auto estimated_runtime = get_estimated_runtime();
    double runtime_factor = 1.0 / (1.0 + estimated_runtime.count() / 60.0);  // Normalize by minutes
    score += runtime_factor * 0.1;
    
    // Boost score for jobs waiting longer
    auto wait_time = std::chrono::system_clock::now() - submit_time;
    double wait_minutes = std::chrono::duration_cast<std::chrono::minutes>(wait_time).count();
    score += wait_minutes * 0.01;  // Small boost for aging
    
    // Penalty for jobs with unsatisfied dependencies
    if (status == JobStatus::WAITING_DEPS) {
        score -= 1.0;
    }
    
    // Boost for interactive jobs
    if (type == JobType::INTERACTIVE) {
        score += 0.2;
    }
    
    // Boost for critical priority
    if (priority == JobPriority::CRITICAL) {
        score += 2.0;
    }
    
    return score;
}

} // namespace threadshell 