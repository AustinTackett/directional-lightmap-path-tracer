#version 430 core

#define BIND_SSBO_VERTICES 2u
#define BIND_SSBO_FACES 3u

// VBO Data
struct Vertex 
{
    vec3 position;
    vec3 normal;
    vec2 materialTexCoords; // ( [0, 1], [0, 1] )
    vec2 lightMapTexCoords; // ( [0, 1], [0, 1] )
};

layout(std430, binding = BIND_SSBO_VERTICES) readonly buffer Vertices
{
    Vertex vertices[];
};

// EBO Data
struct Face 
{
    uint vertexIndex0;
    uint vertexIndex1;
    uint vertexIndex2;
};

layout(std430, binding = BIND_SSBO_FACES) readonly buffer Faces
{
    Face faces[];
};

out vec3 fragPosition;
out vec3 fragNormal;

void main() {
    // Vertex pulling
    uint faceIndex = uint(gl_VertexID) / 3u;
    Face face = faces[faceIndex];
    uint vertexIndex = 0;
    uint faceVertexOffset = uint(gl_VertexID) % 3u;
    if      (faceVertexOffset == 0u) vertexIndex = face.vertexIndex0;
    else if (faceVertexOffset == 1u) vertexIndex = face.vertexIndex1;
    else                             vertexIndex = face.vertexIndex2;
    Vertex vertex = vertices[vertexIndex];

    fragPosition = vertex.position;        // World Space Position
    fragNormal = normalize(vertex.normal); // World Space Normal

    // Map lightmap texture coords to xy plane in clip space
    // ( [-1, 1], [-1, 1], 0, 1 )
    gl_Position = vec4(
        vertex.lightMapTexCoords.x * 2.0 - 1.0,
        vertex.lightMapTexCoords.y * 2.0 - 1.0,
        0.0,
        1.0
    );
}