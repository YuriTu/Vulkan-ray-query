
#include <nvvk/context_vk.hpp>
#include <nvvk/structs_vk.hpp> // 处理各类结构体
#include <nvvk/resourceallocator_vk.hpp>

int main(int argc, const char** argv)
{
    nvvk::ContextCreateInfo deviceInfo;
    deviceInfo.apiMajor = 1;
    deviceInfo.apiMinor = 2;
    // ray query需要
    deviceInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    // 确定具体功能
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures = nvvk::make<VkPhysicalDeviceAccelerationStructureFeaturesKHR>();
    // optional == false 不是可选khr，是require
    deviceInfo.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &asFeatures);

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = nvvk::make<VkPhysicalDeviceRayQueryFeaturesKHR>();
    deviceInfo.addDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, false, &rayQueryFeatures);


    nvvk::Context context;
    context.init(deviceInfo);
    // asFeature中包括各种Feature的支持情况
    assert(asFeatures.accelerationStructure == VK_TRUE && rayQueryFeatures.rayQuery == VK_TRUE);

    // memory allocator
    nvvk::ResourceAllocatorDedicated allocator;
    // 做了隐式转换，所以type看来其对不上
    allocator.init(context, context.m_physicalDevice);



    context.deinit();

    
} 