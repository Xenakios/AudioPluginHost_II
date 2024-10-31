cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)


project(AudioPluginHost2 VERSION 0.0.1)

add_subdirectory(JUCE)

include_directories(
    libs/clap/include
    libs/clap-helpers/include
    Source/AudioGraph
    
    libs/signalsmith-stretch
    libs/choc
    libs/sst-basic-blocks/include
    libs/fmt/include
    libs/sst-jucegui/include
    libs/concurrentqueue
    libs/pybind11/include
    libs/sst-voicemanager/include
    libs/rtaudio
    libs/MTS-ESP/Master
    libs/tuning-library/include
    libs/sst-filters/include
    libs/sst-effects/include
    libs/oscpkt
)

# for "reasons", this is still a console app even though a GUI app is built from it
# we should have a separate real console app and a real GUI app

add_subdirectory(libs/fmt)

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

# add_subdirectory(libs/fmt EXCLUDE_FROM_ALL)

# add_subdirectory(libs/rtaudio)
project(cmaketest)
add_subdirectory(libs/pybind11)
# set (PYBIND11_PYTHON_VERSION "3.13")
pybind11_add_module(xenakios 
    Source/PythonBindings/pybindings.cpp
    Source/PythonBindings/pybindings_ex1.cpp
    Source/Xaps/clap_xaudioprocessor.cpp
    Source/Host/claphost.cpp
    
    libs/MTS-ESP/Master/libMTSMaster.cpp
    libs/rtaudio/RtAudio.cpp 
)
# Source/Experimental/xaudiograph.cpp
# target_link_libraries(xenakios PRIVATE juce::juce_core juce::juce_audio_utils)

# PYBIND11_FINDPYTHON=ON

target_compile_definitions(xenakios PRIVATE 
    _USE_MATH_DEFINES=1 
    XENPYTHONBINDINGS=1 
    NOJUCE=1
    JUCE_PLUGINHOST_VST3=1
    __WINDOWS_WASAPI__
    )
    
add_custom_command(TARGET xenakios POST_BUILD 
  COMMAND "${CMAKE_COMMAND}" -E copy 
     "$<TARGET_FILE:xenakios>"
     "C:/Users/teemu/AppData/Local/Programs/Python/Python313/Lib/site-packages/$<TARGET_FILE_NAME:xenakios>" 
  COMMENT "Copying to output directory")

add_custom_command(TARGET xenakios POST_BUILD 
  COMMAND pybind11-stubgen 
  "xenakios" 
  "-o" 
  "C:/Users/teemu/AppData/Local/Programs/Python/Python313/Lib/site-packages"
     
  COMMENT "Generating Python stubs")

# this is used for simple C++ testing/experimenting stuff without Juce
add_executable(TestingProgram 
    Source/Experimental/misctest.cpp 
    Source/Xaps/clap_xaudioprocessor.cpp
    Source/Host/claphost.cpp
    
    libs/rtaudio/RtAudio.cpp  
    )
# Source/Experimental/xaudiograph.cpp
target_compile_definitions(TestingProgram PRIVATE NOJUCE=1 _USE_MATH_DEFINES=1 __WINDOWS_WASAPI__)

# mini host that can be controlled with OSC messages etc
add_executable(ClapMiniHost 
    Source/Host/claphostserver.cpp 
    Source/Xaps/clap_xaudioprocessor.cpp
    Source/Host/claphost.cpp
    libs/rtaudio/RtAudio.cpp  
    )
target_compile_definitions(ClapMiniHost PRIVATE NOJUCE=1 _USE_MATH_DEFINES=1 __WINDOWS_WASAPI__)


# juce::juce_recommended_lto_flags
# juce_add_bundle_resources_directory(AudioPluginHost2 Assets)
