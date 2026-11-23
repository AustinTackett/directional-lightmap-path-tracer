#include <cstdlib>
#include <vector>
#include <algorithm>

#include <GL/glew.h>
#define  GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "glSsbo.hpp"
#include "glUbo.hpp"
#include "glVao.hpp"
#include "glTexture2D.hpp"
#include "glRenderTexture.hpp"
#include "glShader.hpp"
#include "glPixelState.hpp"

#include "Bvh.hpp"
#include "Texture2D.hpp"
#include "Model.hpp"
#include "Camera.hpp"
#include "DirectionalLight.hpp"
#include "SceneConfig.hpp"

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
        BIND_SSBO_PIXEL_STATE = 7,
        BIND_SSBO_L1_SH = 8,
        BIND_SSBO_L1_SH_OUTPUT = 9
    };
    enum TextureUnits : GLuint {
        TEX_UNIT_LIGHT_MAP = 0,
        TEX_UNIT_POSITION = 2,
        TEX_UNIT_NORMAL = 3,
        TEX_UNIT_HDRI = 6,
        TEX_UNIT_NOISE = 7
    };
    enum FrameColorAttachments : GLuint {
        FRAME_ATTACHMENT_LIGHT_MAP = 0,
        FRAME_ATTACHMENT_LIGHT_MAP_COORD = 0, // Preview G-buffer reuses slot 0 for UVs.
        FRAME_ATTACHMENT_POSITION = 1,
        FRAME_ATTACHMENT_NORMAL = 2,
        FRAME_ATTACHMENT_ALBEDO = 3,
        FRAME_ATTACHMENT_RMO = 4,
        FRAME_ATTACHMENT_EMISSION = 6
    };

    const float ABSOLUTE_ERROR = 0.001f;
    const int SHADOW_MAP_SIZE = 4096;

    /*
        Lading from yaml file
    */
    const std::string configPath = argc > 1 ? argv[1] : "SceneConfig.yaml";
    SceneConfig config;
    try {
        config = SceneConfig::load(configPath);
    } catch (const std::exception& error) {
        std::cerr << "Could not load scene config " << configPath << ": " << error.what() << std::endl;
        return EXIT_FAILURE;
    }

    /*
        Cpu Data Loading/Initializatoin
    */

    DirectionalLight sunlight{};
    Model cpuModel;
    if (!cpuModel.loadModel(config.modelPath)) {
        std::cerr << "Model failed to load at: " << config.modelPath << std::endl;
        return EXIT_FAILURE;
    }

    // Frame the loaded world space geometry for the scene preview.
    glm::vec3 boundsMin = cpuModel.vertices.front().position;
    glm::vec3 boundsMax = boundsMin;
    for (const Vertex& vertex : cpuModel.vertices) {
        boundsMin = glm::min(boundsMin, vertex.position);
        boundsMax = glm::max(boundsMax, vertex.position);
    }
    const float radius = std::max(glm::length(boundsMax - boundsMin) * 0.5f, 0.01f);
    Camera camera(config.cameraPosition, config.cameraFov, radius);

    sunlight.direction  = config.sunDirection;
    sunlight.Irradiance = config.sunIrradiance;
    const std::vector<DirectionalLight> lights = {sunlight};

    const float texelSize = 1.0f / std::max(cpuModel.lightmap.texelsPerUnit, 0.0001f);

    Texture2D cpuNoise;
    cpuNoise.createNoise(cpuModel.lightmap.width, cpuModel.lightmap.height);

    Texture2D cpuHdri;
    const bool hasHdri = !config.hdriPath.empty();
    if(!hasHdri) {
        std::cout << "No hdri path provided. Will bake scene with no environmental lighting." << std::endl;
    }
    if(hasHdri && !cpuHdri.loadHdri(config.hdriPath)) {
        std::cerr << "Hdri failed to load at: " << config.hdriPath << std::endl;
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

    // OpenGL function pointers
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
    if (!GLEW_NV_conservative_raster)
    {
        std::cerr << "GL_NV_conservative_raster is not supported" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }

    /*
        Compile Shaders
    */
    const std::string LIGHTMAP_PATH_TRACE_VERTEX_SHADER = "shaders/1-lightmap-path-tracing/LightMapPathTrace.vert";
    const std::string LIGHTMAP_PATH_TRACE_FRAG_SHADER   = "shaders/1-lightmap-path-tracing/LightMapPathTrace.frag";
    const std::string LIGHTMAP_GBUFFER_VERTEX_SHADER    = "shaders/0-lightmap-gbuffer/LightMapGbuffer.vert";
    const std::string LIGHTMAP_GBUFFER_FRAG_SHADER      = "shaders/0-lightmap-gbuffer/LightMapGbuffer.frag";
    const std::string DENOISE_COMPUTE_SHADER            = "shaders/2-lightmap-denoise/LightMapDenoise.comp";
    const std::string SHADOW_VERTEX_SHADER              = "shaders/3-direct-shadow-map/DirectShadowMap.vert";
    const std::string SHADOW_FRAG_SHADER                = "shaders/3-direct-shadow-map/DirectShadowMap.frag";
    const std::string GBUFFER_VERTEX_SHADER             = "shaders/4-gbuffer/GBuffer.vert";
    const std::string GBUFFER_FRAG_SHADER               = "shaders/4-gbuffer/GBuffer.frag";
    const std::string DEFERRED_LIGHTING_VERTEX_SHADER   = "shaders/5-raster-direct-lighting/RasterDirectLighting.vert";
    const std::string DEFERRED_LIGHTING_FRAG_SHADER     = "shaders/5-raster-direct-lighting/RasterDirectLighting.frag";
    const std::string POST_PROCESS_VERTEX_SHADER        = "shaders/6-post-process/PostProcess.vert";
    const std::string POST_PROCESS_FRAG_SHADER          = "shaders/6-post-process/PostProcess.frag";

    glShader lightMapPathTraceShader;
    if (!lightMapPathTraceShader.loadFromSource(LIGHTMAP_PATH_TRACE_VERTEX_SHADER, LIGHTMAP_PATH_TRACE_FRAG_SHADER)) {
        std::cerr << "Could not load lightmap path trace shader" << std::endl;
        return EXIT_FAILURE;
    }
    glShader lightMapGbufferShader;
    if (!lightMapGbufferShader.loadFromSource(LIGHTMAP_GBUFFER_VERTEX_SHADER, LIGHTMAP_GBUFFER_FRAG_SHADER)) {
        std::cerr << "Could not load lightmap gbuffer shader" << std::endl;
        return EXIT_FAILURE;
    }
    glShader denoiseShader;
    if (!denoiseShader.loadFromSource(DENOISE_COMPUTE_SHADER)) {
        std::cerr << "Could not load denoise shader" << std::endl;
        return EXIT_FAILURE;
    }
    glShader shadowShader;
    if (!shadowShader.loadFromSource(SHADOW_VERTEX_SHADER, SHADOW_FRAG_SHADER)) {
        std::cerr << "Could not load shadow shader" << std::endl;
        return EXIT_FAILURE;
    }
    glShader gBufferShader;
    if (!gBufferShader.loadFromSource(GBUFFER_VERTEX_SHADER, GBUFFER_FRAG_SHADER)) {
        std::cerr << "Could not load gbuffer shader" << std::endl;
        return EXIT_FAILURE;
    }
    glShader deferredLightingShader;
    if (!deferredLightingShader.loadFromSource(DEFERRED_LIGHTING_VERTEX_SHADER, DEFERRED_LIGHTING_FRAG_SHADER)) {
        std::cerr << "Could not load deferred lighting shader" << std::endl;
        return EXIT_FAILURE;
    }
    glShader postProcessShader;
    if (!postProcessShader.loadFromSource(POST_PROCESS_VERTEX_SHADER, POST_PROCESS_FRAG_SHADER)) {
        std::cerr << "Could not load post process shader" << std::endl;
        return EXIT_FAILURE;
    }

    /*
        Grouping GPU scene buffer data 
        -> I keep changing it and its just a simple wrapper only for this file so its easier to leave it here
    */
    struct glScene
    {
        // Geometry and materials.
        glVao                    vao;
        glSsbo<Vertex>           vertices;
        glSsbo<Face>             faces;
        glSsbo<FaceData>         faceData;
        glSsbo<Material>         materials;
        glSsbo<GLuint64>         textureHandles;
        std::vector<glTexture2D> textures;

        // Acceleration structure.
        glSsbo<BvhNode>      bvhNodes;
        glSsbo<unsigned int> bvhFaceIndices;

        // Image based lighting
        glTexture2D   hdri;

        // Delta Lights
        glUbo<DirectionalLight, UBO_LIGHT_MAX_COUNT> directionalLights;

        // Lightmap baking and denoising.
        glTexture2D          noise;
        glSsbo<glPixelState> pixelStates;
        glSsbo<glm::vec4>    shCoefficients;
        glSsbo<glm::vec4>    shDenoiseScratch;
        RenderTexture        lightMapGbuffer;
        RenderTexture        lightMaps[2];
        unsigned int         lightMapreadIndex = 0;
        unsigned int         lightMapwriteIndex = 1;

        // Raster Render
        RenderTexture gBuffer;
        RenderTexture deferredLighting;
        int           viewportWidth = 0;
        int           viewportHeight = 0;
        RenderTexture shadowMaps[UBO_LIGHT_MAX_COUNT];
        glm::mat4     shadowLightMatrices[UBO_LIGHT_MAX_COUNT];

    };
    glScene scene{};

    /*
        Load Mesh to GPU
    */
    scene.vertices.setStorageData(cpuModel.vertices);
    scene.faces.setStorageData(cpuModel.faces);
    scene.faceData.setStorageData(cpuModel.faceData);
    scene.materials.setStorageData(cpuModel.materials);
    scene.bvhNodes.setStorageData(cpuModel.bvh.nodes);
    scene.bvhFaceIndices.setStorageData(cpuModel.bvh.primitiveIndices);

    // Bind vertex attributes and triangle indices to the vao
    scene.vao.bind();
    scene.vertices.setVertexAttribute(0, 3, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    scene.vertices.setVertexAttribute(1, 3, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    scene.vertices.setVertexAttribute(2, 2, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, materialTexCoords)));
    scene.vertices.setVertexAttribute(3, 2, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, lightMapTexCoords)));
    scene.faces.bindAsEbo();

    /*
        Load Texture to GPU
    */

    // compute material texture formats: 
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
    const bool hasAnisotropy = GLEW_EXT_texture_filter_anisotropic || GLEW_ARB_texture_filter_anisotropic;
    GLfloat materialAnisotropy = 1.0f;
    if (hasAnisotropy) {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &materialAnisotropy);
        materialAnisotropy = std::min(materialAnisotropy, 16.0f);
    }
    scene.textures.resize(numTextures);
    std::vector<GLuint64> textureHandles(numTextures);
    for (size_t i = 0; i < numTextures; ++i) {
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
        
        // Texture parameters must be set before creating the bindless handle.
        if (hasAnisotropy) {
            texture.setAnisotropy(materialAnisotropy);
        }
        if (!texture.makeResident()) {
            std::cerr << "Failed to make texture resident: " << i << '\n';
            return EXIT_FAILURE;
        }
        textureHandles[i] = texture.handle();
    }
    scene.textureHandles.setStorageData(textureHandles);

    // upload per-texel noise
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
        scene.hdri.setWrapMode(GL_REPEAT, GL_CLAMP_TO_EDGE);
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
        lightMap.setColorFiltering(FRAME_ATTACHMENT_LIGHT_MAP, GL_LINEAR, GL_LINEAR);
        lightMap.bindFrameBuffer();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // upload color attachments to lightmap gbuffer
    GLuint lightMapGbufferColorAttachments[3] = {FRAME_ATTACHMENT_POSITION, FRAME_ATTACHMENT_NORMAL, FRAME_ATTACHMENT_ALBEDO};
    for (GLuint attachment : lightMapGbufferColorAttachments) {
        bool success = scene.lightMapGbuffer.addColorTexture(
            attachment,
            static_cast<GLsizei>(cpuModel.lightmap.width),
            static_cast<GLsizei>(cpuModel.lightmap.height),
            GL_FLOAT,
            attachment == FRAME_ATTACHMENT_POSITION ? GL_RGBA32F : GL_RGBA16F,
            GL_RGBA,
            false);
        if (!success) {
            std::cerr << "Failed to generate lightmap guide render target" << std::endl;
            return EXIT_FAILURE;
        }
        scene.lightMapGbuffer.setColorFiltering(attachment, GL_NEAREST, GL_NEAREST);
    }

    /*
        Upload Lights to GPU
    */
    scene.directionalLights.setData(lights);

    /*
        Light Map Gbuffer Render Pass
    */
    auto lightMapGbufferPass = [&](){
        scene.vao.bind();
        scene.faceData.bind(BIND_SSBO_FACE_DATA);
        scene.materials.bind(BIND_SSBO_MATERIALS);
        scene.textureHandles.bind(BIND_SSBO_TEXTURE_HANDLES);

        lightMapGbufferShader.bind();
        
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        scene.lightMapGbuffer.bindFrameBuffer();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_CONSERVATIVE_RASTERIZATION_NV);

        // Render in tiles while ping ponging between texture targets
        const int width  = cpuModel.lightmap.width;
        const int height = cpuModel.lightmap.height;
        glEnable(GL_SCISSOR_TEST);
        for (int y = 0; y < height; y += config.tileHeight) {
            for (int x = 0; x < width; x += config.tileWidth) {
                // Bind lightmap framebuffer
                scene.lightMapGbuffer.bindFrameBuffer();
                glEnable(GL_SCISSOR_TEST);
                glfwPollEvents();

                // Draw Tile
                GLsizei validTileWidth  = std::min(config.tileWidth, width - x);
                GLsizei validTileHeight = std::min(config.tileHeight, height - y);
                glScissor(x, y, validTileWidth, validTileHeight);
                glDrawElements(GL_TRIANGLES, scene.faces.count() * 3, GL_UNSIGNED_INT, nullptr);

                glFinish();
                glDisable(GL_SCISSOR_TEST);

                // Display tile in default framebuffer
                glReadBuffer(GL_COLOR_ATTACHMENT0 + FRAME_ATTACHMENT_ALBEDO);
                RenderTexture::bindDefaultFrameBuffer(GL_DRAW_FRAMEBUFFER);
                glBlitFramebuffer(0, 0, 
                                  cpuModel.lightmap.width, cpuModel.lightmap.height,
                                  0, 0, 
                                  windowResolutionX, windowResolutionY,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glfwSwapBuffers(window);
            }
        }

        glDisable(GL_CONSERVATIVE_RASTERIZATION_NV);
        glDisable(GL_SCISSOR_TEST);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    };

    /*
        Light Map Path Tracing
    */
    const size_t lightMapTexelCount = static_cast<size_t>(cpuModel.lightmap.width) * cpuModel.lightmap.height;
    scene.pixelStates.allocateStorage(lightMapTexelCount);
    scene.shCoefficients.allocateStorage(lightMapTexelCount * 3);
    scene.shDenoiseScratch.allocateStorage(lightMapTexelCount * 3, GL_DYNAMIC_COPY, false);

    auto lightMapPathTracePass = [&]() -> bool {
        scene.vao.bind();
        scene.pixelStates.bind(BIND_SSBO_PIXEL_STATE);
        scene.shCoefficients.bind(BIND_SSBO_L1_SH);
        scene.vertices.bind(BIND_SSBO_VERTICES);
        scene.faces.bind(BIND_SSBO_FACES);
        scene.faceData.bind(BIND_SSBO_FACE_DATA);
        scene.materials.bind(BIND_SSBO_MATERIALS);
        scene.textureHandles.bind(BIND_SSBO_TEXTURE_HANDLES);
        scene.bvhNodes.bind(BIND_SSBO_BVH_NODES);
        scene.bvhFaceIndices.bind(BIND_SSBO_BVH_FACE_INDICES);
        scene.directionalLights.bind(BIND_UBO_LIGHT);
        scene.noise.bind(TEX_UNIT_NOISE);

        scene.lightMapGbuffer.bindColorTexture(FRAME_ATTACHMENT_POSITION, TEX_UNIT_POSITION);
        scene.lightMapGbuffer.bindColorTexture(FRAME_ATTACHMENT_NORMAL, TEX_UNIT_NORMAL);

        lightMapPathTraceShader.bind();
        lightMapPathTraceShader.setUniform1i("positionTexture", TEX_UNIT_POSITION);
        lightMapPathTraceShader.setUniform1i("normalTexture", TEX_UNIT_NORMAL);
        lightMapPathTraceShader.setUniform1ui("screenWidth", cpuModel.lightmap.width);
        lightMapPathTraceShader.setUniform1ui("lightCount", scene.directionalLights.count());

        if (hasHdri) scene.hdri.bind(TEX_UNIT_HDRI);
        
        lightMapPathTraceShader.setUniform1i("noiseTexture", TEX_UNIT_NOISE);
        lightMapPathTraceShader.setUniform1i("hdri", TEX_UNIT_HDRI);
        lightMapPathTraceShader.setUniform1i("hasHdri", hasHdri ? 1 : 0);

        lightMapPathTraceShader.setUniform1ui("maxBounces", config.maxBounces);
        lightMapPathTraceShader.setUniform1ui("minSamples", config.minSamples);
        lightMapPathTraceShader.setUniform1f("relativeError", config.relativeError);
        lightMapPathTraceShader.setUniform1f("absoluteError", ABSOLUTE_ERROR);

        const int width  = cpuModel.lightmap.width;
        const int height = cpuModel.lightmap.height;

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);

        // Preserve the previous sample while tiles are replaced.
        scene.lightMaps[scene.lightMapwriteIndex].bindFrameBuffer();
        scene.lightMaps[scene.lightMapreadIndex].bindFrameBuffer(GL_READ_FRAMEBUFFER);

        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(
            0, 0, width, height,
            0, 0, width, height,
            GL_COLOR_BUFFER_BIT, GL_NEAREST
        );

        scene.lightMaps[scene.lightMapwriteIndex].bindFrameBuffer();

        // Submit tiles asynchronously without CPU-GPU synchronization.
        glEnable(GL_SCISSOR_TEST);

        for (int y = 0; y < height; y += config.tileHeight) {
            for (int x = 0; x < width; x += config.tileWidth) {
                glfwPollEvents();

                if (glfwWindowShouldClose(window) ||
                    glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                    glDisable(GL_SCISSOR_TEST);
                    return false;
                }

                GLsizei validTileWidth = std::min(config.tileWidth, width - x);
                GLsizei validTileHeight = std::min(config.tileHeight, height - y);

                glScissor(x, y, validTileWidth, validTileHeight);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
        }

        glDisable(GL_SCISSOR_TEST);

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        
        glfwGetFramebufferSize(window, &windowResolutionX, &windowResolutionY);
        if (windowResolutionX > 0 && windowResolutionY > 0) {
            glReadBuffer(GL_COLOR_ATTACHMENT0);

            RenderTexture::bindDefaultFrameBuffer(GL_DRAW_FRAMEBUFFER);
            glBlitFramebuffer(
                0, 0, width, height,
                0, 0, windowResolutionX, windowResolutionY,
                GL_COLOR_BUFFER_BIT, GL_NEAREST
            );
            glfwSwapBuffers(window);

            scene.lightMaps[scene.lightMapwriteIndex].bindFrameBuffer();
        }

        std::swap(scene.lightMapreadIndex, scene.lightMapwriteIndex);
        return true;
    };
    

    /*  
        Denoise Light Map Render Pass
    */
    auto denoisePass = [&](){
        denoiseShader.bind();

        // ping pong shaders
        auto* shReadBuffer  = &scene.shCoefficients;
        auto* shWriteBuffer = &scene.shDenoiseScratch;
        scene.pixelStates.bind(BIND_SSBO_PIXEL_STATE);
        
        scene.lightMapGbuffer.bindColorTexture(FRAME_ATTACHMENT_POSITION, TEX_UNIT_POSITION);
        scene.lightMapGbuffer.bindColorTexture(FRAME_ATTACHMENT_NORMAL, TEX_UNIT_NORMAL);
        
        denoiseShader.setUniform1i("positionTexture", TEX_UNIT_POSITION);
        denoiseShader.setUniform1i("normalTexture", TEX_UNIT_NORMAL);
        denoiseShader.setUniform1f("normalExponent", 4.0f);
        denoiseShader.setUniform1f("luminancePhi", 12.0f);
        denoiseShader.setUniform1f("minLuminanceStdDev", 0.001f);
        denoiseShader.setUniform1f("relativeVarianceFloor", 0.05f);

        denoiseShader.setUniform1f("maxPlaneDistance", 2.0f * texelSize);
        denoiseShader.setUniform1i("irradianceTexture", TEX_UNIT_LIGHT_MAP);

        for (int pass = 0; pass < 4; ++pass) {
            denoiseShader.setUniform1i("firstPass", pass == 0);
            
            const int stepWidth = 1 << pass; // 1 2 4 8
            
            shReadBuffer->bind(BIND_SSBO_L1_SH);
            shWriteBuffer->bind(BIND_SSBO_L1_SH_OUTPUT);
            
            denoiseShader.setUniform1i("stepWidth", stepWidth);
            denoiseShader.setUniform1f("maxWorldDistance", 3.0f * texelSize * stepWidth);
            
            scene.lightMaps[scene.lightMapreadIndex].bindColorTexture(FRAME_ATTACHMENT_LIGHT_MAP, TEX_UNIT_LIGHT_MAP);
            scene.lightMaps[scene.lightMapwriteIndex].bindColorImage(FRAME_ATTACHMENT_LIGHT_MAP, 0, GL_WRITE_ONLY, GL_RGBA16F);
            
            glDispatchCompute((cpuModel.lightmap.width + 7u) / 8u, (cpuModel.lightmap.height + 7u) / 8u, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            
            // ping pong render target and ssbos
            std::swap(shReadBuffer, shWriteBuffer);
            std::swap(scene.lightMapreadIndex, scene.lightMapwriteIndex);
        }

        scene.shCoefficients.bind(BIND_SSBO_L1_SH);
        glSsbo<glm::vec4>::unbind(BIND_SSBO_L1_SH_OUTPUT);
    };

    /*
        Raster Shadow Mapping Render Pass
    */
    auto shadowPass = [&](){
        scene.vao.bind();
        scene.materials.bind(BIND_SSBO_MATERIALS);
        scene.textureHandles.bind(BIND_SSBO_TEXTURE_HANDLES);
        scene.faceData.bind(BIND_SSBO_FACE_DATA);
        shadowShader.bind();
        
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);
        glDisable(GL_CULL_FACE);

        for (size_t i = 0; i < lights.size(); ++i) {
            glm::vec3 lightDirection = glm::normalize(lights[i].direction);
            glm::vec3 up             = Transform::WorldY;
            if (glm::abs(glm::dot(lightDirection, up)) > 0.98f) up = Transform::WorldZ;

            glm::vec3 target          = (boundsMin + boundsMax) * 0.5f;
            glm::mat4 lightView       = glm::lookAt(target - lightDirection * radius * 2.0f, target, up);
            glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, radius * 0.5f, radius * 3.5f);
            glm::mat4 lightViewProj   = lightProjection * lightView;
            
            scene.shadowLightMatrices[i] = lightViewProj;
            scene.shadowMaps[i].bindFrameBuffer();
    
            shadowShader.setUniformMat4f("lightMvp", lightViewProj);

            glClear(GL_DEPTH_BUFFER_BIT);
            glDrawElements(GL_TRIANGLES, scene.faces.count() * 3, GL_UNSIGNED_INT, nullptr);
        }

        glCullFace(GL_BACK);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_CULL_FACE);
    };

    /*
        Raster Gbuffer pass
    */
    auto gBufferPass = [&](){
        scene.vao.bind();
        scene.materials.bind(BIND_SSBO_MATERIALS);
        scene.textureHandles.bind(BIND_SSBO_TEXTURE_HANDLES);
        scene.faceData.bind(BIND_SSBO_FACE_DATA);
        gBufferShader.bind();
        
        glm::mat4 view = camera.getViewTransform();
        glm::mat4 projection = camera.getPerspectiveTransform(scene.viewportWidth, scene.viewportHeight);
        gBufferShader.setUniformMat4f("model", glm::mat4(1.0f));
        gBufferShader.setUniformMat4f("mvp", projection * view);
        
        scene.gBuffer.bindFrameBuffer();
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, scene.faces.count() * 3, GL_UNSIGNED_INT, nullptr);
    };

    /*
        Raster Deferred Shading
    */
    auto deferredLightingPass = [&](){
        deferredLightingShader.bind();
        deferredLightingShader.setUniform1ui("lightMapWidth", cpuModel.lightmap.width);
        deferredLightingShader.setUniform1ui("lightMapHeight", cpuModel.lightmap.height);

        scene.shCoefficients.bind(BIND_SSBO_L1_SH);
        
        for (size_t i = 0; i < lights.size(); ++i) {
            scene.shadowMaps[i].bindDepthTexture(8 + static_cast<GLuint>(i));
            deferredLightingShader.setUniformMat4f("shadowLightMatrices[" + std::to_string(i) + "]", scene.shadowLightMatrices[i]);
        }
        
        scene.gBuffer.bindColorTexture(FRAME_ATTACHMENT_LIGHT_MAP_COORD, 0);
        
        scene.lightMaps[scene.lightMapreadIndex].bindColorTexture(FRAME_ATTACHMENT_LIGHT_MAP, 1);
        scene.lightMapGbuffer.bindColorTexture(FRAME_ATTACHMENT_NORMAL, 5);
        
        scene.gBuffer.bindColorTexture(FRAME_ATTACHMENT_NORMAL, 2);
        scene.gBuffer.bindColorTexture(FRAME_ATTACHMENT_ALBEDO, 3);
        scene.gBuffer.bindColorTexture(FRAME_ATTACHMENT_RMO, FRAME_ATTACHMENT_RMO);
        scene.gBuffer.bindDepthTexture(6);
        scene.gBuffer.bindColorTexture(FRAME_ATTACHMENT_EMISSION, 7);
        
        scene.directionalLights.bind(BIND_UBO_LIGHT);
        
        deferredLightingShader.setUniformVec3f("cameraPosition", camera.position);
        deferredLightingShader.setUniformMat4f("invViewProj", glm::inverse(
            camera.getPerspectiveTransform(scene.viewportWidth, scene.viewportHeight) * camera.getViewTransform()));
        deferredLightingShader.setUniform1ui("lightCount", scene.directionalLights.count());
        
        scene.deferredLighting.bindFrameBuffer();
        glDisable(GL_DEPTH_TEST);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };

    /*
        Exposure adjustments and tone-mapping
    */
    auto postProcessPass = [&](){
        postProcessShader.bind();
        postProcessShader.setUniform1i("inputColor", 0);
        postProcessShader.setUniform1f("exposure", 3.5f);

        scene.deferredLighting.bindColorTexture(0, 0);
        
        RenderTexture::bindDefaultFrameBuffer();
        glViewport(0, 0, scene.viewportWidth, scene.viewportHeight);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };

    /*
        Begin Drawing using Path Tracing
    */
    std::cout << "Starting lightmap baking path tracer..." << std::endl;
    glfwSwapInterval(0);
    
    std::cout << "Computing Lightmap Gbuffer..." << std::endl;
    lightMapGbufferPass();

    glfwShowWindow(window);
    std::cout << "Computing Indirect Lighting..." << std::endl;
    for (unsigned int i = 0; i < config.numberOfSamples; ++i) {
        if (!lightMapPathTracePass()) return EXIT_SUCCESS;
        std::cout << "Sample " << i + 1 << "/" << config.numberOfSamples << std::endl;
    }

    // Path Tracing is the last consumer of the GPU BVH
    glFinish();
    glSsbo<BvhNode>::unbind(BIND_SSBO_BVH_NODES);
    glSsbo<unsigned int>::unbind(BIND_SSBO_BVH_FACE_INDICES);
    scene.bvhNodes.allocateStorage(0);
    scene.bvhFaceIndices.allocateStorage(0);

    std::cout << std::endl << "Denoising lightmap..." << std::endl;
    denoisePass();
    
    // Denoising is the last consumer of the sampling state, write lightmap, and write sh buffer
    glFinish();
    glSsbo<glPixelState>::unbind(BIND_SSBO_PIXEL_STATE);
    scene.pixelStates.allocateStorage(0);
    scene.lightMapGbuffer.clearColorTexture(FRAME_ATTACHMENT_POSITION);
    scene.lightMapGbuffer.clearColorTexture(FRAME_ATTACHMENT_ALBEDO);
    scene.shDenoiseScratch.allocateStorage(0);
    scene.lightMaps[scene.lightMapwriteIndex].clearColorTextures();

    /*
        Begin Drawing using rasterization
    */
    std::cout << "Finished lightmap global illumination" << std::endl;
    for (size_t i = 0; i < lights.size(); ++i) {
        if (!scene.shadowMaps[i].addDepthTexture(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE)) return EXIT_FAILURE;
        scene.shadowMaps[i].setDepthFiltering(GL_LINEAR, GL_LINEAR);
        scene.shadowMaps[i].setDepthCompareMode(GL_COMPARE_REF_TO_TEXTURE, GL_LEQUAL);
    }

    // Configure window attachments for rasterization
    glfwGetFramebufferSize(window, &windowResolutionX, &windowResolutionY);
    scene.viewportWidth  = std::max(windowResolutionX, 1);
    scene.viewportHeight = std::max(windowResolutionY, 1);
    const int gBufferAttachments[] = {
        FRAME_ATTACHMENT_LIGHT_MAP_COORD, 
        FRAME_ATTACHMENT_NORMAL,
        FRAME_ATTACHMENT_ALBEDO, 
        FRAME_ATTACHMENT_RMO, 
        FRAME_ATTACHMENT_EMISSION
    };

    for (int attachment : gBufferAttachments) {
        if (!scene.gBuffer.addColorTexture(attachment, scene.viewportWidth, scene.viewportHeight, GL_FLOAT,
            attachment == FRAME_ATTACHMENT_LIGHT_MAP_COORD ? GL_RG32F : GL_RGBA16F,
            attachment == FRAME_ATTACHMENT_LIGHT_MAP_COORD ? GL_RG : GL_RGBA, false)) 
        {
            return EXIT_FAILURE;
        }

        scene.gBuffer.setColorFiltering(attachment, GL_NEAREST, GL_NEAREST);
    }

    if (!scene.gBuffer.addDepthTexture(scene.viewportWidth, scene.viewportHeight) ||
        !scene.deferredLighting.addColorTexture(0, scene.viewportWidth, scene.viewportHeight, GL_FLOAT, GL_RGBA16F, GL_RGBA, false)) 
    {
        return EXIT_FAILURE;
    }
    
    scene.deferredLighting.setColorFiltering(0, GL_NEAREST, GL_NEAREST);

    /*
        Raster window loop
    */
    glfwSetWindowTitle(window, "Baked Scene Diffuse GI - Raster");
    glfwSwapInterval(1);
    
    shadowPass();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        // ESC leaves window
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        
        // ignore window with 0 resolution
        glfwGetFramebufferSize(window, &windowResolutionX, &windowResolutionY);
        if (windowResolutionX == 0 || windowResolutionY == 0) {
            glfwWaitEventsTimeout(0.05);
            continue;
        }
        
        // Resize render textures to window size
        if (scene.viewportWidth != windowResolutionX || scene.viewportHeight != windowResolutionY) {
            scene.viewportWidth = windowResolutionX;
            scene.viewportHeight = windowResolutionY;
            if (!scene.gBuffer.resize(scene.viewportWidth, scene.viewportHeight) ||
                !scene.deferredLighting.resize(scene.viewportWidth, scene.viewportHeight))
                return EXIT_FAILURE;
        }

        // Raster draw loop
        camera.update(window, config.cameraSpeed);
        gBufferPass();
        deferredLightingPass();
        postProcessPass();
        glfwSwapBuffers(window);
    }

    return EXIT_SUCCESS;
}
