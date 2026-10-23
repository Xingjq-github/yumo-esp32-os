idf_component_register(SRCS "decoder.c" INCLUDE_DIRS "include" PRIV_REQUIRES heap)
target_compile_definitions(${COMPONENT_LIB} PUBLIC MINIMP3_NO_STDIO MINIMP3_ONLY_MP3 MINIMP3_NO_SIMD)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
