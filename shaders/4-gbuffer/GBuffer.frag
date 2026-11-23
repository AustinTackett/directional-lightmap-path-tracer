#version 430 core
#extension GL_ARB_bindless_texture : require
#extension GL_NV_gpu_shader5 : require

/*
    Constants
*/
#define BIND_SSBO_MATERIALS 4
#define FRAME_ATTACHMENT_LIGHT_MAP_COORD 0
#define FRAME_ATTACHMENT_NORMAL 2
#define FRAME_ATTACHMENT_ALBEDO 3
#define FRAME_ATTACHMENT_RMO 4
#define FRAME_ATTACHMENT_EMISSION 6

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

layout(std430, binding = 5) readonly buffer TextureHandles { 
    uvec2 textureHandles[]; 
};

/*
    Face data
*/
struct FaceData { 
    uint materialIndex;
};

layout(std430, binding = 6) readonly buffer FaceMaterials { 
    FaceData faceMaterials[]; 
};

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragMaterialTexCoord;
in vec2 fragLightMapTexCoord;

/*
    Sampling normal map 
    -> use tbn to convert from local texture space to world space
*/
vec3 fetchNormalMap(Material material)
{   
    // Estimate TBN using screen space derivatives of position and tex coords
    vec3 N = normalize(fragNormal);

    if (material.normalTextureIndex < 0)
        return N;

    vec3 dpdx = dFdx(fragPosition);
    vec3 dpdy = dFdy(fragPosition);

    vec2 dudx = dFdx(fragMaterialTexCoord);
    vec2 dudy = dFdy(fragMaterialTexCoord);

    float det = dudx.x * dudy.y - dudx.y * dudy.x;

    if (abs(det) < 1e-8)
        return N;

    /*
        Jacobian: dp/dx = dp/du * du/dx + dp/dv * dv/dx
        T := dp/du, B := dp/dv
        
                           J
        [ dp/dx ]   [ du/dx  dv/dx ] [ T ]
        [ dp/dy ] = [ du/dy  dv/dy ] [ B ]
        
        Inverse Jacobian Matrix

                           J^-1
        [ T ]                 [  dv/dy -dv/dx ] [ dp/dx ]
        [ B ] =  1 / det(J) * [ -du/dy  du/dx ] [ dp/dy ]
    */
    vec3 T = (dpdx * dudy.y - dpdy * dudx.y) / det;
    vec3 B = (dpdy * dudx.x - dpdx * dudy.x) / det;

    // Gram schmidt process to make indepedent basis vectors orthonormal
    T = normalize(T - N * dot(N, T));
    B = normalize(B - N * dot(N, B) - T * dot(T, B));

    // sample texture from [0, 1] to [-1, 1]
    vec3 sampledNormal = texture(
        sampler2D(textureHandles[material.normalTextureIndex]),
        fragMaterialTexCoord
    ).xyz * 2.0 - 1.0;

    // return in world space
    sampledNormal.xy *= material.normal;
    return normalize(mat3(T, B, N) * sampledNormal);
}

/*
    in/out
*/
layout(location = FRAME_ATTACHMENT_LIGHT_MAP_COORD) out vec2 outLightMapTexCoord;
layout(location = FRAME_ATTACHMENT_NORMAL)          out vec4 outNormal;
layout(location = FRAME_ATTACHMENT_ALBEDO)          out vec4 outAlbedo;
layout(location = FRAME_ATTACHMENT_RMO)             out vec4 outRmo;
layout(location = FRAME_ATTACHMENT_EMISSION)        out vec4 outEmission;

/*
    Write material/position/normal data to texture
*/
void main()
{
    Material material = materials[faceMaterials[gl_PrimitiveID].materialIndex];
    
    // get albedo
    vec4 albedo = material.baseColor;
    if (material.baseColorTextureIndex >= 0)
        albedo *= texture(sampler2D(textureHandles[material.baseColorTextureIndex]), fragMaterialTexCoord);
    
    // get alpha
    if (material.alphaMode == 1 && albedo.a < material.alphaCutoff) discard;
    if (material.alphaMode != 2) albedo.a = 1.0;

    // get roughness/metallic
    float roughness = material.roughness;
    float metallic  = material.metallic;
    if (material.metallicRoughnessTextureIndex >= 0) {
        vec4 packedMaterial = texture(sampler2D(textureHandles[material.metallicRoughnessTextureIndex]), fragMaterialTexCoord);
        roughness *= packedMaterial.g;
        metallic *= packedMaterial.b;
    }
    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic, 0.0, 1.0);
    
    // get occlusion
    float occlusion = 1.0;
    if (material.occlusionTextureIndex >= 0) {
        float textureOcclusion = texture(sampler2D(textureHandles[material.occlusionTextureIndex]), fragMaterialTexCoord).r;
        occlusion = mix(1.0, textureOcclusion, clamp(material.occlusion, 0.0, 1.0));
    }

    // get emission
    vec3 emission = material.emissive;
    if (material.emissiveTextureIndex >= 0)
        emission *= texture(sampler2D(textureHandles[material.emissiveTextureIndex]), fragMaterialTexCoord).rgb;

    // store in gbuffer texture
    outLightMapTexCoord = fragLightMapTexCoord;
    outEmission         = vec4(emission, 1.0);
    outAlbedo           = vec4(albedo.rgb, clamp(albedo.a, 0.0, 1.0));
    outNormal           = vec4(fetchNormalMap(material) * 0.5 + 0.5, 1.0);
    outRmo              = vec4(roughness, metallic, occlusion, 1.0);

}
