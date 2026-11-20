cmake_minimum_required(VERSION 3.24)

project(SagePreview VERSION 1.0.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "SagePreview is a native Windows 11 application.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Please build the x64 configuration.")
endif()

# Release build: statically link the MSVC runtime so installation does not
# require the end user to separately install a Visual C++ Redistributable.
# All in-tree targets and fetched zlib should use the same runtime setting.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(SageClip STATIC src/clip.cpp)
target_compile_features(SageClip PRIVATE cxx_std_20)
target_compile_definitions(SageClip PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(SageClip PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(SageClip PUBLIC winsqlite3)

add_library(SageNavigation STATIC src/navigation.cpp)
target_compile_features(SageNavigation PUBLIC cxx_std_20)
target_include_directories(SageNavigation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
    target_compile_options(SageNavigation PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_library(SagePreviewLayout STATIC src/preview_layout.cpp)
target_compile_features(SagePreviewLayout PUBLIC cxx_std_20)
target_include_directories(SagePreviewLayout PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
    target_compile_options(SagePreviewLayout PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(SagePreview WIN32 src/main.cpp)
target_compile_features(SagePreview PRIVATE cxx_std_20)
target_compile_definitions(SagePreview PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SagePreview PRIVATE
    SageClip
    SageNavigation
    SagePreviewLayout
    d2d1
    gdi32
    windowscodecs
    ole32
    shell32
    shlwapi
    comdlg32
)

if(MSVC)
    target_compile_options(SageClip PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(SagePreview PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()


# Manually started, tray-free Spacebar listener. Keep this as a separate process:
# neither hooks nor COM selection lookups run inside Explorer itself.
add_executable(SagePreviewListener WIN32 src/listener.cpp)
target_compile_features(SagePreviewListener PRIVATE cxx_std_20)
target_compile_definitions(SagePreviewListener PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SagePreviewListener PRIVATE ole32 shell32 user32 uuid)
if(MSVC)
    target_compile_options(SagePreviewListener PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(SagePreviewLayoutTests tests/preview_layout_tests.cpp)
    target_compile_features(SagePreviewLayoutTests PRIVATE cxx_std_20)
    target_link_libraries(SagePreviewLayoutTests PRIVATE SagePreviewLayout)
    add_test(NAME PreviewLayout COMMAND SagePreviewLayoutTests)

    add_executable(SageClipTests tests/clip_tests.cpp)
    target_compile_features(SageClipTests PRIVATE cxx_std_20)
    target_link_libraries(SageClipTests PRIVATE SageClip)
    if(MSVC)
        target_compile_options(SageClipTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME ClipExtraction COMMAND SageClipTests
             "${CMAKE_CURRENT_SOURCE_DIR}/samples/synthetic-test.clip")

    add_executable(SageNavigationTests tests/navigation_tests.cpp)
    target_compile_features(SageNavigationTests PRIVATE cxx_std_20)
    target_link_libraries(SageNavigationTests PRIVATE SageNavigation)
    if(MSVC)
        target_compile_options(SageNavigationTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME SiblingNavigation COMMAND SageNavigationTests)
endif()

# New, independent Shell thumbnail component. Neither the existing preview
# executable nor the working Explorer Spacebar listener is rebuilt from new source.
# IMPORTANT: CMake only compiles these files. It NEVER registers the DLL.
add_library(SageThumbnailProvider SHARED
    src/thumbnail_provider.cpp src/thumbnail_ids.h src/thumbnail_provider.def)
target_compile_features(SageThumbnailProvider PRIVATE cxx_std_20)
target_compile_definitions(SageThumbnailProvider PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SageThumbnailProvider PRIVATE SageClip ole32 windowscodecs gdi32 uuid)
if(MSVC)
    target_compile_options(SageThumbnailProvider PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(SageThumbProbe src/thumbnail_probe.cpp src/thumbnail_ids.h)
target_compile_features(SageThumbProbe PRIVATE cxx_std_20)
target_compile_definitions(SageThumbProbe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SageThumbProbe PRIVATE ole32 shlwapi windowscodecs gdi32 uuid)
add_dependencies(SageThumbProbe SageThumbnailProvider)
if(MSVC)
    target_compile_options(SageThumbProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

# Convenience: place opt-in scripts alongside the built DLL/probe.
add_custom_command(TARGET SageThumbnailProvider POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/Register-ClipThumbnail.ps1"
      "$<TARGET_FILE_DIR:SageThumbnailProvider>/Register-ClipThumbnail.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/Unregister-ClipThumbnail.ps1"
      "$<TARGET_FILE_DIR:SageThumbnailProvider>/Unregister-ClipThumbnail.ps1"
    VERBATIM)

if(BUILD_TESTING)
    add_executable(SageClipStreamTests tests/clip_stream_tests.cpp)
    target_compile_features(SageClipStreamTests PRIVATE cxx_std_20)
    target_link_libraries(SageClipStreamTests PRIVATE SageClip)
    if(MSVC)
        target_compile_options(SageClipStreamTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME ClipStreamExtraction COMMAND SageClipStreamTests
        "${CMAKE_CURRENT_SOURCE_DIR}/samples/synthetic-test.clip")

    add_test(NAME ThumbnailProviderDirectProbe COMMAND SageThumbProbe
        "${CMAKE_CURRENT_SOURCE_DIR}/samples/synthetic-test.clip"
        "${CMAKE_CURRENT_BINARY_DIR}/sagepreview-thumbn-test.png" 256)
endif()

# v0.7: Krita's compressed ZIP composite. zlib is linked statically; no DLL to ship.
# A one-time internet download occurs at CMake configuration, not at runtime.
include(FetchContent)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sage_zlib
    URL https://github.com/madler/zlib/releases/download/v1.4/zlib-1.4.tar.gz
    URL_HASH SHA256=02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sage_zlib)

add_library(SageKra STATIC src/kra.cpp src/kra.h)
target_compile_features(SageKra PUBLIC cxx_std_20)
target_include_directories(SageKra PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(SageKra PUBLIC zlibstatic)
if(MSVC)
    target_compile_options(SageKra PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
# Viewer gains Krita support; no changes to the original .clip DLL provider.
target_link_libraries(SagePreview PRIVATE SageKra)

# An independent COM class and DLL lets the existing .clip registration stay
# in place until the eventual production release consolidates the handlers.
add_library(SageKraThumbnailProvider SHARED
    src/kra_thumbnail_provider.cpp src/kra_thumbnail_ids.h
    src/kra_thumbnail_provider.def)
target_compile_features(SageKraThumbnailProvider PRIVATE cxx_std_20)
target_compile_definitions(SageKraThumbnailProvider PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SageKraThumbnailProvider PRIVATE SageKra ole32 windowscodecs gdi32 uuid)
if(MSVC)
    target_compile_options(SageKraThumbnailProvider PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
add_executable(SageKraThumbProbe src/kra_thumbnail_probe.cpp src/kra_thumbnail_ids.h)
target_compile_features(SageKraThumbProbe PRIVATE cxx_std_20)
target_compile_definitions(SageKraThumbProbe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SageKraThumbProbe PRIVATE ole32 shlwapi windowscodecs gdi32 uuid)
add_dependencies(SageKraThumbProbe SageKraThumbnailProvider)
if(MSVC)
    target_compile_options(SageKraThumbProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
add_custom_command(TARGET SageKraThumbnailProvider POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/Register-KraThumbnail.ps1"
      "$<TARGET_FILE_DIR:SageKraThumbnailProvider>/Register-KraThumbnail.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/Unregister-KraThumbnail.ps1"
      "$<TARGET_FILE_DIR:SageKraThumbnailProvider>/Unregister-KraThumbnail.ps1"
    VERBATIM)
if(BUILD_TESTING)
    add_executable(SageKraTests tests/kra_tests.cpp)
    target_compile_features(SageKraTests PRIVATE cxx_std_20)
    target_link_libraries(SageKraTests PRIVATE SageKra)
    add_test(NAME KritaExtraction COMMAND SageKraTests
        "${CMAKE_CURRENT_SOURCE_DIR}/samples")
    add_test(NAME KritaThumbnailProviderDirectProbe COMMAND SageKraThumbProbe
        "${CMAKE_CURRENT_SOURCE_DIR}/samples/synthetic-deflate.kra"
        "${CMAKE_CURRENT_BINARY_DIR}/sagepreview-kra-thumbnail-test.png" 256)
endif()

# v0.8: Photoshop PSD v1 saved-composite support. Uses the zlib already
# fetched for Krita; this does not add any third-party dependency.
add_library(SagePsd STATIC src/psd.cpp src/psd.h)
target_compile_features(SagePsd PUBLIC cxx_std_20)
target_include_directories(SagePsd PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(SagePsd PUBLIC zlibstatic)
if(MSVC)
    target_compile_options(SagePsd PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
target_link_libraries(SagePreview PRIVATE SagePsd)

# Separate, optional PSD handler. No COM registration at build time.
# Existing .clip and .kra COM classes remain unchanged.
add_library(SagePsdThumbnailProvider SHARED
    src/psd_thumbnail_provider.cpp src/psd_thumbnail_ids.h
    src/psd_thumbnail_provider.def)
target_compile_features(SagePsdThumbnailProvider PRIVATE cxx_std_20)
target_compile_definitions(SagePsdThumbnailProvider PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SagePsdThumbnailProvider PRIVATE
    SagePsd ole32 windowscodecs gdi32 uuid)
if(MSVC)
    target_compile_options(SagePsdThumbnailProvider PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
add_executable(SagePsdThumbProbe src/psd_thumbnail_probe.cpp src/psd_thumbnail_ids.h)
target_compile_features(SagePsdThumbProbe PRIVATE cxx_std_20)
target_compile_definitions(SagePsdThumbProbe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SagePsdThumbProbe PRIVATE
    ole32 shlwapi windowscodecs gdi32 uuid)
add_dependencies(SagePsdThumbProbe SagePsdThumbnailProvider)
if(MSVC)
    target_compile_options(SagePsdThumbProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
add_custom_command(TARGET SagePsdThumbnailProvider POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/Register-PsdThumbnail.ps1"
      "$<TARGET_FILE_DIR:SagePsdThumbnailProvider>/Register-PsdThumbnail.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/Unregister-PsdThumbnail.ps1"
      "$<TARGET_FILE_DIR:SagePsdThumbnailProvider>/Unregister-PsdThumbnail.ps1"
    VERBATIM)
if(BUILD_TESTING)
    add_executable(SagePsdTests tests/psd_tests.cpp)
    target_compile_features(SagePsdTests PRIVATE cxx_std_20)
    target_link_libraries(SagePsdTests PRIVATE SagePsd)
    if(MSVC)
        target_compile_options(SagePsdTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME PhotoshopComposite COMMAND SagePsdTests
        "${CMAKE_CURRENT_SOURCE_DIR}/samples")
    add_test(NAME PhotoshopThumbnailProviderDirectProbe COMMAND SagePsdThumbProbe
        "${CMAKE_CURRENT_SOURCE_DIR}/samples/synthetic-rle.psd"
        "${CMAKE_CURRENT_BINARY_DIR}/sagepreview-psd-thumbnail-test.png" 256)
endif()

# v0.9: Windows 11 AVIF support via an installed WIC codec. No bundled
# AVIF decoder is introduced, and CMake never registers a shell extension.
# Viewer and Spacebar listener already dispatch .avif to WIC by extension.
add_library(SageAvifThumbnailProvider SHARED
    src/avif_thumbnail_provider.cpp src/avif_thumbnail_ids.h
    src/avif_thumbnail_provider.def)
target_compile_features(SageAvifThumbnailProvider PRIVATE cxx_std_20)
target_compile_definitions(SageAvifThumbnailProvider PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SageAvifThumbnailProvider PRIVATE
    ole32 windowscodecs gdi32 uuid)
if(MSVC)
    target_compile_options(SageAvifThumbnailProvider PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(SageAvifThumbProbe src/avif_thumbnail_probe.cpp
    src/avif_thumbnail_ids.h)
target_compile_features(SageAvifThumbProbe PRIVATE cxx_std_20)
target_compile_definitions(SageAvifThumbProbe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SageAvifThumbProbe PRIVATE
    ole32 shlwapi windowscodecs gdi32 uuid)
add_dependencies(SageAvifThumbProbe SageAvifThumbnailProvider)
if(MSVC)
    target_compile_options(SageAvifThumbProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(SageAvifCodecCheck src/avif_codec_check.cpp)
target_compile_features(SageAvifCodecCheck PRIVATE cxx_std_20)
target_compile_definitions(SageAvifCodecCheck PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SageAvifCodecCheck PRIVATE ole32 windowscodecs)
if(MSVC)
    target_compile_options(SageAvifCodecCheck PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_dependencies(SageAvifThumbnailProvider SageAvifCodecCheck)
add_custom_command(TARGET SageAvifThumbnailProvider POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/Register-AvifThumbnail.ps1"
      "$<TARGET_FILE_DIR:SageAvifThumbnailProvider>/Register-AvifThumbnail.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/Unregister-AvifThumbnail.ps1"
      "$<TARGET_FILE_DIR:SageAvifThumbnailProvider>/Unregister-AvifThumbnail.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/synthetic-avif.avif"
      "$<TARGET_FILE_DIR:SageAvifThumbnailProvider>/synthetic-avif.avif"
    VERBATIM)

if(BUILD_TESTING)
    # AVIF is a separately installed Windows codec on some Windows 11 systems.
    # This test reports SKIPPED (exit code 77) instead of falsely claiming
    # success if the Windows WIC codec is unavailable.
    add_test(NAME AvifCodecAvailability COMMAND SageAvifCodecCheck
        "${CMAKE_CURRENT_SOURCE_DIR}/samples/synthetic-avif.avif")
    set_tests_properties(AvifCodecAvailability PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Release staging (no registration, installer, or elevation at build time).
# Build x64 Release, run CTest, then:
# cmake --install out/release --config Release --prefix dist/SagePreview
# The resulting directory is portable until the user opts into registration.
install(TARGETS
    SagePreview SagePreviewListener
    SageThumbnailProvider SageKraThumbnailProvider
    SagePsdThumbnailProvider SageAvifThumbnailProvider
    SageAvifCodecCheck
    RUNTIME DESTINATION . LIBRARY DESTINATION .)
install(FILES
    tools/Install-SagePreview.ps1
    tools/Uninstall-SagePreview.ps1
    tools/Test-Release.ps1
    samples/synthetic-avif.avif
    samples/webp-codec-test.webp
    README.md
    docs/THIRD_PARTY.md
    docs/RELEASE_CHECKLIST.md
    docs/INSTALLATION.md
    LICENSE
    licenses/ZLIB-LICENSE.txt
    DESTINATION .)

# Native Windows version metadata for the two user-facing executables.
target_sources(SagePreview PRIVATE src/version.rc)
target_sources(SagePreviewListener PRIVATE src/version_listener.rc)
