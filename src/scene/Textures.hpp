#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <stddef.h>

#include <stb_image.h>

/*
    Texture Container Classes
*/

struct TextureMetaData
{
    enum Format {
        RGBA8,
        //... 
    };

    size_t width    = 0;
    size_t height   = 0;
    size_t channels = 4;

    Format format = RGBA8;
};

struct Texture2D
{
    TextureMetaData metaData; 
    std::vector<unsigned char> data; // Type-Erased Bytes

    bool LoadTexture(const std::string& filePath)
    {
        int width = 0, height = 0, channels = 4;
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> texData(stbi_load(
            filePath.c_str(),
            &width,
            &height,
            nullptr,
            channels
        ), stbi_image_free);

        if (!texData) {
            std::cerr << "Failed to load texture: " + filePath << '\n';
            return false;
        }

        Texture2D texture;
        size_t dataCount = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
        data.assign(texData.get(), texData.get() + dataCount);
        metaData.width    = width;
        metaData.height   = height;
        metaData.channels = channels;
        metaData.format   = TextureMetaData::RGBA8;

        return true;
    }
};


