#pragma once

#include "glm/geometric.hpp"
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <glm/vec3.hpp>
#include <yaml-cpp/yaml.h>

/*
    Load Scene Config .yaml into C++ data
*/

struct SceneConfig
{
    // Lightmap Path Tracing
    unsigned int numberOfSamples;
    unsigned int minSamples;
    unsigned int maxBounces;
    float relativeError;
    int tileWidth;
    int tileHeight;

    // Directional Light
    glm::vec3 sunDirection;
    glm::vec3 sunIrradiance;

    // Camera
    float cameraFov;
    float cameraSpeed;
    glm::vec3 cameraPosition;

    // Assets
    std::string modelPath;
    std::string hdriPath;

    static SceneConfig load(const std::string& path)
    {
        const YAML::Node root = YAML::LoadFile(path);
        SceneConfig config{};

        // Lightmap Path Tracing
        config.numberOfSamples = readPositiveInt(root, "number_of_samples");
        config.minSamples      = readPositiveInt(root, "min_samples");
        config.maxBounces      = readPositiveInt(root, "max_bounces");
        config.relativeError   = readFloat(root, "relative_error");
        config.tileWidth       = readPositiveInt(root, "tile_width");
        config.tileHeight      = readPositiveInt(root, "tile_height");

        // Validate path tracing params
        if (config.minSamples > config.numberOfSamples)
            throw std::runtime_error("min_samples must not exceed number_of_samples");
        if (config.relativeError < 0.0f)
            throw std::runtime_error("relative_error must be nonnegative");

        // Directional Light
        config.sunDirection  = readVector(root, "sun_direction");
        config.sunIrradiance = readVector(root, "sun_irradiance");

        // Validate directional light params
        const double directionLength = glm::length(config.sunDirection);
        if (directionLength == 0.0)
            throw std::runtime_error("sun_direction must be nonzero");
        if (config.sunIrradiance.x < 0.0f || config.sunIrradiance.y < 0.0f || config.sunIrradiance.z < 0.0f)
            throw std::runtime_error("sun_irradiance must be nonnegative");

        config.sunDirection = glm::normalize(glm::vec3(
            config.sunDirection.x / directionLength,
            config.sunDirection.y / directionLength,
            config.sunDirection.z / directionLength));

        // Camera
        config.cameraFov      = readFloat(root, "camera_fov");
        config.cameraSpeed    = readFloat(root, "camera_speed");
        config.cameraPosition = readVector(root, "camera_position");

        // Validate camera params
        if (config.cameraFov <= 0.0f || config.cameraFov >= 180.0f)
            throw std::runtime_error("camera_fov must be between 0 and 180 degrees");
        if (config.cameraSpeed < 0.0f)
            throw std::runtime_error("camera_speed must be nonnegative");

        // Model/hdri file paths
        const auto directory = std::filesystem::path(path).parent_path();
        config.modelPath = root["model-path"].as<std::string>();
        config.hdriPath  = root["hdri-path"].as<std::string>("");
        
        // Validate file paths
        if (config.modelPath.empty())
            throw std::runtime_error("model-path must not be empty");
        config.modelPath = (directory / config.modelPath).lexically_normal().string();
        if (!config.hdriPath.empty())
            config.hdriPath = (directory / config.hdriPath).lexically_normal().string();

        return config;
    }

private:

    /*
        Parsing yaml data types
    */
    static int readPositiveInt(const YAML::Node& root, const char* key)
    {
        const int value = root[key].as<int>();
        if (value < 1)
            throw std::runtime_error(std::string(key) + " must be positive");

        return value;
    }

    static float readFloat(const YAML::Node& root, const char* key)
    {
        const float value = root[key].as<float>();
        if (!std::isfinite(value))
            throw std::runtime_error(std::string(key) + " must be finite");

        return value;
    }

    static glm::vec3 readVector(const YAML::Node& root, const char* key)
    {
        const YAML::Node values = root[key];
        if (!values.IsSequence() || values.size() != 3)
            throw std::runtime_error(std::string(key) + " must contain three numbers");

        const glm::vec3 value(values[0].as<float>(), values[1].as<float>(), values[2].as<float>());
        if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
            throw std::runtime_error(std::string(key) + " must contain finite numbers");

        return value;
    }
};
