#version 430 core
#extension GL_ARB_bindless_texture : require
#extension GL_NV_gpu_shader5 : require

/*
    Constants
*/
#define BIND_SSBO_MATERIALS 4
#define BIND_SSBO_TEXTURE_HANDLES 5
#define BIND_SSBO_FACE_DATA 6

/*
    Materials
*/  
struct Material
{
    vec4  baseColor;
    vec3  emissive;
    float metallic;
    float roughness;
    float normal;
    float occlusion;
    int   baseColorTextureIndex;
    int   normalTextureIndex;
    int   emissiveTextureIndex;
    int   metallicRoughnessTextureIndex;
    int   occlusionTextureIndex;
    int   alphaMode;
    float alphaCutoff;
};

layout(std430, binding = BIND_SSBO_MATERIALS) readonly buffer Materials
{
    Material materials[];
};

layout(std430, binding = BIND_SSBO_TEXTURE_HANDLES) readonly buffer TextureHandles
{
    uvec2 textureHandles[];
};

struct FaceMaterial { uint materialIndex; };
layout(std430, binding = BIND_SSBO_FACE_DATA) readonly buffer FaceMaterials
{
    FaceMaterial faceMaterials[];
};

/*
    Ignore the depth fragments with transparent alpha
    -> depth fragments generated to framebuffer after vertex shader nothing much to do here
*/

in vec2 fragMaterialTexCoord;

void main()
{
    Material material = materials[faceMaterials[gl_PrimitiveID].materialIndex];
    if (material.alphaMode == 1) {
        float alpha = material.baseColor.a;
        
        if (material.baseColorTextureIndex >= 0)
            alpha *= texture(sampler2D(textureHandles[material.baseColorTextureIndex]), fragMaterialTexCoord).a;
        
        if (alpha < material.alphaCutoff) discard;
    }
}
