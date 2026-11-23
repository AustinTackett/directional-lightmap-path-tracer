#version 430 core

uniform mat4 mvp;
uniform mat4 model;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 materialTexCoords;

layout(location = 3) in vec2 lightMapTexCoords;
out vec2 fragLightMapTexCoord;

out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragMaterialTexCoord;

/*
    Raster g-buffer that also includes the new lightmap data
*/
void main()
{
    fragLightMapTexCoord = lightMapTexCoords;
    fragMaterialTexCoord = materialTexCoords;
    fragPosition = vec3(model * vec4(position, 1.0));
    fragNormal   = normalize(mat3(transpose(inverse(model))) * normal); // normal matrix

    gl_Position = mvp * vec4(position, 1.0);
}
