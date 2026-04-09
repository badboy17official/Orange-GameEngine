// This file declares the Profiler class, which is used for performance profiling.
// It includes methods like startProfile(), stopProfile(), and logResults.

#ifndef PROFILER_H
#define PROFILER_H

#include <string>
#include <chrono>
#include <iostream>
#include <unordered_map>

class Profiler {
public:
    void startProfile(const char*);
    void stopProfile(const char*);
    void logResults(const char*, double);

private:
    struct ProfileData {
        std::chrono::high_resolution_clock::time_point startTime;
        std::chrono::duration<double> totalDuration;
        int callCount;
    };

    std::unordered_map<std::string, ProfileData> profileMap;
};

#endif // PROFILER_H