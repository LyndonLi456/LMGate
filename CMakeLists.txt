cmake_minimum_required(VERSION 3.16)
project(llm-gateway VERSION 1.1.0 LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Platform detection ----
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    add_compile_options(/utf-8)
endif()

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00)
endif()

# ---- Options ----
option(STATIC_BUILD "Statically link runtime libraries" OFF)
option(BUILD_GUI "Build with native GUI (Dear ImGui + OpenGL)" ON)

# ---- Find packages ----
if(WIN32)
    find_package(OpenSSL QUIET)
    find_package(ZLIB QUIET)
else()
    find_package(OpenSSL REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)
endif()

# ---- SQLite3 ----
add_library(sqlite3 STATIC deps/sqlite3.c)
target_include_directories(sqlite3 PUBLIC deps)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
)

# ---- Dear ImGui ----
set(IMGUI_SOURCES
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_widgets.cpp
    deps/imgui/imgui_tables.cpp
)

if(WIN32)
    list(APPEND IMGUI_SOURCES deps/imgui/imgui_impl_win32.cpp)
else()
    # Linux/macOS: use GLFW backend
    find_package(glfw3 QUIET)
    if(NOT glfw3_FOUND)
        message(FATAL_ERROR "GLFW3 is required for GUI on Linux. "
            "Install with: sudo apt install libglfw3-dev (Ubuntu/Debian) "
            "or brew install glfw (macOS)")
    endif()
    list(APPEND IMGUI_SOURCES deps/imgui/imgui_impl_glfw.cpp)
endif()

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC deps/imgui deps)

# ---- Main executable ----
if(WIN32)
    add_executable(llm-gateway WIN32
        src/main.cpp
        src/config_manager.cpp
        src/provider_manager.cpp
        src/stats_db.cpp
        src/token_counter.cpp
        src/format_transformer.cpp
        src/request_forwarder.cpp
        src/api_router.cpp
        src/gui_app.cpp
        src/png_loader.cpp
        deps/imgui/imgui_impl_opengl3.cpp
    )
else()
    add_executable(llm-gateway
        src/main.cpp
        src/config_manager.cpp
        src/provider_manager.cpp
        src/stats_db.cpp
        src/token_counter.cpp
        src/format_transformer.cpp
        src/request_forwarder.cpp
        src/api_router.cpp
        src/gui_app.cpp
        src/png_loader.cpp
        deps/imgui/imgui_impl_opengl3.cpp
    )
endif()

target_include_directories(llm-gateway PRIVATE deps src deps/imgui)
target_link_libraries(llm-gateway PRIVATE sqlite3 imgui)

# ---- Platform-specific link libraries & flags ----
if(WIN32)
    if(MSVC)
        set_target_properties(llm-gateway PROPERTIES
            LINK_FLAGS "/ENTRY:mainCRTStartup /SUBSYSTEM:WINDOWS"
        )
    endif()

    target_compile_definitions(llm-gateway PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

    # Link OpenSSL (from vcpkg or manual install)
    if(OPENSSL_FOUND)
        target_link_libraries(llm-gateway PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    else()
        message(WARNING "OpenSSL not found via find_package. "
            "Install vcpkg and run: vcpkg install openssl zlib\n"
            "Or set CMAKE_PREFIX_PATH to your OpenSSL install directory.")
        # Fallback: try common conda/vcpkg paths
        if(EXISTS "$ENV{CONDA_PREFIX}/Library")
            target_include_directories(llm-gateway PRIVATE "$ENV{CONDA_PREFIX}/Library/include")
            target_link_directories(llm-gateway PRIVATE "$ENV{CONDA_PREFIX}/Library/lib")
        endif()
        target_link_libraries(llm-gateway PRIVATE libssl libcrypto)
    endif()
    if(ZLIB_FOUND)
        target_link_libraries(llm-gateway PRIVATE ZLIB::ZLIB)
    else()
        target_link_libraries(llm-gateway PRIVATE zlib)
    endif()

    target_link_libraries(llm-gateway PRIVATE
        ws2_32 crypt32 opengl32 gdi32 dwmapi shell32
    )

    if(NOT MSVC)
        target_link_options(llm-gateway PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()

    # Copy resources to output directory
    file(GLOB PNG_RESOURCES "${CMAKE_SOURCE_DIR}/resources/*.png")
    file(GLOB ICO_RESOURCES "${CMAKE_SOURCE_DIR}/resources/*.ico")
    list(APPEND ALL_RESOURCES ${PNG_RESOURCES} ${ICO_RESOURCES})

    add_custom_command(TARGET llm-gateway POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:llm-gateway>/resources"
    )

    foreach(RES ${ALL_RESOURCES})
        add_custom_command(TARGET llm-gateway POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RES}" "$<TARGET_FILE_DIR:llm-gateway>/resources"
        )
    endforeach()

    # Copy webui
    add_custom_command(TARGET llm-gateway POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/src/webui" "$<TARGET_FILE_DIR:llm-gateway>/webui"
    )
else()
    # Linux / macOS
    target_compile_definitions(llm-gateway PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

    if(STATIC_BUILD)
        target_link_options(llm-gateway PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()

    target_link_libraries(llm-gateway PRIVATE
        OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB
        OpenGL::GL glfw Threads::Threads ${CMAKE_DL_LIBS}
    )

    # Copy resources and webui (post-build on Linux too)
    file(GLOB PNG_RESOURCES "${CMAKE_SOURCE_DIR}/resources/*.png")
    file(GLOB ICO_RESOURCES "${CMAKE_SOURCE_DIR}/resources/*.ico")
    list(APPEND ALL_RESOURCES ${PNG_RESOURCES} ${ICO_RESOURCES})

    add_custom_command(TARGET llm-gateway POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:llm-gateway>/resources"
    )

    foreach(RES ${ALL_RESOURCES})
        add_custom_command(TARGET llm-gateway POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RES}" "$<TARGET_FILE_DIR:llm-gateway>/resources"
        )
    endforeach()

    add_custom_command(TARGET llm-gateway POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/src/webui" "$<TARGET_FILE_DIR:llm-gateway>/webui"
    )
endif()
