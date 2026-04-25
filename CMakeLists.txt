cmake_minimum_required(VERSION 3.20)

project(LocaleEmulatorLauncher LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(LocaleEmulator WIN32
    src/main.cpp
    resources/icon.rc
)

target_include_directories(LocaleEmulator PRIVATE src)
target_link_libraries(LocaleEmulator PRIVATE Ole32 Shell32)

add_executable(CreateLEShortcut WIN32
    src/create_shortcut.cpp
    resources/icon.rc
)

target_include_directories(CreateLEShortcut PRIVATE src)
target_link_libraries(CreateLEShortcut PRIVATE Ole32 Shell32)

if (MSVC)
    target_compile_options(LocaleEmulator PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(CreateLEShortcut PRIVATE /W4 /permissive- /utf-8)
endif()
