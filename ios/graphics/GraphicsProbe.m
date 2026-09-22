#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "SDL_metal.h"
#include "SDL_syswm.h"
#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

static SDL_Window *window;
static SDL_MetalView metalView;
static CAMetalLayer *layer;
static id<MTLCommandQueue> queue;
static id<MTLRenderPipelineState> pipeline;
static UILabel *label;
static NSUInteger frames;
static BOOL finished;
static BOOL paused;
static NSUInteger touches;

static void SaveResult(BOOL passed, NSString *detail, NSDictionary *extra)
{
    finished = YES;
    label.text = [NSString stringWithFormat:@"SDL + Metal: %@\n%@\nTouch the screen to test input", passed ? @"PASS" : @"FAIL", detail];
    NSMutableDictionary *result = [@{@"passed": @(passed), @"detail": detail,
        @"system": UIDevice.currentDevice.systemVersion, @"frames": @(frames),
        @"touch_events": @(touches), @"renderer": @"Metal", @"device": layer.device.name ?: @"none"} mutableCopy];
    [result addEntriesFromDictionary:extra];
    NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSError *error = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:&error];
    if (!data || ![data writeToURL:[documents URLByAppendingPathComponent:@"graphics.json"] options:NSDataWritingAtomic error:&error])
        NSLog(@"Graphics result write failed: %@", error);
    NSLog(@"Graphics probe %@: %@", passed ? @"PASS" : @"FAIL", detail);
}

static BOOL NearByte(unsigned char actual, int expected)
{
    return abs((int)actual - expected) <= 2;
}

static void DrawFrame(void *unused)
{
    @autoreleasepool {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_APP_WILLENTERBACKGROUND)
                paused = YES;
            if (event.type == SDL_APP_DIDENTERFOREGROUND)
                paused = NO;
            if (event.type == SDL_FINGERDOWN) {
                ++touches;
                label.text = [NSString stringWithFormat:@"SDL + Metal\nTouch events: %lu\nx: %.3f  y: %.3f", (unsigned long)touches, event.tfinger.x, event.tfinger.y];
            }
        }
        if (paused)
            return;
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (!drawable)
            return;
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.05, 0.1, 0.2, 1.0);
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:pipeline];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];

        // Validate the drawable's actual GPU output once, after several frames.
        // Reuse the same rendered texture for presentation and readback.
        BOOL verify = !finished && frames >= 3;
        NSUInteger width = drawable.texture.width, height = drawable.texture.height;
        NSUInteger rowBytes = (width * 4 + 255) & ~255UL;
        id<MTLBuffer> pixels = nil;
        if (verify) {
            pixels = [layer.device newBufferWithLength:rowBytes * height options:MTLResourceStorageModeShared];
            if (!pixels) {
                SaveResult(NO, @"GPU readback buffer allocation failed", @{});
                return;
            }
            id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
            [blit copyFromTexture:drawable.texture sourceSlice:0 sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(width, height, 1)
                        toBuffer:pixels destinationOffset:0 destinationBytesPerRow:rowBytes
                        destinationBytesPerImage:rowBytes * height];
            [blit endEncoding];
        }
        [commands presentDrawable:drawable];
        [commands commit];
        ++frames;
        if (verify) {
            [commands waitUntilCompleted];
            if (commands.status == MTLCommandBufferStatusError) {
                SaveResult(NO, commands.error.localizedDescription, @{});
                return;
            }
            unsigned char *corner = pixels.contents;
            unsigned char *center = corner + rowBytes * (height / 2) + (width / 2) * 4;
            BOOL valid = NearByte(corner[0], 51) && NearByte(corner[1], 26) && NearByte(corner[2], 13) && corner[3] == 255 &&
                         NearByte(center[0], 77) && NearByte(center[1], 217) && NearByte(center[2], 26) && center[3] == 255;
            int pointsW, pointsH;
            SDL_GetWindowSize(window, &pointsW, &pointsH);
            NSString *detail = [NSString stringWithFormat:@"%lux%lu pixels (%dx%d points)\nGPU pixel readback: %@", (unsigned long)width, (unsigned long)height, pointsW, pointsH, valid ? @"PASS" : @"FAIL"];
            SaveResult(valid, detail, @{@"pixel_width": @(width), @"pixel_height": @(height),
                @"point_width": @(pointsW), @"point_height": @(pointsH),
                @"center_bgra": @[@(center[0]), @(center[1]), @(center[2]), @(center[3])],
                @"corner_bgra": @[@(corner[0]), @(corner[1]), @(corner[2]), @(corner[3])]});
        }
    }
}

static int StartGraphics(UIWindowScene *scene)
{
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait LandscapeLeft LandscapeRight");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SaveResult(NO, @(SDL_GetError()), @{});
        return 1;
    }
    window = SDL_CreateWindow("Source graphics probe", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              640, 480, SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) {
        SaveResult(NO, @(SDL_GetError()), @{});
        return 1;
    }
    // The vendored SDL predates UIScene. Adopt its public native window in our
    // scene rather than using SDL's UIApplicationDelegate or patching SDL.
    SDL_SysWMinfo native = {0};
    SDL_VERSION(&native.version);
    if (!SDL_GetWindowWMInfo(window, &native) || native.subsystem != SDL_SYSWM_UIKIT) {
        SaveResult(NO, @"SDL did not expose a UIKit window", @{});
        return 1;
    }
    native.info.uikit.window.windowScene = scene;
    [native.info.uikit.window makeKeyAndVisible];
    if (!(metalView = SDL_Metal_CreateView(window))) {
        SaveResult(NO, @(SDL_GetError()), @{});
        return 1;
    }
    layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(metalView);
    layer.device = MTLCreateSystemDefaultDevice();
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO; // The probe reads its drawable back for verification.
    queue = [layer.device newCommandQueue];
    if (!queue) {
        SaveResult(NO, @"No Metal device/command queue", @{});
        return 1;
    }
    NSString *shader = @"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 probe_vertex(uint i [[vertex_id]]) {"
        "const float2 p[3] = {float2(-0.8,-0.7),float2(0.8,-0.7),float2(0,0.7)};"
        "return float4(p[i],0,1); }\n"
        "fragment float4 probe_fragment() { return float4(0.1,0.85,0.3,1); }";
    NSError *error = nil;
    id<MTLLibrary> library = [layer.device newLibraryWithSource:shader options:nil error:&error];
    if (!library) {
        SaveResult(NO, error.localizedDescription ?: @"Metal shader compilation failed", @{});
        return 1;
    }
    MTLRenderPipelineDescriptor *description = [MTLRenderPipelineDescriptor new];
    description.vertexFunction = [library newFunctionWithName:@"probe_vertex"];
    description.fragmentFunction = [library newFunctionWithName:@"probe_fragment"];
    description.colorAttachments[0].pixelFormat = layer.pixelFormat;
    pipeline = [layer.device newRenderPipelineStateWithDescriptor:description error:&error];
    if (!pipeline) {
        SaveResult(NO, error.localizedDescription, @{});
        return 1;
    }
    UIView *view = (__bridge UIView *)metalView;
    label = [[UILabel alloc] initWithFrame:CGRectMake(20, 60, view.bounds.size.width - 40, 140)];
    label.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    label.numberOfLines = 0;
    label.textColor = UIColor.whiteColor;
    label.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightMedium];
    label.text = @"SDL + Metal\nVerifying GPU output…";
    [view addSubview:label];
    if (SDL_iPhoneSetAnimationCallback(window, 1, DrawFrame, NULL) != 0) {
        SaveResult(NO, @(SDL_GetError()), @{});
        return 1;
    }
    return 0; // UIKit owns the run loop; SDL's display link calls DrawFrame.
}

@interface GraphicsScene : UIResponder <UIWindowSceneDelegate>
@end
@implementation GraphicsScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options
{
    StartGraphics((UIWindowScene *)scene);
}
- (void)sceneWillResignActive:(UIScene *)scene { paused = YES; }
- (void)sceneDidBecomeActive:(UIScene *)scene { paused = NO; }
- (void)sceneDidDisconnect:(UIScene *)scene
{
    if (window) SDL_iPhoneSetAnimationCallback(window, 0, NULL, NULL);
    if (metalView) SDL_Metal_DestroyView(metalView);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    window = NULL;
    metalView = NULL;
    layer = nil;
    queue = nil;
    pipeline = nil;
    label = nil;
    frames = 0;
    finished = NO;
    touches = 0;
}
@end

@interface GraphicsApp : UIResponder <UIApplicationDelegate>
@end
@implementation GraphicsApp
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options
{
    UISceneConfiguration *configuration = [[UISceneConfiguration alloc] initWithName:@"Graphics" sessionRole:session.role];
    configuration.delegateClass = GraphicsScene.class;
    return configuration;
}
@end

int main(int argc, char **argv)
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(GraphicsApp.class));
    }
}
