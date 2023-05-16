
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <nvh/fileoperations.hpp>
#include <nvvk/context_vk.hpp>
#include <nvvk/error_vk.hpp>
#include <nvvk/resourceallocator_vk.hpp>
#include <nvvk/structs_vk.hpp> // 处理各类结构体
#include <nvvk/shaders_vk.hpp>


static const uint64_t render_width = 800;
static const uint64_t render_height = 600;
static const uint32_t workgroup_width = 16;
static const uint32_t workgroup_height = 8;

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

    // debug
    deviceInfo.addDeviceExtension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
    VkValidationFeaturesEXT validationInfo = nvvk::make<VkValidationFeaturesEXT>();
    VkValidationFeatureEnableEXT validationFeatureToEnable = VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT;
    validationInfo.enabledValidationFeatureCount = 1;
    validationInfo.pEnabledValidationFeatures = &validationFeatureToEnable;
    deviceInfo.instanceCreateInfoExt = &validationInfo;

    #ifdef _WIN32
        _putenv_s("DEBUG_PRINTF_TO_STDOUT", "1");
    #else  // If not _WIN32
        static char putenvString[] = "DEBUG_PRINTF_TO_STDOUT=1";
        putenv(putenvString);
    #endif  // _WIN32


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

    // 准备shader的情况
      const std::string        exePath(argv[0], std::string(argv[0]).find_last_of("/\\") + 1);
        std::vector<std::string> searchPaths = {exePath + PROJECT_RELDIRECTORY, exePath + PROJECT_RELDIRECTORY "..",
                                          exePath + PROJECT_RELDIRECTORY "../..", exePath + PROJECT_NAME};



    // prepare fill memory
    VkCommandPoolCreateInfo cmdPoolInfo = nvvk::make<VkCommandPoolCreateInfo>();
    cmdPoolInfo.queueFamilyIndex = context.m_queueGCT;

    VkCommandPool cmdPool;
    NVVK_CHECK(vkCreateCommandPool(context, &cmdPoolInfo, nullptr, &cmdPool));

    // 读取shader 创建pipeline
    VkShaderModule rayTraceModule = 
        nvvk::createShaderModule(context, nvh::loadFile("shaders/raytrace.comp.glsl.spv", true, searchPaths));
    
    // 确定具体的shader module情况
    VkPipelineShaderStageCreateInfo shaderStageCreateInfo = nvvk::make<VkPipelineShaderStageCreateInfo>();
    // shader stage type
    shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageCreateInfo.module = rayTraceModule;
    // shader entrypoint
    shaderStageCreateInfo.pName = "main";

    // 空的pipeline layout 乜有什么附加资源
      // For the moment, create an empty pipeline layout. You can ignore this code
  // for now; we'll replace it in the next chapter.
  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = nvvk::make<VkPipelineLayoutCreateInfo>();
  pipelineLayoutCreateInfo.setLayoutCount             = 0;
  pipelineLayoutCreateInfo.pushConstantRangeCount     = 0;
  VkPipelineLayout pipelineLayout;
  NVVK_CHECK(vkCreatePipelineLayout(context, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

    // 创建一个pipeline
    VkComputePipelineCreateInfo pipelineCreateInfo = nvvk::make<VkComputePipelineCreateInfo>();
    pipelineCreateInfo.stage = shaderStageCreateInfo;
    pipelineCreateInfo.layout = pipelineLayout;
    VkPipeline computePipeline;
    NVVK_CHECK(vkCreateComputePipelines(
        context, VK_NULL_HANDLE,1, &pipelineCreateInfo,
        nullptr, &computePipeline
    ));

    

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

    // 填充buffer 一般用于clear或者初始化
    // const float fillValue = 0.5f;
    // const uint32_t& fillValueU32 = reinterpret_cast<const uint32_t&>(fillValue);
    // vkCmdFillBuffer(cmdBuffer, buffer.buffer, 0, bufferSizeByte, fillValueU32);
    
    // 等fill 操作完成后，gpu再读取，这里需要注意cmd的同步问题与barrier的功能


    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdDispatch(cmdBuffer,1 ,1 ,1);


    VkMemoryBarrier memoryBarrier = nvvk::make<VkMemoryBarrier>();
    // 保护从 transter 开始写入到
    memoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    // HOST READ 即cpu 可以读
    memoryBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer, 
        // VK_PIPELINE_STAGE_TRANSFER_BIT, 从transfer 到shader
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        1, &memoryBarrier,
        0, nullptr, 0, nullptr
        );
    // 这里cmd的命令
    // 1. fill buffer
    // 2. pipeline barrier

    NVVK_CHECK(vkEndCommandBuffer(cmdBuffer));

    // 命令处理完了，准备queue submit
    VkSubmitInfo submitInfo = nvvk::make<VkSubmitInfo>();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    NVVK_CHECK(vkQueueSubmit(context.m_queueGCT, 1, &submitInfo, VK_NULL_HANDLE));
    // await 
    NVVK_CHECK(vkQueueWaitIdle(context.m_queueGCT));

    
    // 从GPU传回CPU
    // todo 1. 所以buffer是开在gpu上的？ 取决于是不是独显，核显是在一起的，独显在cpu上 
    // map是把storge 映射到cpu raw上？可以这么理解，严格的说gpu 的vram需要有flags VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 但我们有host的一致性，所以可以理解为gpu
    void* data = allocator.map(buffer);
    stbi_write_hdr("./out.hdr", render_width, render_height, 3, reinterpret_cast<float *>(data));
    allocator.unmap(buffer);

    vkDestroyPipeline(context, computePipeline, nullptr);
    vkDestroyShaderModule(context, rayTraceModule, nullptr);
    vkDestroyPipelineLayout(context, pipelineLayout, nullptr);

    vkFreeCommandBuffers(context, cmdPool, 1, &cmdBuffer);
    vkDestroyCommandPool(context, cmdPool, nullptr);

    allocator.destroy(buffer);
    allocator.deinit();
    context.deinit();
} 