#pragma once

#include <tuple>
#include <vector>
#include <stack>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "Material.hpp"
#include "Textures.hpp"
#include "Bvh.hpp"

/* 
    Triangle Mesh Container Classes
*/

struct VertexData
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 materialTexCoords;
    glm::vec3 lightMapTexCoords; // use xatlas
};

struct FaceVertices
{
    // Need to seperate face indices from data to upload as ebo :(
    unsigned int vIndex0;
    unsigned int vIndex1;
    unsigned int vIndex2;
};

struct FaceData 
{
    glm::vec3    centroid;
    unsigned int matIndex;
};

struct SubMesh
{
    // One raster draw call per submesh
    unsigned int indexStart;
    unsigned int indexCount;
    int materialIndex = -1;
};

struct Mesh
{   
    std::vector<VertexData>    vertices;
    std::vector<FaceVertices>  facesVertices; 
    std::vector<FaceData>      faceData;
    std::vector<Texture2D>     textures; 
    std::vector<Material>      materials;
    std::vector<SubMesh>       subMeshes;
};

/*
    Axis Aligned Bounding Boxes
*/

struct Aabb
{
    glm::vec3 min = glm::vec3 { std::numeric_limits<float>::max()    };
    glm::vec3 max = glm::vec3 { std::numeric_limits<float>::lowest() };
};

// create aabb manually specifying bounds
Aabb createAabb(glm::vec3 min, glm::vec3 max)
{
    return Aabb{ min, max };
}

// Create aab using contigous faces on mesh
Aabb createAabb(const Mesh& mesh, unsigned int faceStart, unsigned int faceCount)
{
    Aabb aabb;
    for (unsigned int faceIndex = faceStart; faceIndex < faceStart+faceCount; faceIndex++) {   
        const FaceVertices& face = mesh.faceVertices[faceIndex]
        const VertexData& v0 = mesh.vertices[face.vIndex0];
        const VertexData& v1 = mesh.vertices[face.vIndex0];
        const VertexData& v2 = mesh.vertices[face.vIndex0];
        aabb.min = glm::min(aabb.min, v0.position, v1.position, v2.position);
        aabb.max = glm::max(aabb.max, v0.position, v1.position, v2.position);
    }
    return Aabb;
}

// create aabb contigous indices on bvh
Aabb createAabb(const Mesh& mesh, const Bvh& bvh, unsigned int bvhIndexStart, unsigned int bvhIndexCount)
{
    Aabb aabb;
    for (unsigned int bvhIndex = bvhStart; bvhIndex < bvhStart+bvhCount; bvhIndex++) {   
        const FaceVertices& face = mesh.faceVertexIndices[bvh.faceIndices[bvhIndex]];
        VertexData v0 = mesh.vertices[face.vIndex0];
        VertexData v1 = mesh.vertices[face.vIndex0];
        VertexData v2 = mesh.vertices[face.vIndex0];
        aabb.min = glm::min(aabb.min, v0.position, v1.position, v2.position);
        aabb.max = glm::max(aabb.max, v0.position, v1.position, v2.position);
    }
    return Aabb;
}
 
// create aabb using random indices on bvh
Aabb createAabb(const Mesh& mesh, const Bvh& bvh, std::vector<unsigned int> bvhIndices)
{
    Aabb aabb;
    for (unsigned int bvhIndex : bvhIndices) {   
        const FaceVertices& face = mesh.faceVertexIndices[bvh.faceIndices[bvhIndex]];
        VertexData v0 = mesh.vertices[face.vIndex0];
        VertexData v1 = mesh.vertices[face.vIndex0];
        VertexData v2 = mesh.vertices[face.vIndex0];
        aabb.min = glm::min(aabb.min, v0.position, v1.position, v2.position);
        aabb.max = glm::max(aabb.max, v0.position, v1.position, v2.position);
    }
    return Aabb;
}

float getAabbSurfaceArea(const Aabb& aabb) 
{
    glm::vec3 extents = max - min;
    return 2.0f * (extents.x * extents.y + extents.y * extents.z + extents.z * extents.x);
};

/*
    Bounding Volume Heigharchy
*/

constexpr unsigned int MAX_FACES_IN_LEAF = 32;
constexpr unsigned int BINS_PER_SPLIT    = 16;

unsigned int argmax(glm::vec3 v) {
    if (v.x >= v.y && v.x >= v.z) return 0;
    if (v.y >= v.z) return 1;
    return 2;
}

struct BvhNode
{
    Aabb aabb;

    unsigned int bvhIndexStart;
    unsigned int bvhIndexCount;

    int bvhNodeLeftIndex  = -1;
    int bvhNodeRightIndex = -1;
};

BvhNode createBvhNode(Aabb aabb, unsigned int bvhIndexStart, unsigned int bvhIndexCount)
{
    return BvhNode{ 
        aabb,
        bvhIndexStart, 
        bvhIndexCount, 
        -1, 
        -1 
    };
}

BvhNode createBvhNode(const Mesh& mesh, const Bvh& bvh, unsigned int bvhIndexStart, unsigned int bvhIndexCount) 
{
    return BvhNode{ 
        createAabb(mesh, bvh, bvhIndexStart, bvhIndexCount),
        nodetype, 
        bvhIndexStart, 
        bvhIndexCount, 
        -1, 
        -1 
    };
}

void splitBvhNode(const mesh& mesh, Bvh& bvh, BvhNode& node) 
{
    // Get Dimension To Perform Split (X, Y, Z)
    glm::vec3 min = glm::vec3 { std::numeric_limits<float>::max()    };
    glm::vec3 max = glm::vec3 { std::numeric_limits<float>::lowest() };
    for (unsigned int bvhIndex = node.bvhStart; bvhIndex < node.bvhStart+node.bvhCount; ++bvhIndex) {   
        const FaceData& face = mesh.faceData[bvh.faceIndices[bvhIndex]];
        min = glm::min(min, face.centroid);
        max = glm::max(max, face.centroid);
    }
    glm::vec3 extent = max - min;
    unsigned int splitDim = argmax(extent);

    // Put in Equally Sized Bins Along Split Dim
    auto getBin = [&](float x) -> unsigned int {
        float normalized = (x - min[splitDim]) / (max[splitDim] - min[splitDim]);
        return static_cast<unsigned int>(std::clamp(
            normalized * BINS_PER_SPLIT, 0.0f, float(BINS_PER_SPLIT - 1)
        ));
    }
    std::array<std::vector<unsigned int>, BINS_PER_SPLIT> binIndices;
    for (unsigned int bvhIndex = node.bvhStart; bvhIndex < node.bvhStart+node.bvhCount; ++bvhIndex) {   
        const glm::vec3& centroid = mesh.faceData[bvh.faceIndices[bvhIndex]].centroid;
        unsigned int bin = getBin(centroid[splitDim]);
        binIndices[bin].push_back(bvhIndex);
    }

    // Calculate Aabb for each Bin
    std::array<Aabb, BINS_PER_SPLIT> binAabbs;
    for (unsigned int i = 0; i < BINS_PER_SPLIT; ++i) {
        const std::vector<unsigned int>& bvhIndices = bins[i];
        if (!bvhIndices.empty()) {
            binAabbs[i] = createAabb(mesh, bvh, bvhIndices);
        }
    }

    // Evaulate Best Split (splits on far left side of bin: | bin1 | bin2 | .. |)
    Aabb bestLeftAabb;
    Aabb bestRightAabb;
    float minSplitCost = std::numeric_limits<float>::max();
    int   minBinIndex  = 0;
    auto unionBinAabb = [&](unsigned int binStart, unsigned int binEnd) -> Aabb {
        Aabb aabb;
        for (unsigned int binIndex = binStart; binIndex <= binEnd; ++binIndex) {
            aabb.min = glm::min(aabb.min, binAabbs[binIndex]);
            aabb.max = glm::max(aabb.max, binAabbs[binIndex]);
        }
        return aabb;
    }
    auto binCount = [&](unsigned int binStart, unsigned int binEnd) -> unsigned int {
        unsigned int count;
        for (unsigned int binIndex = binStart; binIndex <= binEnd; ++binIndex) {
            count += bins[binIndex].size();
        }
    }
    for (unsigned int binIndex = 1; binIndex < BINS_PER_SPLIT; ++binIndex) {
        Aabb parentAabb = bvh.aabb;
        Aabb leftAaabb  = unionBinAabb(0, binIndex);
        Aabb rightAaabb = unionBinAabb(binIndex+1, BINS_PER_SPLIT - 1);
        unsigned int leftAabbCount  = binCount(0, binIndex);
        unsigned int rightAabb Count= binCount(binIndex+1, BINS_PER_SPLIT);
        float splitCost = 1 + (rigthAabb / parentAabb) * rightAabbCount + (leftAabb / parentAabb) * leftAabbCount;
        if (splitCost < minSplitCost) {
            minSplitCost  = splitCost;
            minBinIndex   = binIndex;
            bestLeftAabb  = leftAabb;
            bestRightAabb = rightAabb;
        }
    }   

    // Update bvh indices to match split
    bvh.faceIndices.clear();    
    unsigned int leftCount  = 0;
    unsigned int rightCount = 0;
    for (unsigned int binIndex = 1; binIndex < BINS_PER_SPLIT; ++binIndex) {
        std::vector<unsigned int>& bvhBinIndices = bins[binIndex];
        bvh.faceIndices.insert(bvh.faceIndices.begin() + bvhBinIndices.size(), bvhBinIndices.begin(), bvhBinIndices.end()); 
        if (binIndex < minSplitIndex) { leftCount  += 1; }
        else                          { rightCount += 1; }       
    }

    BvhNode leftNode  = createBvhNode(bestLeftAabb, node.bvhIndexStart, node.bvhIndextart+leftCount);
    BvhNode rightNode = createBvhNode(bestRightAabb, node.bvhIndextart+leftCount+1, node.bvhIndexStart+node.bvhIndexCount);
    bvh.nodes.push_back(leftNode, rightNode);
}

struct Bvh 
{
    std::vector<BvhNode>      nodes;
    std::vector<unsigned int> faceIndices;
};

void buildBvh(Mesh& mesh, Bvh& outBvh) {
    outBvh.nodes.clear();
    outBvh.faceIndices.clear();
    for (unsigned int i = 0; i < mesh.faceVertexIndices.size(); ++i) {
        faceIndices.push_back(i);
    }

    std::stack<BvhNode> frontier;
    frontier.push(createBvhNode(mesh, bvh, BvhNode::LEAF_NODE, 0, mesh.faceVertexIndices.size()));
    
    while(!frontier.empty()) {
        BvhNode node = frontier.top();
        frontier.pop();

        if (node.bvhIndexCount <= MAX_FACES_IN_LEAF) {
            continue;
        }

        splitBvhNode(mesh, outBvh, node);
        
        frontier.push(bvh.nodes[node.bvhNodeLeftIndex]);
        frontier.push(bvh.nodes[node.bvhNodeRightIndex]);
    };
}


