cmake_minimum_required(VERSION 3.20)
project(FAIRWORLD VERSION 0.1.0 LANGUAGES CXX)

# Impostiamo il C++23 come standard rigoroso (per std::expected)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)

# Linker Flags per abilitare ottimizzazioni in Release
if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /MAP")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG /MAP")
endif()

# Direzioniamo gli output di compilazione nella cartella 'bin'
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/modules)

# --- 1. DIPENDENZE ---
# Trova l'SDK di Vulkan installato nel sistema
find_package(Vulkan REQUIRED)

include(FetchContent)

# JoltPhysics Configuration (Strict AAA Performance settings)
set(TARGET_ENABLE_RTTI OFF CACHE BOOL "Enable RTTI in Jolt" FORCE)
set(TARGET_ENABLE_EXCEPTIONS OFF CACHE BOOL "Enable exceptions in Jolt" FORCE)
set(USE_AVX2 ON CACHE BOOL "Enable AVX2 for Jolt" FORCE)
set(USE_SSE4_2 ON CACHE BOOL "Enable SSE4.2 for Jolt" FORCE)
set(ENABLE_PROFILER OFF CACHE BOOL "Disable Jolt Profiler" FORCE)
set(ENABLE_ASSERTS OFF CACHE BOOL "Disable Jolt Asserts" FORCE)

FetchContent_Declare(
    joltphysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.0.0
    SOURCE_SUBDIR  Build
)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "Disable warnings as errors in Jolt" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "Use dynamic MSVC runtime for Jolt" FORCE)
FetchContent_MakeAvailable(joltphysics)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES 
    "${CMAKE_SOURCE_DIR}/src/modules/Modeling.cpp"
    "${CMAKE_SOURCE_DIR}/src/modules/Sculpting.cpp"
    "${CMAKE_SOURCE_DIR}/src/modules/GeometryNodes.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/app/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/blocks/uv/uv_tests_main.cpp"
)
file(GLOB IMGUI_SOURCES CONFIGURE_DEPENDS "third_party/imgui/*.cpp")

# --- 2. LIBRERIA STATICA CENTRALE (FairworldCore) ---
add_library(FairworldCore STATIC ${CORE_SOURCES} ${IMGUI_SOURCES})

# Includiamo le directory per gli 8 Pilastri Fondamentali
target_include_directories(FairworldCore PUBLIC 
    src
    src/states
    src/blocks
    src/blocks/factory
    src/blocks/sculpt
    src/blocks/texture
    src/blocks/uv
    src/world
    src/systems
    src/render
    src/hardware
    src/simulation
    src/modules
    src/ui
    src/components
    src/core
    src/core/app 
    src/core/memory 
    src/core/entities 
    src/core/utils 
    third_party 
    third_party/imgui 
    packages/Microsoft.Web.WebView2.1.0.4022.49/build/native/include 
    packages/OpenXR.Headers.1.0.10.2/include
    ${joltphysics_SOURCE_DIR}
)

# --- 1.1 Supporto nlohmann_json ---
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_SOURCE_DIR}/third_party)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)

# Linkiamo Vulkan, Jolt, nlohmann_json e librerie OS al Core
target_link_libraries(FairworldCore PUBLIC Vulkan::Vulkan Jolt Shell32 nlohmann_json::nlohmann_json)
target_link_libraries(FairworldCore PUBLIC ${CMAKE_SOURCE_DIR}/packages/Microsoft.Web.WebView2.1.0.4022.49/build/native/x64/WebView2Loader.dll.lib)
target_link_libraries(FairworldCore PUBLIC ${CMAKE_SOURCE_DIR}/packages/OpenXR.Loader.1.0.10.2/native/x64/release/lib/openxr_loader.lib)

# Ottimizzazioni specifiche per il compilatore (MSVC vs GCC/Clang)
if(MSVC)
    target_compile_options(FairworldCore PRIVATE $<$<CONFIG:Release>:/O2> /fp:fast /wd4819 /wd4244)
else()
    target_compile_options(FairworldCore PRIVATE $<$<CONFIG:Release>:-O3> -ffast-math -pthread)
endif()

# --- 3. APPLICAZIONE PRINCIPALE (FairworldEditor) ---
add_executable(FairworldEditor src/core/app/main.cpp)
target_link_libraries(FairworldEditor PRIVATE FairworldCore)

# Comando Post-Build per copiare le DLL dinamiche e asset accanto all'eseguibile FairworldEditor
add_custom_command(TARGET FairworldEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        "${CMAKE_SOURCE_DIR}/packages/OpenXR.Loader.1.0.10.2/native/x64/release/bin/openxr_loader.dll"
        $<TARGET_FILE_DIR:FairworldEditor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        "${CMAKE_SOURCE_DIR}/packages/Microsoft.Web.WebView2.1.0.4022.49/build/native/x64/WebView2Loader.dll"
        $<TARGET_FILE_DIR:FairworldEditor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/vert.spv"
        $<TARGET_FILE_DIR:FairworldEditor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/frag.spv"
        $<TARGET_FILE_DIR:FairworldEditor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/sky_vert.spv"
        $<TARGET_FILE_DIR:FairworldEditor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/sky_frag.spv"
        $<TARGET_FILE_DIR:FairworldEditor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/chunk_culling.spv"
        $<TARGET_FILE_DIR:FairworldEditor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/terrain_generation.spv"
        $<TARGET_FILE_DIR:FairworldEditor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/forge_vert.spv"
        $<TARGET_FILE_DIR:FairworldEditor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/forge_frag.spv"
        $<TARGET_FILE_DIR:FairworldEditor>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/definitions"
        "$<TARGET_FILE_DIR:FairworldEditor>/assets/definitions"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/textures"
        "$<TARGET_FILE_DIR:FairworldEditor>/assets/textures"
)

# --- 4. TEST RUNNER HEADLESS (FairworldUVTests) ---
add_executable(FairworldUVTests src/blocks/uv/uv_tests_main.cpp)
target_link_libraries(FairworldUVTests PRIVATE FairworldCore)


# --- 3. I MODULI DINAMICI (Hot-Reloading) ---
# Compiliamo ogni workspace come libreria condivisa (.dll / .so)
add_library(Module_Modeling SHARED src/modules/Modeling.cpp)
add_library(Module_Sculpting SHARED src/modules/Sculpting.cpp)
add_library(Module_GeometryNodes SHARED src/modules/GeometryNodes.cpp)

# Facciamo in modo che i moduli vedano gli header del core
target_include_directories(Module_Modeling PRIVATE src/core src/core/ecs src/core/memory src/core/entities src/core/app src/core/utils third_party)
target_include_directories(Module_Sculpting PRIVATE src/core src/core/ecs src/core/memory src/core/entities src/core/app src/core/utils third_party)
target_include_directories(Module_GeometryNodes PRIVATE src/core src/core/ecs src/core/memory src/core/entities src/core/app src/core/utils third_party)

# Trigger CMake re-generation for ChunkEditorState, PlanetMapperState, AppBaseState, PlanetSystems, Sculpting (BM-S2 EditableMesh & BM-S3 MeshSelectionService), MeshBridge (FG-01R MG-3B MeshBridgeContractTests).
