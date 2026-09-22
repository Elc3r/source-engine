#import <UIKit/UIKit.h>
#include <dlfcn.h>
#include <math.h>
#include "FoundationChecks.h"

static void CollectCheck(const char *name, int passed, const char *detail, void *context)
{
    NSMutableArray *checks = (__bridge NSMutableArray *)context;
    [checks addObject:@{@"name": @(name), @"passed": passed ? @YES : @NO, @"detail": @(detail)}];
    NSLog(@"Foundation %s: %s — %s", name, passed ? "PASS" : "FAIL", detail);
}

// This launcher deliberately does not link tier0: loading it exercises the same
// signed-bundle boundary that the engine's other modules will use later.
@interface BootstrapScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

@implementation BootstrapScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session
      options:(UISceneConnectionOptions *)options
{
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UIViewController *controller = [UIViewController new];
    controller.view.backgroundColor = UIColor.systemBackgroundColor;
    UITextView *status = [UITextView new];
    status.editable = NO;
    status.font = [UIFont monospacedSystemFontOfSize:17 weight:UIFontWeightRegular];
    status.translatesAutoresizingMaskIntoConstraints = NO;
    [controller.view addSubview:status];
    UILayoutGuide *safe = controller.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [status.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:20],
        [status.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-20],
        [status.topAnchor constraintEqualToAnchor:safe.topAnchor constant:20],
        [status.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-20]
    ]];
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];

    NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                          inDomains:NSUserDomainMask].firstObject;
    NSURL *logURL = [documents URLByAppendingPathComponent:@"bootstrap.json"];
    NSMutableArray *checks = [NSMutableArray new];
    void (^finish)(BOOL, NSString *) = ^(BOOL passed, NSString *detail) {
        status.text = [NSString stringWithFormat:@"Source Engine\niOS bootstrap\n\n%@\n\n%@",
                       passed ? @"PASS" : @"FAIL", detail];
        NSDictionary *result = @{@"passed": @(passed), @"detail": detail,
                                 @"system": UIDevice.currentDevice.systemVersion,
                                 @"schema_version": @2, @"checks": checks};
        NSError *error = nil;
        NSData *data = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:&error];
        if (!data || ![data writeToURL:logURL options:NSDataWritingAtomic error:&error]) {
            status.text = [status.text stringByAppendingFormat:@"\nLog write failed: %@", error];
        }
        NSLog(@"Source bootstrap %@: %@", passed ? @"PASS" : @"FAIL", detail);
    };

    NSString *library = [NSBundle.mainBundle.privateFrameworksPath stringByAppendingPathComponent:@"libtier0.dylib"];
    // Keep the module loaded for the lifetime of the app, including its globals.
    void *module = dlopen(library.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        finish(NO, [NSString stringWithUTF8String:dlerror()]);
        return;
    }
    double (*clockFunction)(void) = (double (*)(void))dlsym(module, "Plat_FloatTime");
    void (*message)(const char *, ...) = (void (*)(const char *, ...))dlsym(module, "Msg");
    if (!clockFunction || !message) {
        finish(NO, @"tier0 loaded, but required exports are missing.");
        return;
    }
    message("iOS: tier0 loaded from the application bundle.\n");
    double start = clockFunction();
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        double elapsed = clockFunction() - start;
        BOOL passed = isfinite(elapsed) && elapsed > 0;
        NSString *timerDetail = [NSString stringWithFormat:@"Plat_FloatTime: %.6f s", elapsed];
        [checks addObject:@{@"name": @"tier0_load_and_timer", @"passed": @(passed), @"detail": timerDetail}];
        if (!passed) {
            finish(NO, timerDetail);
            return;
        }
        status.text = @"Source Engine\niOS bootstrap\n\nRunning foundation checks…";
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            NSString *probePath = [NSBundle.mainBundle.privateFrameworksPath stringByAppendingPathComponent:@"libfoundation_checks.dylib"];
            void *probe = dlopen(probePath.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
            if (!probe) {
                NSString *error = [NSString stringWithUTF8String:dlerror()];
                dispatch_async(dispatch_get_main_queue(), ^{ finish(NO, error); });
                return;
            }
            FoundationRun run = (FoundationRun)dlsym(probe, "Source_RunFoundationChecks");
            if (!run) {
                dispatch_async(dispatch_get_main_queue(), ^{ finish(NO, @"Foundation entry point missing."); });
                return;
            }
            BOOL allPassed = run(CollectCheck, (__bridge void *)checks) != 0;
            dispatch_async(dispatch_get_main_queue(), ^{
                NSMutableString *summary = [NSMutableString new];
                for (NSDictionary *check in checks)
                    [summary appendFormat:@"%@: %@\n", check[@"name"], [check[@"passed"] boolValue] ? @"PASS" : @"FAIL"];
                [summary appendString:@"\nLog: Documents/bootstrap.json"];
                finish(allPassed, summary);
            });
        });
    });
}
@end

@interface BootstrapApp : UIResponder <UIApplicationDelegate>
@end
@implementation BootstrapApp
- (UISceneConfiguration *)application:(UIApplication *)application
    configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options
{
    UISceneConfiguration *configuration = [[UISceneConfiguration alloc] initWithName:@"Bootstrap" sessionRole:session.role];
    configuration.delegateClass = BootstrapScene.class;
    return configuration;
}
@end

int main(int argc, char **argv)
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(BootstrapApp.class));
    }
}
