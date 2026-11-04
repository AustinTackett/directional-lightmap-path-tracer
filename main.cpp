#include "src/bvh/Bvh.hpp"
#include "src/scene/model/Texture2D.hpp"
#include <cstdlib>
#include <limits>
#include <stdexcept>
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
        Shaders
    */

    const std::string PATH_TRACE_VERTEX_SHADER = "src/shaders/1-path-tracing/PathTrace.vert";
    const std::string PATH_TRACE_FRAG_SHADER   = "src/shaders/1-path-tracing/PathTrace.frag";

    const std::string GBUFFER_VERTEX_SHADER    = "src/shaders/2-gbuffer/GBuffer.vert";
    const std::string GBUFFER_FRAG_SHADER      = "src/shaders/2-gbuffer/GBuffer.frag";

    glShader pathTraceShader;
    if (!pathTraceShader.loadFromSource(PATH_TRACE_VERTEX_SHADER, PATH_TRACE_FRAG_SHADER)) {
        std::cout << "Could not load shader" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glShader gbufferShader;
    if (!gbufferShader.loadFromSource(GBUFFER_VERTEX_SHADER, GBUFFER_FRAG_SHADER)) {
        std::cout << "Could not load shader" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }

    /*
        Load Model's Geometry
    */

    Model model;
    if (!model.loadModel(modelPath)) return EXIT_FAILURE;

    glVao vao;
    vao.bind();

    glSsbo<Vertex> vertices;
    glSsbo<Face>   faces;
    vertices.setData(model.vertices);
    faces.setData(model.faces);

    /*
        Load Model's Materials
    */

    glSsbo<Material> materials;
    materials.setData(model.materials);

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

    glTexture2D baseColorTexture;
    glTexture2D metallicRoughnessTexture;
    glTexture2D normalTextureIndexTexture;
    glTexture2D emmissiveTexture;
    glTexture2D occlusionTexture;

    /*
        Load Model's BLAS
    */

    glSsbo<BvhNode>      bvhNodes;
    glSsbo<unsigned int> bvhFaceIndices;

    bvhNodes.setData(model.bvh.nodes);
    bvhFaceIndices.setData(model.bvh.primitiveIndices);

    /*
        Load Model's Light Map (Render To Texture)
    */

    RenderTexture lightmapTexture;
    lightmapTexture.setFiltering(GL_LINEAR, GL_LINEAR);
    bool lightMapSuccess = lightmapTexture.addTextureTargets(
        1,
        static_cast<GLsizei>(model.lightmap.width),
        static_cast<GLsizei>(model.lightmap.height),
        false,
        false,
        GL_FLOAT,
        GL_RGBA16F,
        GL_RGBA,
        false);
    if (!lightMapSuccess) {
        std::cerr << "Failed to generate lightmap render target" << std::endl;
        return EXIT_FAILURE;
    }

    /*
        Sunlight 
    */

    SunLight sunlight;

    /*
        User Camera IO
    */

    Camera camera;
    auto updateCamera = []() {
        
    };

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
        UNIT_LIGHT_MAP,
        UNIT_BASE_COLOR,
        UNIT_METALLIC_ROUGHNESS,
        UNIT_NORMAL,
        UNIT_EMISSIVE,
        UNIT_OCCLUSION,
    };

    auto pathTracePass = [&]() {
        vao.bind();
        bvhNodes.bind(BIND_BVH_NODES);
        bvhFaceIndices.bind(BIND_BVH_FACE_INDICES);
        vertices.bind(BIND_VERTICES);
        faces.bind(BIND_FACES);
        materials.bind(BIND_MATERIALS);

        pathTraceShader.bind();
        pathTraceShader.setUniformMat4f("modelMatrix", model.transform.getModelTransform());
        pathTraceShader.setUniform1ui("faceCount", faces.count());
        pathTraceShader.setUniform1ui("bvhNodeCount", bvhNodes.count());

        lightmapTexture.bindFrameBuffer();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClear(GL_COLOR_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLES, 0, model.faces.size() * 3);
    };

    auto drawMaterialPass = [&]() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gbufferShader.setUniformMat4f("modelMatrix", model.transform.getModelTransform());
        gbufferShader.setUniformMat4f("viewMatrix", camera.getViewTransform());
        gbufferShader.setUniformMat4f("perspectiveMatrix", camera.getPerspectiveTransform(width, height));

        lightmapTexture.bindColorTexture(0, UNIT_LIGHT_MAP);
        gbufferShader.setUniform1i("lightMap", UNIT_LIGHT_MAP);

        

        gbufferShader.setUniform1i(name, static_cast<GLint>(unit));
        gbufferShader.setUniform1i("has_" + name, index >= 0);  

        for (const SubMesh& subMesh : model.subMeshes) {
            const Material fallback{};
            const Material& material = subMesh.materialIndex < 0 ? fallback :
                model.materials.at(static_cast<size_t>(subMesh.materialIndex));
            gbufferShader.setUniform1i("materialIndex", subMesh.materialIndex);
            // A negative materialIndex tells GLSL to use default factors.
            bindTexture("baseColor", material.baseColorTextureIndex, 1);
            bindTexture("normal", material.normalTextureIndex, 2);
            bindTexture("emissive", material.emissiveTextureIndex, 3);
            bindTexture("metallicRoughness", material.metallicRoughnessTextureIndex, 4);
            bindTexture("occlusion", material.occlusionTextureIndex, 5);
            gbufferShader.setUniform1ui("firstCorner", subMesh.indexStart);
            // Pull corner = firstCorner + uint(gl_VertexID).
            drawCorners(subMesh.indexCount);
        }
    };

    // drawLightMapPass();
    // drawMaterialPass(viewProjection, framebufferWidth, framebufferHeight);
    return 0;
}
