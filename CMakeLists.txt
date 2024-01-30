cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)


project(AudioPluginHost2 VERSION 0.0.1)

add_subdirectory(JUCE)

include_directories(
    libs/clap/include
    libs/clap-helpers/include
    Source/AudioGraph
    Source/Plugins/noise-plethora/rack/include
    libs/signalsmith-stretch
    libs/choc
    libs/sst-basic-blocks/include
    libs/fmt/include
    libs/sst-jucegui/include
    libs/concurrentqueue
    libs/WDL
    libs/pybind11/include
    C:/Users/teemu/AppData/Local/Programs/Python/Python312/include
)

# for "reasons", this is still a console app even though a GUI app is built from it
# we should have a separate real console app and a real GUI app
juce_add_console_app(MainHostApp
    PRODUCT_NAME "Console App Example") 
juce_generate_juce_header(MainHostApp)
target_sources(MainHostApp
    PRIVATE
        Source/Experimental/main.cpp
        Source/Experimental/xapfactory.cpp
        Source/Experimental/xaudiograph.cpp
        Source/Experimental/xap_utils.cpp
        Source/Experimental/xaps/xap_notegenerator.cpp
        Source/Experimental/xaps/fileplayer_xaudioprocessor.cpp
        Source/Experimental/xaps/clap_xaudioprocessor.cpp
        Source/Experimental/xaudioprocessor.cpp
        Source/Experimental/wdl_resampler_adapter.cpp
        
        libs/sst-jucegui/src/sst/jucegui/components/Knob.cpp
        libs/sst-jucegui/src/sst/jucegui/components/HSlider.cpp
        libs/sst-jucegui/src/sst/jucegui/components/ContinuousParamEditor.cpp
        libs/sst-jucegui/src/sst/jucegui/components/ComponentBase.cpp
        libs/sst-jucegui/src/sst/jucegui/style/StyleSheet.cpp
        libs/sst-jucegui/src/sst/jucegui/style/StyleAndSettingsConsumer.cpp
        libs/WDL/WDL/resample.cpp
        )
    
target_compile_definitions(MainHostApp
PRIVATE
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
add_subdirectory(libs/fmt)

project(cmaketest)
add_subdirectory(libs/pybind11)
pybind11_add_module(xenakios 
    Source/Experimental/pybindtest.cpp
    Source/Experimental/xaps/clap_xaudioprocessor.cpp
)
target_link_libraries(xenakios PRIVATE noiseplethora)
target_compile_definitions(xenakios PRIVATE _USE_MATH_DEFINES=1)
add_custom_command(TARGET xenakios POST_BUILD 
  COMMAND "${CMAKE_COMMAND}" -E copy 
     "$<TARGET_FILE:xenakios>"
     "C:/Users/teemu/AppData/Local/Programs/Python/Python312/Lib/site-packages/$<TARGET_FILE_NAME:xenakios>" 
  COMMENT "Copying to output directory")


add_library(noiseplethora
        Source/Plugins/noise-plethora/random.cpp
        Source/Plugins/noise-plethora/teensy/effect_freeverb.cpp
        Source/Plugins/noise-plethora/teensy/effect_bitcrusher.cpp
        Source/Plugins/noise-plethora/teensy/effect_granular.cpp
        Source/Plugins/noise-plethora/teensy/effect_combine.cpp
        Source/Plugins/noise-plethora/teensy/effect_multiply.cpp
        Source/Plugins/noise-plethora/teensy/effect_flange.cpp
        Source/Plugins/noise-plethora/teensy/effect_wavefolder.cpp
        Source/Plugins/noise-plethora/teensy/filter_variable.cpp
        Source/Plugins/noise-plethora/teensy/synth_pwm.cpp
        Source/Plugins/noise-plethora/teensy/synth_pinknoise.cpp
        Source/Plugins/noise-plethora/teensy/synth_whitenoise.cpp
        Source/Plugins/noise-plethora/plugins/Banks.cpp
)
target_compile_definitions(noiseplethora PRIVATE _USE_MATH_DEFINES=1)

# this is used for simple C++ testing/experimenting stuff without Juce
add_executable(TestingProgram 
    Source/Experimental/misctest.cpp 
    Source/Experimental/xaps/clap_xaudioprocessor.cpp
    )
target_sources(TestingProgram
    PRIVATE
    
)
target_compile_definitions(TestingProgram PRIVATE _USE_MATH_DEFINES=1)
target_link_libraries(TestingProgram PRIVATE noiseplethora)

# juce::juce_recommended_lto_flags
# juce_add_bundle_resources_directory(AudioPluginHost2 Assets)
