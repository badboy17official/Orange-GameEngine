#include "Profiler.h"
#include <chrono>
#include <iostream>

void Profiler::startProfile(const char* name) {
    auto& data = profileMap[name];
    data.startTime = std::chrono::high_resolution_clock::now();
    data.callCount++;
}

void Profiler::stopProfile(const char* name) {
    auto end = std::chrono::high_resolution_clock::now();
    auto& data = profileMap[name];
    auto duration = end - data.startTime;
    data.totalDuration += duration;
    logResults(name, std::chrono::duration<double>(duration).count());
}

void Profiler::logResults(const char* name, double duration) {
    std::cout << "Profile [" << name << "]: " << duration << " seconds\n";
}