#include "engine/rhi/IRhiDevice.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

#if defined(TPS_HAS_VULKAN)
#include <vulkan/vulkan.h>
#endif

namespace {
class NullRhiDevice final : public IRhiDevice {
public:
    bool initialize() noexcept override {
        frameActive_ = false;
        return true;
    }

    void shutdown() noexcept override {
        frameActive_ = false;
    }

    void beginFrame() noexcept override {
        frameActive_ = true;
    }

    void endFrame() noexcept override {
        frameActive_ = false;
    }

    const char* backendName() const noexcept override {
        return "null";
    }

    bool supportsGpuTimestamps() const noexcept override {
        return false;
    }

    GpuTimestampToken beginTimestampScope(const char* label) noexcept override {
        (void)label;
        return GpuTimestampToken{};
    }

    void endTimestampScope(GpuTimestampToken token) noexcept override {
        (void)token;
    }

    bool resolveTimestampScopeMs(GpuTimestampToken token, double& outMs) noexcept override {
        (void)token;
        outMs = 0.0;
        return false;
    }

private:
    bool frameActive_ = false;
};

#if defined(TPS_HAS_VULKAN)
const char* vkResultToString(VkResult result) noexcept {
    switch (result) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
#if defined(VK_ERROR_UNKNOWN)
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
#endif
        default:
            return "VK_ERROR_UNRECOGNIZED";
    }
}

class VulkanRhiDevice final : public IRhiDevice {
public:
    bool initialize() noexcept override {
        shutdown();

        if (!createInstance()) {
            shutdown();
            return false;
        }

        if (!pickPhysicalDevice()) {
            shutdown();
            return false;
        }

        if (!createLogicalDevice()) {
            shutdown();
            return false;
        }

        if (!createCommandObjects()) {
            shutdown();
            return false;
        }

        if (!createQueryPool()) {
            shutdown();
            return false;
        }

        initialized_ = true;
        return true;
    }

    void shutdown() noexcept override {
        initialized_ = false;
        frameActive_ = false;
        scopes_.clear();

        if (device_ != VK_NULL_HANDLE) {
            (void)vkDeviceWaitIdle(device_);
        }

        if (queryPool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device_, queryPool_, nullptr);
            queryPool_ = VK_NULL_HANDLE;
        }

        if (frameFence_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, frameFence_, nullptr);
            frameFence_ = VK_NULL_HANDLE;
        }

        if (commandPool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
            commandBuffer_ = VK_NULL_HANDLE;
        }

        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }

        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }

        physicalDevice_ = VK_NULL_HANDLE;
        queue_ = VK_NULL_HANDLE;
        queueFamilyIndex_ = 0;
        timestampPeriodNs_ = 1.0;
        gpuTimestampsSupported_ = false;
        nextQuery_ = 0;
        frameSubmitted_ = false;
    }

    void beginFrame() noexcept override {
        if (!initialized_) {
            return;
        }

        if (!waitForPreviousFrame()) {
            return;
        }

        if (vkResetFences(device_, 1U, &frameFence_) != VK_SUCCESS) {
            return;
        }

        if (vkResetCommandPool(device_, commandPool_, 0U) != VK_SUCCESS) {
            return;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer_, &beginInfo) != VK_SUCCESS) {
            return;
        }

        if (queryPool_ != VK_NULL_HANDLE) {
            vkCmdResetQueryPool(commandBuffer_, queryPool_, 0U, kMaxTimestampQueries);
        }

        scopes_.clear();
        nextQuery_ = 0;
        frameSubmitted_ = false;
        frameActive_ = true;
    }

    void endFrame() noexcept override {
        if (!initialized_ || !frameActive_) {
            return;
        }

        frameActive_ = false;

        if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS) {
            return;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1U;
        submitInfo.pCommandBuffers = &commandBuffer_;

        if (vkQueueSubmit(queue_, 1U, &submitInfo, frameFence_) != VK_SUCCESS) {
            return;
        }

        frameSubmitted_ = true;
        (void)waitForPreviousFrame();
    }

    const char* backendName() const noexcept override {
        return "vulkan";
    }

    bool supportsGpuTimestamps() const noexcept override {
        return initialized_ && gpuTimestampsSupported_;
    }

    GpuTimestampToken beginTimestampScope(const char* label) noexcept override {
        (void)label;

        if (!initialized_ || !frameActive_ || !gpuTimestampsSupported_ || queryPool_ == VK_NULL_HANDLE) {
            return GpuTimestampToken{};
        }

        if ((nextQuery_ + 1U) >= kMaxTimestampQueries) {
            return GpuTimestampToken{};
        }

        Scope scope{};
        scope.beginQuery = nextQuery_;
        scope.endQuery = nextQuery_ + 1U;

        vkCmdWriteTimestamp(
            commandBuffer_,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            queryPool_,
            scope.beginQuery);

        nextQuery_ += 2U;
        scopes_.push_back(scope);

        GpuTimestampToken token{};
        token.index = static_cast<std::uint32_t>(scopes_.size() - 1U);
        return token;
    }

    void endTimestampScope(GpuTimestampToken token) noexcept override {
        if (!initialized_ || !frameActive_ || !gpuTimestampsSupported_ || queryPool_ == VK_NULL_HANDLE) {
            return;
        }

        if (token.index >= scopes_.size()) {
            return;
        }

        Scope& scope = scopes_[token.index];
        if (scope.ended) {
            return;
        }

        vkCmdWriteTimestamp(
            commandBuffer_,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            queryPool_,
            scope.endQuery);

        scope.ended = true;
    }

    bool resolveTimestampScopeMs(GpuTimestampToken token, double& outMs) noexcept override {
        outMs = 0.0;

        if (!initialized_ || !gpuTimestampsSupported_ || queryPool_ == VK_NULL_HANDLE || token.index >= scopes_.size()) {
            return false;
        }

        Scope& scope = scopes_[token.index];
        if (scope.resolved) {
            outMs = scope.ms;
            return true;
        }

        if (!frameSubmitted_) {
            return false;
        }

        if (!resolveScopes()) {
            return false;
        }

        if (!scope.resolved) {
            return false;
        }

        outMs = scope.ms;
        return true;
    }

private:
    struct Scope {
        std::uint32_t beginQuery = 0;
        std::uint32_t endQuery = 0;
        bool ended = false;
        bool resolved = false;
        double ms = 0.0;
    };

    static constexpr std::uint32_t kMaxTimestampQueries = 8192U;

    bool createInstance() noexcept {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "TPS_Engine";
        appInfo.applicationVersion = VK_MAKE_VERSION(0U, 1U, 0U);
        appInfo.pEngineName = "TPS_Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(0U, 1U, 0U);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;

        const VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance_);
        if (result != VK_SUCCESS) {
            std::cerr << "[RHI][Vulkan] vkCreateInstance failed: " << vkResultToString(result) << '\n';
            return false;
        }

        return true;
    }

    bool pickPhysicalDevice() noexcept {
        std::uint32_t deviceCount = 0;
        const VkResult countResult = vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
        if (countResult != VK_SUCCESS) {
            std::cerr << "[RHI][Vulkan] vkEnumeratePhysicalDevices(count) failed: " << vkResultToString(countResult) << '\n';
            return false;
        }
        if (deviceCount == 0U) {
            std::cerr << "[RHI][Vulkan] No Vulkan physical device found.\n";
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        const VkResult listResult = vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
        if (listResult != VK_SUCCESS) {
            std::cerr << "[RHI][Vulkan] vkEnumeratePhysicalDevices(list) failed: " << vkResultToString(listResult) << '\n';
            return false;
        }

        VkPhysicalDevice fallbackDevice = VK_NULL_HANDLE;
        std::uint32_t fallbackQueueFamilyIndex = 0U;
        double fallbackTimestampPeriodNs = 1.0;

        for (VkPhysicalDevice candidate : devices) {
            std::uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            if (familyCount == 0U) {
                continue;
            }

            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

            for (std::uint32_t familyIndex = 0; familyIndex < familyCount; ++familyIndex) {
                const VkQueueFamilyProperties& props = families[familyIndex];
                const bool supportsTimestamp = props.timestampValidBits > 0U;
                const bool supportsQueue = (props.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0U;
                if (!supportsQueue) {
                    continue;
                }

                VkPhysicalDeviceProperties physicalProps{};
                vkGetPhysicalDeviceProperties(candidate, &physicalProps);

                if (supportsTimestamp) {
                    physicalDevice_ = candidate;
                    queueFamilyIndex_ = familyIndex;
                    timestampPeriodNs_ = static_cast<double>(physicalProps.limits.timestampPeriod);
                    gpuTimestampsSupported_ = true;
                    return true;
                }

                if (fallbackDevice == VK_NULL_HANDLE) {
                    fallbackDevice = candidate;
                    fallbackQueueFamilyIndex = familyIndex;
                    fallbackTimestampPeriodNs = static_cast<double>(physicalProps.limits.timestampPeriod);
                }
            }
        }

        if (fallbackDevice != VK_NULL_HANDLE) {
            physicalDevice_ = fallbackDevice;
            queueFamilyIndex_ = fallbackQueueFamilyIndex;
            timestampPeriodNs_ = fallbackTimestampPeriodNs;
            gpuTimestampsSupported_ = false;
            std::cerr << "[RHI][Vulkan] Selected device queue without timestamp support; GPU pass timings disabled.\n";
            return true;
        }

        std::cerr << "[RHI][Vulkan] No suitable graphics/compute queue family found.\n";
        return false;
    }

    bool createLogicalDevice() noexcept {
        const float queuePriority = 1.0f;

        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamilyIndex_;
        queueInfo.queueCount = 1U;
        queueInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1U;
        deviceInfo.pQueueCreateInfos = &queueInfo;

        const VkResult createDeviceResult = vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_);
        if (createDeviceResult != VK_SUCCESS) {
            std::cerr << "[RHI][Vulkan] vkCreateDevice failed: " << vkResultToString(createDeviceResult) << '\n';
            return false;
        }

        vkGetDeviceQueue(device_, queueFamilyIndex_, 0U, &queue_);
        if (queue_ == VK_NULL_HANDLE) {
            std::cerr << "[RHI][Vulkan] vkGetDeviceQueue returned null queue handle.\n";
            return false;
        }

        return queue_ != VK_NULL_HANDLE;
    }

    bool createCommandObjects() noexcept {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndex_;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        const VkResult commandPoolResult = vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_);
        if (commandPoolResult != VK_SUCCESS) {
            std::cerr << "[RHI][Vulkan] vkCreateCommandPool failed: " << vkResultToString(commandPoolResult) << '\n';
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool_;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1U;

        const VkResult allocateResult = vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer_);
        if (allocateResult != VK_SUCCESS) {
            std::cerr << "[RHI][Vulkan] vkAllocateCommandBuffers failed: " << vkResultToString(allocateResult) << '\n';
            return false;
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        const VkResult fenceResult = vkCreateFence(device_, &fenceInfo, nullptr, &frameFence_);
        if (fenceResult != VK_SUCCESS) {
            std::cerr << "[RHI][Vulkan] vkCreateFence failed: " << vkResultToString(fenceResult) << '\n';
            return false;
        }

        return true;
    }

    bool createQueryPool() noexcept {
        if (!gpuTimestampsSupported_) {
            queryPool_ = VK_NULL_HANDLE;
            return true;
        }

        VkQueryPoolCreateInfo queryInfo{};
        queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = kMaxTimestampQueries;

        const VkResult queryPoolResult = vkCreateQueryPool(device_, &queryInfo, nullptr, &queryPool_);
        if (queryPoolResult != VK_SUCCESS) {
            std::cerr << "[RHI][Vulkan] vkCreateQueryPool failed: " << vkResultToString(queryPoolResult) << '\n';
            return false;
        }

        return true;
    }

    bool waitForPreviousFrame() noexcept {
        if (frameFence_ == VK_NULL_HANDLE) {
            return false;
        }

        constexpr std::uint64_t kFenceTimeoutNs = 1'000'000'000ULL;
        const VkResult waitResult = vkWaitForFences(device_, 1U, &frameFence_, VK_TRUE, kFenceTimeoutNs);
        return waitResult == VK_SUCCESS;
    }

    bool resolveScopes() noexcept {
        if (nextQuery_ == 0U) {
            return true;
        }

        const std::size_t resultCount = static_cast<std::size_t>(nextQuery_);
        std::vector<std::uint64_t> queryData(resultCount, 0U);

        const VkResult result = vkGetQueryPoolResults(
            device_,
            queryPool_,
            0U,
            nextQuery_,
            queryData.size() * sizeof(std::uint64_t),
            queryData.data(),
            sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

        if (result != VK_SUCCESS) {
            return false;
        }

        for (Scope& scope : scopes_) {
            if (!scope.ended) {
                continue;
            }

            const std::size_t beginIndex = static_cast<std::size_t>(scope.beginQuery);
            const std::size_t endIndex = static_cast<std::size_t>(scope.endQuery);
            if (endIndex >= queryData.size() || beginIndex >= queryData.size()) {
                continue;
            }

            const std::uint64_t beginTicks = queryData[beginIndex];
            const std::uint64_t endTicks = queryData[endIndex];
            if (endTicks < beginTicks) {
                continue;
            }

            const double deltaTicks = static_cast<double>(endTicks - beginTicks);
            const double deltaNs = deltaTicks * timestampPeriodNs_;
            scope.ms = deltaNs * 1e-6;
            scope.resolved = true;
        }

        return true;
    }

    bool initialized_ = false;
    bool frameActive_ = false;
    bool frameSubmitted_ = false;
    bool gpuTimestampsSupported_ = false;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queueFamilyIndex_ = 0;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence frameFence_ = VK_NULL_HANDLE;
    VkQueryPool queryPool_ = VK_NULL_HANDLE;

    double timestampPeriodNs_ = 1.0;

    std::vector<Scope> scopes_;
    std::uint32_t nextQuery_ = 0;
};
#else
class VulkanRhiDevice final : public IRhiDevice {
public:
    bool initialize() noexcept override {
        return true;
    }

    void shutdown() noexcept override {
    }

    void beginFrame() noexcept override {
    }

    void endFrame() noexcept override {
    }

    const char* backendName() const noexcept override {
        return "vulkan_stub";
    }

    bool supportsGpuTimestamps() const noexcept override {
        return false;
    }

    GpuTimestampToken beginTimestampScope(const char* label) noexcept override {
        (void)label;
        return GpuTimestampToken{};
    }

    void endTimestampScope(GpuTimestampToken token) noexcept override {
        (void)token;
    }

    bool resolveTimestampScopeMs(GpuTimestampToken token, double& outMs) noexcept override {
        (void)token;
        outMs = 0.0;
        return false;
    }
};
#endif

RhiBackend parseRhiBackend(const char* value) noexcept {
    if (value == nullptr || value[0] == '\0') {
        return RhiBackend::VulkanStub;
    }

    const std::string_view backend(value);
    if (backend == "null" || backend == "none") {
        return RhiBackend::Null;
    }

    if (backend == "v" || backend == "vk" || backend == "vulkan" || backend == "vulkan_stub" || backend == "auto") {
        return RhiBackend::VulkanStub;
    }

    return RhiBackend::VulkanStub;
}
}  // namespace

std::unique_ptr<IRhiDevice> createRhiDevice(RhiBackend backend) noexcept {
    switch (backend) {
        case RhiBackend::VulkanStub:
            return std::make_unique<VulkanRhiDevice>();
        case RhiBackend::Null:
        default:
            return std::make_unique<NullRhiDevice>();
    }
}

std::unique_ptr<IRhiDevice> createRhiDeviceFromEnvironment() noexcept {
    const char* value = std::getenv("TPS_RHI_BACKEND");
    return createRhiDevice(parseRhiBackend(value));
}
