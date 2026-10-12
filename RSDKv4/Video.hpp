#ifndef VIDEO_HPP
#define VIDEO_HPP

// Only the symbols other translation units actually use are exported. Everything that used to be a global but was only ever touched by
// Video.cpp (currentVideoFrame, videoFrameCount, videoAR, videoVidData, callbacks, videoData, videoFilePos, vidFrameMS, vidBaseticks,
// videoSkipped, videoTouchHeld) is now `static` in Video.cpp -- fewer exported symbols, and no more generic names like `callbacks`
// leaking into every file that includes this header.

extern int videoWidth;  // read by Drawing.cpp (SDL2 renderer path)
extern int videoHeight; // read by Drawing.cpp (SDL2 renderer path)

extern THEORAPLAY_Decoder *videoDecoder; // read by the audio thread (Audio.cpp)
extern bool videoPlaying;                // read by the audio thread (Audio.cpp)

extern int videoFadeIn;           // read by Drawing.cpp
extern int videoFadeOut;          // read by Drawing.cpp
extern int videoPrevRefreshRate;  // read by RetroGameLoop.cpp

void PlayVideoFile(char *filePath);
void UpdateVideoFrame();
int ProcessVideo();
void StopVideoPlayback();
void SetupVideoBuffer(int width, int height);
void CloseVideoBuffer();
#if RETRO_USING_OPENGL || RETRO_USING_SDL1
void DrawVideoFrame();
#endif
#if RETRO_USING_OPENGL
void CreateVideoTexture(int width, int height);
void DrawVideoFrameGL();
#endif

#endif // VIDEO_HPP
