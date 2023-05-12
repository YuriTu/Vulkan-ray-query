
#include <nvvk/context_vk.hpp>
#include <nvvk/error_vk.hpp>
#include <nvvk/resourceallocator_vk.hpp>
#include <nvvk/structs_vk.hpp> // 处理各类结构体


static const uint64_t render_width = 800;
static const uint64_t render_height = 600;

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

    // transfer operator destination buffer
    VkDeviceSize bufferSizeByte = render_width * render_height * 3 * sizeof(float);
    VkBufferCreateInfo bufferCreateInfo = nvvk::make<VkBufferCreateInfo>();
    bufferCreateInfo.size = bufferSizeByte;
    // 用途 storage transfer
    bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    // 这里不用VkImage的原因单纯是为了简单
    nvvk::Buffer buffer = allocator.createBuffer(
        bufferCreateInfo,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT // 可见性 cpu 可以访问
        | VK_MEMORY_PROPERTY_HOST_CACHED_BIT // cpu 可caches
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT // cpu gpu 共享性
    );


    // 从GPU传回CPU
    // todo 1. 所以buffer是开在gpu上的？ 取决于是不是独显，核显是在一起的，独显在cpu上 
    // map是把storge 映射到cpu raw上？no，gpu 的vram需要有flags VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    // void* data = allocator.map(buffer);
    // float* fltData = reinterpret_cast<float*>(data);
    // printf("First three elements: %f, %f, %f\n", fltData[0], fltData[1], fltData[2]);
    // allocator.unmap(buffer);

    // prepare fill memory
    VkCommandPoolCreateInfo cmdPoolInfo = nvvk::make<VkCommandPoolCreateInfo>();
    cmdPoolInfo.queueFamilyIndex = context.m_queueGCT;

    VkCommandPool cmdPool;
    NVVK_CHECK(vkCreateCommandPool(context, &cmdPoolInfo, nullptr, &cmdPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = nvvk::make<VkCommandBufferAllocateInfo>();
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = cmdPool;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmdBuffer;
    NVVK_CHECK(vkAllocateCommandBuffers(context, &cmdAllocInfo, &cmdBuffer));
    
    // start exec 
    VkCommandBufferBeginInfo beiginInfo = nvvk::make<VkCommandBufferBeginInfo>();
    beiginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    NVVK_CHECK(vkBeginCommandBuffer(cmdBuffer, &beiginInfo));

    

    


    allocator.destroy(buffer);
    allocator.deinit();
    context.deinit();
} 