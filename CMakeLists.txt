cmake_minimum_required(VERSION 3.26)

project(Radiary VERSION 1.0.1 LANGUAGES CXX)

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
    src/renderer/D3D11ShaderUtils.cpp
    src/renderer/D3D11ResourceUtils.cpp
    src/renderer/GpuFlameRenderer.cpp
    src/renderer/GpuPathRenderer.cpp
    src/renderer/PathDrawListBuilder.cpp
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

set(radiary_shader_files
    src/renderer/shaders/GpuFlameRenderer.hlsl
    src/renderer/shaders/GpuPathRenderer.hlsl
    src/renderer/shaders/GpuDofRenderer.hlsl
    src/renderer/shaders/GpuDenoiser.hlsl
    src/renderer/shaders/GpuPostProcess.hlsl
)

if (WIN32)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")

    set(RADIARY_RESOURCE_LINES "")
    set(RADIARY_APP_ICON_PNG "${CMAKE_SOURCE_DIR}/assets/ui/app-icon.png")
    if (EXISTS "${RADIARY_APP_ICON_PNG}")
        find_program(RADIARY_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
        set(RADIARY_APP_ICON_ICO "${CMAKE_BINARY_DIR}/generated/RadiaryAppIcon.ico")
        execute_process(
            COMMAND
                "${RADIARY_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/cmake/ConvertPngToIco.ps1"
                -InputPath "${RADIARY_APP_ICON_PNG}"
                -OutputPath "${RADIARY_APP_ICON_ICO}"
            RESULT_VARIABLE RADIARY_ICON_RESULT
            OUTPUT_VARIABLE RADIARY_ICON_STDOUT
            ERROR_VARIABLE RADIARY_ICON_STDERR)
        if (NOT RADIARY_ICON_RESULT EQUAL 0)
            message(FATAL_ERROR
                "Failed to convert ${RADIARY_APP_ICON_PNG} to .ico\n"
                "stdout:\n${RADIARY_ICON_STDOUT}\n"
                "stderr:\n${RADIARY_ICON_STDERR}")
        endif()

        file(TO_CMAKE_PATH "${RADIARY_APP_ICON_ICO}" RADIARY_APP_ICON_RESOURCE_PATH)
        string(APPEND RADIARY_RESOURCE_LINES
            "IDI_RADIARY_APP_ICON ICON \"${RADIARY_APP_ICON_RESOURCE_PATH}\"\n")
    endif()

    if (RADIARY_EMBED_FFMPEG AND EXISTS "${RADIARY_FFMPEG_EXE}")
        file(TO_CMAKE_PATH "${RADIARY_FFMPEG_EXE}" RADIARY_FFMPEG_RESOURCE_PATH)
        string(APPEND RADIARY_RESOURCE_LINES
            "IDR_RADIARY_FFMPEG RCDATA \"${RADIARY_FFMPEG_RESOURCE_PATH}\"\n")
    endif()

    if (NOT RADIARY_RESOURCE_LINES STREQUAL "")
        configure_file(
            "${CMAKE_SOURCE_DIR}/src/app/RadiaryResources.rc.in"
            "${CMAKE_BINARY_DIR}/generated/RadiaryResources.rc"
            @ONLY)
        list(APPEND radiary_sources "${CMAKE_BINARY_DIR}/generated/RadiaryResources.rc")
    endif()
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
    RADIARY_SHADER_DIR=L\"shaders\"
)

if (MSVC)
    target_compile_options(Radiary PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(Radiary PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(Radiary PRIVATE windowscodecs comdlg32 ole32 dwmapi d3d11 dxgi gdiplus)

install(TARGETS Radiary RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)
install(FILES ${radiary_shader_files} DESTINATION shaders)
install(FILES README.md DESTINATION .)

add_custom_command(TARGET Radiary POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:Radiary>/shaders")

foreach(shader_file IN LISTS radiary_shader_files)
    add_custom_command(TARGET Radiary POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/${shader_file}"
            "$<TARGET_FILE_DIR:Radiary>/shaders")
endforeach()

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
    if (DEFINED RADIARY_APP_ICON_ICO AND EXISTS "${RADIARY_APP_ICON_ICO}")
        set(CPACK_NSIS_MUI_ICON "${RADIARY_APP_ICON_ICO}")
        set(CPACK_NSIS_MUI_UNIICON "${RADIARY_APP_ICON_ICO}")
    endif()
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/CPackProjectConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/CPackProjectConfig.cmake"
    @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_BINARY_DIR}/CPackProjectConfig.cmake")

include(CPack)
