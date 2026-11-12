# README

## Material Values/Textures Supported
1. Vec3: Base Color
2. Vec3: Emmision Color
3. Vec3: Normal
4. Scalar: Metallness
5. Scalar: Roughness
6. Scalar: Ambient Occlusion
7. Scalar: Alpha Coverage (in progress)

## Texture Format Conventions
1. Base Color: sRGB
2. Emmision Color: sRGB 
3. Normal: RGB
4. Metallness: R
5. Roughness: G
6. Ambient Occlusion: B 
7. Alpha Coverage: R

Note: Textures may be embedded or stored as external file paths. 

## Information On Gltf's Pbr Material Model:
https://www.khronos.org/gltf/pbr/

## Image Based Lighting
Currently only supports images containing equirectangularly projected radiance values stored as .hdr or .rgbe files.

## Limitations 

1. Only tested on gltf/glb files. I am using Assimp for importing which supports many file types, and if you follow the correct texture conventions it *should* work, but I make no guarantee. 

2. No support for gltf/glb non-core extensions (such as transmission), because I haven't gotten around to it.

3. Must support nvidia's "GL_NV_gpu_shader5" extension. This is for non-uniform bindless textures which is helpful for ray tracing. I had started working on rewriting the code to support unified texture atlases to avoid non-uniform bindless textures, but I have since decided to spend that time learning Vulkan which supports it natively.

4. No support for .exr image based lighting.

## Build Instructions
```text
(Ninja)
cmake --preset ninja-debug -DFETCHCONTENT_QUIET=OFF
cmake --build --preset ninja-debug --target SceneRasterizer

(Visual Studio 2022)
cmake --preset vs2022 -DFETCHCONTENT_QUIET=OFF
cmake --build --preset vs-debug --target SceneRasterizer
```