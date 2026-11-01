#pragma once

#include <string>

#include "Bvh.hpp"
#include "SunLight.hpp"
#include "Camera.hpp"
#include "Model.hpp"
#include "Mesh.hpp"
#include "LightMap.hpp"

/*
    Contains entire representation of the scene and is passed to the renering pipeline
*/

struct Scene 
{
    // Scene Lighting
    SunLight  sunLight;
    LightMap  lightMap;  // One texture per scene

    // User Controlled View
    Camera camera;

    // Models to Render
    std::vector<Model> models;
};

void SetSunLight(Scene& scene, SunLight sunLight) {
    scene.sunLight = sunLight;
}
void SetCamera(Scene& scene, Camera camera) {
    scene.camera = camera;
}

// Add model to be tracked for rendering and set index within scene model list
bool AddModel(Scene& scene, unsigned int& modelIndex, const std::string& meshPath) 
{
    Model& model = scene.models.emplace_back();
    if (!LoadMesh(meshPath, model.mesh)) {
        scene.models.pop_back();
        modelIndex = 0;
        return false;
    }
    modelIndex = scene.models.size() - 1;
    return true;
}

// Calculate lightmaps and bvhs before rendering
bool InitModelsInScene(Scene& scene)
{
    if(!BuildLightMapUvs(scene.lightMap, scene.models)) {
        return false;
    }

    // Build Bvhs after lightmaps changed any vertex data
    for (Model& model : scene.models) {
        if(!BuildBvh(model.bvh, model.mesh)) {
            return false;
        }
    }
    return true;
}