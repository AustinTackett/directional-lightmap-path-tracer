#include "src/bvh/Bvh.hpp"
#include "src/scene/model/Texture2D.hpp"
#include <cstdlib>
#include <vector>

#include <GL/glew.h>
#define  GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "glSsbo.hpp"
#include "glVao.hpp"


#include "glTexture2D.hpp"
#include "glRenderTexture.hpp"
#include "glShader.hpp"

#include "Model.hpp"
#include "Camera.hpp"
#include "SunLight.hpp"

int main(int argc, char** argv)
{
    if (argc < 2) return EXIT_FAILURE;
    const std::string modelPath = argv[1];

    /*
        Create Window
    */

    int glfwErrorCode = glfwInit();
    if (GLFW_TRUE != glfwErrorCode)
    {
        std::cout << "Could not initialize GLFW" << std::endl;
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    int windowResolutionX = 1280;
    int windowResolutionY = 960;
    GLFWwindow* window = glfwCreateWindow(windowResolutionX, windowResolutionY, "InteractiveGraphicsProject", NULL, NULL);
    if (!window) {
        std::cout << "Could not initialize a GLFW window" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwMakeContextCurrent(window);
    if (glfwGetCurrentContext() != window) {
        std::cout << "Could not GLFW window current context" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glewExperimental = GL_TRUE;
    GLenum glewErrorCode = glewInit();
    if (GLEW_OK != glewErrorCode) {
        std::cout << "Could not initialize GLEW" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }

    /*
        Load Scene
    */

    struct glScene
    {
        // CPU data
        SunLight  sunlight;
        Camera    camera;
        Model     cpuModel;

        // GPU data handles
        glVao          vao;
        glSsbo<Vertex> vertices;
        glSsbo<Face>   faces;
    
        std::vector<glTexture2D> textures;
        glSsbo<Material>         materials;
        glSsbo<unsigned int>     faceMaterialIndex;

        glSsbo<BvhNode>      bvhNodes;
        glSsbo<unsigned int> bvhFaceIndices;

        RenderTexture lightMap;
    };

    glScene scene{};

    if (!scene.cpuModel.loadModel(modelPath)) {
        return EXIT_FAILURE;
    }

    scene.vertices.setData(scene.cpuModel.vertices);
    scene.faces.setData(scene.cpuModel.faces);
    scene.materials.setData(scene.cpuModel.materials);
    scene.bvhNodes.setData(scene.cpuModel.bvh.nodes);
    scene.bvhFaceIndices.setData(scene.cpuModel.bvh.primitiveIndices);

    auto loadTexture = [](glTexture2D& texture, const Texture2D& cpuTexture)
    {
        texture.setFiltering(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);
        texture.setWrapMode(GL_REPEAT, GL_REPEAT);
        texture.createFromData(
            cpuTexture.data,
            static_cast<GLsizei>(cpuTexture.metaData.width),
            static_cast<GLsizei>(cpuTexture.metaData.height),
            GL_UNSIGNED_BYTE,
            GL_RGBA8,
            GL_RGBA,
            false
        );
    };

    int numTextures = scene.textures.size();
    scene.textures.resize(numTextures);
    for (int i = 0; i < numTextures; ++i) {
        loadTexture(scene.textures[i], scene.cpuModel.textures[i]);
    }

    scene.lightMap.setFiltering(GL_LINEAR, GL_LINEAR);
    bool lightMapSuccess = scene.lightMap.addColorTexture(
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

    auto updateCamera = [](Camera& camera) {
        // user io
    };

    /*
        Compile Shaders
    */

    const std::string LIGHTMAP_PATH_TRACE_VERTEX_SHADER = "src/shaders/1-lightmap-path-tracing/LightMapPathTrace.vert";
    const std::string LIGHTMAP_PATH_TRACE_FRAG_SHADER   = "src/shaders/1-lightmap-path-tracing/LightMapPathTrace.frag";

    glShader lightMapPathTraceShader;
    if (!lightMapPathTraceShader.loadFromSource(LIGHTMAP_PATH_TRACE_VERTEX_SHADER, LIGHTMAP_PATH_TRACE_FRAG_SHADER)) {
        std::cout << "Could not load shader" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }

    /*
        Render Passes
    */

    enum SsboBinding : GLuint {
        BIND_BVH_NODES = 0, 
        BIND_BVH_FACE_INDICES = 1, 
        BIND_VERTICES = 2, 
        BIND_FACES = 3, 
        BIND_MATERIALS = 4
    };

    enum TextureUnits : GLuint {
        UNIT_LIGHT_MAP = 0,
        UNIT_BASE_COLOR = 1,
        UNIT_METALLIC_ROUGHNESS = 2,
        UNIT_NORMAL = 3,
        UNIT_EMISSIVE = 4,
        UNIT_OCCLUSION = 5
    };

    auto lightMapPathTracePass = [&](){
        scene.vao.bind();
        scene.vertices.bind(BIND_VERTICES);
        scene.faces.bind(BIND_FACES);
        scene.bvhNodes.bind(BIND_BVH_NODES);
        scene.bvhFaceIndices.bind(BIND_BVH_FACE_INDICES);

        lightMapPathTraceShader.bind();
        lightMapPathTraceShader.setUniform1ui("faceCount", scene.faces.count());
        lightMapPathTraceShader.setUniform1ui("bvhNodeCount", scene.bvhNodes.count());

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClear(GL_COLOR_BUFFER_BIT);

        // manual vertex pulling instead of draw elements
        scene.lightMap.bindFrameBuffer();
        glDrawArrays(GL_TRIANGLES, 0, scene.faces.count() * 3); 
    };

    
    return 0;
}
