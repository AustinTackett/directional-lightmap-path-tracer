#pragma once

struct Model;

struct LightMap
{
    const unsigned int resolution = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    float texelsPerUnit = 0.0f;
    unsigned int padding = 12;

    bool buildFromModel(Model& model, float texelsPerUnit=42.0f, unsigned int padding=6);
};
