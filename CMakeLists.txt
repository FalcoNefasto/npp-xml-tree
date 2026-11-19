cmake_minimum_required(VERSION 3.20)
project(NppXmlTree VERSION 1.0 LANGUAGES CXX)
enable_language(RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: the plugin ships as a single DLL with no redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# --- engine: offsets and an index, no Win32 ---------------------------------
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")

add_library(xmltree_core STATIC ${CORE_SOURCES})
target_include_directories(xmltree_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/core")

# --- plugin DLL --------------------------------------------------------------
file(GLOB PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/*.rc")

add_library(NppXmlTree MODULE ${PLUGIN_SOURCES})
target_include_directories(NppXmlTree PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/npp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/scintilla")
target_compile_definitions(NppXmlTree PRIVATE UNICODE _UNICODE)
target_link_libraries(NppXmlTree PRIVATE xmltree_core user32 gdi32 comctl32 shell32 dwmapi uxtheme)
set_target_properties(NppXmlTree PROPERTIES PREFIX "" OUTPUT_NAME "NppXmlTree")
