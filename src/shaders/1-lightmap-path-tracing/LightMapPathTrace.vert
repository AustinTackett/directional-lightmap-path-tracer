#version 430 core

struct Vertex 
{
    vec3 position;
    vec3 normal;
    vec2 materialTexCoords; // ([0, 1], [0, 1])
    vec2 lightMapTexCoords; // ([0, 1], [0, 1])
};

layout(std430, binding = 2) readonly buffer Vertices
{
    Vertex vertices[];
};

out vec3 fragPosition;
out vec3 fragNormal;

void main() {
    // Vertex pulling (no vbo used)
    Vertex vertex = vertices[uint(gl_VertexID)];

    fragPosition = vertex.position;
    fragNormal = vertex.normal;

    // Map lightmap texture coords to xy plane in clip space
    gl_Position = vec4(
        vertex.lightMapTexCoords.x * 2.0 - 1.0,
        vertex.lightMapTexCoords.y * 2.0 - 1.0,
        0.0,
        1.0
    );
}
