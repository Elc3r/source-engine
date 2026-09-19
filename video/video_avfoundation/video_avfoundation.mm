// Native macOS startup movies. Uses the existing QuickTime subsystem slot,
// but does not depend on the obsolete QuickTime C API.
#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#import <AVKit/AVKit.h>
#include <SDL.h>
#undef MIN
#undef MAX
#define DONT_DEFINE_BOOL
#include "video/ivideoservices.h"
#include "videosubsystem.h"

class CAVFoundationVideo : public CBaseAppSystem<IVideoSubSystem>
{
    IVideoCommonServices *m_common = nullptr;
    VideoResult_t m_result = VideoResult::SUCCESS;
    VideoResult_t Result(VideoResult_t result) { return m_result = result; }
public:
    VideoSystem_t GetSystemID() override { return VideoSystem::QUICKTIME; }
    VideoSystemStatus_t GetSystemStatus() override
    { return m_common ? VideoSystemStatus::OK : VideoSystemStatus::NOT_INITIALIZED; }
    VideoSystemFeature_t GetSupportedFeatures() override
    { return VideoSystemFeature::PLAY_VIDEO_FILE_FULL_SCREEN; }
    const char *GetVideoSystemName() override { return "AVFoundation"; }
    bool InitializeVideoSystem(IVideoCommonServices *common) override
    { m_common = common; return common != nullptr; }
    bool ShutdownVideoSystem() override { m_common = nullptr; return true; }
    VideoResult_t VideoSoundDeviceCMD(VideoSoundDeviceOperation_t, void *, void *) override
    { return Result(VideoResult::FEATURE_NOT_AVAILABLE); }
    int GetSupportedFileExtensionCount() override { return 3; }
    const char *GetSupportedFileExtension(int n) override
    {
        static const char *extensions[] = { ".mov", ".mp4", ".m4v" };
        return n >= 0 && n < 3 ? extensions[n] : nullptr;
    }
    VideoSystemFeature_t GetSupportedFileExtensionFeatures(int n) override
    { return GetSupportedFileExtension(n) ? GetSupportedFeatures() : VideoSystemFeature::NO_FEATURES; }
    IVideoMaterial *CreateVideoMaterial(const char *, const char *, VideoPlaybackFlags_t) override
    { Result(VideoResult::FEATURE_NOT_AVAILABLE); return nullptr; }
    VideoResult_t DestroyVideoMaterial(IVideoMaterial *) override
    { return Result(VideoResult::FEATURE_NOT_AVAILABLE); }
    IVideoRecorder *CreateVideoRecorder() override
    { Result(VideoResult::FEATURE_NOT_AVAILABLE); return nullptr; }
    VideoResult_t DestroyVideoRecorder(IVideoRecorder *) override
    { return Result(VideoResult::FEATURE_NOT_AVAILABLE); }
    VideoResult_t CheckCodecAvailability(VideoEncodeCodec_t) override
    { return Result(VideoResult::FEATURE_NOT_AVAILABLE); }
    VideoResult_t GetLastResult() override { return m_result; }

    VideoResult_t PlayVideoFileFullScreen(const char *filename, void *mainWindow,
        int, int, int, int, bool, float forcedMinTime, VideoPlaybackFlags_t flags) override;
};

VideoResult_t CAVFoundationVideo::PlayVideoFileFullScreen(const char *filename,
    void *mainWindow, int, int, int, int, bool, float forcedMinTime, VideoPlaybackFlags_t flags)
{
    if (!m_common || ![NSThread isMainThread])
        return Result(VideoResult::SYSTEM_NOT_AVAILABLE);
    if (!filename || !filename[0] || !mainWindow)
        return Result(VideoResult::BAD_INPUT_PARAMETERS);
    // This first implementation supports startup movies, not arbitrary playback modes.
    if (flags & (VideoPlaybackFlags::LOOP_VIDEO | VideoPlaybackFlags::PRELOAD_VIDEO |
                 VideoPlaybackFlags::DONT_AUTO_START_VIDEO | VideoPlaybackFlags::PAUSE_ON_ANY_KEY))
        return Result(VideoResult::FEATURE_NOT_AVAILABLE);

    @autoreleasepool
    {
        NSString *path = [NSString stringWithUTF8String:filename];
        if (!path || ![[NSFileManager defaultManager] fileExistsAtPath:path])
            return Result(VideoResult::VIDEO_FILE_NOT_FOUND);
        NSWindow *window = (__bridge NSWindow *)mainWindow;
        NSView *host = window.contentView;
        if (!host)
            return Result(VideoResult::SYSTEM_ERROR_OCCURED);

        AVPlayerItem *item = [AVPlayerItem playerItemWithURL:[NSURL fileURLWithPath:path]];
        AVPlayer *player = [AVPlayer playerWithPlayerItem:item];
        player.volume = m_common->GetSystemVolume();
        player.muted = (flags & VideoPlaybackFlags::NO_AUDIO) != 0;
        AVPlayerView *view = [[AVPlayerView alloc] initWithFrame:host.bounds];
        view.controlsStyle = AVPlayerViewControlsStyleNone;
        view.videoGravity = AVLayerVideoGravityResizeAspect;
        view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        view.player = player;
        NSResponder *previousResponder = window.firstResponder;
        SDL_Window *sdlWindow = SDL_GetKeyboardFocus();
        SDL_bool relativeMouse = SDL_GetRelativeMouseMode();
        SDL_SetRelativeMouseMode(SDL_FALSE);
        [host addSubview:view positioned:NSWindowAbove relativeTo:nil];

        __block bool ended = false;
        id observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:AVPlayerItemDidPlayToEndTimeNotification object:item
            queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *) { ended = true; }];
        [player play];
        VideoResult_t result = VideoResult::SUCCESS;
        bool abort = false, quit = false;
        double lastProgress = [NSProcessInfo processInfo].systemUptime;
        double previousTime = -1.0;
        while (!ended && !abort)
        {
            @autoreleasepool
            {
                SDL_Event event;
                while (SDL_PollEvent(&event))
                {
                    if (event.type == SDL_QUIT ||
                        (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                         (!sdlWindow || event.window.windowID == SDL_GetWindowID(sdlWindow))))
                        quit = abort = true;
                    if (event.type == SDL_KEYDOWN && !event.key.repeat)
                    {
                        const SDL_Keycode key = event.key.keysym.sym;
                        bool skip = (key == SDLK_ESCAPE && (flags & VideoPlaybackFlags::ABORT_ON_ESC)) ||
                            (key == SDLK_SPACE && (flags & VideoPlaybackFlags::ABORT_ON_SPACE)) ||
                            (key == SDLK_RETURN && (flags & VideoPlaybackFlags::ABORT_ON_RETURN));
                        double played = CMTimeGetSeconds(player.currentTime);
                        if (skip && (!(flags & VideoPlaybackFlags::FORCE_MIN_PLAY_TIME) ||
                                     played >= forcedMinTime))
                            abort = true;
                    }
                }
                if (item.status == AVPlayerItemStatusFailed || player.status == AVPlayerStatusFailed)
                {
                    NSLog(@"Source startup video failed: %@", item.error ?: player.error);
                    result = VideoResult::VIDEO_ERROR_OCCURED;
                    break;
                }
                // Keep AVFoundation callbacks and Cocoa composition moving without
                // running the engine render loop over the movie view.
                [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
                double now = [NSProcessInfo processInfo].systemUptime;
                double time = CMTimeGetSeconds(player.currentTime);
                if (time > previousTime) { previousTime = time; lastProgress = now; }
                if (now - lastProgress > 15.0)
                {
                    NSLog(@"Source startup video timed out: %@", path);
                    result = VideoResult::VIDEO_ERROR_OCCURED;
                    break;
                }
            }
        }
        [player pause];
        [[NSNotificationCenter defaultCenter] removeObserver:observer];
        view.player = nil;
        [view removeFromSuperview];
        [player replaceCurrentItemWithPlayerItem:nil];
        [window makeFirstResponder:previousResponder];
        [host setNeedsDisplay:YES];
        SDL_SetRelativeMouseMode(relativeMouse);
        // Do not swallow a request to close the game during the intro.
        if (quit)
        {
            SDL_Event event = {};
            event.type = SDL_QUIT;
            SDL_PushEvent(&event);
        }
        return Result(result);
    }
}

static CAVFoundationVideo g_AVFoundationVideo;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CAVFoundationVideo, IVideoSubSystem,
    VIDEO_SUBSYSTEM_INTERFACE_VERSION, g_AVFoundationVideo);
