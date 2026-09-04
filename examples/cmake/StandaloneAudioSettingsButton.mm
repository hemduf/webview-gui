#import <Cocoa/Cocoa.h>

namespace {
constexpr CGFloat kHorizontalPadding = 6.0;
}

@interface WebviewGuiStandaloneAudioSettingsButtonInstaller : NSObject
@end

@implementation WebviewGuiStandaloneAudioSettingsButtonInstaller

+ (void)load
{
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(applicationDidFinishLaunching:)
               name:NSApplicationDidFinishLaunchingNotification
             object:nil];
}

+ (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    [self installButton];
}

+ (void)installButton
{
    id delegate = [NSApp delegate];
    if (delegate == nil || ![delegate respondsToSelector:@selector(openAudioSettingsWindow:)])
        return;

    NSWindow *window = nil;
    @try
    {
        window = [delegate valueForKey:@"window"];
    }
    @catch (NSException *)
    {
        return;
    }
    if (window == nil)
        return;

    NSButton *button = [NSButton buttonWithTitle:@"Audio / MIDI"
                                          target:delegate
                                          action:@selector(openAudioSettingsWindow:)];
    [button sizeToFit];

    NSRect buttonFrame = [button frame];
    buttonFrame.origin.x = kHorizontalPadding;
    buttonFrame.origin.y = 0.0;
    [button setFrame:buttonFrame];

    NSView *container = [[NSView alloc]
        initWithFrame:NSMakeRect(0.0,
                                 0.0,
                                 buttonFrame.size.width + (2.0 * kHorizontalPadding),
                                 buttonFrame.size.height)];
    [container addSubview:button];

    NSTitlebarAccessoryViewController *accessory =
        [[NSTitlebarAccessoryViewController alloc] init];
    [accessory setView:container];
    [accessory setLayoutAttribute:NSLayoutAttributeRight];
    [window addTitlebarAccessoryViewController:accessory];

#if !__has_feature(objc_arc)
    [accessory release];
    [container release];
#endif

    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:NSApplicationDidFinishLaunchingNotification
                                                  object:nil];
}

@end
