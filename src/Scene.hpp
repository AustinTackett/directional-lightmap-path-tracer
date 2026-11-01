#pragma once

#include <string>

#include "Bvh.hpp"
#include "SunLight.hpp"
#include "Hdri.hpp"
#include "Camera.hpp"
#include "Hdri.hpp"
#include "Model.hpp"
#include "Mesh.hpp"
#include "LightMap.hpp"

struct Scene 
{
    // Scene Lighting
    SunLight  sunLight;
    Hdri      hdri; 
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

bool AddMesh(Scene& scene, const std::string& meshPath) 
{
    Model& model = scene.models.emplace_back();
    if (!LoadMesh(meshPath, model.mesh)) {
        scene.models.pop_back();
        return false;
    }
    return true;
}

bool AddHdri(Scene& scene, const std::string& hdriPath)
{
    return true;
}

bool PrepareSceneForRender(Scene& scene)
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