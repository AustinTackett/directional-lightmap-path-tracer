# README

```Powershell

cmake --preset ninja-debug -DFETCHCONTENT_QUIET=OFF
cmake --build --preset ninja-debug --target SceneRasterizer

```

# Limitations 

1. Only loads gltf and glb files. I am using assimp, but I haven't handled non-gltf texture packing conventions
2. 