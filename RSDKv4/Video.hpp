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

void PlayVideoFile(char *filePath);
void UpdateVideoFrame();
int ProcessVideo();
void StopVideoPlayback();
void SetupVideoBuffer(int width, int height);
void CloseVideoBuffer();

#endif // VIDEO_HPP
