#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cmath>

#include <stb_image_write.h>

#include <GL/glew.h>
#define  GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "glSsbo.hpp"
#include "glUbo.hpp"
#include "glVao.hpp"
#include "glTexture2D.hpp"
#include "glRenderTexture.hpp"
#include "glShader.hpp"

#include "Bvh.hpp"
#include "Texture2D.hpp"
#include "Model.hpp"
#include "Camera.hpp"
#include "SingularityLight.hpp"

int main(int argc, char** argv)
{
    /*
        OpenGL Information
    */
    enum UboBindings : GLuint {
        BIND_UBO_LIGHT = 0,
    };
    enum UboMaxCounts : GLuint {
        UBO_LIGHT_MAX_COUNT = 4
    };
    enum SsboBindings : GLuint {
        BIND_SSBO_BVH_NODES = 0, 
        BIND_SSBO_BVH_FACE_INDICES = 1, 
        BIND_SSBO_VERTICES = 2, 
        BIND_SSBO_FACES = 3, 
        BIND_SSBO_MATERIALS = 4,
        BIND_SSBO_TEXTURE_HANDLES = 5,
        BIND_SSBO_FACE_DATA = 6,
        BIND_SSBO_SHADOW_MASK = 7
    };
    enum TextureUnits : GLuint {
        TEX_UNIT_LIGHT_MAP = 0,
        TEX_UNIT_SHADOW_MASK = 1,
        TEX_UNIT_HDRI = 6,
        TEX_UNIT_NOISE = 7
    };
    enum FrameColorAttachments : GLuint {
        FRAME_ATTACHMENT_LIGHT_MAP = 0,
        FRAME_ATTACHMENT_SHADOW_MASK = 0
    };

    const unsigned int NUMBER_OF_SAMPLES = 64;
    const unsigned int NUMBER_OF_BOUNCES = 2; // Needs to be updated in shader (includes direct hit)
    const int TILE_WIDTH  = 256;
    const int TILE_HEIGHT = 256;

    // no point of wrapping this in another file, just wanted to organize my source a little more
    // and not over-wrap every peice of data
    struct glScene
    {
        glVao                      vao;
        glSsbo<Vertex>             vertices;
        glSsbo<Face>               faces;
    
        std::vector<glTexture2D>   textures;
        glSsbo<Material>           materials;
        glSsbo<GLuint64>           textureHandles;
        glSsbo<FaceData>           faceData;
        glSsbo<GLuint>             lightVisibility;
        RenderTexture             shadowMask;

        glSsbo<BvhNode>            bvhNodes;
        glSsbo<unsigned int>       bvhFaceIndices;

        glTexture2D                hdri;
        glTexture2D                noise;
        
        // ping pong render targets
        unsigned int               lightMapreadIndex = 0;
        unsigned int               lightMapwriteIndex = 1;
        RenderTexture              lightMaps[2]; 
       
        glUbo<SingularityLight, UBO_LIGHT_MAX_COUNT> singularityLights;
    };

    /*
        Cpu Data Loading/Initializatoin
    */

    if (argc < 2) {
        std::cerr << "1. Model path must be passed as first command arg" << std::endl;
        std::cerr << "2. An optional hdri may be passed as the second command arg" << std::endl;
        return EXIT_FAILURE;
    }
    const std::string modelPath = argv[1];
    const std::string hdriPath  = argc > 2 ? argv[2] : "";

    SingularityLight sunlight{};
    Camera camera{};
    Model cpuModel;
    if (!cpuModel.loadModel(modelPath)) {
        std::cerr << "Model failed to load at: " << modelPath << std::endl;
        return EXIT_FAILURE;
    }

    Texture2D cpuNoise;
    cpuNoise.createNoise(cpuModel.lightmap.width, cpuModel.lightmap.height);

    Texture2D cpuHdri;
    const bool hasHdri = !hdriPath.empty();
    if(!hasHdri) {
        std::cout << "No hdri path provided. Will bake scene with no environmental lighting." << std::endl;
    }
    if(hasHdri && !cpuHdri.loadHdri(hdriPath)) {
        std::cerr << "Hdri failed to load at: " << hdriPath << std::endl;
        return EXIT_FAILURE;
    }

    /*
        Create Window
    */

    int glfwErrorCode = glfwInit();
    if (GLFW_TRUE != glfwErrorCode)
    {
        std::cerr << "Could not initialize GLFW" << std::endl;
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    int windowResolutionX = 1280;
    int windowResolutionY = 960;
    GLFWwindow* window = glfwCreateWindow(windowResolutionX, windowResolutionY, "Baked Scene Diffuse GI", NULL, NULL);
    if (!window) {
        std::cerr << "Could not initialize a GLFW window" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwMakeContextCurrent(window);
    if (glfwGetCurrentContext() != window) {
        std::cerr << "Could not GLFW window current context" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // OpenGL functions
    glewExperimental = GL_TRUE;
    GLenum glewErrorCode = glewInit();
    if (GLEW_OK != glewErrorCode) {
        std::cerr << "Could not initialize GLEW" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }
    // Check OpenGL extensions
    if (!GLEW_ARB_bindless_texture) {
        std::cerr << "GL_ARB_bindless_texture is required" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }
    if (!GLEW_NV_gpu_shader5) {
        std::cerr << "GL_NV_gpu_shader5 is required for non-uniform bindless texture access" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glScene scene{};

    /*
        Compile Shaders
    */

    const std::string LIGHTMAP_PATH_TRACE_VERTEX_SHADER = "shaders/1-lightmap-path-tracing/LightMapPathTrace.vert";
    const std::string LIGHTMAP_PATH_TRACE_FRAG_SHADER   = "shaders/1-lightmap-path-tracing/LightMapPathTrace.frag";

    const std::string SHADOW_MASK_VERTEX_SHADER = "shaders/0-shadow-mask/ShadowMask.vert";
    const std::string SHADOW_MASK_FRAG_SHADER = "shaders/0-shadow-mask/ShadowMask.frag";

    glShader lightMapPathTraceShader;
    if (!lightMapPathTraceShader.loadFromSource(LIGHTMAP_PATH_TRACE_VERTEX_SHADER, LIGHTMAP_PATH_TRACE_FRAG_SHADER)) {
        std::cout << "Could not load shader" << std::endl;
        return EXIT_FAILURE;
    }
    glShader shadowMaskShader;
    if (!shadowMaskShader.loadFromSource(SHADOW_MASK_VERTEX_SHADER, SHADOW_MASK_FRAG_SHADER)) {
        std::cerr << "Could not load shadow mask shader" << std::endl;
        return EXIT_FAILURE;
    }

    /*
        Load Mesh to GPU
    */

    // upload mesh data
    scene.vertices.setData(cpuModel.vertices);
    scene.faces.setData(cpuModel.faces);
    scene.faceData.setData(cpuModel.faceData);
    scene.materials.setData(cpuModel.materials);
    scene.bvhNodes.setData(cpuModel.bvh.nodes);
    scene.bvhFaceIndices.setData(cpuModel.bvh.primitiveIndices);

    /*
        Load Texture to GPU
    */

    // compute texture formats: 
    // srgb/nonlinear = (colors: baseColor/emmission), rgb/linear = (raw data: normal/roughness/etc)
    const size_t numTextures = cpuModel.textures.size();
    std::vector<GLenum> formats(numTextures, GL_RGBA8);
    for (Material& material : cpuModel.materials) {
        int nonLinearTextureIndices[] = {material.baseColorTextureIndex, material.emissiveTextureIndex};
        for (int index : nonLinearTextureIndices) {
            if (index >= 0 && static_cast<size_t>(index) < numTextures)
                formats[index] = GL_SRGB8_ALPHA8;
        }
    }

    // upload textures
    scene.textures.resize(numTextures);
    std::vector<GLuint64> textureHandles(numTextures);
    for (size_t i = 0; i < numTextures; ++i)
    {
        glTexture2D& texture = scene.textures[i];
        Texture2D& cpuTexture = cpuModel.textures[i];
        texture.setFiltering(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
        texture.setWrapMode(GL_REPEAT, GL_REPEAT);
        texture.createFromData(
            cpuTexture.data,
            static_cast<GLsizei>(cpuTexture.metaData.width),
            static_cast<GLsizei>(cpuTexture.metaData.height),
            GL_UNSIGNED_BYTE,
            formats[i],
            GL_RGBA,
            true);
        if (!texture.makeResident()) {
            std::cerr << "Failed to make texture resident: " << i << '\n';
            return EXIT_FAILURE;
        }
        textureHandles[i] = texture.handle();
    }
    scene.textureHandles.setData(textureHandles);

    // upload per-texel random offsets
    scene.noise.setFiltering(GL_NEAREST, GL_NEAREST);
    scene.noise.setWrapMode(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    scene.noise.createFromData(
        cpuNoise.data,
        static_cast<GLsizei>(cpuNoise.metaData.width),
        static_cast<GLsizei>(cpuNoise.metaData.height),
        GL_UNSIGNED_BYTE,
        GL_RGBA8,
        GL_RGBA,
        false);

    // upload environmental lighting
    if (hasHdri) {
        scene.hdri.setFiltering(GL_LINEAR, GL_LINEAR);
        scene.hdri.setWrapMode(GL_REPEAT, GL_REPEAT);
        scene.hdri.createFromData(
            cpuHdri.data,
            static_cast<GLsizei>(cpuHdri.metaData.width),
            static_cast<GLsizei>(cpuHdri.metaData.height),
            GL_FLOAT,
            GL_RGBA32F,
            GL_RGBA,
            false
        );
    } else {
        std::cout << "No HDRI found. Build running without image based lighting." << std::endl;
    }

    // upload light map texture render target
    for (RenderTexture& lightMap : scene.lightMaps) {
        bool lightMapSuccess = lightMap.addColorTexture(
            FRAME_ATTACHMENT_LIGHT_MAP,
            static_cast<GLsizei>(cpuModel.lightmap.width),
            static_cast<GLsizei>(cpuModel.lightmap.height),
            GL_FLOAT,
            GL_RGBA16F,
            GL_RGBA,
            false);
        if (!lightMapSuccess) {
            std::cerr << "Failed to generate lightmap render target" << std::endl;
            return EXIT_FAILURE;
        }
        lightMap.setColorFiltering(FRAME_ATTACHMENT_LIGHT_MAP, GL_NEAREST, GL_NEAREST);
        lightMap.bindFrameBuffer();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    bool shadowMaskSuccess = scene.shadowMask.addColorTexture(
        FRAME_ATTACHMENT_SHADOW_MASK,
        static_cast<GLsizei>(cpuModel.lightmap.width),
        static_cast<GLsizei>(cpuModel.lightmap.height),
        GL_FLOAT,
        GL_RGBA16F,
        GL_RGBA,
        false);
    if (!shadowMaskSuccess) {
        std::cerr << "Failed to generate shadow mask render target" << std::endl;
        return EXIT_FAILURE;
    }
    scene.shadowMask.setColorFiltering(FRAME_ATTACHMENT_SHADOW_MASK, GL_NEAREST, GL_NEAREST);

    /*
        Upload Lights to GPU
    */

    sunlight.lightType  = SingularityLight::DIRECTIONAL_LIGHT;
    sunlight.position   = glm::vec3(0.0f);
    sunlight.direction  = glm::vec3(0.0f, -1.0f, 0.0f);
    sunlight.irradiance = glm::vec3(1.0f);
    scene.singularityLights.setData({sunlight});

    // Direct visibility for each light is stored as binary values packed in 32 bit uints
    const size_t wordsPerTexel = (scene.singularityLights.count() + 31u) / 32u;
    const size_t shadowMaskSize = static_cast<size_t>(cpuModel.lightmap.width) * cpuModel.lightmap.height * wordsPerTexel;
    scene.lightVisibility.setData(std::vector<GLuint>(shadowMaskSize, 0u), GL_DYNAMIC_COPY);

    /*
        User IO
    */

    auto updateCamera = [](Camera& camera) {
        // user io
    };

    /*
        Render Passes
    */

    unsigned int pixelSampleCount = 0;
    size_t       totalRayCount    = 0;
    auto shadowMaskPass = [&](){
        scene.vao.bind();
        scene.vertices.bind(BIND_SSBO_VERTICES);
        scene.faces.bind(BIND_SSBO_FACES);
        scene.faceData.bind(BIND_SSBO_FACE_DATA);
        scene.materials.bind(BIND_SSBO_MATERIALS);
        scene.textureHandles.bind(BIND_SSBO_TEXTURE_HANDLES);
        scene.bvhNodes.bind(BIND_SSBO_BVH_NODES);
        scene.bvhFaceIndices.bind(BIND_SSBO_BVH_FACE_INDICES);
        scene.singularityLights.bind(BIND_UBO_LIGHT);
        scene.lightVisibility.bind(BIND_SSBO_SHADOW_MASK);

        shadowMaskShader.bind();
        shadowMaskShader.setUniform1ui("faceCount", scene.faces.count());
        shadowMaskShader.setUniform1ui("bvhNodeCount", scene.bvhNodes.count());
        shadowMaskShader.setUniform1ui("screenWidth", cpuModel.lightmap.width);
        shadowMaskShader.setUniform1ui("screenHeight", cpuModel.lightmap.height);
        shadowMaskShader.setUniform1ui("lightCount", scene.singularityLights.count());
        
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        scene.shadowMask.bindFrameBuffer();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Render in tiles while ping ponging between texture targets
        const int width = cpuModel.lightmap.width;
        const int height = cpuModel.lightmap.height;
        glEnable(GL_SCISSOR_TEST);
        for (int y = 0; y < height; y += TILE_HEIGHT) {
            for (int x = 0; x < width; x += TILE_WIDTH) {
                GLsizei validTileWidth = std::min(TILE_WIDTH, width - x);
                GLsizei validTileHeight =  std::min(TILE_HEIGHT, height - y);
                glScissor(x, y, validTileWidth, validTileHeight);
                glDrawArrays(GL_TRIANGLES, 0, scene.faces.count() * 3);
                glFinish();
                glfwPollEvents();
            }
        }

        glDisable(GL_SCISSOR_TEST);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    };

    auto lightMapPathTracePass = [&](){
        scene.vao.bind();
        scene.vertices.bind(BIND_SSBO_VERTICES);
        scene.faces.bind(BIND_SSBO_FACES);
        scene.faceData.bind(BIND_SSBO_FACE_DATA);
        scene.materials.bind(BIND_SSBO_MATERIALS);
        scene.textureHandles.bind(BIND_SSBO_TEXTURE_HANDLES);
        scene.bvhNodes.bind(BIND_SSBO_BVH_NODES);
        scene.bvhFaceIndices.bind(BIND_SSBO_BVH_FACE_INDICES);
        scene.singularityLights.bind(BIND_UBO_LIGHT);
        scene.lightVisibility.bind(BIND_SSBO_SHADOW_MASK);

        lightMapPathTraceShader.bind();
        scene.shadowMask.bindColorTexture(FRAME_ATTACHMENT_SHADOW_MASK, TEX_UNIT_SHADOW_MASK);
        lightMapPathTraceShader.setUniform1i("shadowMask", TEX_UNIT_SHADOW_MASK);
        lightMapPathTraceShader.setUniform1ui("faceCount", scene.faces.count());
        lightMapPathTraceShader.setUniform1ui("bvhNodeCount", scene.bvhNodes.count());
        lightMapPathTraceShader.setUniform1ui("screenWidth", cpuModel.lightmap.width);
        lightMapPathTraceShader.setUniform1ui("screenHeight", cpuModel.lightmap.height);
        lightMapPathTraceShader.setUniform1ui("lightCount", scene.singularityLights.count());
        
        if (hasHdri) scene.hdri.bind(TEX_UNIT_HDRI);
        scene.noise.bind(TEX_UNIT_NOISE);
        lightMapPathTraceShader.setUniform1i("noiseTexture", TEX_UNIT_NOISE);
        lightMapPathTraceShader.setUniform1i("hdri", TEX_UNIT_HDRI);
        lightMapPathTraceShader.setUniform1i("hasHdri", hasHdri ? 1 : 0);
        
        lightMapPathTraceShader.setUniform1i("previousLightMap", TEX_UNIT_LIGHT_MAP);
        lightMapPathTraceShader.setUniform1ui("pixelSampleCount", pixelSampleCount);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        scene.lightMaps[scene.lightMapreadIndex].bindColorTexture(FRAME_ATTACHMENT_LIGHT_MAP, TEX_UNIT_LIGHT_MAP);
        scene.lightMaps[scene.lightMapwriteIndex].bindFrameBuffer();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Render in tiles while ping ponging between texture targets
        const int width = cpuModel.lightmap.width;
        const int height = cpuModel.lightmap.height;
        glEnable(GL_SCISSOR_TEST);
        for (int y = 0; y < height; y += TILE_HEIGHT) {
            for (int x = 0; x < width; x += TILE_WIDTH) {
                GLsizei validTileWidth = std::min(TILE_WIDTH, width - x);
                GLsizei validTileHeight =  std::min(TILE_HEIGHT, height - y);
                glScissor(x, y, validTileWidth, validTileHeight);
                glDrawArrays(GL_TRIANGLES, 0, scene.faces.count() * 3);
                glFinish();
                glfwPollEvents();
                totalRayCount += static_cast<size_t>(validTileWidth) * validTileHeight;
                std::cout << "\rComplete Paths traced: " << totalRayCount << std::flush;
            }
        }

        glDisable(GL_SCISSOR_TEST);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
        std::swap(scene.lightMapreadIndex, scene.lightMapwriteIndex);
        ++pixelSampleCount;
    };
    
    // Draw
    std::cout << "Starting lightmap baking path tracer..." << std::endl;
    const double bakeStart = glfwGetTime();
    
    std::cout << "Computing Shadow Mask..." << std::endl;
    shadowMaskPass();
    std::cout << "Computing Indirect Lighting..." << std::endl;
    
    for (unsigned int i = 0; i < NUMBER_OF_SAMPLES; ++i) {
        lightMapPathTracePass();
    }

    glFinish();
    std::cout << std::endl << "Finished lightmap bake path tracer in: " << glfwGetTime() - bakeStart << " seconds" << std::endl;

    {
    const int width  = cpuModel.lightmap.width;
    const int height = cpuModel.lightmap.height;
    // Read HDR irradiance
    std::vector<float> hdrPixels(width * height * 4);
    scene.lightMaps[scene.lightMapreadIndex].bindFrameBuffer();
    glReadBuffer(GL_COLOR_ATTACHMENT0 + FRAME_ATTACHMENT_LIGHT_MAP);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, hdrPixels.data());
    // Tone map and convert to sRGB
    std::vector<unsigned char> pixels(width * height * 4);
    float exposure = 1.0f;
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        for (int c = 0; c < 3; c++)
        {
            float x = std::max(0.0f, hdrPixels[i + c] * exposure);
            // Reinhard tone mapping
            x = x / (1.0f + x);
            // Linear to sRGB
            x = x <= 0.0031308f
                ? 12.92f * x
                : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
            pixels[i + c] = static_cast<unsigned char>(
                std::clamp(x, 0.0f, 1.0f) * 255.0f
            );
        }
        pixels[i + 3] = hdrPixels[i + 3] > 0.5f ? 255 : 0;
    }
    stbi_flip_vertically_on_write(1);
    stbi_write_png("lightmap.png", width, height, 4, pixels.data(), width * 4);
    stbi_flip_vertically_on_write(0);
    };

    return EXIT_SUCCESS;
}
