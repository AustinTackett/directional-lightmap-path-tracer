#version 430 core

uniform mat4 lightMvp;

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 materialTexCoords;

out vec2 fragMaterialTexCoord;

/*
    Render the scene from the lights pov
    -> only directional lights currently
*/
void main()
{
    fragMaterialTexCoord = materialTexCoords;
    gl_Position = lightMvp * vec4(position, 1.0);
}
