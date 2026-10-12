#include "RetroEngine.hpp"

int videoWidth  = 0;
int videoHeight = 0;

THEORAPLAY_Decoder *videoDecoder = nullptr;
bool videoPlaying                = false;

// Dedicated to the video only -- kept separate from the engine's shared screen-fade globals (fadeMode/fadeR/G/B/A, set by the
// script-facing SetScreenFade) so a script fading the screen right before/after a video (as Start.txt does) can never interact
// with the video's own fade in/out
int videoFadeIn          = 0; // 0..255, counts DOWN from 255 to 0 over the first ~32 frames: the video brightens in
int videoFadeOut         = 0; // 0..255, counts UP from 0 once a skip/natural end is detected: the video darkens out
int videoPrevRefreshRate = 0; // Engine.refreshRate from just before we throttled it down to the video's own fps; restored once it ends

// ---------------------------------------------------------------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------------------------------------------------------------
static const THEORAPLAY_VideoFrame *videoVidData = nullptr; // frame fetched from the decoder, waiting for its play time
static THEORAPLAY_Io callbacks;

static bool videoSkipped   = false;
static bool videoTouchHeld = false; // tracks whether the screen was already being touched, so we can require a fresh tap to skip
static int vidFrameMS      = 0;
static Uint32 vidBaseticks = 0;

// Legacy .rsv (GIF-based) video state, used by UpdateVideoFrame only
static int currentVideoFrame = 0;
static int videoFrameCount   = 0;
static byte videoData        = 0;
static int videoFilePos      = 0;

// On the pure-OpenGL path (SDL2 + OpenGL, no SDL1) the decoded RGBA frame is uploaded to the GPU straight from the decoder's own buffer,
// instead of being memcpy'd into an intermediate SDL_Surface first (one full-frame copy less per video frame). The SDL1 path still needs
// the surface because DrawVideoFrame() converts from it.
#if RETRO_USING_OPENGL && !RETRO_USING_SDL1
#define VIDEO_DIRECT_GL 1
#else
#define VIDEO_DIRECT_GL 0
#endif

#if RETRO_USING_OPENGL
static bool videoFrameDirty = false; // a new frame has arrived since the last texture upload (so unchanged ticks upload nothing)
static bool videoTextureHasData = false;
#if VIDEO_DIRECT_GL
static const THEORAPLAY_VideoFrame *videoPendingFrame = nullptr; // decoded frame waiting for upload; freed right after the upload
#endif
#endif

// Fit the video inside the screen keeping its aspect ratio (pillarbox / letterbox).
// Returns false if there is nothing sensible to draw.
#if RETRO_USING_OPENGL || RETRO_USING_SDL1
static inline bool GetVideoFit(int *outW, int *outH)
{
    int dstW = SCREEN_XSIZE;
    int dstH = SCREEN_YSIZE;
    if (dstW * videoHeight > dstH * videoWidth)
        dstW = dstH * videoWidth / videoHeight; // video is narrower than the screen: pillarbox
    else
        dstH = dstW * videoHeight / videoWidth; // video is wider than the screen: letterbox

    *outW = dstW;
    *outH = dstH;
    return dstW > 0 && dstH > 0;
}

// Whichever of fade-in/fade-out is currently darker wins -- in the ordinary case only one of them is ever nonzero at a time, but this
// keeps things sane even if the video is skipped during its own fade-in (no pop, the two blend smoothly into each other)
static inline int GetVideoFade() // 256 = fully visible ... 1 = (almost) black
{
    int darkness = videoFadeIn > videoFadeOut ? videoFadeIn : videoFadeOut;
    if (darkness > 255)
        darkness = 255;
    return 256 - darkness;
}
#endif

static long videoRead(THEORAPLAY_Io *io, void *buf, long buflen)
{
    FileIO *file    = (FileIO *)io->userdata;
    const size_t br = fRead(buf, 1, buflen * sizeof(byte), file);
    if (br == 0)
        return -1;
    return (long)br;
}

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
    snprintf(filepath, sizeof(filepath), "%.*s/Videos/", gamePathLen, gamePath);
#else
    StrCopy(filepath, BASE_PATH "Videos/");
#endif

    const int len = StrLength(filePath);

    if (len >= 2 && StrComp(filePath + ((size_t)len - 2), "us")) {
        filePath[len - 2] = 0;
    }

    // bounded append (the old StrAdd calls could overflow filepath with a long base path / script string)
    const size_t cur = strlen(filepath);
    snprintf(filepath + cur, sizeof(filepath) - cur, "%s.ogv", filePath);

    FileIO *file = fOpen(filepath, "rb");
    if (!file) {
        PrintLog("Couldn't find file '%s'!", filepath);
        return;
    }

    PrintLog("Loaded File '%s'!", filepath);

    callbacks.read     = videoRead;
    callbacks.close    = videoClose;
    callbacks.userdata = (void *)file;
    // The SDL2 renderer path uploads planar YUV straight into a YV12 texture. Every other path (SDL1, and SDL2 + OpenGL) wants a
    // 32-bit RGBA frame, so the decoder must output RGBA there.
#if RETRO_USING_SDL2 && !RETRO_USING_OPENGL
    videoDecoder = THEORAPLAY_startDecode(&callbacks, /*FPS*/ 30, THEORAPLAY_VIDFMT_IYUV, GetGlobalVariableByName("Options.Soundtrack") ? 1 : 0);
#else
    videoDecoder = THEORAPLAY_startDecode(&callbacks, /*FPS*/ 30, THEORAPLAY_VIDFMT_RGBA, GetGlobalVariableByName("Options.Soundtrack") ? 1 : 0);
#endif

    if (!videoDecoder) {
        // THEORAPLAY_startDecode already closed the file through videoClose() on failure -- closing it again here would double-close it
        PrintLog("Video Decoder Error!");
        return;
    }

    // Wait for the first frame so we know the video's size/fps. The old loop spun at 100% CPU with no sleep and no exit condition: a corrupt or
    // video-less file hung the game here forever. Now it sleeps between polls and gives up as soon as the decoder has nothing more to give.
    videoVidData = THEORAPLAY_getVideo(videoDecoder);
    while (!videoVidData && THEORAPLAY_isDecoding(videoDecoder)) {
        SDL_Delay(1);
        videoVidData = THEORAPLAY_getVideo(videoDecoder);
    }

    if (!videoVidData) {
        PrintLog("Video Error!");
        THEORAPLAY_stopDecode(videoDecoder); // also closes the file through videoClose
        videoDecoder = nullptr;
        return;
    }

    videoWidth  = videoVidData->width;
    videoHeight = videoVidData->height;

    SetupVideoBuffer(videoWidth, videoHeight);
    vidBaseticks = SDL_GetTicks();
    vidFrameMS   = (videoVidData->fps == 0.0) ? 0 : ((int)(1000.0 / videoVidData->fps));
    videoPlaying = true;
    trackID      = TRACK_COUNT - 1;

    // Seed with whatever the screen is doing right now (e.g. still held from the tap that started this scene) so that
    // touch isn't mistaken for a fresh tap-to-skip until the player actually lifts their finger and taps again
    videoTouchHeld = false;
    for (int t = 0; t < 8; ++t)
        videoTouchHeld |= touchDown[t] != 0;

    videoSkipped = false;
    videoFadeIn  = 0xFF;
    videoFadeOut = 0;

    // The whole engine loop normally runs at Engine.refreshRate (60 by default), via RetroEngine::Run()'s own frame timer. Nothing
    // needs to happen faster than the video's own frame rate while it plays -- there's no gameplay to update (ProcessObjects only
    // runs during ENGINE_MAINGAME) and nothing new to show between the video's own frames -- so throttling the loop itself down to
    // match saves the whole render pass each skipped tick, not just the redundant texture upload
    videoPrevRefreshRate = Engine.refreshRate;
    const int videoTargetRate = (int)(videoVidData->fps + 0.5);
    if (videoTargetRate >= 1)
        Engine.refreshRate = videoTargetRate;
    // else: fps unknown/zero -- leave Engine.refreshRate untouched rather than risk a bad value

    Engine.gameMode = ENGINE_VIDEOWAIT;
}

void UpdateVideoFrame()
{
    if (videoPlaying) {
        if (videoFrameCount > currentVideoFrame) {
            GFXSurface *surface = &gfxSurface[videoData];

            // Offset of the next frame. (4 single-byte reads collapsed into one read.)
            byte sizeBytes[4];
            FileRead(sizeBytes, 4);
            videoFilePos += sizeBytes[0] | (sizeBytes[1] << 8) | (sizeBytes[2] << 16) | (sizeBytes[3] << 24);

            // 128-entry palette: one 384-byte read instead of 128 three-byte reads
            byte palBytes[0x80 * 3];
            FileRead(palBytes, sizeof(palBytes));
            for (int i = 0; i < 0x80; ++i) {
                const byte r = palBytes[i * 3 + 0];
                const byte g = palBytes[i * 3 + 1];
                const byte b = palBytes[i * 3 + 2];

                activePalette32[i].r = r;
                activePalette32[i].g = g;
                activePalette32[i].b = b;
                activePalette[i]     = ((ushort)(r >> 3) << 11) | ((ushort)(g >> 2) << 5) | (b >> 3);
            }

            byte fileBuffer = 0;
            FileRead(&fileBuffer, 1);
            while (fileBuffer != ',') FileRead(&fileBuffer, 1); // gif image start identifier

            // IMAGE LEFT / TOP / WIDTH / HEIGHT (2 bytes each). The old code read these with FileRead(&fileBuffer, 2) into a 1-byte variable,
            // overflowing it on every frame -- they're now read into a real 8-byte buffer.
            byte imageDesc[8];
            FileRead(imageDesc, 8);

            FileRead(&fileBuffer, 1); // PaletteType
            const bool interlaced = (fileBuffer & 0x40) >> 6;
            if (fileBuffer >> 7 == 1) {
                // skip the local palette's upper half (0x80 entries * 3 bytes). Same overflow as above, previously 128 reads of 3 bytes into 1.
                byte skip[0x80 * 3];
                FileRead(skip, sizeof(skip));
            }
            ReadGifPictureData(surface->width, surface->height, interlaced, graphicData, surface->dataPosition);

            SetFilePosition(videoFilePos);
            ++currentVideoFrame;
        }
        else {
            videoPlaying = false;
            CloseFile();
        }
    }
}

int ProcessVideo()
{
    if (!videoPlaying)
        return 0; // its not even initialised

    CheckKeyPress(&keyPress);

    // A tap/click anywhere on the screen skips the video too, not just pressing A -- this is the RSDKv4 Plus (Origins) behaviour,
    // rather than RSDKv4-V's, which only responds to the A button. Only a FRESH tap counts (touchDown was false last frame, true this
    // frame) -- otherwise a touch still held from whatever screen led into this video (e.g. the tap that started the game) would
    // read as a skip the instant the video starts, before the player ever meant to skip anything
    bool touched = false;
    for (int t = 0; t < 8 && !touched; ++t)
        touched = touchDown[t] != 0;

    const bool touchSkip = touched && !videoTouchHeld;
    videoTouchHeld       = touched;

    // Runs every frame from the start, independent of skipping: the video opens on black and brightens up over its first ~32 frames
    if (videoFadeIn > 0)
        videoFadeIn = (videoFadeIn < 8) ? 0 : (videoFadeIn - 8);

    // Start the fade-out the first time either the player skips or the video plays out on its own -- a natural end fades out exactly
    // like a skip does, instead of cutting straight to black the instant decoding finishes. (THEORAPLAY_isDecoding takes a mutex, so it is
    // only queried while we haven't already decided to stop.)
    if (!videoSkipped && (keyPress.A || touchSkip || !THEORAPLAY_isDecoding(videoDecoder)))
        videoSkipped = true;

    if (videoSkipped) {
        if (videoFadeOut < 0xFF)
            videoFadeOut += 8;

        if (videoFadeOut >= 0xFF) {
            StopVideoPlayback();
            return 1; // video finished (either played out or was skipped, faded to black either way)
        }

        // Once we've decided to fade out -- whether the video reached its real last frame or the player skipped early -- stop pulling
        // new frames from the decoder entirely. The video texture/buffer keeps holding the last frame it received, so the fade plays out
        // over that held frame instead of the video continuing to advance underneath it
        return 2;
    }

    const Uint32 now = SDL_GetTicks() - vidBaseticks;

    if (!videoVidData)
        videoVidData = THEORAPLAY_getVideo(videoDecoder);

    // Play video frames when it's time.
    if (videoVidData && videoVidData->playms <= now) {
        if (vidFrameMS && (int)(now - videoVidData->playms) >= vidFrameMS) {
            // Skip frames to catch up, but keep track of the last one in case we catch up to a series of dupe frames, which means we'd
            // have to draw that final frame and then wait for more.
            const THEORAPLAY_VideoFrame *last = videoVidData;
            const THEORAPLAY_VideoFrame *next;
            while ((next = THEORAPLAY_getVideo(videoDecoder)) != NULL) {
                THEORAPLAY_freeVideo(last);
                last = next;
                if ((int)(now - next->playms) < vidFrameMS)
                    break;
            }
            videoVidData = last; // never NULL here: if the decoder ran dry we show the newest frame we have
        }

#if RETRO_USING_SDL2 && !RETRO_USING_OPENGL
        {
            const int w      = videoVidData->width;
            const int h      = videoVidData->height;
            const int half_w = w / 2;
            const Uint8 *y   = (const Uint8 *)videoVidData->pixels;
            const Uint8 *u   = y + (w * h);
            const Uint8 *v   = u + (half_w * (h / 2));
            SDL_UpdateYUVTexture(Engine.videoBuffer, NULL, y, w, u, half_w, v, half_w);
        }
        THEORAPLAY_freeVideo(videoVidData);
#elif VIDEO_DIRECT_GL
        // Hand the decoded frame to DrawVideoFrameGL as-is (no intermediate copy); it uploads and frees it. If a previous frame was never
        // drawn (can't normally happen, but costs nothing to be safe), it is dropped here.
        if (videoPendingFrame)
            THEORAPLAY_freeVideo(videoPendingFrame);
        videoPendingFrame = videoVidData;
        videoFrameDirty   = true;
#else
        // SDL1 (software conversion in DrawVideoFrame reads from the surface)
        memcpy(Engine.videoBuffer->pixels, videoVidData->pixels, (size_t)videoVidData->width * videoVidData->height * sizeof(uint));
#if RETRO_USING_OPENGL
        videoFrameDirty = true;
#endif
        THEORAPLAY_freeVideo(videoVidData);
#endif
        videoVidData = NULL;
    }

    return 2; // its playing as expected
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
#if VIDEO_DIRECT_GL
        if (videoPendingFrame) {
            THEORAPLAY_freeVideo(videoPendingFrame);
            videoPendingFrame = NULL;
        }
#endif
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
    // nullptr: just reserves the storage. The pixels are streamed in every new frame afterwards via glTexSubImage2D (see DrawVideoFrameGL)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    videoFrameDirty     = false;
    videoTextureHasData = false;
}
#endif

void SetupVideoBuffer(int width, int height)
{
#if RETRO_USING_SDL1 || (RETRO_USING_SDL2 && RETRO_USING_OPENGL)
#if !VIDEO_DIRECT_GL
    Engine.videoBuffer = SDL_CreateRGBSurface(0, width, height, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    if (!Engine.videoBuffer)
        PrintLog("Failed to create video buffer!");
#endif
    // VIDEO_DIRECT_GL: no CPU-side surface at all -- frames go from the decoder's buffer straight to the GPU texture
#endif
#if RETRO_USING_SDL2 && !RETRO_USING_OPENGL
    Engine.videoBuffer = SDL_CreateTexture(Engine.renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!Engine.videoBuffer)
        PrintLog("Failed to create video buffer!");
#endif

#if RETRO_USING_OPENGL
    CreateVideoTexture(width, height);
#endif
}

void CloseVideoBuffer()
{
    if (videoPlaying) {
#if RETRO_USING_SDL1 || (RETRO_USING_SDL2 && RETRO_USING_OPENGL)
        if (Engine.videoBuffer)
            SDL_FreeSurface(Engine.videoBuffer);
#endif
#if RETRO_USING_SDL2 && !RETRO_USING_OPENGL
        if (Engine.videoBuffer)
            SDL_DestroyTexture(Engine.videoBuffer);
#endif
        Engine.videoBuffer = nullptr;

#if RETRO_USING_OPENGL
        // Give the video-sized texture back to the GPU (it used to stay allocated until the next video replaced it)
        TextureInfo *texture = &textureList[VIDEO_TEXTURE_SLOT];
        if (texture->id) {
            glDeleteTextures(1, &texture->id);
            texture->id = 0;
        }
        videoFrameDirty     = false;
        videoTextureHasData = false;
#endif
    }
}

#if RETRO_USING_OPENGL
// Draws the video at its own native resolution, via its own dedicated texture (see CreateVideoTexture) -- completely separate from the
// small internal game-screen buffer that DrawVideoFrame (below) downsamples into. This is the OpenGL-path equivalent of DrawVideoFrame;
// RetroGameLoop's ENGINE_VIDEOWAIT case calls one or the other depending on the render path, never both.
void DrawVideoFrameGL()
{
    if (!videoPlaying || videoWidth <= 0 || videoHeight <= 0)
        return;

    TextureInfo *texture = &textureList[VIDEO_TEXTURE_SLOT];

    // Only touch the GPU when a NEW frame arrived. The engine ticks at the video's fps, but a skipped/fading video (and any tick where the
    // decoder hasn't produced the next frame yet) used to re-upload the identical full-resolution frame every single tick.
    if (videoFrameDirty) {
#if VIDEO_DIRECT_GL
        if (videoPendingFrame) {
            glBindTexture(GL_TEXTURE_2D, texture->id);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoWidth, videoHeight, GL_RGBA, GL_UNSIGNED_BYTE, videoPendingFrame->pixels);
            glBindTexture(GL_TEXTURE_2D, 0);

            THEORAPLAY_freeVideo(videoPendingFrame);
            videoPendingFrame   = NULL;
            videoTextureHasData = true;
        }
#else
        if (Engine.videoBuffer) {
            glBindTexture(GL_TEXTURE_2D, texture->id);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoWidth, videoHeight, GL_RGBA, GL_UNSIGNED_BYTE, Engine.videoBuffer->pixels);
            glBindTexture(GL_TEXTURE_2D, 0);
            videoTextureHasData = true;
        }
#endif
        videoFrameDirty = false;
    }

    // Nothing has been uploaded yet: the texture storage is uninitialised, so don't draw garbage
    if (!videoTextureHasData)
        return;

    // Same aspect-preserving fit as DrawVideoFrame, just kept in floats here since RenderImage takes float scale factors rather than
    // integer destination pixels
    int dstW, dstH;
    if (!GetVideoFit(&dstW, &dstH))
        return;

    // Same combined fade-in/fade-out darkness as DrawVideoFrame, applied here as a vertex color tint (RenderImage multiplies the
    // texture's pixels by vertexR/G/B) instead of baking it into the pixels, since we're not touching the pixels ourselves this time
    const int fade = GetVideoFade();
    const byte tint = (byte)(fade > 255 ? 255 : fade);
    vertexR         = tint;
    vertexG         = tint;
    vertexB         = tint;

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
    if (!videoPlaying || !Engine.videoBuffer || videoWidth <= 0 || videoHeight <= 0) {
        memset(Engine.frameBuffer, 0, GFX_LINESIZE * SCREEN_YSIZE * sizeof(ushort));
        return;
    }

    int dstW, dstH;
    if (!GetVideoFit(&dstW, &dstH)) {
        memset(Engine.frameBuffer, 0, GFX_LINESIZE * SCREEN_YSIZE * sizeof(ushort));
        return;
    }

    const int offX = (SCREEN_XSIZE - dstW) / 2;
    const int offY = (SCREEN_YSIZE - dstH) / 2;

    // Only the letterbox/pillarbox bars are cleared now, not the whole screen first and then ~all of it overwritten again.
    ushort *fb = Engine.frameBuffer;
    if (offY > 0)
        memset(fb, 0, (size_t)offY * GFX_LINESIZE * sizeof(ushort));
    const int bottomRows = SCREEN_YSIZE - offY - dstH;
    if (bottomRows > 0)
        memset(fb + (size_t)(offY + dstH) * GFX_LINESIZE, 0, (size_t)bottomRows * GFX_LINESIZE * sizeof(ushort));
    const int rightMargin = GFX_LINESIZE - (offX + dstW);

    const uint *src    = (const uint *)Engine.videoBuffer->pixels;
    const int srcPitch = Engine.videoBuffer->pitch / (int)sizeof(uint);

    const int fade = GetVideoFade();

    // While fading, three 256-entry lookup tables (rebuilt once per frame, 768 entries) replace three multiplies + shifts per pixel.
    // Layout matches RGB565: the table values can simply be OR'd together.
    ushort lutR[256], lutG[256], lutB[256];
    if (fade < 256) {
        for (int i = 0; i < 256; ++i) {
            const int c = (i * fade) >> 8;
            lutR[i]     = (ushort)((c >> 3) << 11);
            lutG[i]     = (ushort)((c >> 2) << 5);
            lutB[i]     = (ushort)(c >> 3);
        }
    }

    // Fixed-point (16.16) steps replace a per-pixel divide with a per-pixel add: on a slow in-order CPU (e.g. a low-end Android phone) an
    // integer division can cost 10-20x what an add does, and this runs for every one of the ~100,000 pixels in the frame, every frame.
    const int xStep = (videoWidth << 16) / dstW;
    const int yStep = (videoHeight << 16) / dstH;
    int yAccum      = 0;
    int lastSrcY    = -1;
    const ushort *prevRow = NULL;

    for (int y = 0; y < dstH; ++y) {
        const int srcY = yAccum >> 16;
        yAccum += yStep;

        ushort *rowStart = &fb[(size_t)(y + offY) * GFX_LINESIZE];
        ushort *dst      = rowStart + offX;

        if (offX > 0)
            memset(rowStart, 0, (size_t)offX * sizeof(ushort));
        if (rightMargin > 0)
            memset(dst + dstW, 0, (size_t)rightMargin * sizeof(ushort));

        if (srcY == lastSrcY && prevRow) {
            // Upscaling: this destination row maps to the same source row as the previous one -- just copy it instead of converting again
            memcpy(dst, prevRow, (size_t)dstW * sizeof(ushort));
        }
        else {
            const uint *row = src + (size_t)srcY * srcPitch;
            int xAccum      = 0;

            if (fade < 256) {
                for (int x = 0; x < dstW; ++x) {
                    const uint px = row[xAccum >> 16];
                    xAccum += xStep;
                    dst[x] = (ushort)(lutR[px & 0xFF] | lutG[(px >> 8) & 0xFF] | lutB[(px >> 16) & 0xFF]);
                }
            }
            else {
                for (int x = 0; x < dstW; ++x) {
                    const uint px = row[xAccum >> 16];
                    xAccum += xStep;
                    dst[x] = (ushort)((((px >> 0) & 0xF8) << 8) | (((px >> 8) & 0xFC) << 3) | (((px >> 16) & 0xF8) >> 3));
                }
            }

            lastSrcY = srcY;
            prevRow  = dst;
        }
    }
}
#endif
