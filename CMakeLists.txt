cmake_minimum_required(VERSION 3.22)
project(LyraTune VERSION 2.0.0 LANGUAGES C CXX)

# Product naming lives here only; DSP code never refers to it.
set(PRODUCT_NAME "Miku Tune v2.0" CACHE STRING "User-visible plugin name")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static release C runtime (/MT) in all configs: no runtime DLLs needed on the target, and xwin ships no debug runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

add_subdirectory(third_party/JUCE)

juce_add_plugin(LyraTune
    COMPANY_NAME "Luna (mod of LyraTune by stack)"
    BUNDLE_ID com.luna.mikutune
    PLUGIN_MANUFACTURER_CODE Lyra
    PLUGIN_CODE Mkt2
    PRODUCT_NAME "${PRODUCT_NAME}"
    FORMATS VST3 AU          # AU is built on macOS only; JUCE skips it elsewhere
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    VST3_CATEGORIES Fx Pitch Shift
    VST3_AUTO_MANIFEST FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(LyraTuneAssets SOURCES
    Resources/fonts/Inter-Regular.ttf
    Resources/fonts/Inter-SemiBold.ttf
    Resources/ui/ui_base.png
    Resources/ui/ui_scale_off.png
    Resources/ui/ui_scale_on.png
    Resources/ui/ui_keys_off.png
    Resources/ui/ui_keys_on.png
    Resources/ui/ui_sparkle_off.png
    Resources/ui/ui_sparkle_on.png)

target_sources(LyraTune PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/Theme.cpp
    Source/UI/LyraLookAndFeel.cpp
    Source/UI/Knob.cpp
    Source/UI/Orb.cpp
    Source/UI/KeyboardPanel.cpp
    Source/UI/Widgets.cpp
    Source/DSP/Pitch/TargetSelector.cpp
    Source/DSP/Pitch/YinDetector.cpp
    Source/DSP/Pitch/PitchShifter.cpp
    Source/DSP/Pitch/CorrectionEngine.cpp
    Source/DSP/TunerChain.cpp
    Source/DSP/Doubler.cpp
    Source/DSP/FormantAccent.cpp)

target_compile_definitions(LyraTune PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(LyraTune
    PRIVATE LyraTuneAssets juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Dev tool: ui_snapshot renders the real editor to PNG under Wine (not installed, not part of the plugin).
# Lives in the shared LyraTools checkout; skipped when it is absent.
set(LYRA_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../LyraTools" CACHE PATH "Shared Lyra dev tools")
if(EXISTS "${LYRA_TOOLS_DIR}/ui_snapshot/LyraUiSnapshot.cmake")
    include("${LYRA_TOOLS_DIR}/ui_snapshot/LyraUiSnapshot.cmake")
    lyra_add_ui_snapshot(LyraTune)
endif()
