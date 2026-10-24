cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Zielarchitektur (Apple Silicon)")
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimale macOS-Version")

project(Autosampler VERSION 1.0.2 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Workaround: Veraltete Reste unter CommandLineTools/usr/include/c++/v1 überdecken
# die libc++-Header des SDK ("'utility' file not found"). Dann die SDK-Header
# explizit verwenden. Über ENV gilt das auch für den juceaide-Unterbuild.
if(APPLE)
    execute_process(COMMAND xcrun --find clang++ OUTPUT_VARIABLE _clangxx OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _sdk_path OUTPUT_STRIP_TRAILING_WHITESPACE)
    get_filename_component(_toolchain_bin "${_clangxx}" DIRECTORY)
    set(_toolchain_libcxx "${_toolchain_bin}/../include/c++/v1")
    set(_sdk_libcxx "${_sdk_path}/usr/include/c++/v1")

    if(EXISTS "${_toolchain_libcxx}" AND NOT EXISTS "${_toolchain_libcxx}/utility" AND EXISTS "${_sdk_libcxx}/utility")
        message(STATUS "Unvollständige libc++-Header in den CommandLineTools gefunden, nutze ${_sdk_libcxx}")
        set(_libcxx_flags "-nostdinc++ -isystem \"${_sdk_libcxx}\"")
        string(APPEND CMAKE_CXX_FLAGS " ${_libcxx_flags}")
        string(APPEND CMAKE_OBJCXX_FLAGS " ${_libcxx_flags}")
        set(ENV{CXXFLAGS} "$ENV{CXXFLAGS} ${_libcxx_flags}")
        set(ENV{OBJCXXFLAGS} "$ENV{OBJCXXFLAGS} ${_libcxx_flags}")
    endif()
endif()

# JUCE: lokaler Checkout unter external/JUCE, sonst per FetchContent laden
set(AUTOSAMPLER_JUCE_TAG "9.0.4")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${AUTOSAMPLER_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(Autosampler
    PRODUCT_NAME            "Autosampler"
    COMPANY_NAME            "Autosampler"
    COMPANY_COPYRIGHT       "Copyright 2026 Autosampler"
    PLIST_TO_MERGE          "<plist><dict><key>LSApplicationCategoryType</key><string>public.app-category.music</string></dict></plist>"
    BUNDLE_ID               "com.autosampler.app"
    VERSION                 ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Wird für das Sampling von Hardware-Synthesizern benötigt."
    SEND_APPLE_EVENTS_PERMISSION_ENABLED TRUE
    SEND_APPLE_EVENTS_PERMISSION_TEXT "Einige Plugins benötigen dies, um ihre Hilfsprogramme (z. B. zur Lizenzierung) zu steuern."
    NEEDS_CURL              FALSE
    NEEDS_WEB_BROWSER       FALSE)

# Engine ohne GUI-Abhängigkeiten, wird von App und PluginProbe genutzt
set(AUTOSAMPLER_ENGINE_SOURCES
    Source/engine/AudioAnalysis.cpp
    Source/engine/Exporter.cpp
    Source/engine/ExportSettings.cpp
    Source/engine/LoopBatch.cpp
    Source/engine/LoopCrossfade.cpp
    Source/engine/LivePlaySettings.cpp
    Source/engine/LoopFinder.cpp
    Source/engine/NoteRenderer.cpp
    Source/engine/PerformTools.cpp
    Source/engine/SampleWriter.cpp
    Source/engine/SamplerEngine.cpp
    Source/engine/SamplerInstrument.cpp
    Source/engine/SamplerLoader.cpp
    Source/engine/SamplingJob.cpp
    Source/engine/SamplingSettings.cpp
    Source/engine/Session.cpp
    Source/engine/ZoneSmoothing.cpp)

# Schriften der Oberfläche (VT323, Rubik; SIL OFL) und QR-Code des About-Fensters
juce_add_binary_data(AutosamplerFonts SOURCES
    Resources/Fonts/VT323-Regular.ttf
    Resources/Fonts/Rubik-Bold.ttf
    Resources/Fonts/Rubik-Black.ttf
    Resources/About/SupportQr.png)

target_sources(Autosampler PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/host/LivePlayProcessor.cpp
    Source/host/PluginHost.cpp
    Source/host/PluginScanner.cpp
    Source/host/Vst3ModuleInfoFix.cpp
    Source/ui/AboutOverlay.cpp
    Source/ui/AutosampleView.cpp
    Source/ui/EditorView.cpp
    Source/ui/ExportView.cpp
    Source/ui/Fonts.cpp
    Source/ui/LivePlayView.cpp
    Source/ui/PluginWindow.cpp
    Source/ui/SampleCard.cpp
    Source/ui/SidebarView.cpp
    Source/ui/Appearance.mm
    Source/ui/Theme.cpp
    Source/ui/Widgets.cpp
    ${AUTOSAMPLER_ENGINE_SOURCES})

target_include_directories(Autosampler PRIVATE Source)

target_compile_definitions(Autosampler PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Autosampler,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Autosampler,JUCE_VERSION>")

target_link_libraries(Autosampler
    PRIVATE
        AutosamplerFonts
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Signatur mit Hardened Runtime und Entitlements. JUCE macht das nur für Xcode-
# Projekte; manche Plugins (KORG) prüfen die Signatur des Hosts.
# Standard ist ad-hoc ("-"), für die Weitergabe eine Developer ID Application
# (siehe Release.md); die Notarisierung verlangt dann einen sicheren Zeitstempel.
set(AUTOSAMPLER_SIGN_IDENTITY "-" CACHE STRING "Codesign-Identität (- = ad-hoc)")

if(AUTOSAMPLER_SIGN_IDENTITY STREQUAL "-")
    set(_sign_timestamp "--timestamp=none")
else()
    set(_sign_timestamp "--timestamp")
endif()

add_custom_command(TARGET Autosampler POST_BUILD
    COMMAND codesign --force --deep --sign "${AUTOSAMPLER_SIGN_IDENTITY}"
            --options runtime ${_sign_timestamp}
            --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Autosampler.entitlements"
            "$<TARGET_BUNDLE_DIR:Autosampler>"
    COMMENT "Signiere Autosampler.app mit Entitlements"
    VERBATIM)

# Entwickler-Tool: Plugin ohne GUI laden und Busse/Fehler ausgeben
juce_add_console_app(PluginProbe PRODUCT_NAME "PluginProbe")
target_sources(PluginProbe PRIVATE Tools/PluginProbe.cpp Source/host/LivePlayProcessor.cpp Source/host/Vst3ModuleInfoFix.cpp ${AUTOSAMPLER_ENGINE_SOURCES})
target_include_directories(PluginProbe PRIVATE Source)
target_compile_definitions(PluginProbe PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISABLE_CAUTIOUS_PARAMETER_ID_CHECKING=1)
target_link_libraries(PluginProbe
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
