#version 430 core

/*
    Constants
*/
#define PI 3.14159265359
#define BACKGROUND_COLOR vec4(0.25, 0.45, 0.70, 1.0)
#define MAX_LIGHTS 4
#define EPS 1e-8f

/*
    Uniforms
*/
layout(binding = 0) uniform sampler2D lightMapCoordTexture;
layout(binding = 1) uniform sampler2D irradianceTexture;
layout(binding = 2) uniform sampler2D normalTexture;
layout(binding = 3) uniform sampler2D albedoTexture;
layout(binding = 4) uniform sampler2D rmoTexture;
layout(binding = 5) uniform sampler2D geometricNormalTexture;
layout(binding = 6) uniform sampler2D depthTexture;
layout(binding = 7) uniform sampler2D emissionTexture;
layout(binding = 8) uniform sampler2DShadow shadowTextures[MAX_LIGHTS];

uniform vec3 cameraPosition;
uniform mat4 invViewProj;

uniform uint lightCount;
uniform uint lightMapWidth;
uniform uint lightMapHeight;

uniform mat4 shadowLightMatrices[MAX_LIGHTS];

/*
    Lights
*/
struct DirectionalLight {
    vec3 direction;
    vec3 irradiance;
};

layout(std140, binding = 0) uniform Lights { 
    DirectionalLight directionalLights[4]; 
};


/*
    Spherical Harmonics
*/
struct ShCoefficient {
    vec4 c_r;
    vec4 c_g;
    vec4 c_b;
};

layout(std430, binding = 8) readonly buffer ShCoefficients {
    ShCoefficient shCoefficients[];
};

/*
    PCF shadow maps
*/
float gaussianPcfShadow(uint lightIndex, vec3 worldPos, vec3 normal, vec3 lightDir)
{
    // Weighted visibility = sum(w_gaussian_xy * V_xy) / sum(w_gaussian_xy)

    // 5x5 binomial gaussian kernel -> 1/256 * (weights^T * weights)
    const float weights[5] = float[5](
        1.0, 4.0, 6.0, 4.0, 1.0
    );

    // Get uv in shadowTexture for specific light index
    vec4 shadowCoord = shadowLightMatrices[lightIndex] * vec4(worldPos, 1.0);
    vec3 uv = (shadowCoord.xyz / shadowCoord.w) * 0.5 + 0.5;
    if (any(lessThan(uv, vec3(0.0))) || any(greaterThan(uv, vec3(1.0))))
        return 1.0;

    float nDotL = max(dot(normalize(normal), normalize(lightDir)), 0.0);
    float bias = max(0.001 * (1.0 - nDotL), 0.0001);
    vec2  texelSize = 1.0 / vec2(textureSize(shadowTextures[lightIndex], 0));

    float visibility = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float weight = ( weights[x + 2] * weights[y + 2] ) / 256.0;

            // sampler2DShadow compares reference depth (vec3) against stored texture depth.
            // With GL_LEQUAL: 1 = visible, 0 = occluded.
            float depthComparison = texture(shadowTextures[lightIndex], vec3(uv.xy + vec2(x, y) * texelSize, uv.z - bias));
            
            visibility += weight * depthComparison;
        }
    }
 
    return visibility;
}

/*
    Pbr microfacet shading
*/
float distributionGGX(vec3 n, vec3 h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(n, h), 0.0);
    float nDotH2 = nDotH * nDotH;

    float denom = (nDotH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 0.00001);
}

float geometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.00001);
}

float geometrySmith(vec3 n, vec3 v, vec3 l, float roughness)
{
    float nDotV = max(dot(n, v), 0.0);
    float nDotL = max(dot(n, l), 0.0);
    return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

/*
    Directional Light Map Sampling
*/

// degree 0 order 0
float Y_0()
{
    return sqrt(1.0 / (4.0 * PI));
}

// degree 1 basis has 3 orders
vec3 Y_1(vec3 xyz)
{
    return sqrt(3.0 / (4.0 * PI)) * xyz.yzx;
}

vec3 shIrradianceNormalMap(ShCoefficient sh, vec3 bakedIrradiance, vec3 lightMapNormal, vec3 shadingNormal)
{
    // Irradiance is the cosine-weighted integral of incoming radiance from hemisphere
    // E(n) = Integral_S2 [ L_i(w) * max(0, dot(n, w)) ] dw
    //
    // Radiance represented using spherical harmonics:
    // L_i(w) ~= Sum_l,m [ c_lm * Y_lm(w) ]
    //
    // Cosine convolution converts radiance SH into irradiance SH:
    // E_SH(n) ~= Sum_l,m [ A_l * c_lm * Y_lm(n) ]
    // A_0 = PI, A_1 = (2 * PI) / 3
    //
    // Compute the change in irradiance caused by the normal map:
    // E_delta = E_SH(n_shading) - E_SH(n_geo)
    //
    // The L0 terms cancel because Y_00 is constant for both
    // E_corrected = E_baked + E_delta
    //
    // Reference:
    // https://cseweb.ucsd.edu/~ravir/papers/envmap/envmap.pdf

    const float A1 = 2.0 * PI / 3.0;

    vec3 Y_1_geo     = Y_1(normalize(lightMapNormal));
    vec3 Y_1_shading = Y_1(normalize(shadingNormal));

    vec3 E_geo= A1 * vec3(
        dot(sh.c_r.yzw, Y_1_geo),
        dot(sh.c_g.yzw, Y_1_geo),
        dot(sh.c_b.yzw, Y_1_geo)
    );

    vec3 E_shading = A1 * vec3(
        dot(sh.c_r.yzw, Y_1_shading),
        dot(sh.c_g.yzw, Y_1_shading),
        dot(sh.c_b.yzw, Y_1_shading)
    );

    // Return irradiance correct for shading normal
    vec3 E_delta = E_shading - E_geo;
    return max(bakedIrradiance + E_delta, vec3(0.0));
}

vec3 shRadiance(ShCoefficient sh, vec3 direction)
{
    // L_i(w) ~= sum_l,m [ c_lm * Y_lm(w) ]

    float Y0 = Y_0();
    vec3  Y1 = Y_1(normalize(direction));

    vec3 L0 = Y0 * vec3(
        sh.c_r.x,
        sh.c_g.x,
        sh.c_b.x
    );

    vec3 L1 = vec3(
        dot(sh.c_r.yzw, Y1),
        dot(sh.c_g.yzw, Y1),
        dot(sh.c_b.yzw, Y1)
    );

    return max(L0 + L1, vec3(0.0));
}

// Not that rigourous but it looks nice if you tune it right
vec3 shReflectedRadiance(ShCoefficient sh, vec3 normal, vec3 viewDirection, vec3 F0, float roughness, float occlusion)
{
    // get radiance along reflection direction
    vec3 n = normalize(normal);
    vec3 v = normalize(viewDirection);
    vec3 r = reflect(-v, n);
    vec3 radiance = shRadiance(sh, r);

    // Fresnel := ratio of light reflected over transmitted
    float nDotV = max(dot(n, v), 0.0);
    vec3  F = fresnelSchlick(nDotV, F0);

    // The sh only describes broad variation in radiance so its not good for smooth surfaces
    // (artistic, ie: any physical derivations)
    float specularScale = mix(
        0.2,  // Smooth: L1 is a poor approximation.
        0.5,  // Rough:  L1 is better representative.
        roughness * roughness
    );

    // Get the amount of radiance reflected weighted by user generated artistic occlusion
    return specularScale * radiance * F * occlusion;
}

/*
    Gbuffer sampling
*/
vec3 getWorldPosition(vec2 uv, float depth)
{
    // given position in clip space, convert it to world space
    vec4 clip   = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = invViewProj * clip;
    return world.xyz / world.w;
}

/*
    Pixel Shading
*/
in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    // sample gbuffer
    vec4  albedoAlpha = texture(albedoTexture, fragTexCoord);
    vec4  normalTex   = texture(normalTexture, fragTexCoord);
    vec4  rmoTex      = texture(rmoTexture, fragTexCoord);
    float depth       = texture(depthTexture, fragTexCoord).r;
    if (depth >= 1.0) {
        outColor = BACKGROUND_COLOR;
        return;
    }

    // get material data
    vec3  albedo    = albedoAlpha.rgb;
    vec3  n         = normalize(normalTex.rgb * 2.0 - 1.0);
    float roughness = clamp(rmoTex.r, 0.04, 1.0);
    float metallic  = clamp(rmoTex.g, 0.0, 1.0);
    float occlusion = pow(clamp(rmoTex.b, 0.0, 1.0), 2);
    vec3  emission  = texture(emissionTexture, fragTexCoord).rgb;
    
    // reconstruct camera view direction 
    vec3  worldPos = getWorldPosition(fragTexCoord, depth);
    vec3  v        = normalize(cameraPosition - worldPos);

    // 0 metallic: 4% reflection, 1 metallic: Albedo colored reflections
    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    
    // Get pixel coordinate of data on lightmap
    vec2  lightMapUv = texture(lightMapCoordTexture, fragTexCoord).rg;
    ivec2 lightMapSize = ivec2(lightMapWidth, lightMapHeight);
    ivec2 lightMapCoord = clamp(
        ivec2(lightMapUv * vec2(lightMapSize)),
        ivec2(0),
        lightMapSize - 1
    );

    // Fetch SH coefficients for lightmap pixel
    int texelIndex = lightMapCoord.y * lightMapSize.x + lightMapCoord.x;
    ShCoefficient sh = shCoefficients[texelIndex];

    // Fetch baked lighting from lightmap
    vec3 bakedIrradiance = texelFetch(irradianceTexture, lightMapCoord, 0).rgb;

    // Fetch geometric normal
    vec4 normalSample = texelFetch(geometricNormalTexture, lightMapCoord, 0);

    // Lightmap was baked with geometric normal not shading normal
    bool validGeometricNormal = normalSample.a > 0.5 && dot(normalSample.xyz, normalSample.xyz) > EPS;
    vec3 lightMapNormal = validGeometricNormal ? normalize(normalSample.xyz) : n;

    // Get baked irradiance and use spherical harmonics to get normal map lighting
    vec3 directionalIrradiance = validGeometricNormal 
        ? shIrradianceNormalMap(sh, bakedIrradiance, lightMapNormal, n)
        : bakedIrradiance;
    
    /*
        Different Debug options for lightmap shading
    */

    // RELEASE: Directional lightmap lighting not including direct hits from delta lights
    vec3 indirectDiffuse  = (1.0 - metallic) * directionalIrradiance * (albedo / PI) * occlusion;
    vec3 indirectSpecular = shReflectedRadiance(sh, n, v, F0, roughness, occlusion);
    vec3 color = indirectDiffuse + indirectSpecular + emission;

    // DEBUG: shading without lightmaps
    // vec3 color = emission;

    // DEBUG: shading with non-directional lightmaps 
    // -> (no indirect specularities or indirect diffuse lighting that interacts with normal maps)
    // vec3 indirectDiffuse  = (1.0 - metallic) * bakedIrradiance * (albedo / PI) * occlusion;
    // vec3 color = indirectDiffuse + emission;

    for (uint i = 0u; i < lightCount; ++i)
    {
        // Sample light
        DirectionalLight light = directionalLights[i];
        vec3 l = normalize(-light.direction);
        vec3 lightRadiance = light.irradiance;
        
        // No light recieved for faces not facing light
        float nDotL = max(dot(n, l), 0.0);
        if (nDotL <= 0.0) continue;
        
        // No light recieved for faces in shadow
        float shadow = gaussianPcfShadow(i, worldPos, lightMapNormal, l);
        if (shadow <= 0.0) continue;
        
        // Specular brdf
        vec3  h = normalize(v + l);
        float D = distributionGGX(n, h, roughness);
        float G = geometrySmith(n, v, l, roughness);
        vec3  F = fresnelSchlick(max(dot(h, v), 0.0), F0);
        vec3  numerator = D * G * F;
        float denom = max(4.0 * max(dot(n, v), 0.0) * nDotL, 0.00001);
        vec3  specular = numerator / denom;
        
        // Diffuse brdf
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 diffuse = kD * albedo / PI;
        
        // Outgoing radiance
        color += (diffuse + specular) * lightRadiance * nDotL * shadow;
    }

    outColor = vec4(color, albedoAlpha.a);
}
