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
    enum UboBindings : GLuint {
        BIND_UBO_LIGHT = 0,
    };

    enum SsboBindings : GLuint {
        BIND_SSBO_BVH_NODES = 0, 
        BIND_SSBO_BVH_FACE_INDICES = 1, 
        BIND_SSBO_VERTICES = 2, 
        BIND_SSBO_FACES = 3, 
        BIND_SSBO_MATERIALS = 4,
        BIND_SSBO_TEXTURE_HANDLES = 5,
        BIND_SSBO_FACE_DATA = 6
    };

    enum TextureUnits : GLuint {
        TEX_UNIT_HDRI = 6
    };

    enum FrameColorAttachments : GLuint {
        FRAME_ATTACHMENT_LIGHT_MAP = 0
    };

    if (argc < 2) {
        std::cerr << "Model path must be passed as first command arg" << std::endl;
        std::cerr << "An optional hdri may be passed as the second command arg" << std::endl;
        return EXIT_FAILURE;
    }
    const std::string modelPath = argv[1];
    const std::string hdriPath  = argc > 2 ? argv[2] : "";

    /*
        Create Window
    */

    int glfwErrorCode = glfwInit();
    if (GLFW_TRUE != glfwErrorCode)
    {
        std::cerr << "Could not initialize GLFW" << std::endl;
        return EXIT_FAILURE;
    }

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

    /*
        Load Scene
    */

    struct glScene
    {
        // CPU data
        SingularityLight           sunlight;
        Camera                     camera;
        Model                      cpuModel;
        Texture2D                  cpuHdri;

        // GPU data handles
        glVao                      vao;
        glSsbo<Vertex>             vertices;
        glSsbo<Face>               faces;
    
        std::vector<glTexture2D>   textures;
        glSsbo<Material>           materials;
        glSsbo<GLuint64>           textureHandles;
        glSsbo<FaceData>           faceData;

        glSsbo<BvhNode>            bvhNodes;
        glSsbo<unsigned int>       bvhFaceIndices;

        glUbo<SingularityLight, 1> singularityLights;
        RenderTexture              lightMap;

        glTexture2D                hdri;
    };

    glScene scene{};

    // load model
    if (!scene.cpuModel.loadModel(modelPath)) {
        std::cerr << "Model failed to load at: " << modelPath << std::endl;
        return EXIT_FAILURE;
    }

    // upload mesh data
    scene.vertices.setData(scene.cpuModel.vertices);
    scene.faces.setData(scene.cpuModel.faces);
    scene.faceData.setData(scene.cpuModel.faceData);
    scene.materials.setData(scene.cpuModel.materials);
    scene.bvhNodes.setData(scene.cpuModel.bvh.nodes);
    scene.bvhFaceIndices.setData(scene.cpuModel.bvh.primitiveIndices);

    // compute texture formats: 
    // srgb/nonlinear = (colors: baseColor/emmission), rgb/linear = (raw data: normal/roughness/etc)
    const size_t numTextures = scene.cpuModel.textures.size();
    std::vector<GLenum> formats(numTextures, GL_RGBA8);
    for (Material& material : scene.cpuModel.materials) {
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
        Texture2D& cpuTexture = scene.cpuModel.textures[i];
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

    // upload environmental lighting
    const bool hasHdri = !hdriPath.empty() && scene.cpuHdri.loadHdri(hdriPath);
    if (hasHdri) {
        scene.hdri.setFiltering(GL_LINEAR, GL_LINEAR);
        scene.hdri.setWrapMode(GL_REPEAT, GL_REPEAT);
        scene.hdri.createFromData(
            scene.cpuHdri.data,
            static_cast<GLsizei>(scene.cpuHdri.metaData.width),
            static_cast<GLsizei>(scene.cpuHdri.metaData.height),
            GL_FLOAT,
            GL_RGBA32F,
            GL_RGBA,
            false
        );
    } else {
        std::cout << "No HDRI found. Build running without image based lighting." << std::endl;
    }

    // upload light map texture
    scene.lightMap.setFiltering(GL_LINEAR, GL_LINEAR);
    bool lightMapSuccess = scene.lightMap.addColorTexture(
        FRAME_ATTACHMENT_LIGHT_MAP,
        static_cast<GLsizei>(scene.cpuModel.lightmap.width),
        static_cast<GLsizei>(scene.cpuModel.lightmap.height),
        GL_FLOAT,
        GL_RGBA16F,
        GL_RGBA,
        false);
    if (!lightMapSuccess) {
        std::cerr << "Failed to generate lightmap render target" << std::endl;
        return EXIT_FAILURE;
    }

    /*
        User IO
    */

    auto updateCamera = [](Camera& camera) {
        // user io
    };

    /*
        Lighting
    */

    scene.sunlight.lightType  = SingularityLight::DIRECTIONAL_LIGHT;
    scene.sunlight.position   = glm::vec3(0.0f);
    scene.sunlight.direction  = glm::vec3(0.0f, -1.0f, 0.0f);
    scene.sunlight.irradiance = glm::vec3(1.0f);
    scene.singularityLights.setData({scene.sunlight});

    /*
        Compile Shaders
    */

    const std::string LIGHTMAP_PATH_TRACE_VERTEX_SHADER = "shaders/1-lightmap-path-tracing/LightMapPathTrace.vert";
    const std::string LIGHTMAP_PATH_TRACE_FRAG_SHADER   = "shaders/1-lightmap-path-tracing/LightMapPathTrace.frag";

    glShader lightMapPathTraceShader;
    if (!lightMapPathTraceShader.loadFromSource(LIGHTMAP_PATH_TRACE_VERTEX_SHADER, LIGHTMAP_PATH_TRACE_FRAG_SHADER)) {
        std::cout << "Could not load shader" << std::endl;
        return EXIT_FAILURE;
    }

    /*
        Render Passes
    */

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

        lightMapPathTraceShader.bind();
        lightMapPathTraceShader.setUniform1ui("faceCount", scene.faces.count());
        lightMapPathTraceShader.setUniform1ui("bvhNodeCount", scene.bvhNodes.count());
        lightMapPathTraceShader.setUniform1ui("screenWidth", scene.cpuModel.lightmap.width);
        lightMapPathTraceShader.setUniform1ui("screenHeight", scene.cpuModel.lightmap.height);
        lightMapPathTraceShader.setUniform1ui("lightCount", scene.singularityLights.count());
        
        if (hasHdri) scene.hdri.bind(TEX_UNIT_HDRI);
        lightMapPathTraceShader.setUniform1i("hdri", TEX_UNIT_HDRI);
        lightMapPathTraceShader.setUniform1i("hasHdri", hasHdri ? 1 : 0);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        scene.lightMap.bindFrameBuffer();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // manual vertex pulling instead of draw elements
        glDrawArrays(GL_TRIANGLES, 0, scene.faces.count() * 3); 
    };
    
    // Draw
    lightMapPathTracePass();

    // Temporary: export lightmap as png
    {
    const int width  = scene.cpuModel.lightmap.width;
    const int height = scene.cpuModel.lightmap.height;
    // Read HDR irradiance
    std::vector<float> hdrPixels(width * height * 4);
    glReadBuffer(GL_COLOR_ATTACHMENT0 + FRAME_ATTACHMENT_LIGHT_MAP);
    glReadPixels(0, 0, width, height,
                GL_RGBA, GL_FLOAT, hdrPixels.data());
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
        pixels[i + 3] = 255;
    }
    stbi_flip_vertically_on_write(1);
    stbi_write_png("lightmap.png", width, height, 4, pixels.data(), width * 4);
    stbi_flip_vertically_on_write(0);
    };

    return EXIT_SUCCESS;
}