# README

## Material Values/Textures
1. Vec3: Base Color
2. Vec3: Emmision Color (In Progress)
3. Vec3: Normal
4. Scalar: Metallness
5. Scalar: Roughness
6. Scalar: Ambient Occlusion
7. Scalar: Alpha Coverage (Alpha Mask Mode Only)

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

3. Exenstions used for light mapping and path tracing in OpenGL:
    - 

4. No support for .exr image based lighting.

## Build Instructions

Run these commands from the project root. Use Release for faster model loading, UV atlas generation, and BVH construction.

```text
(Ninja Debug)
cmake --preset ninja-debug -DFETCHCONTENT_QUIET=OFF
cmake --build --preset ninja-debug --target SceneRasterizer

(Ninja Release)
cmake --preset ninja-release -DFETCHCONTENT_QUIET=OFF
cmake --build --preset ninja-release --target SceneRasterizer

(Visual Studio 2022 Debug)
cmake --preset vs2022 -DFETCHCONTENT_QUIET=OFF
cmake --build --preset vs-debug --target SceneRasterizer

(Visual Studio 2022 Release)
cmake --preset vs2022 -DFETCHCONTENT_QUIET=OFF
cmake --build --preset vs-release --target SceneRasterizer
```

Run the Visual Studio Release build from the project root:

```text
.\build\vs2022\Release\SceneRasterizer.exe SceneConfig.yaml
```

The config path is optional and defaults to `SceneConfig.yaml`. Configure tracing,
tile dimensions, sunlight, camera, and asset paths in that file. Camera FOV is in
degrees; vector settings contain three numbers. Asset paths are relative to the
config file. Set `hdri-path` to `""` to disable environment lighting.
