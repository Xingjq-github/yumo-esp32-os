idf_component_register(
    SRCS "recorder_denoise.c" "upstream/libspeexdsp/preprocess.c"
         "upstream/libspeexdsp/filterbank.c" "upstream/libspeexdsp/fftwrap.c"
         "upstream/libspeexdsp/smallft.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private" "upstream/include" "upstream/libspeexdsp"
    PRIV_REQUIRES heap)
target_compile_definitions(${COMPONENT_LIB} PRIVATE FLOATING_POINT USE_SMALLFT
    OS_SUPPORT_CUSTOM RECORDER_NO_ECHO EXPORT=)
# Scalar FFT needs no target assembly, trained model, codec or resampler.
