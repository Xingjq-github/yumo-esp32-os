idf_component_register(SRCS "photo_gif.c" "giflib/dgif_lib.c" "giflib/gifalloc.c"
    "giflib/gif_err.c" "giflib/openbsd-reallocarray.c"
    INCLUDE_DIRS "." PRIV_INCLUDE_DIRS "giflib" PRIV_REQUIRES heap)
