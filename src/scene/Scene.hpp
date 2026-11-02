#pragma once

#include <string>
#include <vector>

#include "Bvh.hpp"
#include "SunLight.hpp"
#include "Camera.hpp"
#include "Model.hpp"
#include "Mesh.hpp"
#include "LightMap.hpp"

/*
    A class that wraps data in the scene that registered to the rendering pipeline
*/

class Scene
{
    SunLight sunLight;
    LightMap lightMap; // One texture per scene
    Camera   camera;
    std::vector<Model> models;

    void SetSunLight(SunLight sunLight) {
        this->sunLight = sunLight;
    }

    void SetCamera(Camera camera) {
        this->camera = camera;
    }

    // Add model to be tracked for rendering and set index within scene model list
    bool AddModel(unsigned int& modelIndex, const std::string& meshPath)
    {
        Model& model = models.emplace_back();
        if (!LoadMesh(meshPath, model.mesh)) {
            models.pop_back();
            modelIndex = 0;
            return false;
        }
        modelIndex = static_cast<unsigned int>(models.size() - 1);
        return true;
    }

    // Calculate lightmaps and bvhs before rendering
    bool InitModelsInScene()
    {
        if (!BuildLightMapUvs(lightMap, models)) {
            return false;
        }

        // Build Bvhs after lightmaps changed any vertex data
        for (Model& model : models) {
            if (!BuildBvh(model.bvh, model.mesh)) {
                return false;
            }
        }
        return true;
    }
};
