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

file(GLOB_RECURSE CORE_SOURCES "src/core/*.cpp" "src/render/*.cpp")
list(APPEND CORE_SOURCES "src/core/ecs/PortalSystem.cpp")
file(GLOB IMGUI_SOURCES "third_party/imgui/*.cpp")

add_executable(FairworldCore ${CORE_SOURCES} ${IMGUI_SOURCES})

# Includiamo le directory per far trovare gli header
target_include_directories(FairworldCore PRIVATE 
    src/core 
    src/core/ecs 
    src/core/memory 
    src/core/entities 
    src/core/app 
    src/core/utils 
    src/render 
    third_party 
    third_party/imgui 
    packages/Microsoft.Web.WebView2.1.0.4022.49/build/native/include 
    packages/OpenXR.Headers.1.0.10.2/include
    ${joltphysics_SOURCE_DIR}
)

# Linkiamo Vulkan, Jolt e librerie OS al Core
target_link_libraries(FairworldCore PRIVATE Vulkan::Vulkan Jolt Shell32)
target_link_libraries(FairworldCore PRIVATE ${CMAKE_SOURCE_DIR}/packages/Microsoft.Web.WebView2.1.0.4022.49/build/native/x64/WebView2Loader.dll.lib)
target_link_libraries(FairworldCore PRIVATE ${CMAKE_SOURCE_DIR}/packages/OpenXR.Loader.1.0.10.2/native/x64/release/lib/openxr_loader.lib)

# Comando Post-Build per copiare le DLL dinamiche accanto all'eseguibile (così non crasha all'avvio)
add_custom_command(TARGET FairworldCore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        "${CMAKE_SOURCE_DIR}/packages/OpenXR.Loader.1.0.10.2/native/x64/release/bin/openxr_loader.dll"
        $<TARGET_FILE_DIR:FairworldCore>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        "${CMAKE_SOURCE_DIR}/packages/Microsoft.Web.WebView2.1.0.4022.49/build/native/x64/WebView2Loader.dll"
        $<TARGET_FILE_DIR:FairworldCore>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/vert.spv"
        $<TARGET_FILE_DIR:FairworldCore>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/frag.spv"
        $<TARGET_FILE_DIR:FairworldCore>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/sky_vert.spv"
        $<TARGET_FILE_DIR:FairworldCore>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/sky_frag.spv"
        $<TARGET_FILE_DIR:FairworldCore>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/forge_vert.spv"
        $<TARGET_FILE_DIR:FairworldCore>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/src/render/forge_frag.spv"
        $<TARGET_FILE_DIR:FairworldCore>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/definitions"
        "$<TARGET_FILE_DIR:FairworldCore>/assets/definitions"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/textures"
        "$<TARGET_FILE_DIR:FairworldCore>/assets/textures"
)

# Ottimizzazioni specifiche per il compilatore (MSVC vs GCC/Clang)
if(MSVC)
    target_compile_options(FairworldCore PRIVATE $<$<CONFIG:Release>:/O2> /fp:fast /wd4819 /wd4244)
else()
    target_compile_options(FairworldCore PRIVATE $<$<CONFIG:Release>:-O3> -ffast-math -pthread)
endif()

# --- 3. I MODULI DINAMICI (Hot-Reloading) ---
# Compiliamo ogni workspace come libreria condivisa (.dll / .so)
add_library(Module_Modeling SHARED src/modules/Modeling.cpp)
add_library(Module_Sculpting SHARED src/modules/Sculpting.cpp)
add_library(Module_GeometryNodes SHARED src/modules/GeometryNodes.cpp)

# Facciamo in modo che i moduli vedano gli header del core
target_include_directories(Module_Modeling PRIVATE src/core src/core/ecs src/core/memory src/core/entities src/core/app src/core/utils third_party)
target_include_directories(Module_Sculpting PRIVATE src/core src/core/ecs src/core/memory src/core/entities src/core/app src/core/utils third_party)
target_include_directories(Module_GeometryNodes PRIVATE src/core src/core/ecs src/core/memory src/core/entities src/core/app src/core/utils third_party)

# Trigger CMake re-generation for ChunkEditorState, PlanetMapperState, AppBaseState, PlanetSystems, and the three Worker Renderers.
