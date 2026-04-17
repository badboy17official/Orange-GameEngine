#include "engine/rhi/IRhiDevice.h"

#include <array>
#include <cctype>
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

bool parseBoolEnv(const char* value, bool fallback) noexcept {
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }

    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(value[0])));
    if (c == '0' || c == 'f' || c == 'n') {
        return false;
    }

    return true;
}

bool shouldEnableValidationLayers() noexcept {
#if defined(NDEBUG)
    constexpr bool kDebugDefault = false;
#else
    constexpr bool kDebugDefault = true;
#endif
    return parseBoolEnv(std::getenv("TPS_VK_VALIDATION"), kDebugDefault);
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                      VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                      void* userData) {
    (void)messageTypes;
    (void)userData;

    const char* severity = "INFO";
    if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0U) {
        severity = "ERROR";
    } else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0U) {
        severity = "WARN";
    } else if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0U) {
        severity = "VERBOSE";
    }

    if (callbackData != nullptr && callbackData->pMessage != nullptr) {
        std::cerr << "[RHI][Vulkan][Validation][" << severity << "] " << callbackData->pMessage << '\n';
    }

    return VK_FALSE;
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

        currentFrameSerial_ = 0;
        initialized_ = true;
        return true;
    }

    void shutdown() noexcept override {
        initialized_ = false;
        frameActive_ = false;
        frameSubmitted_ = false;

        scopes_.clear();
        completedFrames_.clear();

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

        destroyDebugMessenger();

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
        currentFrameSerial_ = 0;
        validationEnabled_ = false;
    }

    void beginFrame() noexcept override {
        if (!initialized_) {
            return;
        }

        if (!waitForPreviousFrame()) {
            return;
        }

        if (frameSubmitted_) {
            (void)resolveScopes();
            cacheCompletedFrame();
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
        ++currentFrameSerial_;
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

        vkCmdWriteTimestamp(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool_, scope.beginQuery);

        nextQuery_ += 2U;
        scopes_.push_back(scope);

        GpuTimestampToken token{};
        token.frameSerial = currentFrameSerial_;
        token.index = static_cast<std::uint32_t>(scopes_.size() - 1U);
        return token;
    }

    void endTimestampScope(GpuTimestampToken token) noexcept override {
        if (!initialized_ || !frameActive_ || !gpuTimestampsSupported_ || queryPool_ == VK_NULL_HANDLE) {
            return;
        }

        if (token.frameSerial != currentFrameSerial_ || token.index >= scopes_.size()) {
            return;
        }

        Scope& scope = scopes_[token.index];
        if (scope.ended) {
            return;
        }

        vkCmdWriteTimestamp(commandBuffer_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool_, scope.endQuery);
        scope.ended = true;
    }

    bool resolveTimestampScopeMs(GpuTimestampToken token, double& outMs) noexcept override {
        outMs = 0.0;

        if (!initialized_ || !gpuTimestampsSupported_ || queryPool_ == VK_NULL_HANDLE || token.index == GpuTimestampToken::kInvalidIndex) {
            return false;
        }

        if (tryResolveFromCompletedFrames(token, outMs)) {
            return true;
        }

        if (token.frameSerial != currentFrameSerial_ || !frameSubmitted_ || token.index >= scopes_.size()) {
            return false;
        }

        if (!resolveScopes()) {
            return false;
        }

        const Scope& scope = scopes_[token.index];
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

    struct CompletedFrame {
        std::uint32_t frameSerial = 0;
        std::vector<double> scopeMs;
        std::vector<bool> scopeResolved;
    };

    static constexpr std::uint32_t kMaxTimestampQueries = 8192U;
    static constexpr std::size_t kCompletedFrameHistory = 8U;

    bool createInstance() noexcept {
        const bool requestValidation = shouldEnableValidationLayers();

        std::vector<const char*> layers;
        std::vector<const char*> extensions;

        if (requestValidation) {
            if (hasInstanceLayer("VK_LAYER_KHRONOS_validation")) {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                validationEnabled_ = true;

                if (hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
                    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                } else {
                    std::cerr << "[RHI][Vulkan] Validation requested but VK_EXT_debug_utils is unavailable.\n";
                }
            } else {
                std::cerr << "[RHI][Vulkan] Validation requested but VK_LAYER_KHRONOS_validation is unavailable.\n";
            }
        }

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
        instanceInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
        instanceInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        instanceInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

        const VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance_);
        if (result != VK_SUCCESS) {
            std::cerr << "[RHI][Vulkan] vkCreateInstance failed: " << vkResultToString(result) << '\n';
            return false;
        }

        if (validationEnabled_ && !extensions.empty() && !createDebugMessenger()) {
            std::cerr << "[RHI][Vulkan] Validation layer enabled, but debug messenger setup failed.\n";
        }

        if (validationEnabled_) {
            std::cerr << "[RHI][Vulkan] Validation enabled (TPS_VK_VALIDATION).\n";
        }

        return true;
    }

    bool hasInstanceLayer(const char* layerName) const noexcept {
        std::uint32_t layerCount = 0U;
        if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) != VK_SUCCESS || layerCount == 0U) {
            return false;
        }

        std::vector<VkLayerProperties> properties(layerCount);
        if (vkEnumerateInstanceLayerProperties(&layerCount, properties.data()) != VK_SUCCESS) {
            return false;
        }

        for (const VkLayerProperties& property : properties) {
            if (std::string_view(property.layerName) == layerName) {
                return true;
            }
        }

        return false;
    }

    bool hasInstanceExtension(const char* extensionName) const noexcept {
        std::uint32_t extensionCount = 0U;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr) != VK_SUCCESS || extensionCount == 0U) {
            return false;
        }

        std::vector<VkExtensionProperties> properties(extensionCount);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, properties.data()) != VK_SUCCESS) {
            return false;
        }

        for (const VkExtensionProperties& property : properties) {
            if (std::string_view(property.extensionName) == extensionName) {
                return true;
            }
        }

        return false;
    }

    bool createDebugMessenger() noexcept {
        if (instance_ == VK_NULL_HANDLE) {
            return false;
        }

        auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (createFn == nullptr) {
            return false;
        }

        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = vkDebugMessageCallback;

        const VkResult result = createFn(instance_, &createInfo, nullptr, &debugMessenger_);
        if (result != VK_SUCCESS) {
            debugMessenger_ = VK_NULL_HANDLE;
            return false;
        }

        return true;
    }

    void destroyDebugMessenger() noexcept {
        if (instance_ == VK_NULL_HANDLE || debugMessenger_ == VK_NULL_HANDLE) {
            return;
        }

        auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyFn != nullptr) {
            destroyFn(instance_, debugMessenger_, nullptr);
        }
        debugMessenger_ = VK_NULL_HANDLE;
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

        if (!frameSubmitted_) {
            return true;
        }

        constexpr std::uint64_t kFenceTimeoutNs = 1'000'000'000ULL;
        const VkResult waitResult = vkWaitForFences(device_, 1U, &frameFence_, VK_TRUE, kFenceTimeoutNs);
        return waitResult == VK_SUCCESS;
    }

    bool resolveScopes() noexcept {
        if (nextQuery_ == 0U) {
            return true;
        }

        for (Scope& scope : scopes_) {
            if (!scope.ended || scope.resolved) {
                continue;
            }

            const std::array<std::uint64_t, 4> kUnavailable = {0U, 0U, 0U, 0U};
            std::array<std::uint64_t, 4> data = kUnavailable;

            const VkResult result = vkGetQueryPoolResults(
                device_,
                queryPool_,
                scope.beginQuery,
                2U,
                sizeof(data),
                data.data(),
                sizeof(std::uint64_t) * 2U,
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

            if (result == VK_NOT_READY) {
                continue;
            }

            if (result != VK_SUCCESS) {
                return false;
            }

            if (data[1] == 0U || data[3] == 0U) {
                continue;
            }

            const std::uint64_t beginTicks = data[0];
            const std::uint64_t endTicks = data[2];
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

    void cacheCompletedFrame() noexcept {
        if (!frameSubmitted_) {
            return;
        }

        CompletedFrame frame{};
        frame.frameSerial = currentFrameSerial_;
        frame.scopeMs.assign(scopes_.size(), 0.0);
        frame.scopeResolved.assign(scopes_.size(), false);

        for (std::size_t i = 0; i < scopes_.size(); ++i) {
            const Scope& scope = scopes_[i];
            if (!scope.resolved) {
                continue;
            }
            frame.scopeMs[i] = scope.ms;
            frame.scopeResolved[i] = true;
        }

        completedFrames_.push_back(std::move(frame));
        if (completedFrames_.size() > kCompletedFrameHistory) {
            completedFrames_.erase(completedFrames_.begin());
        }
    }

    bool tryResolveFromCompletedFrames(GpuTimestampToken token, double& outMs) const noexcept {
        for (auto it = completedFrames_.rbegin(); it != completedFrames_.rend(); ++it) {
            if (it->frameSerial != token.frameSerial) {
                continue;
            }

            const std::size_t index = static_cast<std::size_t>(token.index);
            if (index >= it->scopeResolved.size()) {
                return false;
            }

            if (!it->scopeResolved[index]) {
                return false;
            }

            outMs = it->scopeMs[index];
            return true;
        }

        return false;
    }

    bool initialized_ = false;
    bool frameActive_ = false;
    bool frameSubmitted_ = false;
    bool gpuTimestampsSupported_ = false;
    bool validationEnabled_ = false;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
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
    std::vector<CompletedFrame> completedFrames_;
    std::uint32_t nextQuery_ = 0;
    std::uint32_t currentFrameSerial_ = 0;
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
