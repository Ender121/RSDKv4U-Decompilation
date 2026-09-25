#ifndef VIDEO_HPP
#define VIDEO_HPP

extern int currentVideoFrame;
extern int videoFrameCount;
extern int videoWidth;
extern int videoHeight;
extern float videoAR;

extern THEORAPLAY_Decoder *videoDecoder;
extern const THEORAPLAY_VideoFrame *videoVidData;
extern THEORAPLAY_Io callbacks;

extern byte videoData;
extern int videoFilePos;
extern bool videoPlaying;
extern int vidFrameMS;
extern int vidBaseticks;

extern bool videoSkipped;
extern int videoFadeIn;
extern int videoFadeOut;

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
