#pragma once

struct Model;

struct LightMap
{
    const unsigned int resolution = 0;
    unsigned int width = 0;
    unsigned int height = 0;

    bool buildFromModel(Model& model, float texelsPerUnit=32.0f, unsigned int padding=4);
};