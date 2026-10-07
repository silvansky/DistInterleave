cmake_minimum_required(VERSION 3.22)
project(DistInterleave VERSION 0.1.0 LANGUAGES C CXX)

set(JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE checkout (optional)")
if(JUCE_SOURCE_DIR)
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 501c07674e1ad693085a7e7c398f205c2677f5da
        GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(plugin_formats VST3)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()
juce_add_plugin(DistInterleave
    COMPANY_NAME "Valentine Silvansky"
    BUNDLE_ID "com.silvansky.DistInterleave"
    PLUGIN_MANUFACTURER_CODE VSil
    PLUGIN_CODE Ditl
    FORMATS ${plugin_formats}
    PRODUCT_NAME "DistInterleave"
    DESCRIPTION "Zero-crossing cycle interleaver"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES "Fx" "Distortion"
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_SANDBOX_SAFE TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(effect_sources src/PluginProcessor.cpp src/PluginEditor.cpp src/LookAndFeel.cpp)
target_sources(DistInterleave PRIVATE ${effect_sources})
target_include_directories(DistInterleave PUBLIC src)
target_compile_features(DistInterleave PUBLIC cxx_std_20)
target_compile_definitions(DistInterleave PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(DistInterleave PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# A custom standalone exposes all four hardware inputs; JUCE's default wrapper
# only exposes the main bus in its audio-device selector.
juce_add_gui_app(DistInterleave_Standalone
    PRODUCT_NAME "DistInterleave"
    COMPANY_NAME "Valentine Silvansky"
    BUNDLE_ID "com.silvansky.DistInterleave.standalone"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "DistInterleave processes your audio inputs.")
target_sources(DistInterleave_Standalone PRIVATE src/Standalone.cpp ${effect_sources})
target_include_directories(DistInterleave_Standalone PRIVATE src)
target_compile_features(DistInterleave_Standalone PRIVATE cxx_std_20)
target_compile_definitions(DistInterleave_Standalone PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(DistInterleave_Standalone PRIVATE juce::juce_audio_utils
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(APPLE)
    find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
    # The VST3 manifest helper signs its own bundle. Seal the other bundles too,
    # including resources and Info.plist, for reliable local loading on macOS.
    foreach(bundle_target DistInterleave_AU DistInterleave_Standalone)
        add_custom_command(TARGET ${bundle_target} POST_BUILD
            COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none
                "$<TARGET_BUNDLE_DIR:${bundle_target}>"
            VERBATIM)
    endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(EngineTests tests/EngineTests.cpp)
    target_include_directories(EngineTests PRIVATE src)
    target_compile_features(EngineTests PRIVATE cxx_std_20)
    add_test(NAME engine COMMAND EngineTests)
    add_executable(ProcessorTests tests/ProcessorTests.cpp)
    target_link_libraries(ProcessorTests PRIVATE DistInterleave juce::juce_audio_utils)
    add_test(NAME processor COMMAND ProcessorTests)
endif()

add_executable(DistInterleaveShot EXCLUDE_FROM_ALL tools/Screenshot.cpp)
target_link_libraries(DistInterleaveShot PRIVATE DistInterleave juce::juce_audio_utils)

if(APPLE AND BUILD_TESTING)
    add_executable(AUSmoke tools/AUSmoke.cpp)
    target_compile_features(AUSmoke PRIVATE cxx_std_20)
    target_link_libraries(AUSmoke PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
    add_dependencies(AUSmoke DistInterleave_AU)
    add_test(NAME au_load_and_render COMMAND AUSmoke "$<TARGET_BUNDLE_DIR:DistInterleave_AU>")
endif()
