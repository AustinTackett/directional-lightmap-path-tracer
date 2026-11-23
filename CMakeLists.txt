# Project Properties
cmake_minimum_required(VERSION 3.28)
include(FetchContent)

project(SceneRasterizer LANGUAGES C CXX)

# Internal sources and headers.
add_executable(SceneRasterizer
    main.cpp
    src/model/Model.cpp
    src/textures/LightMap.cpp
    src/textures/Texture2D.cpp
)

target_include_directories(SceneRasterizer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}",
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scene"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/user"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/textures"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/bvh"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/openGL"
)

set_target_properties(SceneRasterizer PROPERTIES
    EXPORT_COMPILE_COMMANDS ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Define External Dependency File Locations
FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.9.0
    GIT_SHALLOW TRUE
)
FetchContent_Declare(glew
    GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
    GIT_TAG glew-cmake-2.3.1
    GIT_SHALLOW TRUE
)
FetchContent_Declare(assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v6.0.5
    GIT_SHALLOW TRUE
)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE
)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_Declare(xatlas
    GIT_REPOSITORY https://github.com/jpcy/xatlas.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

# External Depedency Build Options (static build)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies as static libraries" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ONLY_LIBS ON CACHE BOOL "" FORCE)
set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(glew-cmake_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)

# Download Dependencies
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
set(YAML_MSVC_SHARED_RT ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glew assimp glfw glm stb xatlas yaml-cpp)

# xatlas uses Premake
add_library(xatlas STATIC "${xatlas_SOURCE_DIR}/source/xatlas/xatlas.cpp")
target_include_directories(xatlas PUBLIC "${xatlas_SOURCE_DIR}/source/xatlas")
target_compile_features(xatlas PUBLIC cxx_std_11)

# add rest of external libraries
target_link_libraries(SceneRasterizer PRIVATE
    yaml-cpp::yaml-cpp
    libglew_static
    assimp::assimp
    glfw
    glm::glm-header-only
    xatlas
)
target_include_directories(SceneRasterizer PRIVATE "${stb_SOURCE_DIR}")
target_sources(SceneRasterizer PRIVATE "${stb_SOURCE_DIR}/stb_rect_pack.h")

