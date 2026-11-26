cmake_minimum_required(VERSION 3.22)

# Versión mínima de macOS (High Sierra). Tiene que ir ANTES de project() para que se aplique.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Versión mínima de macOS")

project(Excellecu VERSION 0.30.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

# Versión visible en la ventana (también en la prueba de humo, que no es un plugin)
add_compile_definitions(EX_VERSION_STRING="${PROJECT_VERSION}")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: el runtime de C++ va dentro del plugin (no hace falta instalar Visual C++ Redistributable)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE se descarga automáticamente al configurar.
# Para usar una copia local: -DFETCHCONTENT_SOURCE_DIR_JUCE=/ruta/a/JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(EX_FORMATS VST3)
if(APPLE)
    list(APPEND EX_FORMATS AU)
endif()

juce_add_plugin(Excellecu
    PRODUCT_NAME "Excellecu"
    COMPANY_NAME "Javhastudios"
    BUNDLE_ID "com.javhastudios.excellecu"
    PLUGIN_MANUFACTURER_CODE Javh
    PLUGIN_CODE Exeq
    FORMATS ${EX_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx EQ
    AU_MAIN_TYPE kAudioUnitType_Effect)

set(EX_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/Theme.cpp
    Source/ui/Views.cpp
    Source/ui/Meters.cpp
    Source/ui/PresetBar.cpp)

target_sources(Excellecu PRIVATE ${EX_SOURCES})
target_include_directories(Excellecu PRIVATE Source)

if(MSVC)
    target_compile_options(Excellecu PRIVATE /utf-8)
else()
    # Las comparaciones exactas de ajustes (como en la web) son intencionadas
    target_compile_options(Excellecu PRIVATE -Wno-float-equal)
endif()

target_compile_definitions(Excellecu PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# macOS: FFT de Accelerate en el motor (ver EqEngine.h)
if(APPLE)
    target_sources(Excellecu PRIVATE Source/dsp/FftAccelerate.cpp)
    target_compile_definitions(Excellecu PRIVATE EX_USE_ACCELERATE=1)
    target_link_libraries(Excellecu PRIVATE "-framework Accelerate")
endif()

target_link_libraries(Excellecu
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Comprobación del motor contra la web (no necesita JUCE)
add_executable(compare_cpp EXCLUDE_FROM_ALL tests/compare_cpp.cpp)
target_include_directories(compare_cpp PRIVATE Source)

# Prueba de humo del plugin completo (no se compila por defecto):
#   cmake --build build --target ex_smoke && ./build/ex_smoke_artefacts/Release/ex_smoke salida/
juce_add_console_app(ex_smoke PRODUCT_NAME "ex_smoke")
set_target_properties(ex_smoke PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(ex_smoke PRIVATE tests/smoke.cpp ${EX_SOURCES})
target_include_directories(ex_smoke PRIVATE Source)
target_compile_definitions(ex_smoke PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ex_smoke PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
