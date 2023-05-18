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

layout(binding = 2, set = 0, scalar) buffer Vertices
{
    vec3 vertices[];
};
layout(binding = 3, set = 0, scalar) buffer Indices
{
    uint indices[];
};


vec3 skyColor(vec3 direction)
{
    if (direction.y > 0.0f)
    {
        return mix(vec3(1.0f), vec3(0.25f, 0.5f, 1.0f), direction.y);
    } 
    else 
    {
        return vec3(0.03f);
    }
}

struct HitInfo
{
    vec3 color;
    vec3 worldPosition;
    vec3 worldNormal;
};

HitInfo getObjectHitInfo(rayQueryEXT rayQuery) 
{
    HitInfo result;
    // 获得 intersec primitive 的id
    const int primitiveID = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, true);
    
    const uint i0 = indices[3 * primitiveID + 0];
    const uint i1 = indices[3 * primitiveID + 1];
    const uint i2 = indices[3 * primitiveID + 2];

    const vec3 v0 = vertices[i0];
    const vec3 v1 = vertices[i1];
    const vec3 v2 = vertices[i2];
    
    // 重心坐标 没用ray的t来判定intesect是为了避免 round的误差
    vec3 barycentrics = vec3(0.0, rayQueryGetIntersectionBarycentricsEXT(rayQuery,true));
    barycentrics.x = 1.0 - barycentrics.y - barycentrics.z;
    const vec3 objectSpaceIntersection = barycentrics.x * v0 + barycentrics.y * v1 + barycentrics.z * v2;

    // pixelColor = vec3(0.5) + 0.25 * objectSpaceIntersection;
    result.worldPosition = objectSpaceIntersection;

    // normal 
    const vec3 objectNormal = normalize(cross(v1 - v0, v2 - v0));
    result.worldNormal = objectNormal;

    // 目前默认一个albedo
    result.color = vec3(0.7f);
    return result;
}

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

    const float fov = 1.0 / 5.0;
    // z指向外部 右手系
    vec3 rayDirection = vec3(fov * screenUV, -1.0);
    rayDirection = normalize(rayDirection);

    vec3 radiance = vec3(1.0);
    vec3 pixelColor = vec3(0.0);

    // 控制32 128 范围
    for (int tracedSegments = 0; tracedSegments < 32; tracedSegments++) {
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
        while(rayQueryProceedEXT(rayQuery))
        {
        }
        // 获得 intersec type
        if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) 
        {
            HitInfo hitInfo = getObjectHitInfo(rayQuery);

            hitInfo.worldNormal = faceforward(hitInfo.worldNormal, rayDirection, hitInfo.worldNormal);

            radiance *= hitInfo.color;

            rayOrigin = hitInfo.worldPosition + 0.0001 * hitInfo.worldNormal;

            rayDirection = reflect(rayDirection, hitInfo.worldNormal);
        }
        else
        {
            pixelColor = radiance * skyColor(rayDirection);
        }
    }
    
    // t的获得
    // const float t = rayQueryGetIntersectionTEXT(rayQuery, true);
    // 制作depth buffer 划分为10级
    // vec3 deepthValue = vec3(t / 10.0);

    uint linearIndex = resolution.x * pixel.y + pixel.x;
    imageData[linearIndex] = pixelColor;
}