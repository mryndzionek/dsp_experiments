cmake_minimum_required(VERSION 3.16.3)

project(sdr_apps)

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")

include_directories(include local/include
                    models
                    dependencies/dlg/include
                    dependencies/tflite_build
                    dependencies/tensorflow_src
                    dependencies/tflite_build/flatbuffers/include/)
link_directories(local/lib)

set(SRCS src/link.c src/logging.c src/util.c dependencies/dlg/src/dlg/dlg.c)
set(LIBS m dl pthread SoapySDR libdill.a liquid libwebsockets.a rtaudio)

add_compile_options(-Wall -fPIC)
add_compile_definitions(DLG_LOG_LEVEL=dlg_level_info)

add_executable(wbfm_demod wbfm_demod/main.c
                          src/soapy_source.c
                          src/resampler.c
                          src/wbfm_demod.c
                          src/fms_demod.c
                          src/audio_sink.c
                          ${SRCS})
target_link_libraries(wbfm_demod ${LIBS})

add_executable(flex_tx flex_tx/main.c
                       src/flex_encoder.c
                       src/audio_sink.c
                       ${SRCS})
target_link_libraries(flex_tx ${LIBS})

add_executable(flex_rx flex_rx/main.c
                       src/audio_source.c
                       src/flex_decoder.c
                       ${SRCS})
target_link_libraries(flex_rx ${LIBS})

add_executable(wav2mel wav2mel/main.c
                       src/mel_spectrum.c
                       src/tflite_runner.cc
                       src/logging.c
                       dependencies/dlg/src/dlg/dlg.c)
target_link_libraries(wav2mel m dl pthread sndfile png
                              libliquid.a libruy.a libXNNPACK.a libcpuinfo.a
                              libpthreadpool.a libflatbuffers.a libfft2d_fftsg.a
                              libfft2d_fftsg2d.a libclog.a libfarmhash.a libtensorflow-lite.a dl)

add_executable(keywords keywords/main.c
                        src/mel_spectrum.c
                        src/audio_source.c
                        src/tflite_runner.cc
                        ${SRCS})
target_link_libraries(keywords m dl pthread pulse-simple pulse libliquid.a
                               libwebsockets.a libdill.a libruy.a libXNNPACK.a libcpuinfo.a
                               libpthreadpool.a libflatbuffers.a libfft2d_fftsg.a libfft2d_fftsg2d.a
                               libclog.a libfarmhash.a libtensorflow-lite.a librtaudio.a dl)

add_executable(lpc_decoder lpc_decoder/main.c lpc_decoder/lpc.c lpc_decoder/lpc_data.c)
target_link_libraries(lpc_decoder ${LIBS})

