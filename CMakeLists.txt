cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# Transcrição ao Vivo - plugin VST3 / AU / Standalone
# JUCE (interface + formatos de plugin) + whisper.cpp (Whisper e VAD Silero)
# ---------------------------------------------------------------------------
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSAO" TAV_VERSAO LIMIT_COUNT 1)
string(STRIP "${TAV_VERSAO}" TAV_VERSAO)
project(TranscricaoAoVivo VERSION ${TAV_VERSAO} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    # Para distribuir, compile UMA arquitetura por vez (-DTAV_OSX_ARCH=arm64 e
    # depois x86_64) e junte com lipo (o CI faz isso). Motivo: o ggml só liga
    # NEON/AVX2 quando CMAKE_OSX_ARCHITECTURES tem exatamente uma arquitetura;
    # com "arm64;x86_64" ele cai no modo genérico, várias vezes mais lento.
    if(NOT DEFINED TAV_OSX_ARCH)
        set(TAV_OSX_ARCH "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
    set(CMAKE_OSX_ARCHITECTURES "${TAV_OSX_ARCH}" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

# Windows: runtime do Visual C++ embutido (/MT). Sem isso o plugin exige o
# "Visual C++ Redistributable" instalado, e num PC sem ele o host simplesmente
# não lista o plugin.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

include(FetchContent)

# ---- JUCE ------------------------------------------------------------------
if(DEFINED TAV_JUCE_DIR AND EXISTS "${TAV_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${TAV_JUCE_DIR}" juce)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- whisper.cpp -----------------------------------------------------------
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE            OFF CACHE BOOL "" FORCE)   # binário portátil

if(APPLE)
    set(GGML_METAL               ON  CACHE BOOL "" FORCE)   # GPU (Apple Silicon e Macs com GPU Metal)
    set(GGML_METAL_EMBED_LIBRARY ON  CACHE BOOL "" FORCE)
    # Accelerate/BLAS desligados: o backend BLAS compilado com SDK novo usa
    # símbolos do Accelerate que não existem no macOS 11/12 e derruba o host.
    set(GGML_ACCELERATE OFF CACHE BOOL "" FORCE)
    set(GGML_BLAS       OFF CACHE BOOL "" FORCE)
endif()

# Em x86 o ggml usa AVX2/FMA (todo PC de 2013 em diante). O plugin confere a
# CPU antes de carregar o modelo, para não derrubar o host numa CPU antiga.
set(TAV_CPU_DEFS "")
set(TAV_X86 OFF)
if(APPLE)
    if(TAV_OSX_ARCH STREQUAL "x86_64")
        set(TAV_X86 ON)
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(TAV_X86 ON)
endif()
if(TAV_X86)
    # explícito: o ggml só liga isso sozinho quando não acha que é compilação
    # cruzada (ex.: fatia x86_64 compilada num Mac arm64)
    foreach(f GGML_SSE42 GGML_AVX GGML_AVX2 GGML_FMA GGML_F16C GGML_BMI2)
        set(${f} ON CACHE BOOL "" FORCE)
    endforeach()
    set(TAV_CPU_DEFS TAV_NEEDS_AVX2=1)
endif()

if(DEFINED TAV_WHISPER_DIR AND EXISTS "${TAV_WHISPER_DIR}/CMakeLists.txt")
    add_subdirectory("${TAV_WHISPER_DIR}" whisper)
    set(TAV_WHISPER_SRC "${TAV_WHISPER_DIR}")
else()
    FetchContent_Declare(whisper
        GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
        GIT_TAG        v1.9.5
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(whisper)
    set(TAV_WHISPER_SRC "${whisper_SOURCE_DIR}")
endif()

# O detector de voz Silero (MIT, 0,9 MB) vai embutido no binário: ninguém
# precisa baixar nada além do modelo Whisper.
set(TAV_VAD_SRC "${TAV_WHISPER_SRC}/models/for-tests-silero-v6.2.0-ggml.bin")
if(NOT EXISTS "${TAV_VAD_SRC}")
    message(FATAL_ERROR "Modelo VAD não encontrado em ${TAV_VAD_SRC}")
endif()
configure_file("${TAV_VAD_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/res/silero_vad.bin" COPYONLY)
juce_add_binary_data(TavData SOURCES "${CMAKE_CURRENT_BINARY_DIR}/res/silero_vad.bin")

# ---- identidade ------------------------------------------------------------
set(TAV_COMPANY     "Davi Sales")
set(TAV_BUNDLE_BASE "com.davisales.transcricaoaovivo")

set(TAV_ENGINE_SOURCES
    Source/Engine/TextFilter.cpp
    Source/Engine/Segmenter.cpp
    Source/Engine/WhisperBackend.cpp
    Source/Engine/Transcriber.cpp
    Source/Engine/Models.cpp
    Source/Common/TranscriptLog.cpp)

set(TAV_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TAV_VERSAO="${TAV_VERSAO}"
    ${TAV_CPU_DEFS})

# No Linux o download de modelos usa libcurl (no macOS/Windows usa a API do sistema)
set(TAV_NEEDS_CURL FALSE)
if(UNIX AND NOT APPLE)
    find_package(CURL QUIET)
    if(CURL_FOUND)
        set(TAV_NEEDS_CURL TRUE)
    endif()
endif()

option(TAV_COPY_PLUGIN "Copia o plugin para a pasta de plugins do sistema após compilar" OFF)

set(TAV_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TAV_FORMATS AU)
endif()

juce_add_plugin(TranscricaoAoVivo
    COMPANY_NAME                "${TAV_COMPANY}"
    BUNDLE_ID                   "${TAV_BUNDLE_BASE}.plugin"
    PLUGIN_MANUFACTURER_CODE    DvSl
    PLUGIN_CODE                 TrAV
    FORMATS                     ${TAV_FORMATS}
    PRODUCT_NAME                "Transcricao ao Vivo"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_CURL                  ${TAV_NEEDS_CURL}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "O Transcrição ao Vivo usa o microfone para transcrever a fala."
    COPY_PLUGIN_AFTER_BUILD     ${TAV_COPY_PLUGIN}
    VST3_CATEGORIES             Analyzer Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(TranscricaoAoVivo PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/TranscriptView.cpp
    Source/UI/PopoutWindow.cpp
    ${TAV_ENGINE_SOURCES})

target_include_directories(TranscricaoAoVivo PRIVATE Source)
target_compile_definitions(TranscricaoAoVivo PUBLIC ${TAV_DEFS})
target_link_libraries(TranscricaoAoVivo
    PRIVATE
        TavData
        whisper
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
if(NOT MSVC)
    target_link_libraries(TranscricaoAoVivo PUBLIC juce::juce_recommended_lto_flags)
endif()

# ---- testes (console) --------------------------------------------------------
option(TAV_BUILD_TESTS "Compila os testes de console" OFF)
if(TAV_BUILD_TESTS)
    juce_add_console_app(TavTests PRODUCT_NAME "TavTests")
    target_sources(TavTests PRIVATE tests/TavTests.cpp ${TAV_ENGINE_SOURCES})
    target_include_directories(TavTests PRIVATE Source)
    target_compile_definitions(TavTests PRIVATE ${TAV_DEFS})
    target_link_libraries(TavTests PRIVATE TavData whisper
        juce::juce_audio_formats juce::juce_events
        PUBLIC juce::juce_recommended_config_flags)

    # host de teste: carrega o VST3 compilado como um DAW
    juce_add_console_app(HostTest PRODUCT_NAME "HostTest")
    target_sources(HostTest PRIVATE tests/HostTest.cpp)
    target_compile_definitions(HostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(HostTest PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
        PUBLIC juce::juce_recommended_config_flags)
endif()
