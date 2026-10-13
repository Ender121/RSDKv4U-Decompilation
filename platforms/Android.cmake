add_library(RetroEngine SHARED ${RETRO_FILES})

set(DEP_PATH android)
set(RETRO_SDL_VERSION 2)
set(RETRO_USE_HW_RENDER ON)
set(RETRO_OUTPUT_NAME "RetroEngine")

set(COMPILE_OGG TRUE)
set(COMPILE_VORBIS TRUE)
set(COMPILE_THEORA TRUE)

set(OGG_FLAGS -ffast-math -fsigned-char -O2 -fPIC -DPIC -DBYTE_ORDER=LITTLE_ENDIAN -D_ARM_ASSEM_ -w)
set(VORBIS_FLAGS -ffast-math -fsigned-char -O2 -fPIC -DPIC -DBYTE_ORDER=LITTLE_ENDIAN -D_ARM_ASSEM_ -w)
# libtheora is the video decoder (IDCT, loop filter, motion compensation) and by far the heaviest part of playing a video. It used to be
# built without these flags, so a Debug APK (the CI builds `assembleDebug`) compiled it at -O0, several times slower than RSDKv3, which
# forces -O2 on it like ogg/vorbis.
set(THEORA_FLAGS -ffast-math -fsigned-char -O2 -fPIC -DPIC -DBYTE_ORDER=LITTLE_ENDIAN -D_ARM_ASSEM_ -w)

# theoraplay.c does the YUV -> RGBA conversion of every video frame. Same reason: keep it optimised even in a Debug build. (A source-file
# property, so it covers both copies of it -- the one built into RetroEngine and the `theoraplay` static library.)
set_source_files_properties(dependencies/all/theoraplay/theoraplay.c PROPERTIES COMPILE_FLAGS "-O2 -ffast-math")

target_link_libraries(RetroEngine
    android
    EGL
    GLESv1_CM
    log
    z
    jnigraphics
    OpenSLES
    SDL2
)

target_link_options(RetroEngine PRIVATE -u GameActivity_onCreate)