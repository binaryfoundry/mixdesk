cmake_minimum_required(VERSION 3.22)

project(MixdeskPhraseWorkspace VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MIXDESK_FETCH_JUCE "Fetch JUCE from source if JUCE_DIR is not provided" ON)
option(MIXDESK_STATIC_RUNTIME "Use the static MSVC runtime to avoid runtime DLL dependencies" ON)

if(MSVC AND MIXDESK_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)

if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(STATUS "Using local JUCE checkout: ${JUCE_DIR}")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
elseif(MIXDESK_FETCH_JUCE)
    message(STATUS "Fetching JUCE from source")
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(JUCE)
else()
    message(FATAL_ERROR "Set JUCE_DIR to a JUCE checkout or enable MIXDESK_FETCH_JUCE.")
endif()

FetchContent_Declare(
    SignalsmithStretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 1.1.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(SignalsmithStretch)
if(NOT TARGET signalsmith-stretch)
    add_library(signalsmith-stretch INTERFACE)
    target_include_directories(signalsmith-stretch SYSTEM INTERFACE "${signalsmithstretch_SOURCE_DIR}")
endif()

juce_add_gui_app(MixdeskPhraseWorkspace
    PRODUCT_NAME "Mixdesk Phrase Workspace"
    COMPANY_NAME "Mixdesk"
    BUNDLE_ID "com.mixdesk.phraseworkspace"
)

target_sources(MixdeskPhraseWorkspace
    PRIVATE
        Source/App/Main.cpp
        Source/App/AppSettings.cpp
        Source/App/AppSettings.h
        Source/App/MainComponent.cpp
        Source/App/MainComponent.h
        Source/Engine/BeatDetector.cpp
        Source/Engine/BeatDetector.h
        Source/Engine/DeckPlaybackEngine.cpp
        Source/Engine/DeckPlaybackEngine.h
        Source/Engine/PhraseAnalyzer.cpp
        Source/Engine/PhraseAnalyzer.h
        Source/Engine/StemPreparation.cpp
        Source/Engine/StemPreparation.h
        Source/Engine/TrackLoader.cpp
        Source/Engine/TrackLoader.h
        Source/Engine/WaveformAnalyzer.cpp
        Source/Engine/WaveformAnalyzer.h
        Source/Engine/WorkspaceController.cpp
        Source/Engine/WorkspaceController.h
        Source/Model/DemoState.cpp
        Source/Model/DemoState.h
        Source/Model/PhraseModel.cpp
        Source/Model/PhraseModel.h
        Source/UI/PhraseWorkspace.cpp
        Source/UI/PhraseWorkspace.h
)

target_include_directories(MixdeskPhraseWorkspace
    PRIVATE
        Source
)

target_compile_definitions(MixdeskPhraseWorkspace
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_MODAL_LOOPS_PERMITTED=0
)

target_link_libraries(MixdeskPhraseWorkspace
    PRIVATE
        juce::juce_gui_extra
        juce::juce_audio_utils
        signalsmith-stretch
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        juce::juce_recommended_lto_flags
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" PREFIX "Source" FILES
    Source/App/Main.cpp
    Source/App/AppSettings.cpp
    Source/App/AppSettings.h
    Source/App/MainComponent.cpp
    Source/App/MainComponent.h
    Source/Engine/BeatDetector.cpp
    Source/Engine/BeatDetector.h
    Source/Engine/DeckPlaybackEngine.cpp
    Source/Engine/DeckPlaybackEngine.h
    Source/Engine/PhraseAnalyzer.cpp
    Source/Engine/PhraseAnalyzer.h
    Source/Engine/StemPreparation.cpp
    Source/Engine/StemPreparation.h
    Source/Engine/TrackLoader.cpp
    Source/Engine/TrackLoader.h
    Source/Engine/WaveformAnalyzer.cpp
    Source/Engine/WaveformAnalyzer.h
    Source/Engine/WorkspaceController.cpp
    Source/Engine/WorkspaceController.h
    Source/Model/DemoState.cpp
    Source/Model/DemoState.h
    Source/Model/PhraseModel.cpp
    Source/Model/PhraseModel.h
    Source/UI/PhraseWorkspace.cpp
    Source/UI/PhraseWorkspace.h
)

enable_testing()

add_executable(MixdeskStemTests
    Tests/StemPreparationTests.cpp
    Source/Engine/StemPreparation.cpp
    Source/Engine/StemPreparation.h
    Source/Model/PhraseModel.cpp
    Source/Model/PhraseModel.h
)

target_include_directories(MixdeskStemTests
    PRIVATE
        Source
)

target_compile_definitions(MixdeskStemTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(MixdeskStemTests
    PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME MixdeskStemTests COMMAND MixdeskStemTests)
