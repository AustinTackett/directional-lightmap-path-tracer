#version 430 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 materialTexCoords;
layout(location = 3) in vec2 lightMapTexCoords;

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragMaterialTexCoord;

void main() {

    fragMaterialTexCoord = materialTexCoords;
    fragPosition = position;        // World Space Position
    fragNormal   = normalize(normal); // World Space Normal

    // Map lightmap texture coords to xy plane in clip space
    // ( [-1, 1], [-1, 1], 0, 1 )
    gl_Position = vec4(
        lightMapTexCoords.x * 2.0 - 1.0,
        lightMapTexCoords.y * 2.0 - 1.0,
        0.0,
        1.0
    );
}