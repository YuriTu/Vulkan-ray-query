

#include <array>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <nvh/fileoperations.hpp>
#include <nvvk/context_vk.hpp>
#include <nvvk/descriptorsets_vk.hpp>
#include <nvvk/error_vk.hpp>
#include <nvvk/raytraceKHR_vk.hpp>
#include <nvvk/resourceallocator_vk.hpp>
#include <nvvk/structs_vk.hpp> // 处理各类结构体
#include <nvvk/shaders_vk.hpp>


static const uint64_t render_width = 800;
static const uint64_t render_height = 600;
static const uint32_t workgroup_width = 16;
static const uint32_t workgroup_height = 8;

VkCommandBuffer AllocateAndBeiginOneTimeCommandBuffer(VkDevice device, VkCommandPool cmdPool)
{
    VkCommandBufferAllocateInfo cmdAllocInfo = nvvk::make<VkCommandBufferAllocateInfo>();
    cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool                 = cmdPool;
    cmdAllocInfo.commandBufferCount          = 1;
    VkCommandBuffer cmdBuffer;
    NVVK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuffer));
    VkCommandBufferBeginInfo beginInfo = nvvk::make<VkCommandBufferBeginInfo>();
    beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    NVVK_CHECK(vkBeginCommandBuffer(cmdBuffer, &beginInfo));
    return cmdBuffer;
}

void EndSubmitWaitAndFreeCommandBuffer(VkDevice device, VkQueue queue,VkCommandPool cmdPool, VkCommandBuffer &cmdBuffer)
{
    NVVK_CHECK(vkEndCommandBuffer(cmdBuffer));
    VkSubmitInfo submitInfo       = nvvk::make<VkSubmitInfo>();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmdBuffer;
    NVVK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    NVVK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
}

VkDeviceAddress GetBufferDeviceAddress(VkDevice device, VkBuffer buffer)
{
    VkBufferDeviceAddressInfo addressInfo = nvvk::make<VkBufferDeviceAddressInfo>();
    addressInfo.buffer = buffer;
    return vkGetBufferDeviceAddress(device, &addressInfo);
}

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
    // deviceInfo.addDeviceExtension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
    // VkValidationFeaturesEXT validationInfo = nvvk::make<VkValidationFeaturesEXT>();
    // VkValidationFeatureEnableEXT validationFeatureToEnable = VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT;
    // validationInfo.enabledValidationFeatureCount = 1;
    // validationInfo.pEnabledValidationFeatures = &validationFeatureToEnable;
    // deviceInfo.instanceCreateInfoExt = &validationInfo;

    // #ifdef _WIN32
    //     _putenv_s("DEBUG_PRINTF_TO_STDOUT", "1");
    // #else  // If not _WIN32
    //     static char putenvString[] = "DEBUG_PRINTF_TO_STDOUT=1";
    //     putenv(putenvString);
    // #endif  // _WIN32


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
    tinyobj::ObjReader render;
    render.ParseFromFile(nvh::findFile("scenes/CornellBox-Original-Merged.obj", searchPaths));
    assert(render.Valid());
    // 所有的vertex
    const std::vector<tinyobj::real_t> objVertices = render.GetAttrib().GetVertices();
    const std::vector<tinyobj::shape_t> objShapes = render.GetShapes();
    // 一个文件一个mesh
    assert(objShapes.size() == 1);
    const tinyobj::shape_t& objShape = objShapes[0];

    // 拿到index 列表 做了一下index_t -> uint32_t
    std::vector<uint32_t> objIndices;
    objIndices.reserve(objShape.mesh.indices.size());
    for(const tinyobj::index_t& index : objShape.mesh.indices)
    {
        objIndices.push_back(index.vertex_index);
    }

    // prepare data 准备command pool
    VkCommandPoolCreateInfo cmdPoolInfo = nvvk::make<VkCommandPoolCreateInfo>();
    cmdPoolInfo.queueFamilyIndex = context.m_queueGCT;

    VkCommandPool cmdPool;
    NVVK_CHECK(vkCreateCommandPool(context, &cmdPoolInfo, nullptr, &cmdPool));


    // 准备向gpu写入 vertex 数据
    nvvk::Buffer vertexBuffer, indexBuffer;
    {
        // 准备一个command buffer 专门用于upload vertex
        VkCommandBuffer uploadCmdBuffer = AllocateAndBeiginOneTimeCommandBuffer(context, cmdPool);
        // buffer usage 1. 在gpu 上，需要addr 2. storage 可读可写 3. 用于as 的构建
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                     | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        vertexBuffer = allocator.createBuffer(uploadCmdBuffer, objVertices, usage);
        indexBuffer = allocator.createBuffer(uploadCmdBuffer, objIndices, usage);

        EndSubmitWaitAndFreeCommandBuffer(context, context.m_queueGCT, cmdPool, uploadCmdBuffer);
        allocator.finalizeAndReleaseStaging();
    }

    // vertex 和 index 准备好了，则可以准备创建blas
    // blas有多个，所以是vector, input 类似于blass的constructor 
    // 1. VkAccelerationStructureGeometryTrianglesDataKHR 说明vertex 和index的信息
    // 2. VkAccelerationStructureGeometryKHR 第一个图元的情况 
    // 3. VkAccelerationStructureBuildRangeInfoKHR 数据使用范围
    std::vector<nvvk::RaytracingBuilderKHR::BlasInput> blases;
    {
        nvvk::RaytracingBuilderKHR::BlasInput blas;
        // 1. VkAccelerationStructureGeometryTrianglesDataKHR 需要获得
        // vertex index buffer的位置 才能拿到信息做as
        VkDeviceAddress vertexBufferAddress = GetBufferDeviceAddress(context, vertexBuffer.buffer);
        VkDeviceAddress indexBufferAddress = GetBufferDeviceAddress(context, indexBuffer.buffer);
        // 说明具体vertex的drawArrays 的mode 、 数据的格式
        VkAccelerationStructureGeometryTrianglesDataKHR triangles = nvvk::make<VkAccelerationStructureGeometryTrianglesDataKHR>();
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = vertexBufferAddress;
        // 步长 3个一组
        triangles.vertexStride                                    = 3 * sizeof(float);
        // vector.size()
        triangles.maxVertex                                       = static_cast<uint32_t>(objVertices.size() / 3 - 1);
        // index的格式
        triangles.indexType                                       = VK_INDEX_TYPE_UINT32;
        // index去哪找
        triangles.indexData.deviceAddress                         = indexBufferAddress;
        // 没有transform
        triangles.transformData.deviceAddress                     = 0; 


        // 2. VkAccelerationStructureGeometryKHR 说明图元的情况 0. 提供具体vertex data 的指针 1. 是三角形 2. 不透明 没有facedetect的问题
        VkAccelerationStructureGeometryKHR geometry = nvvk::make<VkAccelerationStructureGeometryKHR>();
        geometry.geometry.triangles                 = triangles;
        geometry.geometryType                       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags                              = VK_GEOMETRY_OPAQUE_BIT_KHR;
        blas.asGeometry.push_back(geometry);

        // 3. 说明数据范围，类似 gl.vertexAttribPointer 说明数据offset与stride
        VkAccelerationStructureBuildRangeInfoKHR offsetInfo;
        offsetInfo.firstVertex     = 0;
        offsetInfo.primitiveCount  = static_cast<uint32_t>(objIndices.size() / 3);  // Number of triangles
        offsetInfo.primitiveOffset = 0;
        offsetInfo.transformOffset = 0;
        blas.asBuildOffsetInfo.push_back(offsetInfo);

        blases.push_back(blas);
    }

    // 构建具体的blas
    nvvk::RaytracingBuilderKHR raytracingBuilder;
    raytracingBuilder.setup(context, &allocator, context.m_queueGCT);
    raytracingBuilder.buildBlas(blases, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

    // 构建tlas 目前只有一个
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    {
        VkAccelerationStructureInstanceKHR instance{};
        // instance pointer的对应blas
        instance.accelerationStructureReference = raytracingBuilder.getBlasDeviceAddress(0);
        // 不做transform 所以为1
        instance.transform.matrix[0][0] = instance.transform.matrix[1][1] = instance.transform.matrix[2][2] = 1.0f;
        // 查询intersection的部分
        instance.instanceCustomIndex = 0;
        instance.instanceShaderBindingTableRecordOffset = 0;
        
        // 不考虑facing cull
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.mask  = 0xFF;
        instances.push_back(instance);
    }
    raytracingBuilder.buildTlas(instances, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

    // 把tlas 添加到descriptor set 让shader可以访问
    // 根据binding的分配 0 storage buffer 1 acceleration 
    // 在descriptor set 中 写入一个descriptor 位于bind0 set 0
    // 提供 descriptor 与shader 的access的能力
    nvvk::DescriptorSetContainer descriptorSetContainer(context);
    descriptorSetContainer.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    descriptorSetContainer.addBinding(1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    descriptorSetContainer.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    descriptorSetContainer.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    descriptorSetContainer.initLayout();
    descriptorSetContainer.initPool(1);
    descriptorSetContainer.initPipeLayout();// 依赖 pool 和layout

    // 现在两个descriptor set了，所以对应两个buffer
    {
        std::array<VkWriteDescriptorSet, 4> writeDescriptorSets;
        //0
        VkDescriptorBufferInfo descriptorBufferInfo{};
        descriptorBufferInfo.buffer = buffer.buffer;
        descriptorBufferInfo.range = bufferSizeByte;
        writeDescriptorSets[0] = descriptorSetContainer.makeWrite(0,0,&descriptorBufferInfo);
        // 1
        VkWriteDescriptorSetAccelerationStructureKHR descriptorAS = nvvk::make<VkWriteDescriptorSetAccelerationStructureKHR>();
        // 或者tlas的pointer
        VkAccelerationStructureKHR tlasCopy = raytracingBuilder.getAccelerationStructure();
        descriptorAS.accelerationStructureCount = 1;
        descriptorAS.pAccelerationStructures    = &tlasCopy;
        writeDescriptorSets[1]                  = descriptorSetContainer.makeWrite(0, 1, &descriptorAS);
        
        //2 vertex 
        VkDescriptorBufferInfo vertexDescriptorBufferInfo{};
        vertexDescriptorBufferInfo.buffer = vertexBuffer.buffer;
        vertexDescriptorBufferInfo.range = VK_WHOLE_SIZE;
        writeDescriptorSets[2] = descriptorSetContainer.makeWrite(0,2,&vertexDescriptorBufferInfo);
        // 3 index
        VkDescriptorBufferInfo indexDescriptorBufferInfo{};
        indexDescriptorBufferInfo.buffer = indexBuffer.buffer;
        indexDescriptorBufferInfo.range = VK_WHOLE_SIZE;
        writeDescriptorSets[3] = descriptorSetContainer.makeWrite(0,3, &indexDescriptorBufferInfo);

        vkUpdateDescriptorSets(context, 
            static_cast<uint32_t>(writeDescriptorSets.size()), 
            writeDescriptorSets.data(),
            0,nullptr);
    }
    


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

    // 创建一个pipeline
    VkComputePipelineCreateInfo pipelineCreateInfo = nvvk::make<VkComputePipelineCreateInfo>();
    pipelineCreateInfo.stage = shaderStageCreateInfo;
    pipelineCreateInfo.layout = descriptorSetContainer.getPipeLayout();
    VkPipeline computePipeline;
    NVVK_CHECK(vkCreateComputePipelines(
        context, VK_NULL_HANDLE,1, &pipelineCreateInfo,
        nullptr, &computePipeline
    ));
    

    VkCommandBuffer cmdBuffer = AllocateAndBeiginOneTimeCommandBuffer(context, cmdPool);

    // 填充buffer 一般用于clear或者初始化
    // const float fillValue = 0.5f;
    // const uint32_t& fillValueU32 = reinterpret_cast<const uint32_t&>(fillValue);
    // vkCmdFillBuffer(cmdBuffer, buffer.buffer, 0, bufferSizeByte, fillValueU32);
    
    // 等fill 操作完成后，gpu再读取，这里需要注意cmd的同步问题与barrier的功能

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    // bind descriptor set
    VkDescriptorSet descriptorSet = descriptorSetContainer.getSet(0);
    vkCmdBindDescriptorSets(
        cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, descriptorSetContainer.getPipeLayout(),
        0,1,&descriptorSet, 0, nullptr
    );

    vkCmdDispatch(cmdBuffer,
    (uint32_t(render_width) + workgroup_width - 1) / workgroup_width, // ceil(width / work_with) 则可得每个work item 的x
    (uint32_t(render_height) + workgroup_height - 1 ) / workgroup_height ,
    1);


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


    EndSubmitWaitAndFreeCommandBuffer(context, context.m_queueGCT, cmdPool, cmdBuffer);


    
    // 从GPU传回CPU
    // todo 1. 所以buffer是开在gpu上的？ 取决于是不是独显，核显是在一起的，独显在cpu上 
    // map是把storge 映射到cpu raw上？可以这么理解，严格的说gpu 的vram需要有flags VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 但我们有host的一致性，所以可以理解为gpu
    void* data = allocator.map(buffer);
    stbi_write_hdr("./out.hdr", render_width, render_height, 3, reinterpret_cast<float *>(data));
    allocator.unmap(buffer);

    vkDestroyPipeline(context, computePipeline, nullptr);
    vkDestroyShaderModule(context, rayTraceModule, nullptr);
    // vkDestroyPipelineLayout(context, pipelineLayout, nullptr);
    descriptorSetContainer.deinit();
    raytracingBuilder.destroy();
    allocator.destroy(vertexBuffer);
    allocator.destroy(indexBuffer);

    vkDestroyCommandPool(context, cmdPool, nullptr);

    allocator.destroy(buffer);
    allocator.deinit();
    context.deinit();
} 