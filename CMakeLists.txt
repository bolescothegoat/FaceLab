cmake_minimum_required(VERSION 3.21)

project(FaceLab VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# CommonLibSSE-NG 15.5.0 (alandtse fork, GPL-3.0-or-later with the Modding Exception): set COMMONLIB_SSE_FOLDER to its checkout (tag v15.5.0).
# It reads the AE 1.7 Address Library; the older CharmedBaryon 3.7.0 does not.
set(COMMONLIB_DIR "$ENV{COMMONLIB_SSE_FOLDER}" CACHE PATH "CommonLibSSE-NG source (an alandtse/CommonLibSSE-NG checkout, tag v15.5.0)")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
add_subdirectory("${COMMONLIB_DIR}" "${CMAKE_BINARY_DIR}/commonlib" EXCLUDE_FROM_ALL)

add_library(FaceLab SHARED
	src/main.cpp
	src/Lab.cpp
	src/Morph.cpp
	src/Input.cpp
	src/Menu.cpp
	src/Api.cpp
	src/Presets.cpp
	src/TriFile.cpp
	src/Families.cpp
	src/FamilyLoad.cpp
)
target_include_directories(FaceLab PRIVATE src)
target_precompile_headers(FaceLab PRIVATE src/PCH.h)
target_link_libraries(FaceLab PRIVATE CommonLibSSE::CommonLibSSE)
target_compile_definitions(FaceLab PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(FaceLab PRIVATE /permissive- /Zc:preprocessor /utf-8 /W4 /wd4100)

find_package(spdlog CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
target_link_libraries(FaceLab PRIVATE spdlog::spdlog tomlplusplus::tomlplusplus)

set_target_properties(FaceLab PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist/SKSE/Plugins"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist/SKSE/Plugins"
)
