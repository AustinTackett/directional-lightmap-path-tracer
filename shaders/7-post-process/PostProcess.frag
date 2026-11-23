#version 430 core

uniform sampler2D inputColor;
uniform float     exposure;

in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

/*
    Aces Tone Mapping
*/

float linearRgbToSrgb(float x)
{
    x = clamp(x, 0.0f, 1.0f);
    return x <= 0.0031308f
        ? 12.92f * x
        : 1.055f * pow(x, 1.0f / 2.4f) - 0.055f;
}

vec3 linearRgbToSrgb(vec3 x)
{
    return vec3(
        linearRgbToSrgb(x.x),
        linearRgbToSrgb(x.y),
        linearRgbToSrgb(x.z)
    );
}

/*
    Copyright (C) 2019 Damien Seguin

    MIT License

    Source: https://github.com/dmnsgn/shaders-tone-map/blob/main/packages/glsl-tone-map/aces.glsl
    License: \THIRD-PARTY-NOTICES\shaders-tone-map-LICENSE.md
*/
// Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
vec3 Tonemap_ACES(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

void main()
{
    vec4 color = texture(inputColor, fragTexCoord);
    vec3 toneMapped = Tonemap_ACES(color.rgb * exposure);
    outColor = vec4(linearRgbToSrgb(toneMapped), color.a);
}
