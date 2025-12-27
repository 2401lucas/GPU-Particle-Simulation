find_package(glfw3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)

if (WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

set(WINDOWS_KIT_INCLUDE "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0")
include_directories(
        "${WINDOWS_KIT_INCLUDE}/shared"
        "${WINDOWS_KIT_INCLUDE}/um"
        "${WINDOWS_KIT_INCLUDE}/ucrt"
)

# Gather all .cpp and .h files recursively
file(GLOB_RECURSE SOURCE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)


add_executable(GPU_Particle_Sim ${SOURCE_FILES})

# Add include directories (for internal headers)
target_include_directories(GPU_Particle_Sim PRIVATE "Engine/")

# Link DX12 libraries
target_link_libraries(GPU_Particle_Sim PRIVATE d3d12)
target_link_libraries(GPU_Particle_Sim PRIVATE dxgi)
target_link_libraries(GPU_Particle_Sim PRIVATE dxguid)
target_link_libraries(GPU_Particle_Sim PRIVATE d3dcompiler)
target_link_libraries(GPU_Particle_Sim PRIVATE Microsoft::DirectX-Headers)

target_link_libraries(GPU_Particle_Sim PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(GPU_Particle_Sim PUBLIC glfw)
target_link_libraries(GPU_Particle_Sim PRIVATE assimp::assimp)

#target_compile_definitions(GPU_Particle_Sim PUBLIC DEBUG_RENDERGRAPH)
target_compile_definitions(GPU_Particle_Sim PUBLIC DEBUG_EVENTS)
target_compile_definitions(GPU_Particle_Sim PUBLIC RHI_D3D12)
# target_compile_definitions(GPU_Particle_Sim PUBLIC RHI_VULKAN)

if (MINGW)
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-mwindows")
endif()
