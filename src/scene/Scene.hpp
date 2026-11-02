#pragma once

#include <string>
#include <vector>

#include "bvh/Bvh.hpp"
#include "bvh/BvhPrimitives.hpp"
#include "bvh/FacePrimitives.hpp"
#include "SunLight.hpp"
#include "Camera.hpp"
#include "LightMap.hpp"
#include "Mesh.hpp"
#include "Transform.hpp"

/*
    A class that wraps data in the scene that is registered within the rendering pipeline
*/

class Scene
{
    SunLight sunLight;
    LightMap lightMap; // One texture per scene
    Camera   camera;
    Bvh      tlas;
    
    std::vector<Bvh>       meshBlases;
    std::vector<Transform> meshTransforms;
    std::vector<Mesh>      meshes;

    void SetSunLight(SunLight sunLight) {
        this->sunLight = sunLight;
    }

    void SetCamera(Camera camera) {
        this->camera = camera;
    }

    // Meshes, transforms, and BLASes share the same scene index.
    bool RegisterModel(unsigned int& modelIndex, const std::string& meshPath)
    {
        Mesh mesh;
        if (!mesh.LoadMesh(meshPath)) {
            modelIndex = 0;
            return false;
        }
        meshes.push_back(std::move(mesh));
        meshTransforms.emplace_back();
        meshBlases.emplace_back();
        modelIndex = static_cast<unsigned int>(meshes.size() - 1);
        return true;
    }

    Transform& getModelTransform(unsigned int modelIndex)
    {
        return meshTransforms[modelIndex];
    }

    bool StartScene()
    {
        // Build Light Maps (May modify mesh data)
        if (!lightMap.BuildFromMeshes(meshes)) {
            return false;
        }

        // Build static BLAS
        for (size_t i = 0; i < meshes.size(); ++i) {
            FacePrimitives faces(meshes[i]);
            if (!meshBlases[i].BuildBvh(faces)) {
                return false;
            }
        }

        // Build dynamic TLAS
        BvhPrimitives instances(meshBlases, meshTransforms);
        return tlas.BuildBvh(instances);
    }

    bool UpdateScene()
    {
        // Rebuild TLAS
        BvhPrimitives instances(meshBlases, meshTransforms);
        return tlas.BuildBvh(instances);
    }
};
