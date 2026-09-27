#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_metal.h"
#include "SDL_syswm.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl3.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>

#ifdef SOURCE_TOGLES_PROBE
#include "ToGLESChecks.h"
#include "ToGLESRuntime.h"
static NSString *engineDetail = @"not run";
static BOOL engineChecksPassed;
static NSString *runtimeDetail = @"not run";
#define PROBE_RESULT_NAME @"togles.json"
#else
#define PROBE_RESULT_NAME @"angle.json"
#endif

static SDL_Window *window;
static SDL_MetalView metalView;
static EGLDisplay display = EGL_NO_DISPLAY;
static EGLContext context = EGL_NO_CONTEXT;
static EGLSurface surface = EGL_NO_SURFACE;
static GLuint program, texture, vao;
static UILabel *label;
static BOOL finished, paused, renderFailed;
static unsigned suspends, resumes;
static unsigned frames;
static NSString *renderer = @"unavailable", *version = @"unavailable";

static void SaveResult(BOOL passed, NSString *detail, NSDictionary *extra)
{
    finished = YES;
    label.text = [NSString stringWithFormat:@"SDL + ANGLE / Metal: %@\n%@", passed ? @"PASS" : @"FAIL", detail];
#ifdef SOURCE_TOGLES_PROBE
    label.text = [NSString stringWithFormat:@"ToGLES + ANGLE: %@\n%@\nFrames: %u", passed ? @"PASS" : @"FAIL", detail, frames];
    CGRect statusFrame = label.frame;
    BOOL landscape = label.superview.bounds.size.width > label.superview.bounds.size.height;
    statusFrame.origin.y = landscape ? 10 : 60;
    statusFrame.size.height = passed ? (landscape ? 85 : 140) : 240;
    label.frame = statusFrame;
#endif
    NSMutableDictionary *result = [@{@"passed": @(passed), @"detail": detail,
        @"system": UIDevice.currentDevice.systemVersion, @"frames": @(frames),
        @"renderer": renderer, @"gl_version": version} mutableCopy];
    [result addEntriesFromDictionary:extra];
#ifdef SOURCE_TOGLES_PROBE
    result[@"compiled_shaders"] = @([NSFileManager.defaultManager fileExistsAtPath:
        [NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"probe-assets/compiled-shaders.json"]]);
    result[@"engine_checks"] = engineDetail;
    result[@"runtime_checks"] = runtimeDetail;
    result[@"engine_checks_passed"] = @(engineChecksPassed);
#endif
    NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSError *error = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:&error];
    if (!data || ![data writeToURL:[documents URLByAppendingPathComponent:PROBE_RESULT_NAME] options:NSDataWritingAtomic error:&error])
        NSLog(@"ANGLE result write failed: %@", error);
    NSLog(@"ANGLE probe %@: %@", passed ? @"PASS" : @"FAIL", detail);
}

static BOOL EGLFailure(NSString *operation)
{
    SaveResult(NO, [NSString stringWithFormat:@"%@: EGL error 0x%x", operation, eglGetError()], @{});
    return NO;
}

static GLuint Compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[4096] = {0};
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        SaveResult(NO, [NSString stringWithFormat:@"GLSL compile failed: %s", log], @{});
        glDeleteShader(shader);
        return 0;
    }
    return shader;
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
        SaveResult(NO, @"Expected ANGLE Metal renderer", @{});
        return NO;
    }
    const char *vertexSource = "#version 300 es\n"
        "out vec2 uv;\n"
        "void main() { vec2 p[3] = vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));"
        "gl_Position=vec4(p[gl_VertexID],0,1); uv=(p[gl_VertexID]+1.0)*0.5; }";
    const char *fragmentSource = "#version 300 es\nprecision highp float;\n"
        "in vec2 uv; uniform sampler2D checker; out vec4 color;"
        "void main() { color=texture(checker,uv); }";
    GLuint vertex = Compile(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return NO;
    }
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[4096] = {0};
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        SaveResult(NO, [NSString stringWithFormat:@"GLSL link failed: %s", log], @{});
        return NO;
    }
    // Four distinct texels: bottom red/green, top blue/yellow (GL coordinates).
    const GLubyte texels[] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255};
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "checker"), 0);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glDisable(GL_DITHER);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        SaveResult(NO, [NSString stringWithFormat:@"GLES setup error 0x%x", error], @{});
        return NO;
    }
#ifdef SOURCE_TOGLES_PROBE
    char detail[4096] = {0};
    NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    if (!InitializeToGLESRuntime(detail, sizeof(detail))) {
        engineDetail = @(detail); SaveResult(NO, engineDetail, @{}); return NO;
    }
    NSString *assets = [NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"probe-assets"];
    NSString *writable = [documents.path stringByAppendingPathComponent:@"engine-probe"];
    for (int cycle=0; cycle<2; ++cycle) {
        if (!CheckToGLESFilesystem(assets.fileSystemRepresentation, writable.fileSystemRepresentation, detail, sizeof(detail))) {
            engineDetail = @(detail); SaveResult(NO, engineDetail, @{}); ShutdownToGLESBackend(); return NO;
        }
    }
    runtimeDetail = @(detail);
    engineChecksPassed = RunToGLESChecks(documents.path.fileSystemRepresentation, detail, sizeof(detail));
    engineDetail = @(detail);
    if (engineChecksPassed) {
        engineChecksPassed = CheckToGLESUploads(detail, sizeof(detail));
        runtimeDetail = [runtimeDetail stringByAppendingFormat:@"\n%s", detail];
    }
    if (engineChecksPassed) {
        engineChecksPassed = CheckToGLESObjects(detail, sizeof(detail), [NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Frameworks"].fileSystemRepresentation);
        runtimeDetail = [runtimeDetail stringByAppendingFormat:@"\n%s", detail];
    }
    if (engineChecksPassed) engineChecksPassed = StartToGLESMaterialLoop([NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Frameworks"].fileSystemRepresentation, detail, sizeof(detail));
    if (!engineChecksPassed) { SaveResult(NO, @(detail), @{}); return NO; }
#endif
    return YES;
}

static void DrawFrame(void *unused)
{
    @autoreleasepool {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_APP_WILLENTERBACKGROUND) paused = YES;
            if (event.type == SDL_APP_DIDENTERFOREGROUND) paused = NO;
        }
        if (paused || renderFailed) return;
        EGLint width = 0, height = 0;
        if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
            !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
            renderFailed=YES; EGLFailure(@"eglQuerySurface");
            return;
        }
        if (width <= 0 || height <= 0) return;
#ifdef SOURCE_TOGLES_PROBE
        char detail[4096]={0};
        if (!DrawToGLESMaterialLoop(detail,sizeof(detail))) {
            renderFailed=YES;
            SaveResult(NO,@(detail),@{});
            return;
        }
        ++frames;
        if (frames%120==0) SaveResult(YES,@(detail),
            @{@"pixel_width": @(width), @"pixel_height": @(height), @"gl_error": @0,
              @"swap_succeeded": @YES, @"material_loop": @YES, @"suspends": @(suspends), @"resumes": @(resumes)});
        return;
#endif
        glViewport(0, 0, width, height);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        BOOL verify = !finished && frames >= 3;
        BOOL valid = YES;
        NSMutableArray *samples = [NSMutableArray array];
        if (verify) {
            const GLubyte expected[4][4] = {{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}};
            for (int i = 0; i < 4; ++i) {
                GLubyte pixel[4] = {0};
                glReadPixels(width * (i % 2 ? 3 : 1) / 4, height * (i / 2 ? 3 : 1) / 4,
                    1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                for (int c = 0; c < 4; ++c)
                    valid &= abs((int)pixel[c] - expected[i][c]) <= 1;
                [samples addObject:@[@(pixel[0]), @(pixel[1]), @(pixel[2]), @(pixel[3])]];
            }
        }
        GLenum error = glGetError();
        if (!eglSwapBuffers(display, surface)) {
            if (!finished) EGLFailure(@"eglSwapBuffers");
            return;
        }
        ++frames;
        if (verify) {
            valid &= error == GL_NO_ERROR;
            SaveResult(valid, [NSString stringWithFormat:@"%dx%d pixels\nGLSL + texture + readback: %@", width, height, valid ? @"PASS" : @"FAIL"],
                @{@"pixel_width": @(width), @"pixel_height": @(height),
                  @"samples_rgba_bottom_left_bottom_right_top_left_top_right": samples,
                  @"gl_error": @(error), @"swap_succeeded": @YES});
        } else if (error != GL_NO_ERROR && !finished) {
            SaveResult(NO, [NSString stringWithFormat:@"GLES draw error 0x%x", error], @{});
        }
    }
}

static void StopGraphics(void)
{
    if (window) SDL_iPhoneSetAnimationCallback(window, 0, NULL, NULL);
#ifdef SOURCE_TOGLES_PROBE
    StopToGLESMaterialLoop();
    ShutdownToGLESBackend();
#endif
    if (display != EGL_NO_DISPLAY) {
        if (context != EGL_NO_CONTEXT && eglGetCurrentContext() == context) {
            glDeleteVertexArrays(1, &vao);
            glDeleteTextures(1, &texture);
            glDeleteProgram(program);
        }
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        eglTerminate(display);
    }
    if (metalView) SDL_Metal_DestroyView(metalView);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    window = NULL; metalView = NULL; label = nil;
    display = EGL_NO_DISPLAY; context = EGL_NO_CONTEXT; surface = EGL_NO_SURFACE;
    program = texture = vao = frames = 0;
    finished = paused = renderFailed = NO;
    suspends = resumes = 0;
#ifdef SOURCE_TOGLES_PROBE
    engineDetail = @"not run";
    runtimeDetail = @"not run";
    engineChecksPassed = NO;
#endif
}

static void StartGraphics(UIWindowScene *scene)
{
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait LandscapeLeft LandscapeRight");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SaveResult(NO, @(SDL_GetError()), @{}); return;
    }
    window = SDL_CreateWindow("Source ANGLE probe", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        640, 480, SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) { SaveResult(NO, @(SDL_GetError()), @{}); return; }
    SDL_SysWMinfo native = {0};
    SDL_VERSION(&native.version);
    if (!SDL_GetWindowWMInfo(window, &native) || native.subsystem != SDL_SYSWM_UIKIT) {
        SaveResult(NO, @"SDL did not expose a UIKit window", @{}); return;
    }
    native.info.uikit.window.windowScene = scene;
    [native.info.uikit.window makeKeyAndVisible];
    metalView = SDL_Metal_CreateView(window);
    if (!metalView) { SaveResult(NO, @(SDL_GetError()), @{}); return; }
    UIView *view = (__bridge UIView *)metalView;
    label = [[UILabel alloc] initWithFrame:CGRectMake(20, 60, view.bounds.size.width - 40, 140)];
#ifdef SOURCE_TOGLES_PROBE
    label.frame = CGRectMake(20, 60, view.bounds.size.width - 40, 360);
#endif
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    label.numberOfLines = 0;
    label.textColor = UIColor.whiteColor;
    label.backgroundColor = [UIColor.blackColor colorWithAlphaComponent:0.7];
    label.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightMedium];
    label.text = @"SDL + ANGLE / Metal\nVerifying textured GLES output…";
    [view addSubview:label];
    if (!StartRenderer()) return;
    if (SDL_iPhoneSetAnimationCallback(window, 1, DrawFrame, NULL) != 0)
        SaveResult(NO, @(SDL_GetError()), @{});
}

@interface ANGLEScene : UIResponder <UIWindowSceneDelegate>
@end
@implementation ANGLEScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options
{ StartGraphics((UIWindowScene *)scene); }
- (void)sceneWillResignActive:(UIScene *)scene {
    paused = YES; ++suspends;
    if (display != EGL_NO_DISPLAY) eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
}
- (void)sceneDidBecomeActive:(UIScene *)scene {
    if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
        eglMakeCurrent(display,surface,surface,context);
    paused = NO; ++resumes;
}
- (void)sceneDidDisconnect:(UIScene *)scene { StopGraphics(); }
@end

@interface ANGLEApp : UIResponder <UIApplicationDelegate>
@end
@implementation ANGLEApp
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options
{
    UISceneConfiguration *configuration = [[UISceneConfiguration alloc] initWithName:@"ANGLE" sessionRole:session.role];
    configuration.delegateClass = ANGLEScene.class;
    return configuration;
}
@end

int main(int argc, char **argv)
{
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(ANGLEApp.class)); }
}
