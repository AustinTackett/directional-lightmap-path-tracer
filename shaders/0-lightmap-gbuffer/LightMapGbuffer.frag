#version 430 core
#extension GL_ARB_bindless_texture : require
#extension GL_NV_gpu_shader5 : require
layout(early_fragment_tests) in;

/*
    Constants
*/
#define BIND_SSBO_MATERIALS 4u
#define BIND_SSBO_TEXTURE_HANDLES 5u
#define BIND_SSBO_FACE_DATA 6u

#define FRAME_ATTACHMENT_POSITION 1u
#define FRAME_ATTACHMENT_NORMAL 2u
#define FRAME_ATTACHMENT_ALBEDO 3u

/*
    Per Face Data
*/
struct FaceMaterial
{
    uint materialIndex;
};

layout(std430, binding = BIND_SSBO_FACE_DATA) readonly buffer FaceMaterials
{
    FaceMaterial faceMaterials[];
};

/*
    Material Definition
    -> -1 texture index := missing texture
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

/*
    Texture Sampling
    -> returns linear color values
*/
layout(std430, binding = BIND_SSBO_TEXTURE_HANDLES) readonly buffer TextureHandles
{
    uvec2 textureHandles[];
};

// returns linear rgb color
vec3 sampleAlbedo(Material material, vec2 uv)
{
    vec3 albedo = material.baseColor.rgb;
    if (material.baseColorTextureIndex >= 0) {
        uint index = uint(material.baseColorTextureIndex);
        albedo *= texture(sampler2D(textureHandles[index]), uv).rgb;
    }
    return albedo;
}

/*
    in/out
*/
in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragMaterialTexCoord;

layout(location = FRAME_ATTACHMENT_POSITION)  out vec4  outPosition;
layout(location = FRAME_ATTACHMENT_NORMAL)    out vec4  outNormal;
layout(location = FRAME_ATTACHMENT_ALBEDO)    out vec4  outAlbedo;

void main()
{
    // Zero is an empty texel (add one to the index)
    outPosition = vec4(fragPosition, float(gl_PrimitiveID + 1)); // Zeroth position 
    outNormal   = vec4(normalize(fragNormal), 1.0);
    
    // Alpha-Mask materials with alpha above the threshold are ignored by the light
    Material material = materials[faceMaterials[gl_PrimitiveID].materialIndex];
    if (material.alphaMode == 1) {
        
        // Get material alpha
        float alpha = material.baseColor.a;
        if (material.baseColorTextureIndex >= 0)
            alpha *= textureLod(sampler2D(textureHandles[material.baseColorTextureIndex]), fragMaterialTexCoord, 0.0).a;

        // Ignore above 0.5
        if (alpha < material.alphaCutoff) discard;
    }
    
    outAlbedo = vec4(sampleAlbedo(material, fragMaterialTexCoord), 1.0);
}
