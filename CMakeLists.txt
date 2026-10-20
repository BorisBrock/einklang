cmake_minimum_required(VERSION 3.25)

project(Einklang
    VERSION 0.0.1
    DESCRIPTION "Lightweight, self-hosted voice and text chat"
    HOMEPAGE_URL "https://github.com/BorisBrock/einklang"
    LANGUAGES CXX C)

# ---------------------------------------------------------------------------
# Build type
# ---------------------------------------------------------------------------
# Default to Release if no build type is specified (avoids accidental debug bloat).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type specified — defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(EINKLANG_BUILD_CLIENT "Build the EinklangClientApp (requires saucer/webview)" ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# All dependencies are pinned to an exact release so builds are reproducible.
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# Saucer — cross-platform webview framework (client only)
if(EINKLANG_BUILD_CLIENT)
FetchContent_Declare(
    saucer
    GIT_REPOSITORY https://github.com/saucer/saucer.git
    GIT_TAG        v8.0.5
    GIT_SHALLOW    TRUE
)
endif()

# ENet — single-header UDP networking library
FetchContent_Declare(
    enet
    URL https://github.com/zpl-c/enet/releases/download/v2.7.0/enet.h
    DOWNLOAD_NO_EXTRACT TRUE
)

# FlatBuffers — efficient binary serialization
set(FLATBUFFERS_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC    ON  CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATLIB  ON  CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    flatbuffers
    GIT_REPOSITORY https://github.com/google/flatbuffers.git
    GIT_TAG        v25.12.19
    GIT_SHALLOW    TRUE
    SYSTEM
)

# yaml-cpp — YAML configuration parsing
set(YAML_CPP_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS   OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL       OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    yaml_cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        yaml-cpp-0.9.0
    GIT_SHALLOW    TRUE
    SYSTEM
)

# SQLite — single-file amalgamation, no external dependencies
FetchContent_Declare(
    sqlite3
    URL      https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
    URL_HASH SHA3_256=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
)

# miniaudio — single-header audio I/O library
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.25
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _no_cmake_ # header-only use; skip miniaudio's own CMake project
)

# Opus — low-latency audio codec
set(OPUS_BUILD_TESTING             OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING                  OFF CACHE BOOL "" FORCE) # opus 1.6 keys its tests off this
set(OPUS_BUILD_PROGRAMS            OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG        v1.6.1
    GIT_SHALLOW    TRUE
    SYSTEM
)

FetchContent_MakeAvailable(flatbuffers enet yaml_cpp sqlite3)
if(EINKLANG_BUILD_CLIENT)
    FetchContent_MakeAvailable(opus miniaudio saucer)

    # WebKitGTK >= 2.54 ships a webkit.h that only includes the pointer-lock
    # API when ENABLE_POINTER_LOCK is set, although the library always exports
    # it.  saucer uses that API unconditionally, so enable the declaration.
    if(UNIX AND NOT APPLE)
        target_compile_definitions(saucer PRIVATE ENABLE_POINTER_LOCK=1)
    endif()
endif()

# ---------------------------------------------------------------------------
# FlatBuffers code generation
# ---------------------------------------------------------------------------
# Locate the flatc compiler — prefer the one built from source, then PATH.
set(FLATC_EXECUTABLE "" CACHE FILEPATH "Path to the flatc executable")

if(NOT FLATC_EXECUTABLE)
    if(TARGET flatc)
        set(FLATC_EXECUTABLE $<TARGET_FILE:flatc>)
    else()
        find_program(FLATC_EXECUTABLE
            NAMES flatc flatc.exe
            HINTS
                ENV FLATBUFFERS_ROOT
                ENV FLATC_ROOT
            PATH_SUFFIXES bin
        )
    endif()
endif()

if(NOT FLATC_EXECUTABLE)
    message(FATAL_ERROR "flatc not found. Set -DFLATC_EXECUTABLE=<path-to-flatc> or add flatc to PATH.")
endif()

set(FLATC_DEPENDENCY)
if(TARGET flatc)
    set(FLATC_DEPENDENCY flatc)
endif()

set(PROTOCOL_FBS    ${PROJECT_SOURCE_DIR}/src/common/protocol.fbs)
set(FLATBUF_GEN_DIR ${PROJECT_BINARY_DIR}/generated/common)
file(MAKE_DIRECTORY ${FLATBUF_GEN_DIR})

add_custom_command(
    OUTPUT  ${FLATBUF_GEN_DIR}/protocol_generated.h
    COMMAND ${FLATC_EXECUTABLE} --cpp -o ${FLATBUF_GEN_DIR} ${PROTOCOL_FBS}
    DEPENDS ${PROTOCOL_FBS} ${FLATC_DEPENDENCY}
    COMMENT "Generating FlatBuffers C++ header from protocol.fbs"
    VERBATIM
)

add_custom_target(generate_protocol_fbs DEPENDS ${FLATBUF_GEN_DIR}/protocol_generated.h)

# Interface target bundling everything both executables share: the generated
# protocol header, flatbuffers and ENet.
add_library(einklang_protocol INTERFACE)
add_dependencies(einklang_protocol generate_protocol_fbs)
target_include_directories(einklang_protocol INTERFACE ${FLATBUF_GEN_DIR})

# ---------------------------------------------------------------------------
# Static library targets (built from fetched single-file sources)
# ---------------------------------------------------------------------------

# ENet — a small C translation unit defines ENET_IMPLEMENTATION before
# including the single-header download, then compiles into a static lib.
file(WRITE ${PROJECT_BINARY_DIR}/enet_impl.c
    "#define ENET_IMPLEMENTATION\n#include \"enet.h\"\n")

add_library(enet_lib STATIC ${PROJECT_BINARY_DIR}/enet_impl.c)
target_include_directories(enet_lib SYSTEM PUBLIC ${enet_SOURCE_DIR})

find_package(Threads REQUIRED)
if(WIN32)
    target_link_libraries(enet_lib PUBLIC ws2_32 winmm)
else()
    target_link_libraries(enet_lib PUBLIC Threads::Threads)
endif()

target_link_libraries(einklang_protocol INTERFACE enet_lib flatbuffers)

# SQLite3 — built directly from the single-file amalgamation
add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib SYSTEM PUBLIC ${sqlite3_SOURCE_DIR})
target_compile_definitions(sqlite3_lib PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)
target_link_libraries(sqlite3_lib PUBLIC Threads::Threads)

# Common compiler warnings for our own code (not for third-party targets).
add_library(einklang_warnings INTERFACE)
if(MSVC)
    target_compile_options(einklang_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(einklang_warnings INTERFACE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# Link-time optimisation for non-debug builds. Per-config properties keep
# multi-config generators (e.g. Visual Studio) from enabling it for Debug.
include(CheckIPOSupported)
check_ipo_supported(RESULT EINKLANG_IPO_SUPPORTED OUTPUT _ipo_msg)

function(einklang_finalize_target target)
    if(EINKLANG_IPO_SUPPORTED)
        set_target_properties(${target} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE        TRUE
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE
            INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL     TRUE)
    endif()

    # Strip in Release / MinSizeRel builds (GCC/Clang on Linux).
    if(UNIX AND NOT APPLE)
        target_link_options(${target} PRIVATE $<$<CONFIG:Release,MinSizeRel>:-s>)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Client executable
# ---------------------------------------------------------------------------
if(EINKLANG_BUILD_CLIENT)
# miniaudio — header-only; INTERFACE target for include-path propagation
add_library(miniaudio_headers INTERFACE)
target_include_directories(miniaudio_headers SYSTEM INTERFACE ${miniaudio_SOURCE_DIR})
if(UNIX AND NOT APPLE)
    target_link_libraries(miniaudio_headers INTERFACE ${CMAKE_DL_LIBS} m)
endif()

# WIN32 sets /SUBSYSTEM:WINDOWS on MSVC (hides the console window).
# The keyword is silently ignored on Linux/macOS.
add_executable(EinklangClientApp WIN32
    src/client/client.cpp
    src/client/Networking/EnetClient.cpp
    src/client/Networking/ClientPacketFactory.cpp
    src/client/Networking/ServerPacketHandler.cpp
    src/client/Connectors/ChatConnector.cpp
    src/client/Connectors/ConnectionConnector.cpp
    src/client/Connectors/RoomEntryConnector.cpp
    src/client/Connectors/VoiceConnector.cpp
    src/client/GUI/WebUI.cpp
    src/client/GUI/WebViewModel.cpp
    src/client/Configuration/ConfigManager.cpp
    src/client/Voice/VoiceCapture.cpp
    src/client/Voice/VoiceEncoder.cpp
    src/client/Voice/VoiceDecoder.cpp
    src/client/Voice/VoicePlayer.cpp
    src/client/Voice/JitterBuffer.cpp
    src/client/Voice/VoicePipeline.cpp
    src/client/Voice/VoiceActivityDetector.cpp
    src/client/Voice/WavPlaybackSource.cpp
    src/client/Voice/WavFileRecorder.cpp
)

# Bake GUI assets into the binary so no loose files are needed at runtime.
# saucer_embed() is provided by saucer-embed, which saucer itself pulls in.
saucer_embed(
    "${PROJECT_SOURCE_DIR}/src/client/GUI/Assets"
    DESTINATION "${PROJECT_BINARY_DIR}/embedded"
    TARGET EinklangClientApp
)

# Embed the application icon into the Windows executable via a .rc resource file.
if(WIN32)
    set(APP_ICON_PATH "${PROJECT_SOURCE_DIR}/src/client/app.ico")
    configure_file(
        "${PROJECT_SOURCE_DIR}/src/client/app.rc.in"
        "${PROJECT_BINARY_DIR}/app.rc"
        @ONLY
    )
    target_sources(EinklangClientApp PRIVATE "${PROJECT_BINARY_DIR}/app.rc")
    # WIN32 subsystem requires WinMain; redirect to main() via the CRT entry point.
    if(MSVC)
        target_link_options(EinklangClientApp PRIVATE "LINKER:/ENTRY:mainCRTStartup")
    endif()
    target_compile_definitions(EinklangClientApp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_link_libraries(EinklangClientApp PRIVATE
    einklang_protocol
    einklang_warnings
    saucer::saucer
    saucer::embedded
    yaml-cpp::yaml-cpp
    miniaudio_headers
    opus
)
einklang_finalize_target(EinklangClientApp)

# Copy the client configuration file next to the binary after each build.
add_custom_command(TARGET EinklangClientApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${PROJECT_SOURCE_DIR}/src/client/client-config.yml
        $<TARGET_FILE_DIR:EinklangClientApp>/client-config.yml
)
endif() # EINKLANG_BUILD_CLIENT

# ---------------------------------------------------------------------------
# Server executable
# ---------------------------------------------------------------------------
add_executable(EinklangServerApp
    src/server/server.cpp
    src/server/Networking/EnetServer.cpp
    src/server/Networking/ClientPacketHandler.cpp
    src/server/Networking/ServerPacketFactory.cpp
    src/server/Configuration/ConfigManager.cpp
    src/server/Rooms/ChatDatabase.cpp
    src/server/Rooms/ChatRoom.cpp
    src/server/Rooms/ChatRoomManager.cpp
    src/server/Rooms/User.cpp
    src/server/Connectors/LoginConnector.cpp
    src/server/Connectors/ChatConnector.cpp
    src/server/Connectors/VoiceConnector.cpp
)

target_link_libraries(EinklangServerApp PRIVATE
    einklang_protocol
    einklang_warnings
    yaml-cpp::yaml-cpp
    sqlite3_lib
)
einklang_finalize_target(EinklangServerApp)

add_custom_command(TARGET EinklangServerApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${PROJECT_SOURCE_DIR}/src/server/server-config.yml
        $<TARGET_FILE_DIR:EinklangServerApp>/server-config.yml
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(EINKLANG_BUILD_TESTS "Build the test executables" ON)
if(EINKLANG_BUILD_TESTS AND EINKLANG_BUILD_CLIENT)
    enable_testing()
    add_executable(JitterBufferTest
        tests/JitterBufferTest.cpp
        src/client/Voice/JitterBuffer.cpp
        src/client/Voice/VoiceDecoder.cpp
        src/client/Voice/VoiceEncoder.cpp
    )
    target_link_libraries(JitterBufferTest PRIVATE einklang_warnings opus)
    add_test(NAME JitterBufferTest COMMAND JitterBufferTest)
endif()
