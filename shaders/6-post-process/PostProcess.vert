#version 430 core

out vec2 fragTexCoord;

// Full screen triangle
const vec2 positions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main()
{
    vec2 position = positions[gl_VertexID];
    fragTexCoord  = position * 0.5 + 0.5; // [-1, 1] -> [0, 1]
    gl_Position   = vec4(position, 0.0, 1.0);
}
