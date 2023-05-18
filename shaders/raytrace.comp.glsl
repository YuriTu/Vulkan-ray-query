#version 460
// #extension GL_EXT_debug_printf : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_ray_query : require

layout(local_size_x = 16, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, set = 0, scalar) buffer storageBuffer
{
    vec3 imageData[];
};

layout(binding = 1, set = 0) uniform accelerationStructureEXT tlas;

void main()
{
    // debugPrintfEXT("hello world!");
    // debugPrintfEXT("Hello from invocation(%d, %d) !\n", gl_GlobalInvocationID);
    const uvec2 resolution = uvec2(800, 600);

    const uvec2 pixel = gl_GlobalInvocationID.xy;

    if ((pixel.x >= resolution.x) || (pixel.y >= resolution.y)) 
    {
        return;
    }

    const vec3 cameraOrigin = vec3(-0.001, 1.0, 6.0);
    vec3 rayOrigin = cameraOrigin;

    const vec2 screenUV = vec2(
        // uy / resolution.x  * aspect = uv / res.y
        (2.0 * (float(pixel.x) + 0.5) - resolution.x) / resolution.y,
        -(2.0 * (float(pixel.y) + 0.5) - resolution.y) / resolution.y
    );

    // const vec2 screenUV = vec2((2.0 * float(pixel.x) + 1.0 - resolution.x) / resolution.y,    //
    //                          -(2.0 * float(pixel.y) + 1.0 - resolution.y) / resolution.y);  // Flip the y axis
  

    const float fov = 1.0 / 5.0;
    // z指向外部 右手系
    vec3 rayDirection = vec3(fov * screenUV, -1.0);

    rayQueryEXT rayQuery;
    rayQueryInitializeEXT(rayQuery,
        tlas,
        gl_RayFlagsOpaqueEXT,
        0xFF,// mask 类似LOD的处理
        rayOrigin,
        0.0,
        rayDirection,
        10000.0
    );
    // 获得所有intersect(在透明情况下)之后返回false
    float numInter = 0.0;
    while(rayQueryProceedEXT(rayQuery))
    {
        numInter += 1.0;
    }

    vec3 pixelColor;
    // 获得 intersec type
    if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) 
    {
        // 获得 intersec primitive 的id
        // const int primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
        // pixelColor = vec3(primitiveID / 10.0, primitiveID / 100.0, primitiveID / 1000.0);

        // 坐标
        pixelColor = vec3(0.0, rayQueryGetIntersectionBarycentricsEXT(rayQuery,true));
        pixelColor.x = 1.0 - pixelColor.y - pixelColor.z;
    }
    else
    {
        pixelColor = vec3(0.0, 0.0, 0.5);
    }
    // t的获得
    // const float t = rayQueryGetIntersectionTEXT(rayQuery, true);
    // 制作depth buffer 划分为10级
    // vec3 deepthValue = vec3(t / 10.0);

    uint linearIndex = resolution.x * pixel.y + pixel.x;
    imageData[linearIndex] = pixelColor;
}