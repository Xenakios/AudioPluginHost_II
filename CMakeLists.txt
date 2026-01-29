cmake_minimum_required(VERSION 4.1)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(AudioPluginHost2 VERSION 0.0.1)
set(JUCE_AVAILABLE True)
if (JUCE_AVAILABLE)
add_subdirectory(JUCE)
endif()

include_directories(
    libs/clap/include
    libs/clap-helpers/include
    Source/AudioGraph
    libs/signalsmith-linear/include
    libs/signalsmith-stretch
    libs/choc/choc
    libs/sst-basic-blocks/include
    libs/fmt/include
    libs/sst-jucegui/include
    libs/concurrentqueue
    
    libs/sst-voicemanager/include
    libs/rtaudio
    libs/rtmidi
    libs/MTS-ESP/Master
    libs/MTS-ESP/Client
    libs/mts_esp_wrapper
    libs/tuning-library/include
    libs/sst-filters/include
    libs/sst-effects/include
    libs/sst-waveshapers
    libs/oscpkt
    libs/simde
    libs/thread-pool/include
    C:/develop/cmajlang/cmajor/include
    libs/sst-basic-blocks/libs
    libs/tracktion_engine/modules/tracktion_graph
    libs/airwindows_limited
    libs/airwindows_limited/plugins
)

# for "reasons", this is still a console app even though a GUI app is built from it
# we should have a separate real console app and a real GUI app

add_subdirectory(libs/fmt)
# add_subdirectory(C:/develop/airwin2rack build)
if (JUCE_AVAILABLE)
juce_add_console_app(MainHostApp
    PRODUCT_NAME "Console App Example") 
juce_generate_juce_header(MainHostApp)
target_sources(MainHostApp
    PRIVATE
        Source/Experimental/main.cpp
        Source/Experimental/xapfactory.cpp
        Source/Experimental/xaudiograph.cpp
        Source/Common/xap_utils.cpp
        Source/Experimental/xaps/xap_notegenerator.cpp
        Source/Experimental/xaps/fileplayer_xaudioprocessor.cpp
        Source/Xaps/clap_xaudioprocessor.cpp
        Source/Common/xaudioprocessor.cpp
        libs/sst-jucegui/src/sst/jucegui/components/Knob.cpp
        libs/sst-jucegui/src/sst/jucegui/components/HSlider.cpp
        libs/sst-jucegui/src/sst/jucegui/components/ContinuousParamEditor.cpp
        libs/sst-jucegui/src/sst/jucegui/components/ComponentBase.cpp
        libs/sst-jucegui/src/sst/jucegui/style/StyleSheet.cpp
        libs/sst-jucegui/src/sst/jucegui/style/StyleAndSettingsConsumer.cpp
        
        )
    
target_compile_definitions(MainHostApp
PRIVATE
    NOJUCE=0
    # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
    JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_console_app` call
    _USE_MATH_DEFINES=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CURL=0)    # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_console_app` call
    
target_link_libraries(MainHostApp
    PRIVATE
        # ConsoleAppData            # If you'd created a binary data target, you'd link to it here
        juce::juce_core
        juce::juce_audio_utils
        juce::juce_dsp
        fmt
    PUBLIC
        # juce::juce_recommended_config_flags
        # juce::juce_recommended_warning_flags)
)    
endif()
# add_subdirectory(libs/fmt EXCLUDE_FROM_ALL)

# add_subdirectory(libs/rtaudio)
project(cmaketest)
# set (PYBIND11_PYTHON_VERSION "3.14")
# set (PYBIND11_FINDPYTHON ON "OLD")
add_subdirectory(libs/pybind11)

pybind11_add_module(xenakios 
    Source/PythonBindings/pybindings.cpp
    Source/PythonBindings/pybindings_ex1.cpp
    Source/PythonBindings/pybindings_ex2.cpp
    Source/PythonBindings/pybindings_ex3.cpp
    Source/PythonBindings/pybindings_ex4.cpp
    # Source/PythonBindings/pybindings_ex5.cpp
    Source/Xaps/clap_xaudioprocessor.cpp
    Source/Host/claphost.cpp
    
    libs/MTS-ESP/Master/libMTSMaster.cpp
    libs/MTS-ESP/Client/libMTSClient.cpp
    libs/rtaudio/RtAudio.cpp 
    libs/airwindows_limited/airwin_consolidated_base.cpp
    libs/airwindows_limited/plugins/BezEQ.cpp
    libs/airwindows_limited/plugins/BezEQProc.cpp
    libs/airwindows_limited/plugins/HipCrush.cpp
    libs/airwindows_limited/plugins/HipCrushProc.cpp
    
)
# Source/Experimental/xaudiograph.cpp
# target_link_libraries(xenakios PRIVATE juce::juce_core juce::juce_audio_utils)
target_compile_options(xenakios PRIVATE -Werror=return-type)
target_include_directories(xenakios PRIVATE libs/pybind11/include)
# target_link_libraries(xenakios PRIVATE airwin-registry)



# WIN32_LEAN_AND_MEAN

target_compile_definitions(xenakios PRIVATE 
    _CRT_SECURE_NO_WARNINGS
    _USE_MATH_DEFINES=1 
    XENPYTHONBINDINGS=1 
    NOJUCE=1
    JUCE_PLUGINHOST_VST3=0
    __WINDOWS_WASAPI__
    NOMINMAX 
    SST_USE_HUOV_2010
    XENPYAIRWINDOWS=0
    )


add_custom_command(TARGET xenakios POST_BUILD 
  COMMAND "${CMAKE_COMMAND}" -E copy 
     "$<TARGET_FILE:xenakios>"
     "C:/Users/teemu/AppData/Local/Python/pythoncore-3.14-64/Lib/site-packages/$<TARGET_FILE_NAME:xenakios>" 
  COMMENT "Copying to output directory")

add_custom_command(TARGET xenakios POST_BUILD 
  COMMAND C:/Users/teemu/AppData/Local/Python/pythoncore-3.14-64/Scripts/pybind11-stubgen.exe 
  "xenakios" 
  "-o" 
  "C:/Users/teemu/AppData/Local/Python/pythoncore-3.14-64/Lib/site-packages"
     
  COMMENT "Generating Python stubs")

# this is used for simple C++ testing/experimenting stuff without Juce
add_executable(TestingProgram 
    Source/Experimental/misctest.cpp 
    Source/Experimental/misctest2.cpp 
    Source/Xaps/clap_xaudioprocessor.cpp
    Source/Host/claphost.cpp
    Source/Tests/first_tests.cpp
    libs/rtaudio/RtAudio.cpp  
    libs/MTS-ESP/Client/libMTSClient.cpp
    )
# Source/Experimental/xaudiograph.cpp
target_compile_definitions(TestingProgram PRIVATE XENPYTHONBINDINGS=0 NOJUCE=1 _USE_MATH_DEFINES=1 __WINDOWS_WASAPI__)
target_compile_options(TestingProgram PRIVATE -Werror=return-type)
# target_link_libraries(TestingProgram PRIVATE airwin-registry)
# Source/Experimental/xaudiograph.cpp

add_executable(enosccli
    Source/Experimental/enosc_cli.cpp
    Source/granularsynth/js_impl.cpp
)
target_include_directories(enosccli PRIVATE libs/metamodule-core-modules/4ms/core)
# Source/Experimental/xaudiograph.cpp
# target_compile_definitions(TestingProgram PRIVATE XENPYTHONBINDINGS=0 NOJUCE=1 _USE_MATH_DEFINES=1 __WINDOWS_WASAPI__)
# target_link_libraries(TestingProgram PRIVATE airwin-registry)


# mini host that can be controlled with OSC messages etc
add_executable(ClapMiniHost 
    Source/Host/claphostserver.cpp 
    Source/Xaps/clap_xaudioprocessor.cpp
    Source/Host/claphost.cpp
    libs/rtaudio/RtAudio.cpp  
    )
target_compile_definitions(ClapMiniHost PRIVATE _WIN64 NOJUCE=1 _USE_MATH_DEFINES=1 NOMINMAX __WINDOWS_WASAPI__ WIN32_LEAN_AND_MEAN _WINSOCK_DEPRECATED_NO_WARNINGS)

add_executable(grainserver
    Source/cli/grainserver.cpp 
    libs/rtaudio/RtAudio.cpp  
    libs/rtmidi/RtMidi.cpp
    )
target_compile_definitions(grainserver PRIVATE _WIN64 NOJUCE=1 _USE_MATH_DEFINES=1 NOMINMAX __WINDOWS_WASAPI__ __WINDOWS_MM__ WIN32_LEAN_AND_MEAN _WINSOCK_DEPRECATED_NO_WARNINGS)
target_compile_options(grainserver PRIVATE -Werror=return-type)
target_link_libraries(grainserver PRIVATE winmm.lib)
# cmaj testing
add_executable(CMajorTester 
    Source/Experimental/cmajtester.cpp 
    )
target_compile_definitions(CMajorTester PRIVATE NOJUCE=1 _USE_MATH_DEFINES=1 __WINDOWS_WASAPI__)
target_link_libraries(CMajorTester PRIVATE c:/develop/cmajlang/cmajor/tools/CmajDLL/exports.def)

# te testing

add_executable(te_tester 
    Source/Experimental/te_tester.cpp 
    )
target_compile_definitions(te_tester PRIVATE NOJUCE=1 _USE_MATH_DEFINES=1 __WINDOWS_WASAPI__)
# target_link_libraries(CMajorTester PRIVATE c:/develop/cmajlang/cmajor/tools/CmajDLL/exports.def)

# juce::juce_recommended_lto_flags
# juce_add_bundle_resources_directory(AudioPluginHost2 Assets)

juce_add_plugin(ToneGranulatorPlugin
    VERSION 0.1                               # Set this if the plugin version is different to the project version
    # ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    # COMPANY_NAME ...                          # Specify the name of the plugin's author
    # IS_SYNTH TRUE/FALSE                       # Is this a synth or an effect?
    NEEDS_MIDI_INPUT TRUE               # Does the plugin need midi input?
    NEEDS_MIDI_OUTPUT FALSE              # Does the plugin need midi output?
    # IS_MIDI_EFFECT TRUE/FALSE                 # Is this plugin a MIDI effect?
    # EDITOR_WANTS_KEYBOARD_FOCUS TRUE/FALSE    # Does the editor need keyboard focus?
    COPY_PLUGIN_AFTER_BUILD TRUE        # Should the plugin be installed to a default location after building?
    PLUGIN_MANUFACTURER_CODE Xenk               # A four-character manufacturer id with at least one upper-case character
    PLUGIN_CODE 68ae                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
    FORMATS AU VST3 Standalone                  # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
    PRODUCT_NAME "ToneGranulator")        # The name of the final executable, which can differ from the target name

# juce_generate_juce_header(AudioPluginExample)

target_sources(ToneGranulatorPlugin
    PRIVATE
        Source/granularsynth/juceplugin/PluginProcessor.cpp
        Source/granularsynth/juceplugin/PluginEditor.cpp
        Source/granularsynth/js_impl.cpp
)

target_compile_options(ToneGranulatorPlugin PRIVATE -Werror=return-type "$<$<COMPILE_LANGUAGE:CXX>:-fno-char8_t>")
target_compile_definitions(ToneGranulatorPlugin
    PUBLIC
        # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
        JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
        JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_ASIO=1
        )

target_link_libraries(ToneGranulatorPlugin
    PRIVATE
        # AudioPluginData           # If we'd created a binary data target, we'd link to it here
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        # juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # target_compile_options(ToneGranulatorPlugin INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus;/Zc:char8_t->")
elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    # target_compile_options(ToneGranulatorPlugin INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-fno-char8_t>")
endif()
