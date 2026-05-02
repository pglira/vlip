cmake_minimum_required(VERSION 3.16)
project(vlip CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets Svg Concurrent Multimedia MultimediaWidgets)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBHEIF IMPORTED_TARGET libheif)

set(SRCS
    src/main.cpp
    src/MainWindow.cpp
    src/Project.cpp
    src/ProjectIO.cpp
    src/Importer.cpp
    src/MetaProbe.cpp
    src/ThumbnailCache.cpp
    src/TimelinePane.cpp
    src/PropertiesPane.cpp
    src/PreviewPane.cpp
    src/DefaultsPane.cpp
    src/StatusPane.cpp
    src/ImagePreviewWidget.cpp
    src/VideoPreviewWidget.cpp
    src/Renderer.cpp
)

add_executable(vlip ${SRCS})
target_include_directories(vlip PRIVATE src)
target_link_libraries(vlip PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Concurrent
    Qt6::Multimedia Qt6::MultimediaWidgets
)

if(LIBHEIF_FOUND)
    target_link_libraries(vlip PRIVATE PkgConfig::LIBHEIF)
    target_compile_definitions(vlip PRIVATE VLIP_HAS_HEIF=1)
endif()

target_compile_options(vlip PRIVATE -Wall -Wextra -Wno-unused-parameter)

option(BUILD_TESTS "Build the smoke and saveload tests" ON)
if(BUILD_TESTS)
    add_executable(vlip_smoke
        tests/smoke_render.cpp
        src/Project.cpp
        src/ProjectIO.cpp
        src/Importer.cpp
        src/MetaProbe.cpp
        src/ThumbnailCache.cpp
            src/Renderer.cpp
    )
    target_include_directories(vlip_smoke PRIVATE src)
    target_link_libraries(vlip_smoke PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent)
    target_compile_options(vlip_smoke PRIVATE -Wall -Wextra -Wno-unused-parameter)

    add_executable(vlip_saveload
        tests/saveload_test.cpp
        src/Project.cpp
        src/ProjectIO.cpp
        src/Importer.cpp
        src/MetaProbe.cpp
        src/ThumbnailCache.cpp
    )
    target_include_directories(vlip_saveload PRIVATE src)
    target_link_libraries(vlip_saveload PRIVATE Qt6::Core Qt6::Gui)
    target_compile_options(vlip_saveload PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
