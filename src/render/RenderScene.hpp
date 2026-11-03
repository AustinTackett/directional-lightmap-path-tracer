#pragma once

#include "glm/ext/matrix_float4x4.hpp"
#include "glSsbo.hpp"
#include "LightMap.hpp"
#include "mesh/Material.hpp"
#include "mesh/Mesh.hpp"
#include "bvh/Bvh.hpp"

struct glScene
{
    GLuint lightMapTex;

    glSsbo<Vertex>    vertices;
    glSsbo<Face>      faces;
    glSsbo<Mesh>      meshes;
    glSsbo<SubMesh>   subMeshes;
    glSsbo<glm::mat4> meshTransforms;
    
    glSsbo<Material>      materials;
    std::vector<GLuint>   textures;
    std::vector<GLuint64> bindLessTexHandles;

    glSsbo<BvhNode>      blasNodes;
    glSsbo<unsigned int> blasFaceIndices;
    glSsbo<BvhNode>      tlasNodes;
    glSsbo<unsigned int> tlasMeshIndices;
};
