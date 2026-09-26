#include "RetroEngine.hpp"

int currentVideoFrame = 0;
int videoFrameCount   = 0;
int videoWidth        = 0;
int videoHeight       = 0;
float videoAR         = 0;

THEORAPLAY_Decoder *videoDecoder;
const THEORAPLAY_VideoFrame *videoVidData;
THEORAPLAY_Io callbacks;

byte videoData    = 0;
int videoFilePos  = 0;
bool videoPlaying = 0;
int videoTouchReleaseFrames = 0; // consecutive frames with no touch seen since the video started; skip only arms once this
                                  // reaches VIDEO_TOUCH_RELEASE_FRAMES, so a touch still held from whatever led into the video
                                  // (or a single noisy frame where a release gets reported late) can never be mistaken for a tap
int vidFrameMS    = 0;
int vidBaseticks  = 0;

bool videoSkipped = false;
// Dedicated to the video only -- kept separate from the engine's shared screen-fade globals (fadeMode/fadeR/G/B/A, set by the
// script-facing SetScreenFade) so a script fading the screen right before/after a video (as Start.txt does) can never interact
// with the video's own fade in/out
int videoFadeIn  = 0; // 0..255, counts DOWN from 255 to 0 over the first ~32 frames: the video brightens in
int videoFadeOut = 0; // 0..255, counts UP from 0 once a skip/natural end is detected: the video darkens out

static long videoRead(THEORAPLAY_Io *io, void *buf, long buflen)
{
    FileIO *file    = (FileIO *)io->userdata;
    const size_t br = fRead(buf, 1, buflen * sizeof(byte), file);
    if (br == 0)
        return -1;
    return (int)br;
} // IoFopenRead

static void videoClose(THEORAPLAY_Io *io)
{
    FileIO *file = (FileIO *)io->userdata;
    fClose(file);
}

void PlayVideoFile(char *filePath)
{
    char filepath[0x200];
#if RETRO_PLATFORM == RETRO_OSX || RETRO_PLATFORM == RETRO_ANDROID
    // On these platforms the game files live in gamePath (like Data.rsdk), not relative to the working directory. gamePath's trailing
    // slash is inconsistent between platforms (Android's Java-side getBasePath() includes one, OSX's does not), so strip it if present
    // before adding our own -- a doubled slash breaks fcaseopen's case-insensitive directory walk on Android/Linux.
    int gamePathLen = StrLength(gamePath);
    if (gamePathLen > 0 && gamePath[gamePathLen - 1] == '/')
        gamePathLen--;
    memcpy(filepath, gamePath, gamePathLen);
    sprintf(filepath + gamePathLen, "/videos/");
#else
    StrCopy(filepath, BASE_PATH "videos/");
#endif

    int len = StrLength(filePath);

    if (StrComp(filePath + ((size_t)len - 2), "us")) {
        filePath[len - 2] = 0;
    }

    StrAdd(filepath, filePath);
    StrAdd(filepath, ".ogv");

    FileIO *file = fOpen(filepath, "rb");
    if (file) {
        PrintLog("Loaded File '%s'!", filepath);

        callbacks.read     = videoRead;
        callbacks.close    = videoClose;
        callbacks.userdata = (void *)file;
        // The SDL2 renderer path uploads planar YUV straight into a YV12 texture. Every other path (SDL1, and SDL2 + OpenGL) copies the
        // decoded frame into a 32-bit RGBA surface, so the decoder must output RGBA there or the memcpy below reads past the YUV buffer.
#if RETRO_USING_SDL2 && !RETRO_USING_OPENGL
        videoDecoder = THEORAPLAY_startDecode(&callbacks, /*FPS*/ 30, THEORAPLAY_VIDFMT_IYUV, GetGlobalVariableByName("Options.Soundtrack") ? 1 : 0);
#else
        videoDecoder = THEORAPLAY_startDecode(&callbacks, /*FPS*/ 30, THEORAPLAY_VIDFMT_RGBA, GetGlobalVariableByName("Options.Soundtrack") ? 1 : 0);
#endif


        if (!videoDecoder) {
            PrintLog("Video Decoder Error!");
            return;
        }
        while (!videoVidData) {
            if (!videoVidData)
                videoVidData = THEORAPLAY_getVideo(videoDecoder);
        }
        if (!videoVidData) {
            PrintLog("Video Error!");
            return;
        }

        videoWidth  = videoVidData->width;
        videoHeight = videoVidData->height;
        // commit video Aspect Ratio.
        videoAR = float(videoWidth) / float(videoHeight);

        SetupVideoBuffer(videoWidth, videoHeight);
        vidBaseticks = SDL_GetTicks();
        vidFrameMS   = (videoVidData->fps == 0.0) ? 0 : ((Uint32)(1000.0 / videoVidData->fps));
        videoPlaying = true;
        trackID      = TRACK_COUNT - 1;

        videoTouchReleaseFrames = 0;

        videoSkipped    = false;
        videoFadeIn     = 0xFF;
        videoFadeOut    = 0;
        Engine.gameMode = ENGINE_VIDEOWAIT;
    }
    else {
        PrintLog("Couldn't find file '%s'!", filepath);
    }
}

void UpdateVideoFrame()
{
    if (videoPlaying) {
        if (videoFrameCount > currentVideoFrame) {
            GFXSurface *surface = &gfxSurface[videoData];
            byte fileBuffer      = 0;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer << 8;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer << 16;
            FileRead(&fileBuffer, 1);
            videoFilePos += fileBuffer << 24;

            byte clr[3];
            for (int i = 0; i < 0x80; ++i) {
                FileRead(&clr, 3);
                activePalette32[i].r = clr[0];
                activePalette32[i].g = clr[1];
                activePalette32[i].b = clr[2];
                activePalette[i]     = ((ushort)(clr[0] >> 3) << 11) | 32 * (clr[1] >> 2) | (clr[2] >> 3);
            }

            FileRead(&fileBuffer, 1);
            while (fileBuffer != ',') FileRead(&fileBuffer, 1); // gif image start identifier

            FileRead(&fileBuffer, 2); // IMAGE LEFT
            FileRead(&fileBuffer, 2); // IMAGE TOP
            FileRead(&fileBuffer, 2); // IMAGE WIDTH
            FileRead(&fileBuffer, 2); // IMAGE HEIGHT
            FileRead(&fileBuffer, 1); // PaletteType
            bool interlaced = (fileBuffer & 0x40) >> 6;
            if (fileBuffer >> 7 == 1) {
                int c = 0x80;
                do {
                    ++c;
                    FileRead(&fileBuffer, 3);
                } while (c != 0x100);
            }
            ReadGifPictureData(surface->width, surface->height, interlaced, graphicData, surface->dataPosition);

            SetFilePosition(videoFilePos);
            ++currentVideoFrame;
        }
        else {
            videoPlaying = 0;
            CloseFile();
        }
    }
}

int ProcessVideo()
{
    if (videoPlaying) {
        CheckKeyPress(&keyPress);

        // A tap/click anywhere on the screen skips the video too, not just pressing A -- this is the RSDKv4 Plus (Origins) behaviour,
        // rather than RSDKv4-V's, which only responds to the A button. Only a FRESH tap counts (touchDown was false last frame, true this
        // frame) -- otherwise a touch still held from whatever screen led into this video (e.g. the tap that started the game) would
        // read as a skip the instant the video starts, before the player ever meant to skip anything
        bool touched = false;
        for (int t = 0; t < 8 && !touched; ++t)
            touched = touchDown[t] != 0;

        // ~3 frames (50ms @ 60fps): long enough to absorb a touch-release that gets reported a frame late, short enough nobody notices
        // the wait once they actually do lift their finger
        const int VIDEO_TOUCH_RELEASE_FRAMES = 3;

        bool touchSkip = false;
        if (touched) {
            if (videoTouchReleaseFrames >= VIDEO_TOUCH_RELEASE_FRAMES)
                touchSkip = true; // a real release was already confirmed -- this is a fresh tap
        }
        else if (videoTouchReleaseFrames < VIDEO_TOUCH_RELEASE_FRAMES) {
            videoTouchReleaseFrames++;
        }

        const bool skipRequested = keyPress.A || touchSkip;
        const bool videoDone     = !THEORAPLAY_isDecoding(videoDecoder);

        // Runs every frame from the start, independent of skipping: the video opens on black and brightens up over its first ~32 frames
        if (videoFadeIn > 0)
            videoFadeIn = (videoFadeIn < 8) ? 0 : (videoFadeIn - 8);

        // Start the fade-out the first time either the player skips or the video plays out on its own -- a natural end fades out exactly
        // like a skip does, instead of cutting straight to black the instant decoding finishes
        if (skipRequested || videoDone)
            videoSkipped = true;

        if (videoSkipped && videoFadeOut < 0xFF) {
            videoFadeOut += 8;
        }

        if (videoSkipped && videoFadeOut >= 0xFF) {
            StopVideoPlayback();

            return 1; // video finished (either played out or was skipped, faded to black either way)
        }

        // Don't pause or it'll go wild
        if (videoPlaying) {
            // Once we've decided to fade out -- whether the video reached its real last frame or the player skipped early -- stop pulling
            // new frames from the decoder entirely. Engine.videoBuffer is left holding whatever was last copied into it, so the fade plays
            // out over that held frame instead of the video continuing to advance underneath it
            if (!videoSkipped) {
                const Uint32 now = (SDL_GetTicks() - vidBaseticks);

                if (!videoVidData)
                    videoVidData = THEORAPLAY_getVideo(videoDecoder);

                // Play video frames when it's time.
                if (videoVidData && (videoVidData->playms <= now)) {
                    if (vidFrameMS && ((now - videoVidData->playms) >= vidFrameMS)) {

                        // Skip frames to catch up, but keep track of the last one+
                        //  in case we catch up to a series of dupe frames, which
                        //  means we'd have to draw that final frame and then wait for
                        //  more.

                        const THEORAPLAY_VideoFrame *last = videoVidData;
                        while ((videoVidData = THEORAPLAY_getVideo(videoDecoder)) != NULL) {
                            THEORAPLAY_freeVideo(last);
                            last = videoVidData;
                            if ((now - videoVidData->playms) < vidFrameMS)
                                break;
                        }

                        if (!videoVidData)
                            videoVidData = last;
                    }

                    // do nothing; we're far behind and out of options.
                    if (!videoVidData) {
                        // video lagging uh oh
                    }

                    int half_w     = videoVidData->width / 2;
                    const Uint8 *y = (const Uint8 *)videoVidData->pixels;
                    const Uint8 *u = y + (videoVidData->width * videoVidData->height);
                    const Uint8 *v = u + (half_w * (videoVidData->height / 2));

    #if RETRO_USING_SDL2 && !RETRO_USING_OPENGL
        SDL_UpdateYUVTexture(Engine.videoBuffer, NULL, y, videoVidData->width, u, half_w, v, half_w);
    #endif
    #if RETRO_USING_SDL1 || (RETRO_USING_SDL2 && RETRO_USING_OPENGL)
        uint *videoFrameBuffer = (uint *)Engine.videoBuffer->pixels;
        memcpy(videoFrameBuffer, videoVidData->pixels, videoVidData->width * videoVidData->height * sizeof(uint));
    #endif

                    THEORAPLAY_freeVideo(videoVidData);
                    videoVidData = NULL;
                }
            }

            return 2; // its playing as expected
        }
    }

    return 0; // its not even initialised
}

void StopVideoPlayback()
{
    if (videoPlaying) {
        // `videoPlaying` and `videoDecoder` are read by
        // the audio thread, so lock it to prevent a race
        // condition that results in invalid memory accesses.
        SDL_LockAudio();

        if (videoVidData) {
            THEORAPLAY_freeVideo(videoVidData);
            videoVidData = NULL;
        }
        if (videoDecoder) {
            THEORAPLAY_stopDecode(videoDecoder);
            videoDecoder = NULL;
        }

        CloseVideoBuffer();
        videoPlaying = false;

        SDL_UnlockAudio();
    }
}

#if RETRO_USING_OPENGL
// Video gets one dedicated, permanently-reserved texture slot (the very last one -- nothing else in the engine claims a slot by counting
// down from the end, so this can never collide with a sprite sheet a script loads) sized to the video's OWN resolution, completely
// separate from the small textureList[0] "RetroBuffer" the rest of the game draws into. That's what lets the video display at its real
// resolution instead of being downsampled into the game's internal (e.g. 424x240) screen buffer first.
#define VIDEO_TEXTURE_SLOT (TEXTURE_COUNT - 1)

void CreateVideoTexture(int width, int height)
{
    TextureInfo *texture = &textureList[VIDEO_TEXTURE_SLOT];

    if (texture->id)
        glDeleteTextures(1, &texture->id);

    StrCopy(texture->fileName, "__RSDKv4U_VideoTexture");
    texture->width   = width;
    texture->height  = height;
    texture->format  = TEXFMT_RGBA8888;
    texture->widthN  = 1.0f / width;
    texture->heightN = 1.0f / height;

    glGenTextures(1, &texture->id);
    glBindTexture(GL_TEXTURE_2D, texture->id);
    // nullptr: just reserves the storage. The pixels are streamed in every frame afterwards via glTexSubImage2D (see DrawVideoFrameGL)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}
#endif

void SetupVideoBuffer(int width, int height)
{
#if RETRO_USING_SDL1 || (RETRO_USING_SDL2 && RETRO_USING_OPENGL)
    Engine.videoBuffer = SDL_CreateRGBSurface(0, width, height, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
#endif
#if RETRO_USING_SDL2 && !RETRO_USING_OPENGL
    Engine.videoBuffer = SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, width, height);
#endif

    if (!Engine.videoBuffer)
        PrintLog("Failed to create video buffer!");

#if RETRO_USING_OPENGL
    CreateVideoTexture(width, height);
#endif
}

void CloseVideoBuffer()
{
    if (videoPlaying) {
#if RETRO_USING_SDL1 || (RETRO_USING_SDL2 && RETRO_USING_OPENGL)
        SDL_FreeSurface(Engine.videoBuffer);
#endif
#if RETRO_USING_SDL2 && !RETRO_USING_OPENGL
        SDL_DestroyTexture(Engine.videoBuffer);
#endif
        Engine.videoBuffer = nullptr;
    }
}

#if RETRO_USING_OPENGL
// Draws the video at its own native resolution, via its own dedicated texture (see CreateVideoTexture) -- completely separate from the
// small internal game-screen buffer that DrawVideoFrame (below) downsamples into. This is the OpenGL-path equivalent of DrawVideoFrame;
// RetroGameLoop's ENGINE_VIDEOWAIT case calls one or the other depending on the render path, never both.
void DrawVideoFrameGL()
{
    if (!videoPlaying || !Engine.videoBuffer || videoWidth <= 0 || videoHeight <= 0)
        return;

    // Stream this frame's pixels into the texture. Same RGBA8 byte order SDL_CreateRGBSurface was given in SetupVideoBuffer, so this is a
    // straight copy -- no channel reordering needed
    TextureInfo *texture = &textureList[VIDEO_TEXTURE_SLOT];
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoWidth, videoHeight, GL_RGBA, GL_UNSIGNED_BYTE, Engine.videoBuffer->pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Same aspect-preserving fit as DrawVideoFrame, just kept in floats here since RenderImage takes float scale factors rather than
    // integer destination pixels
    int dstW = SCREEN_XSIZE;
    int dstH = SCREEN_YSIZE;
    if (dstW * videoHeight > dstH * videoWidth)
        dstW = dstH * videoWidth / videoHeight; // video is narrower than the screen: pillarbox
    else
        dstH = dstW * videoHeight / videoWidth; // video is wider than the screen: letterbox

    if (dstW <= 0 || dstH <= 0)
        return;

    // Same combined fade-in/fade-out darkness as DrawVideoFrame, applied here as a vertex color tint (RenderImage multiplies the
    // texture's pixels by vertexR/G/B) instead of baking it into the pixels, since we're not touching the pixels ourselves this time
    int darkness = videoFadeIn > videoFadeOut ? videoFadeIn : videoFadeOut;
    if (darkness > 255)
        darkness = 255;
    const byte tint = (byte)(256 - darkness > 255 ? 255 : 256 - darkness);
    vertexR = tint;
    vertexG = tint;
    vertexB = tint;

    // x=0,y=0 is screen center in this coordinate space (see retroVertexList in Drawing.cpp for the same convention). Anchoring on the
    // video's own center (pivotX/Y) and scaling from there keeps the result centered exactly like the letterboxing above intends
    RenderImage(0.0f, 0.0f, 160.0f, (float)dstW / videoWidth, (float)dstH / videoHeight, videoWidth / 2.0f, videoHeight / 2.0f,
                (float)videoWidth, (float)videoHeight, 0.0f, 0.0f, 255, VIDEO_TEXTURE_SLOT);

    // Don't leave the tint set for whatever draws next
    vertexR = 0xFF;
    vertexG = 0xFF;
    vertexB = 0xFF;
}
#endif

#if RETRO_USING_OPENGL || RETRO_USING_SDL1
// Converts the latest decoded video frame (RGBA surface in Engine.videoBuffer) into the engine's 16-bit (RGB565) frame buffer, letterboxed to
// keep the video's aspect ratio. The regular TransferRetroBuffer()/RenderRetroBuffer() path then presents it like any other frame.
void DrawVideoFrame()
{
    memset(Engine.frameBuffer, 0, GFX_LINESIZE * SCREEN_YSIZE * sizeof(ushort));

    if (!videoPlaying || !Engine.videoBuffer || videoWidth <= 0 || videoHeight <= 0)
        return;

    int dstW = SCREEN_XSIZE;
    int dstH = SCREEN_YSIZE;
    if (dstW * videoHeight > dstH * videoWidth)
        dstW = dstH * videoWidth / videoHeight; // video is narrower than the screen: pillarbox
    else
        dstH = dstW * videoHeight / videoWidth; // video is wider than the screen: letterbox

    if (dstW <= 0 || dstH <= 0)
        return;

    const int offX = (SCREEN_XSIZE - dstW) / 2;
    const int offY = (SCREEN_YSIZE - dstH) / 2;

    const uint *src   = (const uint *)Engine.videoBuffer->pixels;
    const int srcPitch = Engine.videoBuffer->pitch / (int)sizeof(uint);

    // Whichever of fade-in/fade-out is currently darker wins -- in the ordinary case only one of them is ever nonzero at a time, but this
    // keeps things sane even if the video is skipped during its own fade-in (no pop, the two blend smoothly into each other)
    int darkness = videoFadeIn > videoFadeOut ? videoFadeIn : videoFadeOut;
    if (darkness > 255)
        darkness = 255;
    const int fade = 256 - darkness;

    // Fixed-point (16.16) steps replace a per-pixel divide with a per-pixel add: on a slow in-order CPU (e.g. a low-end Android phone) an
    // integer division can cost 10-20x what an add does, and this runs for every one of the ~100,000 pixels in the frame, every frame.
    const int xStep = (videoWidth << 16) / dstW;
    const int yStep = (videoHeight << 16) / dstH;
    int yAccum      = 0;

    for (int y = 0; y < dstH; ++y) {
        const uint *row = src + (yAccum >> 16) * srcPitch;
        ushort *dst     = &Engine.frameBuffer[(y + offY) * GFX_LINESIZE + offX];
        yAccum += yStep;

        int xAccum = 0;
        if (fade < 256) {
            for (int x = 0; x < dstW; ++x) {
                const uint px = row[xAccum >> 16];
                xAccum += xStep;
                int r = ((px >> 0) & 0xFF) * fade >> 8;
                int g = ((px >> 8) & 0xFF) * fade >> 8;
                int b = ((px >> 16) & 0xFF) * fade >> 8;

                dst[x] = (ushort)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
        else {
            for (int x = 0; x < dstW; ++x) {
                const uint px = row[xAccum >> 16];
                xAccum += xStep;

                dst[x] = (ushort)((((px >> 0) & 0xF8) << 8) | (((px >> 8) & 0xFC) << 3) | (((px >> 16) & 0xF8) >> 3));
            }
        }
    }
}
#endif
