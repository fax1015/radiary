cmake_minimum_required(VERSION 3.26)

project(Radiary VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(RADIARY_FFMPEG_DIR "${CMAKE_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH "Bundled FFmpeg directory")
set(RADIARY_FFMPEG_EXE "${RADIARY_FFMPEG_DIR}/bin/ffmpeg.exe" CACHE FILEPATH "Bundled FFmpeg executable")
option(RADIARY_EMBED_FFMPEG "Embed bundled FFmpeg into Radiary.exe" ON)

set(radiary_sources
    src/main.cpp
    src/core/Scene.cpp
    src/engine/flame/Variation.cpp
    src/engine/flame/IFSEngine.cpp
    src/engine/path/SplinePath.cpp
    src/renderer/SoftwareRenderer.cpp
    src/renderer/GpuFlameRenderer.cpp
    src/renderer/GpuPathRenderer.cpp
    src/renderer/GpuDofRenderer.cpp
    src/renderer/GpuDenoiser.cpp
    src/renderer/GpuPostProcess.cpp
    src/io/SceneSerializer.cpp
    src/io/PresetLibrary.cpp
    src/app/AppWidgets.cpp
    src/app/CameraUtils.cpp
    src/app/ExportUtils.cpp
    src/app/StartupLogoSvg.cpp
    src/app/AppWindow.cpp
    src/app/AppWindowD3D.cpp
    src/app/AppWindowUI.cpp
    src/app/AppWindowLayers.cpp
    src/app/AppWindowRender.cpp
    src/app/AppWindowExport.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
    third_party/imgui/misc/cpp/imgui_stdlib.cpp
)

if (WIN32 AND RADIARY_EMBED_FFMPEG AND EXISTS "${RADIARY_FFMPEG_EXE}")
    file(TO_CMAKE_PATH "${RADIARY_FFMPEG_EXE}" RADIARY_FFMPEG_RESOURCE_PATH)
    configure_file(
        "${CMAKE_SOURCE_DIR}/src/app/RadiaryResources.rc.in"
        "${CMAKE_BINARY_DIR}/generated/RadiaryResources.rc"
        @ONLY)
    list(APPEND radiary_sources "${CMAKE_BINARY_DIR}/generated/RadiaryResources.rc")
endif()

add_executable(Radiary WIN32 ${radiary_sources})

target_include_directories(Radiary PRIVATE
    src
    third_party/imgui
    third_party/imgui/backends
    third_party/imgui/misc/cpp
)

target_compile_definitions(Radiary PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if (MSVC)
    target_compile_options(Radiary PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(Radiary PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(Radiary PRIVATE windowscodecs comdlg32 ole32 dwmapi d3d11 dxgi gdiplus)

install(TARGETS Radiary RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)
install(FILES README.md DESTINATION .)

if (RADIARY_EMBED_FFMPEG)
    add_custom_command(TARGET Radiary POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:Radiary>/ffmpeg.exe")
elseif (EXISTS "${RADIARY_FFMPEG_EXE}")
    add_custom_command(TARGET Radiary POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RADIARY_FFMPEG_EXE}"
            "$<TARGET_FILE_DIR:Radiary>/ffmpeg.exe")
endif()

if (EXISTS "${RADIARY_FFMPEG_DIR}/LICENSE.txt")
    add_custom_command(TARGET Radiary POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RADIARY_FFMPEG_DIR}/LICENSE.txt"
            "$<TARGET_FILE_DIR:Radiary>/ffmpeg-LICENSE.txt")
    install(FILES "${RADIARY_FFMPEG_DIR}/LICENSE.txt" DESTINATION . RENAME "ffmpeg-LICENSE.txt")
endif()

if (EXISTS "${RADIARY_FFMPEG_DIR}/x264-LICENSE.txt")
    add_custom_command(TARGET Radiary POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RADIARY_FFMPEG_DIR}/x264-LICENSE.txt"
            "$<TARGET_FILE_DIR:Radiary>/ffmpeg-x264-LICENSE.txt")
    install(FILES "${RADIARY_FFMPEG_DIR}/x264-LICENSE.txt" DESTINATION . RENAME "ffmpeg-x264-LICENSE.txt")
endif()

if (EXISTS "${RADIARY_FFMPEG_DIR}/NOTICE.txt")
    add_custom_command(TARGET Radiary POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RADIARY_FFMPEG_DIR}/NOTICE.txt"
            "$<TARGET_FILE_DIR:Radiary>/ffmpeg-NOTICE.txt")
    install(FILES "${RADIARY_FFMPEG_DIR}/NOTICE.txt" DESTINATION . RENAME "ffmpeg-NOTICE.txt")
endif()

if (EXISTS "${RADIARY_FFMPEG_DIR}/BUILD.txt")
    add_custom_command(TARGET Radiary POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RADIARY_FFMPEG_DIR}/BUILD.txt"
            "$<TARGET_FILE_DIR:Radiary>/ffmpeg-BUILD.txt")
    install(FILES "${RADIARY_FFMPEG_DIR}/BUILD.txt" DESTINATION . RENAME "ffmpeg-BUILD.txt")
endif()

set(CPACK_PACKAGE_NAME "Radiary")
set(CPACK_PACKAGE_VENDOR "Radiary")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Radiary")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "Radiary")
set(CPACK_PACKAGE_EXECUTABLES "Radiary" "Radiary")
set(CPACK_CREATE_DESKTOP_LINKS "Radiary")
set(CPACK_MONOLITHIC_INSTALL ON)
set(CPACK_GENERATOR "ZIP;NSIS")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)

if (WIN32)
    set(CPACK_NSIS_DISPLAY_NAME "Radiary")
    set(CPACK_NSIS_PACKAGE_NAME "Radiary")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "Radiary.exe")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "Radiary.exe")
    set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/CPackProjectConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/CPackProjectConfig.cmake"
    @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_BINARY_DIR}/CPackProjectConfig.cmake")

include(CPack)
