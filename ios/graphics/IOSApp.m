#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_metal.h"
#include "SDL_syswm.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl3.h>
#include <unistd.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>


#include "ToGLESRuntime.h"

#if TARGET_OS_SIMULATOR
// A key responder with no hit region: touches continue through SDL's view.
@interface SourceKeyboardInput : UIView
@property(nonatomic,strong) NSMutableSet<NSNumber *> *heldKeys;
- (void)releaseKeys;
@end
@implementation SourceKeyboardInput
- (BOOL)canBecomeFirstResponder { return YES; }
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event { return nil; }
- (void)sendKey:(UIKey *)key down:(BOOL)down {
    if (!key || (int)key.keyCode>=SDL_NUM_SCANCODES) return;
    if (!self.heldKeys) self.heldKeys=[NSMutableSet set];
    NSNumber *code=@(key.keyCode);
    SDL_Event event={0}; event.type=down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state=down ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat=down && [self.heldKeys containsObject:code];
    event.key.keysym.scancode=(SDL_Scancode)key.keyCode;
    event.key.keysym.sym=SDL_GetKeyFromScancode(event.key.keysym.scancode);
    if (down) [self.heldKeys addObject:code]; else [self.heldKeys removeObject:code];
    SDL_PushEvent(&event);
}
- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    for (UIPress *press in presses) [self sendKey:press.key down:YES];
}
- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    for (UIPress *press in presses) [self sendKey:press.key down:NO];
}
- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    [self releaseKeys];
}
- (void)releaseKeys {
    for (NSNumber *code in self.heldKeys) {
        SDL_Event event={0}; event.type=SDL_KEYUP; event.key.state=SDL_RELEASED;
        event.key.keysym.scancode=(SDL_Scancode)code.intValue;
        SDL_PushEvent(&event);
    }
    [self.heldKeys removeAllObjects];
}
@end
static SourceKeyboardInput *keyboardInput;
#endif

static SDL_Window *window;
static SDL_MetalView metalView;
static EGLDisplay display = EGL_NO_DISPLAY;
static EGLContext context = EGL_NO_CONTEXT;
static EGLSurface surface = EGL_NO_SURFACE;
static UILabel *label;
static BOOL paused, renderFailed;
static unsigned suspends, resumes;
static unsigned frames;
static double fps, fpsStart;
static unsigned fpsFrames;
static NSString *renderer = @"unavailable", *version = @"unavailable";

static void UpdateStatus(BOOL passed, NSString *detail, NSDictionary *extra)
{
    label.text = [NSString stringWithFormat:@"ToGLES + ANGLE: %@\n%@\nFrames: %u", passed ? @"PASS" : @"FAIL", detail, frames];
    CGRect statusFrame = label.frame;
    BOOL landscape = label.superview.bounds.size.width > label.superview.bounds.size.height;
    statusFrame.origin.y = landscape ? 10 : 60;
    statusFrame.size.height = passed ? (landscape ? 85 : 140) : 240;
    label.frame = statusFrame;
    if (IsSourceWorldMapLoaded()) {
        BOOL live = getenv("SOURCE_IOS_GAME_STARTUP") &&
            (strcmp(getenv("SOURCE_IOS_GAME_STARTUP"), "play") == 0 || strcmp(getenv("SOURCE_IOS_GAME_STARTUP"), "menu") == 0);
        if (live && passed) {
            label.text = [NSString stringWithFormat:@"Portal • LIVE  %.1f FPS\nFrames: %u", fps, frames];
            label.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];
            CGFloat width = MIN(180, label.superview.bounds.size.width - 200);
            label.frame = CGRectMake((label.superview.bounds.size.width-width)/2,
                                     landscape ? 10 : 60, width, 36);
        }
    }
    if (!passed) NSLog(@"Source iOS: %@", detail);
}

static BOOL EGLFailure(NSString *operation)
{
    UpdateStatus(NO, [NSString stringWithFormat:@"%@: EGL error 0x%x", operation, eglGetError()], @{});
    return NO;
}

static BOOL StartRenderer(void)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC getDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!getDisplay) return EGLFailure(@"eglGetPlatformDisplayEXT unavailable");
    const EGLint attributes[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE};
    display = getDisplay(EGL_PLATFORM_ANGLE_ANGLE, NULL, attributes);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) return EGLFailure(@"eglInitialize(Metal)");
    if (!eglBindAPI(EGL_OPENGL_ES_API)) return EGLFailure(@"eglBindAPI");
    const EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint count = 0;
    if (!eglChooseConfig(display, configAttributes, &config, 1, &count) || count != 1) return EGLFailure(@"eglChooseConfig");
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    if (context == EGL_NO_CONTEXT) return EGLFailure(@"eglCreateContext");
    // ANGLE's Metal window surface accepts the CAMetalLayer owned by SDL.
    surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)SDL_Metal_GetLayer(metalView), NULL);
    if (surface == EGL_NO_SURFACE || !eglMakeCurrent(display, surface, surface, context)) return EGLFailure(@"eglMakeCurrent/window surface");
    renderer = @((const char *)glGetString(GL_RENDERER));
    version = @((const char *)glGetString(GL_VERSION));
    if (![renderer containsString:@"ANGLE"] || ![renderer containsString:@"Metal"]) {
        UpdateStatus(NO, @"Expected ANGLE Metal renderer", @{});
        return NO;
    }
    char detail[4096]={0};
    if (!InitializeToGLESRuntime(detail,sizeof(detail)) ||
        !InitializeIOSFilesystem([NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"engine-assets"].fileSystemRepresentation,detail,sizeof(detail)) ||
        !StartToGLESMaterialLoop([NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Frameworks"].fileSystemRepresentation,detail,sizeof(detail))) {
        UpdateStatus(NO,@(detail),@{}); return NO;
    }
    return YES;
}


static void DrawFrame(void *unused)
{
    @autoreleasepool {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {

            if (event.type==SDL_APP_WILLENTERBACKGROUND) {

            }
            if (event.type == SDL_APP_WILLENTERBACKGROUND) paused = YES;
            if (event.type == SDL_APP_DIDENTERFOREGROUND) paused = NO;
        }
        if (FinishSourceGameQuit()) exit(0);
        if (paused || renderFailed) return;
        EGLint width = 0, height = 0;
        if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
            !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
            renderFailed=YES; EGLFailure(@"eglQuerySurface");
            return;
        }
        if (width <= 0 || height <= 0) return;
        char detail[4096]={0};
        if (!DrawToGLESMaterialLoop(detail,sizeof(detail))) {
            renderFailed=YES;
            UpdateStatus(NO,@(detail),@{});
            return;
        }
        ++frames;
        double presentTime=SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency();
        if (!fpsStart) { fpsStart=presentTime; fpsFrames=0; }
        else ++fpsFrames;
        if (presentTime-fpsStart>=.5) {
            fps=fpsFrames/(presentTime-fpsStart);
            fpsStart=presentTime; fpsFrames=0;
            if (IsSourceWorldMapLoaded() && getenv("SOURCE_IOS_GAME_STARTUP") &&
                (!strcmp(getenv("SOURCE_IOS_GAME_STARTUP"),"play") || !strcmp(getenv("SOURCE_IOS_GAME_STARTUP"),"menu")))
                label.text=[NSString stringWithFormat:@"Portal • LIVE  %.1f FPS\nFrames: %u",fps,frames];
        }
        if (frames%120==0) UpdateStatus(YES,@(detail),
            @{@"pixel_width": @(width), @"pixel_height": @(height), @"gl_error": @0, @"fps": @(fps),
              @"swap_succeeded": @YES, @"material_loop": @YES, @"suspends": @(suspends), @"resumes": @(resumes)});
        return;

    }
}

static void StopGraphics(void)
{
    if (window) SDL_iPhoneSetAnimationCallback(window, 0, NULL, NULL);
    StopToGLESMaterialLoop();
    ShutdownToGLESBackend();
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        eglTerminate(display);
    }
    if (metalView) SDL_Metal_DestroyView(metalView);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
#if TARGET_OS_SIMULATOR
    [keyboardInput releaseKeys]; keyboardInput=nil;
#endif
    window = NULL; metalView = NULL; label = nil;
    display = EGL_NO_DISPLAY; context = EGL_NO_CONTEXT; surface = EGL_NO_SURFACE;
    paused = renderFailed = NO;
    suspends = resumes = 0; fps=fpsStart=0; fpsFrames=0;

}

static void StartGraphics(UIWindowScene *scene)
{
    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY,"playback");
    NSDictionary *settings = [NSDictionary dictionaryWithContentsOfFile:
        [NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"ios-launch.plist"]];
    for (NSString *key in settings) {
        NSString *value = settings[key];
        if ([key isEqualToString:@"SOURCE_IOS_GAME_ROOT"] && [value hasPrefix:@"@documents/"]) {
            NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
            value = [documents stringByAppendingPathComponent:[value substringFromIndex:11]];
        }
        if (!getenv(key.UTF8String)) setenv(key.UTF8String, value.UTF8String, 1);
    }
    SDL_SetMainReady();
    // The engine routes native fingers to VGUI itself. SDL mouse emulation
    // would deliver a second press/release for the same menu interaction.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait LandscapeLeft LandscapeRight");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        UpdateStatus(NO, @(SDL_GetError()), @{}); return;
    }
    window = SDL_CreateWindow("Portal", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        640, 480, SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) { UpdateStatus(NO, @(SDL_GetError()), @{}); return; }
    SDL_SysWMinfo native = {0};
    SDL_VERSION(&native.version);
    if (!SDL_GetWindowWMInfo(window, &native) || native.subsystem != SDL_SYSWM_UIKIT) {
        UpdateStatus(NO, @"SDL did not expose a UIKit window", @{}); return;
    }
    native.info.uikit.window.windowScene = scene;
    [native.info.uikit.window makeKeyAndVisible];
    metalView = SDL_Metal_CreateView(window);
    if (!metalView) { UpdateStatus(NO, @(SDL_GetError()), @{}); return; }
    UIView *view = (__bridge UIView *)metalView;
    label = [[UILabel alloc] initWithFrame:CGRectMake(20, 60, view.bounds.size.width - 40, 140)];
    label.frame = CGRectMake(20, 60, view.bounds.size.width - 40, 360);
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    label.numberOfLines = 0;
    label.textColor = UIColor.whiteColor;
    label.backgroundColor = [UIColor.blackColor colorWithAlphaComponent:0.7];
    label.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightMedium];
    label.text = @"SDL + ANGLE / Metal\nVerifying textured GLES output…";
    [view addSubview:label];
#if TARGET_OS_SIMULATOR
    keyboardInput=[[SourceKeyboardInput alloc] initWithFrame:CGRectMake(0,0,0,0)];
    [view addSubview:keyboardInput];
    [keyboardInput becomeFirstResponder];
#endif
    if (!StartRenderer()) return;
    if (SDL_iPhoneSetAnimationCallback(window, 1, DrawFrame, NULL) != 0)
        UpdateStatus(NO, @(SDL_GetError()), @{});
}

@interface SourceScene : UIResponder <UIWindowSceneDelegate>
@end
@implementation SourceScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options
{ StartGraphics((UIWindowScene *)scene); }
- (void)sceneWillResignActive:(UIScene *)scene {
    paused = YES; ++suspends; fpsStart=0; fpsFrames=0;
    SetSourceGameAudioActive(0);
#if TARGET_OS_SIMULATOR
    [keyboardInput releaseKeys];
#endif
    if (display != EGL_NO_DISPLAY) eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
}
- (void)sceneDidBecomeActive:(UIScene *)scene {
    if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
        eglMakeCurrent(display,surface,surface,context);
    paused = NO; ++resumes;
    SetSourceGameAudioActive(1);
#if TARGET_OS_SIMULATOR
    [keyboardInput becomeFirstResponder];
#endif
}
- (void)sceneDidDisconnect:(UIScene *)scene { StopGraphics(); }
@end

@interface ANGLEApp : UIResponder <UIApplicationDelegate>
@end
@implementation ANGLEApp
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options
{
    UISceneConfiguration *configuration = [[UISceneConfiguration alloc] initWithName:@"Source" sessionRole:session.role];
    configuration.delegateClass = SourceScene.class;
    return configuration;
}
@end

int main(int argc, char **argv)
{
    @autoreleasepool {
        NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        NSString *logPath = [documents stringByAppendingPathComponent:@"Portal.log"];
        FILE *log = fopen(logPath.fileSystemRepresentation, "w");
        if (log) {
            dup2(fileno(log), STDOUT_FILENO);
            dup2(fileno(log), STDERR_FILENO);
            fclose(log);
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
        }
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(ANGLEApp.class));
    }
}
