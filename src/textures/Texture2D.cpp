#include "Texture2D.hpp"

#include <iostream>
#include <memory>
#include <random>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

void Texture2D::createNoise(size_t width, size_t height) {
    std::random_device rd;
    std::mt19937 gen(rd());

    metaData.width = width;
    metaData.height = height;
    metaData.channels = 4;
    metaData.format = TextureMetaData::RGBA8;
    data.resize(width * height * 4);
    
    std::uniform_int_distribution<int> randomByte(0, 255);
    for (unsigned char& value : data) {
        value = static_cast<unsigned char>(randomByte(gen));
    }
}

bool Texture2D::loadImage(std::string filePath) {
    int desiredChannels = 4;
    int width = 0;
    int height = 0;
    int channels = 0;

    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> texData(
        stbi_load(filePath.c_str(), &width, &height, &channels, desiredChannels),
        stbi_image_free
    );

    if (!texData)
    {
        std::cerr << "Failed to load image texture: " << filePath << std::endl;
        return false;
    }

    size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * desiredChannels;

    data.assign(texData.get(), texData.get() + bytes);
    metaData.width    = width;
    metaData.height   = height;
    metaData.channels = desiredChannels;
    metaData.format   = TextureMetaData::RGBA8;

    return true;
}

bool Texture2D::loadHdri(std::string filePath) {
    int desiredChannels = 4;
    int width = 0;
    int height = 0;
    int channels = 0;

    std::cout << "Loading HDRI, this may take a moment..." << std::endl;
    std::unique_ptr<float, decltype(&stbi_image_free)> texData(
        stbi_loadf(filePath.c_str(), &width, &height, &channels, desiredChannels),
        stbi_image_free
    );

    if (!texData)
    {
        std::cerr << "Failed to load HDR texture at: " << filePath << std::endl;
        return false;
    }

    unsigned char* bytesBegin = reinterpret_cast<unsigned char*>(texData.get());
    size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * desiredChannels * sizeof(float); 

    data.assign(bytesBegin, bytesBegin + bytes);
    metaData.width    = width;
    metaData.height   = height;
    metaData.channels = channels;
    metaData.format   = TextureMetaData::RGBA32F;

    std::cout << "HDRI finished loading" << std::endl;

    return true;
}
