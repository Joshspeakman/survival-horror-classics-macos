/* Survival Horror Classics launch window: one app for the natively recompiled Resident Evil, Resident Evil 2,
 * Resident Evil 3: Nemesis and Dino Crisis (the player's own GOG copies). A card per game (its GOG icon, from the game's
 * folder at install time), the chosen game's versions under it (Resident Evil 2: Leon or Claire; Resident Evil 3: the
 * game or The Mercenaries), a row of tiles (Options, Mods, Saves, Game Files, Updates) and Quit / Play. The look is drawn
 * here: a dark field, a slow red bloom behind the chosen card, typewriter captions.
 *
 * This is the app's executable (Contents/MacOS/SurvivalHorrorClassics). Play opens a fresh instance of the app that
 * execs the chosen game's binary beside it (ResidentEvil, ResidentEvil2Leon, ResidentEvil2Claire, ResidentEvil3,
 * ResidentEvil3Mercenaries, DinoCrisis), so the app in the Dock is the game and Game Mode stays on. The game is told:
 *   DS_NO_LAUNCHER=1, DS_DISPLAY=window|fullscreen, DS_NOTCH=below|around, DSR_VSYNC=1|0   main.m
 *   DSR_ANISO=16|4|1                     renderer.m (texture filtering)
 *   RE_NO_MOVIES=1, GTA_NO_MOVIES=1      the movies skipped (amstream.inc, dshow.c)
 *   DS_ARGS="..."                        the game's command line: Resident Evil 2's display mode (-mode0) and its GOG
 *                                        Test Aids (SHC_AIDS=1: re_aids.c; Resident Evil 2 also -wepall)
 *   GTA_MODS=<folders>                   the game's enabled mods, first wins (the Mods window: horror_mods.m; the runtime
 *                                        reads them in place of the game's files: files.c, re_mods.c)
 *   DS_LOG_FILE=<data>/<BIN>.log
 * The games stay where they were installed from (by default the Expansion Pak SD card): a game whose folder is on a
 * volume that is not mounted says so and does not play. Saves and settings: ~/Games/SurvivalHorrorClassics/<id>.
 * install.sh writes Contents/Resources/games.plist (each version's binary, executable, game and data folders, commit).
 * The choices are kept in ~/Games/SurvivalHorrorClassics/launcher.plist.
 * Dressed as the games are: the chosen game's own title screen (captured at install) and GOG's screenshots drifting and
 * crossfading behind the window, fog rolling, a cold rain, a light flickering; the game's own title keyed out of its
 * title screen; its box art in a frame with blood-red brackets; smoked panels; Play's glow breathing. The still part is
 * drawn once into a picture and again only when it changes; the timer runs at 60 fps. Art also: <id>-shot-<n>.jpg and
 * <id>-store-portrait.jpg (tools/shc_art.py --store: GOG's screenshots and box). The home folder is $HOME's.
 * Development: SHC_LAUNCHER_SHOT=<png> renders the window to an image and exits (SHC_LAUNCHER_GAME=0..3 the card,
 * SHC_LAUNCHER_OPEN=1 the options open, SHC_LAUNCHER_TIME=<s> the clock, SHC_LAUNCHER_TIMING=1 what a frame costs);
 * SHC_LAUNCHER_DRY=1 prints what Play would run. */
#import <Cocoa/Cocoa.h>
#import <CoreImage/CoreImage.h>
#import <GameController/GameController.h>
#import <QuartzCore/QuartzCore.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static NSColor *rgb(double r, double g, double b, double a) { return [NSColor colorWithSRGBRed:r green:g blue:b alpha:a]; }
static NSColor *blood(void) { return rgb(0.76, 0.09, 0.11, 1); }
static NSColor *paper(void) { return rgb(0.93, 0.90, 0.84, 1); }
static NSColor *ash(void) { return rgb(0.56, 0.54, 0.52, 1); }
static NSFont *type(CGFloat s, BOOL bold) { return [NSFont fontWithName:bold ? @"AmericanTypewriter-Semibold" : @"AmericanTypewriter" size:s] ?: [NSFont monospacedSystemFontOfSize:s weight:bold ? NSFontWeightSemibold : NSFontWeightRegular]; }
static void ktext(NSString *s, NSFont *f, NSColor *c, CGFloat kern, NSRect r, NSTextAlignment al)   /* letter-spaced, one line */
{
    NSMutableParagraphStyle *p = [NSMutableParagraphStyle new]; p.alignment = al; p.lineBreakMode = NSLineBreakByTruncatingTail;
    [s drawInRect:r withAttributes:@{NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSParagraphStyleAttributeName: p, NSKernAttributeName: @(kern)}];
}
static NSFont *title_font(CGFloat s) { return [NSFont fontWithName:@"Didot-Bold" size:s] ?: [NSFont fontWithName:@"Georgia-Bold" size:s] ?: [NSFont boldSystemFontOfSize:s]; }
static NSString *user_home(void) { const char *h = getenv("HOME"); return h && *h ? @(h) : NSHomeDirectory(); }   /* $HOME (tests point it elsewhere) */
static NSString *home_dir(void) { return getenv("SHC_LAUNCHER_HOME") ? @(getenv("SHC_LAUNCHER_HOME")) : [user_home() stringByAppendingPathComponent:@"Games/SurvivalHorrorClassics"]; }

/* ---- the games and their versions ---- */
typedef struct { const char *id, *label, *bin; } Version;
typedef struct { const char *id, *title, *year, *tag; int nv; Version v[2]; const char *name; int gamebanana; int bonus; } GameDef;
static const GameDef GAMES[] = {   /* gamebanana: GameBanana's game, for the Mods window's browser (Dino Crisis has none) */
    {"re1", "RESIDENT EVIL", "1997", "Spencer Mansion", 1, {{"re1", "Resident Evil", "ResidentEvil"}}, "Resident Evil", 13229},
    {"re2", "RESIDENT EVIL 2", "1999", "Raccoon City", 2, {{"re2", "Leon", "ResidentEvil2Leon"}, {"re2c", "Claire", "ResidentEvil2Claire"}}, "Resident Evil 2", 8862},
    {"re3", "RESIDENT EVIL 3", "2000", "Nemesis", 2, {{"re3", "The Game", "ResidentEvil3"}, {"re3m", "The Mercenaries", "ResidentEvil3Mercenaries"}}, "Resident Evil 3", 7006},
    {"dc", "DINO CRISIS", "2000", "Ibis Island", 1, {{"dc", "Dino Crisis", "DinoCrisis"}}, "Dino Crisis", 0},
    {"dc2", "DINO CRISIS 2", "2002", "Edward City", 1, {{"dc2", "Dino Crisis 2", "DinoCrisis2"}}, "Dino Crisis 2", 0, 1},   /* the bonus card */
};
/* the downloaded app (make_suite.sh): each version set up by its own binary into ~/Games/<folder> (setup/setup_ui.m).
 * suite_cfg: setup/<binary>.json per version id; nil in an app built from source (games.plist) */
static NSMutableDictionary<NSString *, NSDictionary *> *suite_cfg;
static NSString *suite_root(NSDictionary *c) { return [user_home() stringByAppendingPathComponent:[@"Games" stringByAppendingPathComponent:c[@"folder"]]]; }
static NSArray<NSDictionary *> *pending_open;   /* folders dropped on the app before its window was made */
/* horror_mods.m: the Mods window */
NSString *hmods_env(NSString *game);
void hmods_counts(NSString *game, NSUInteger *installed, NSUInteger *on);
void hmods_show(NSWindow *parent, NSString *title, NSString *game, int gamebanana, NSString *gameDir, NSString *prefer, void (^changed)(void));
int hmods_cli(NSString *game, NSString *gameDir, NSString *archive, NSString *place);
enum { NG = (int)(sizeof GAMES / sizeof *GAMES) };

@interface Opt : NSObject
@property NSString *key, *title; @property NSArray<NSArray<NSString *> *> *choices; @property NSInteger index;   /* {value, label, note} */
@end
@implementation Opt
- (NSString *)value { return self.choices[self.index][0]; }
@end

enum { HIT_NONE = -1, HIT_PLAY = 1, HIT_QUIT, HIT_OPTIONS, HIT_MODS, HIT_SAVES, HIT_FILES, HIT_UPDATES, HIT_CARD = 100, HIT_VER = 200, HIT_ROW = 300 };
enum { NTILES = 5 };   /* Options, Mods, Saves, Game Files, Updates: HIT_OPTIONS + k */
@interface HView : NSView { @public NSInteger ver[NG]; }
@property NSInteger game, hover; @property BOOL open; @property NSArray<Opt *> *opts;
@property NSDictionary *installed; @property NSMutableDictionary *art, *backdrop, *soft, *blurred; @property CGFloat glow, fade; @property NSInteger prevGame; @property NSString *status;
@property NSMutableDictionary *slides, *portraits, *logos, *cache; @property double t;   /* per game id: the backdrop's slides, the box, the keyed title */
@property (copy) void (^act)(NSInteger hit);
@end

static void text(NSString *s, NSFont *f, NSColor *c, NSRect r, NSTextAlignment al)
{
    NSMutableParagraphStyle *p = [NSMutableParagraphStyle new]; p.alignment = al; p.lineBreakMode = NSLineBreakByTruncatingTail;
    [s drawInRect:r withAttributes:@{NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSParagraphStyleAttributeName: p}];
}
static BOOL volume_ok(NSString *path)                        /* the game's folder is reachable (its volume mounted) */
{
    return path.length && [[NSFileManager defaultManager] fileExistsAtPath:path];
}
static NSString *volume_name(NSString *path)
{
    NSArray *c = path.pathComponents; return c.count > 2 && [c[1] isEqual:@"Volumes"] ? c[2] : nil;
}

/* an image drawn to fill r (cropped to its shape, centred), at some opacity */
static void draw_fill(NSImage *img, NSRect r, CGFloat alpha)
{
    if (!img) return; NSSize s = img.size; if (s.width <= 0 || s.height <= 0) return;
    CGFloat k = MAX(r.size.width / s.width, r.size.height / s.height), w = r.size.width / k, h = r.size.height / k;
    [img drawInRect:r fromRect:NSMakeRect((s.width - w) / 2, (s.height - h) / 2, w, h) operation:NSCompositingOperationSourceOver fraction:alpha respectFlipped:YES hints:nil];
}
/* the frosted copy of a backdrop: blurred and a little darker */
static NSImage *frost(NSImage *img, CGFloat soft)
{
    CGImageRef cg = [img CGImageForProposedRect:NULL context:nil hints:nil]; if (!cg) return nil;
    CIImage *in = [CIImage imageWithCGImage:cg]; CGRect ext = in.extent;
    CIImage *b = [[in imageByClampingToExtent] imageByApplyingGaussianBlurWithSigma:MAX(ext.size.width, ext.size.height) / soft];
    b = [[b imageByApplyingFilter:@"CIColorControls" withInputParameters:@{@"inputSaturation": @1.15, @"inputBrightness": @-0.06}] imageByCroppingToRect:ext];
    static CIContext *cx; if (!cx) cx = [CIContext contextWithOptions:nil];
    CGImageRef out = [cx createCGImage:b fromRect:ext]; if (!out) return nil;          /* a bitmap, the right way up */
    NSImage *o = [[NSImage alloc] initWithCGImage:out size:img.size]; CGImageRelease(out);
    return o;
}
@implementation HView
- (BOOL)isFlipped { return YES; }
/* frosted glass: the blurred backdrop seen through r, a light tint, a top highlight, a fine edge */
- (void)glass:(NSRect)r radius:(CGFloat)rad tint:(NSColor *)tint edge:(NSColor *)edge width:(CGFloat)ew
{
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:r xRadius:rad yRadius:rad];
    [NSGraphicsContext saveGraphicsState]; [p addClip];
    [rgb(0.03, 0.02, 0.025, 0.74) setFill]; NSRectFillUsingOperation(r, NSCompositingOperationCopy);   /* smoked glass over the moving backdrop */
    [tint ?: rgb(1, 1, 1, 0.07) setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver);
    NSGradient *hi = [[NSGradient alloc] initWithStartingColor:rgb(1, 1, 1, 0.12) endingColor:rgb(1, 1, 1, 0)];
    [hi drawInRect:NSMakeRect(r.origin.x, r.origin.y, r.size.width, MIN(40, r.size.height)) angle:90];
    [NSGraphicsContext restoreGraphicsState];
    NSBezierPath *e = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, ew / 2, ew / 2) xRadius:rad yRadius:rad]; e.lineWidth = ew;
    [edge ?: rgb(1, 1, 1, 0.16) setStroke]; [e stroke];
}
- (BOOL)acceptsFirstResponder { return YES; }
/* the inventory screen: the title and the condition monitor; Examine and the item grid; the item's file and the commands */
- (NSRect)gridRect { return NSMakeRect(346, 166, 574, 254); }
- (NSRect)cardRect:(NSInteger)k { NSRect g = self.gridRect; CGFloat w = (g.size.width - 16) / 3, h = (g.size.height - 8) / 2; return NSMakeRect(g.origin.x + (k % 3) * (w + 8), g.origin.y + (k / 3) * (h + 8), w, h); }
- (NSRect)examineRect { return NSMakeRect(40, 166, 290, 254); }
- (NSRect)monitorRect { return NSMakeRect(self.bounds.size.width - 40 - 250, 30, 250, 100); }
- (NSRect)fileRect { return NSMakeRect(40, 436, 590, 140); }
- (NSRect)verRect:(NSInteger)k of:(NSInteger)n { NSRect f = self.fileRect; return NSMakeRect(f.origin.x + 16 + k * 168, NSMaxY(f) - 36, 160, 26); }
- (NSRect)tileRect:(NSInteger)k { return NSMakeRect(650, 464 + k * 30, 270, 28); }
- (NSRect)playRect { return NSMakeRect(470, 592, 160, 50); }
- (NSRect)quitRect { return NSMakeRect(40, 592, 110, 50); }
- (NSRect)portraitRect { return NSInsetRect(self.examineRect, 14, 30); }
- (NSRect)panelRect { CGFloat h = 70 + 58 * (CGFloat)self.opts.count; return NSMakeRect(150, MAX(40, 300 - h / 2), self.bounds.size.width - 300, h); }
- (NSRect)rowRect:(NSInteger)k { NSRect p = self.panelRect; return NSMakeRect(p.origin.x + 30, p.origin.y + 54 + 58 * k, p.size.width - 60, 50); }
- (const Version *)version:(NSInteger)g { const GameDef *d = &GAMES[g]; NSInteger v = self->ver[g]; return &d->v[v < d->nv ? v : 0]; }
- (NSDictionary *)entry:(NSInteger)g { return self.installed[@([self version:g]->id)]; }
- (NSInteger)hitAt:(NSPoint)p
{
    if (self.open) { for (NSInteger k = 0; k < (NSInteger)self.opts.count; k++) if (NSPointInRect(p, [self rowRect:k])) return HIT_ROW + k;
                     return NSPointInRect(p, self.panelRect) ? HIT_NONE : HIT_OPTIONS; }
    for (NSInteger k = 0; k < NG; k++) if (NSPointInRect(p, [self cardRect:k])) return HIT_CARD + k;
    const GameDef *d = &GAMES[self.game];
    if (d->nv > 1) for (NSInteger k = 0; k < d->nv; k++) if (NSPointInRect(p, [self verRect:k of:d->nv])) return HIT_VER + k;
    for (NSInteger k = 0; k < NTILES; k++) if (NSPointInRect(p, [self tileRect:k])) return HIT_OPTIONS + k;
    if (NSPointInRect(p, self.playRect)) return HIT_PLAY;
    if (NSPointInRect(p, self.quitRect)) return HIT_QUIT;
    return HIT_NONE;
}
- (void)updateTrackingAreas
{
    for (NSTrackingArea *a in self.trackingAreas) [self removeTrackingArea:a];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect owner:self userInfo:nil]];
}
- (void)mouseMoved:(NSEvent *)e { NSInteger h = [self hitAt:[self convertPoint:e.locationInWindow fromView:nil]]; if (h != self.hover) { self.hover = h; self.needsDisplay = YES; } }
- (void)mouseDown:(NSEvent *)e { NSInteger h = [self hitAt:[self convertPoint:e.locationInWindow fromView:nil]]; if (h != HIT_NONE && self.act) self.act(h); }
/* one step of navigation, from the keyboard or a controller: dx/dy a direction, or a button */
enum { NAV_LEFT = 1, NAV_RIGHT, NAV_UP, NAV_DOWN, NAV_OK, NAV_BACK, NAV_OPTIONS, NAV_PLAY, NAV_MODS };
- (void)nav:(int)n
{
    if (!self.act) return;
    if (self.open) {                                                      /* the options panel: a row cursor */
        NSInteger rows = (NSInteger)self.opts.count, r = self.hover >= HIT_ROW && self.hover < HIT_ROW + rows ? self.hover - HIT_ROW : -1;
        if (n == NAV_UP || n == NAV_DOWN) { r = r < 0 ? 0 : (r + (n == NAV_DOWN ? 1 : rows - 1)) % rows; self.hover = HIT_ROW + r; self.needsDisplay = YES; }
        else if ((n == NAV_OK || n == NAV_RIGHT) && r >= 0) self.act(HIT_ROW + r);
        else if (n == NAV_LEFT && r >= 0) { Opt *o = self.opts[r]; NSInteger c = (NSInteger)o.choices.count; if (c > 1) { o.index = (o.index + c - 2) % c; self.act(HIT_ROW + r); } }
        else if (n == NAV_BACK || n == NAV_OPTIONS) { self.hover = HIT_NONE; self.act(HIT_OPTIONS); }
        return;
    }
    if (n == NAV_LEFT || n == NAV_RIGHT) self.act(HIT_CARD + (self.game + (n == NAV_LEFT ? NG - 1 : 1)) % NG);
    else if ((n == NAV_UP || n == NAV_DOWN) && GAMES[self.game].nv > 1) self.act(HIT_VER + (self->ver[self.game] + (n == NAV_DOWN ? 1 : GAMES[self.game].nv - 1)) % GAMES[self.game].nv);
    else if (n == NAV_OK || n == NAV_PLAY) self.act(HIT_PLAY);
    else if (n == NAV_OPTIONS) { self.act(HIT_OPTIONS); if (self.open) { self.hover = HIT_ROW; self.needsDisplay = YES; } }
    else if (n == NAV_MODS) self.act(HIT_MODS);
    else if (n == NAV_BACK) self.act(HIT_QUIT);
}
- (void)keyDown:(NSEvent *)e
{
    unichar ch = e.charactersIgnoringModifiers.length ? [e.charactersIgnoringModifiers characterAtIndex:0] : 0;
    if (ch == 27) { if (self.act) self.act(self.open ? HIT_OPTIONS : HIT_QUIT); return; }
    if (ch == '\r' || ch == 3) { if (self.act && !self.open) self.act(HIT_PLAY); return; }
    if ((ch == 'm' || ch == 'M') && !self.open) { if (self.act) self.act(HIT_MODS); return; }
    if (self.open && (ch == NSUpArrowFunctionKey || ch == NSDownArrowFunctionKey || ch == NSLeftArrowFunctionKey || ch == NSRightArrowFunctionKey || ch == ' ')) {
        [self nav:ch == NSUpArrowFunctionKey ? NAV_UP : ch == NSDownArrowFunctionKey ? NAV_DOWN : ch == NSLeftArrowFunctionKey ? NAV_LEFT : ch == ' ' ? NAV_OK : NAV_RIGHT]; return; }
    if (!self.open && (ch == NSLeftArrowFunctionKey || ch == NSRightArrowFunctionKey)) { if (self.act) self.act(HIT_CARD + (self.game + (ch == NSLeftArrowFunctionKey ? NG - 1 : 1)) % NG); return; }
    if (!self.open && (ch == NSUpArrowFunctionKey || ch == NSDownArrowFunctionKey) && GAMES[self.game].nv > 1) { if (self.act) self.act(HIT_VER + (self->ver[self.game] + 1) % GAMES[self.game].nv); return; }
    [super keyDown:e];
}
/* ---- the moving part (see the top of the file) ---- */
static double hash01(double x) { double s = sin(x * 12.9898) * 43758.5453; return s - floor(s); }
static NSImage *offscreen(NSSize size, CGFloat scale, void (^draw)(void))
{
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)ceil(size.width * scale) pixelsHigh:(NSInteger)ceil(size.height * scale)
                                                                 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    rep = [rep bitmapImageRepByRetaggingWithColorSpace:NSColorSpace.sRGBColorSpace] ?: rep;
    rep.size = size;
    CGContextRef cg = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep].CGContext;
    [NSGraphicsContext saveGraphicsState];
    CGContextTranslateCTM(cg, 0, size.height); CGContextScaleCTM(cg, 1, -1);
    [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithCGContext:cg flipped:YES]];
    draw();
    [NSGraphicsContext restoreGraphicsState];
    NSImage *img = [[NSImage alloc] initWithSize:size]; [img addRepresentation:rep];
    return img;
}
static NSImage *ready(NSImage *img, CGFloat maxw)   /* decoded once, at most maxw pixels wide (its pixels, not its points) */
{
    if (!img || img.size.width < 16) return nil;
    NSSize s = img.size; NSInteger pw = 0, ph = 0;
    for (NSImageRep *r in img.representations) if (r.pixelsWide > pw) { pw = r.pixelsWide; ph = r.pixelsHigh; }
    if (pw > 0 && ph > 0) s = NSMakeSize(pw, ph);
    CGFloat k = MIN(1.0, maxw / s.width); NSSize o = NSMakeSize(round(s.width * k), round(s.height * k));
    return offscreen(o, 1, ^{ [img drawInRect:NSMakeRect(0, 0, o.width, o.height) fromRect:NSZeroRect operation:NSCompositingOperationCopy fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}]; });
}
/* the title on its title screen, keyed out of the screen's top part (above the copyright and PRESS START): red: the
 * Resident Evil titles' red lettering (how much redder than green and blue a pixel is), else the white lettering (bright
 * and grey); nil when the capture has no such title */
static double key_alpha(const uint8_t *q, BOOL red)
{
    double r = q[0] / 255.0, g = q[1] / 255.0, b = q[2] / 255.0, mx = MAX(r, MAX(g, b)), mn = MIN(r, MIN(g, b));
    if (red) return r - MAX(g, b) < 0.18 ? 0 : MIN(1.0, (r - MAX(g, b) - 0.18) * 3.0);
    return mx < 0.55 || mx - mn > 0.22 ? 0 : MIN(1.0, (mx - 0.55) * 3.5);
}
static NSImage *key_title(NSImage *img, BOOL red)
{
    CGImageRef cg = [img CGImageForProposedRect:NULL context:nil hints:nil]; if (!cg) return nil;
    size_t w = CGImageGetWidth(cg), h = CGImageGetHeight(cg), hh = h * 64 / 100; NSMutableData *px = [NSMutableData dataWithLength:w * h * 4];
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB(); CGContextRef c = CGBitmapContextCreate(px.mutableBytes, w, h, 8, w * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(c, CGRectMake(0, 0, w, h), cg); CGContextRelease(c);
    const uint8_t *d = px.bytes; size_t x0 = w, x1 = 0, y0 = h, y1 = 0;
    for (size_t y = 0; y < hh; y++) for (size_t x = 0; x < w; x++) { const uint8_t *q = d + (y * w + x) * 4; if (key_alpha(q, red) > 0.5) { x0 = MIN(x0, x); x1 = MAX(x1, x); y0 = MIN(y0, y); y1 = MAX(y1, y); } }
    if (x1 <= x0 + w / 4 || y1 <= y0 + h / 16) { CGColorSpaceRelease(cs); return nil; }
    x0 = x0 > 12 ? x0 - 12 : 0; y0 = y0 > 12 ? y0 - 12 : 0; x1 = MIN(w - 1, x1 + 12); y1 = MIN(hh - 1, y1 + 12);
    size_t ow = x1 - x0 + 1, oh = y1 - y0 + 1; NSMutableData *o = [NSMutableData dataWithLength:ow * oh * 4]; uint8_t *od = o.mutableBytes;
    for (size_t y = 0; y < oh; y++) for (size_t x = 0; x < ow; x++) {
        const uint8_t *q = d + ((y0 + y) * w + x0 + x) * 4; double a = key_alpha(q, red);
        double edge = MIN(MIN((double)MIN(x, ow - 1 - x), (double)MIN(y, oh - 1 - y)) / 10.0, 1.0); a *= edge;
        uint8_t *r = od + (y * ow + x) * 4; r[0] = (uint8_t)MIN(q[0], a * 255); r[1] = (uint8_t)MIN(q[1], a * 255); r[2] = (uint8_t)MIN(q[2], a * 255); r[3] = (uint8_t)(a * 255);
    }
    CGDataProviderRef dp = CGDataProviderCreateWithCFData((__bridge CFDataRef)o);
    CGImageRef li = CGImageCreate(ow, oh, 8, 32, ow * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast, dp, NULL, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(dp); CGColorSpaceRelease(cs); if (!li) return nil;
    NSImage *out = [[NSImage alloc] initWithCGImage:li size:NSMakeSize(ow, oh)]; CGImageRelease(li); return out;
}
static void draw_fill_z(NSImage *img, NSRect r, CGFloat alpha, CGFloat z, CGFloat fx, CGFloat fy, NSImageInterpolation q)
{
    if (!img) return; NSSize s = img.size; if (s.width <= 0 || s.height <= 0) return;
    CGFloat k = MAX(r.size.width / s.width, r.size.height / s.height) * z, w = r.size.width / k, h = r.size.height / k;
    [img drawInRect:r fromRect:NSMakeRect((s.width - w) * fx, (s.height - h) * (1 - fy), w, h) operation:NSCompositingOperationSourceOver fraction:alpha respectFlipped:YES hints:@{NSImageHintInterpolation: @(q)}];
}
- (CGFloat)scale { return self.window ? self.window.backingScaleFactor : 2; }
- (NSImage *)sprite:(NSString *)key colors:(NSArray<NSColor *> *)cols
{
    NSImage *c = self.cache[key]; if (c) return c;
    c = offscreen(NSMakeSize(128, 128), 1, ^{ [[[NSGradient alloc] initWithColors:cols] drawFromCenter:NSMakePoint(64, 64) radius:0 toCenter:NSMakePoint(64, 64) radius:64 options:0]; });
    self.cache[key] = c; return c;
}
static void draw_sprite(NSImage *img, NSPoint c, CGFloat rx, CGFloat ry, CGFloat alpha)
{
    if (alpha <= 0.002) return;
    [img drawInRect:NSMakeRect(c.x - rx, c.y - ry, 2 * rx, 2 * ry) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:MIN(1, alpha) respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationLow)}];
}
- (void)backdrop:(NSInteger)g alpha:(CGFloat)alpha
{
    NSArray<NSImage *> *sc = self.slides[@(GAMES[g].id)]; NSUInteger n = sc.count; NSRect b = self.bounds; if (!n) return; double tt = MAX(0.0, self.t);
    double per = 8.0, fadeT = 1.6;
    void (^slide)(NSUInteger, double, CGFloat) = ^(NSUInteger j, double local, CGFloat a) {
        double p = local / (per + fadeT); CGFloat dir = (j % 2) ? 1 : -1;
        draw_fill_z(sc[j % n], b, a * alpha, (CGFloat)(1.04 + 0.07 * p), (CGFloat)(0.5 + dir * 0.08 * (p - 0.5)), (CGFloat)(0.45 + 0.05 * (p - 0.5)), NSImageInterpolationMedium);
    };
    if (n == 1) { draw_fill_z(sc[0], b, alpha, (CGFloat)(1.08 + 0.035 * sin(tt * 0.11)), (CGFloat)(0.5 + 0.06 * sin(tt * 0.07)), 0.45, NSImageInterpolationMedium); return; }
    NSUInteger k = (NSUInteger)(tt / per); double local = tt - (double)k * per;
    if (local < fadeT && k > 0) { slide(k - 1, local + per, 1); slide(k, local, (CGFloat)(local / fadeT)); }
    else slide(k, local, 1);
}
- (NSImage *)gradeImage
{
    NSImage *c = self.cache[@"grade"]; if (c) return c;
    NSRect b = self.bounds;
    c = offscreen(b.size, 1, ^{
        NSGradient *left = [[NSGradient alloc] initWithColorsAndLocations:rgb(0, 0, 0, 0.78), 0.0, rgb(0, 0, 0, 0.40), 0.45, rgb(0, 0, 0, 0.10), 0.78, rgb(0, 0, 0, 0.05), 1.0, nil];
        [left drawInRect:b angle:0];
        NSGradient *shade = [[NSGradient alloc] initWithColorsAndLocations:rgb(0, 0, 0, 0.20), 0.0, rgb(0, 0, 0, 0.10), 0.35, rgb(0, 0, 0, 0.62), 0.55, rgb(0, 0, 0, 0.88), 1.0, nil];
        [shade drawInRect:b angle:-90];
        NSGradient *vig = [[NSGradient alloc] initWithStartingColor:rgb(0, 0, 0, 0) endingColor:rgb(0, 0, 0, 0.45)];
        [vig drawInRect:b relativeCenterPosition:NSZeroPoint];
        [rgb(1, 1, 1, 0.018) setFill]; for (CGFloat y = 0; y < b.size.height; y += 3) NSRectFillUsingOperation(NSMakeRect(0, y, b.size.width, 1), NSCompositingOperationSourceOver);
    });
    self.cache[@"grade"] = c; return c;
}
- (NSString *)signature
{
    NSMutableString *s = [NSMutableString stringWithFormat:@"%ld|%ld|%d|%@|%.0f", (long)self.game, (long)self.hover, self.open, self.status ?: @"", self.scale];
    for (NSInteger k = 0; k < NG; k++) [s appendFormat:@"|%ld", (long)self->ver[k]];
    for (Opt *o in self.opts) [s appendFormat:@"|%ld", (long)o.index];
    NSUInteger nm = 0, on = 0; hmods_counts(@(GAMES[self.game].id), &nm, &on); [s appendFormat:@"|%lu/%lu", (unsigned long)on, (unsigned long)nm];
    for (NSInteger k = 0; k < NG; k++) { NSDictionary *e = [self entry:k]; [s appendFormat:@"|%d%d", e != nil, e && volume_ok(e[@"game"])]; }
    return s;
}
static int frames_drawn;
static double ecg(double ph)   /* one beat of a heart's trace, 0..1 of the beat: the P wave, the spike, the T wave */
{
    double p = 0.10 * exp(-pow((ph - 0.18) / 0.035, 2)), q = -0.12 * exp(-pow((ph - 0.30) / 0.010, 2)), r = 1.0 * exp(-pow((ph - 0.32) / 0.012, 2));
    double s2 = -0.25 * exp(-pow((ph - 0.345) / 0.012, 2)), t = 0.22 * exp(-pow((ph - 0.55) / 0.05, 2)); return p + q + r + s2 + t;
}
- (void)drawRect:(NSRect)dirty
{
    NSRect b = self.bounds; double T = self.t; frames_drawn++;
    if (!self.cache) self.cache = [NSMutableDictionary dictionary];
    if (![self.cache[@"scale"] isEqual:@(self.scale)]) { [self.cache removeAllObjects]; self.cache[@"scale"] = @(self.scale); }
    if (![self.cache[@"typeGame"] isEqual:@(self.game)]) { self.cache[@"typeGame"] = @(self.game); self.cache[@"typeT"] = @(getenv("SHC_LAUNCHER_SHOT") ? T - 1.6 : T); }   /* (a shot: mid-sentence) */
    [rgb(0.02, 0.018, 0.02, 1) setFill]; NSRectFill(b);
    BOOL fading = self.fade < 1 && self.prevGame != self.game;
    if (fading) [self backdrop:self.prevGame alpha:0.35];
    [self backdrop:self.game alpha:(fading ? self.fade : 1) * 0.35];      /* the game's own screens, far back in the dark */
    [rgb(0.02, 0.015, 0.02, 0.55) setFill]; NSRectFillUsingOperation(b, NSCompositingOperationSourceOver);
    [[[NSGradient alloc] initWithStartingColor:rgb(0, 0, 0, 0) endingColor:rgb(0, 0, 0, 0.85)] drawInRect:b relativeCenterPosition:NSZeroPoint];
    NSString *sig = self.signature;
    if (!self.cache[@"ui"] || ![self.cache[@"uiSig"] isEqual:sig]) { self.cache[@"ui"] = offscreen(b.size, self.scale, ^{ [self drawStill]; }); self.cache[@"uiSig"] = sig; }
    [(NSImage *)self.cache[@"ui"] drawInRect:b fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    double flick = fmod(T, 7.3) < 0.12 || fmod(T + 2.1, 11.7) < 0.08 ? 0.55 : 1.0;   /* a bad bulb */
    if (!self.open) {   /* Examine: the chosen game, turning slowly in the light */
        NSRect in = self.portraitRect; NSImage *now = self.portraits[@(GAMES[self.game].id)];
        NSImage *glow = [self sprite:@"exglow" colors:@[rgb(0.45, 0.05, 0.06, 0.55), rgb(0.15, 0.01, 0.02, 0.2), rgb(0, 0, 0, 0)]];
        draw_sprite(glow, NSMakePoint(NSMidX(in), NSMidY(in)), 170, 150, 0.6 * flick);
        if (now) {   /* its own shape kept: fitted whole in the frame, drifting a little closer and back, a light gliding over it */
            NSSize is = now.size; CGFloat k = MIN(in.size.width / MAX(1, is.width), in.size.height / MAX(1, is.height)) * (CGFloat)(0.97 + 0.03 * sin(T * 0.4));
            NSRect pr = NSMakeRect(NSMidX(in) - is.width * k / 2, NSMidY(in) - is.height * k / 2, is.width * k, is.height * k);
            NSShadow *sh = [NSShadow new]; sh.shadowColor = rgb(0, 0, 0, 0.9); sh.shadowBlurRadius = 14; sh.shadowOffset = NSMakeSize(0, -6);
            [NSGraphicsContext saveGraphicsState]; [sh set]; [now drawInRect:pr fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:fading ? self.fade : 1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
            [NSGraphicsContext restoreGraphicsState];
            [NSGraphicsContext saveGraphicsState]; NSRectClip(pr);
            CGFloat lx = pr.origin.x + (CGFloat)((0.5 + 0.5 * sin(T * 0.45)) * pr.size.width);
            [[[NSGradient alloc] initWithColors:@[rgb(1, 1, 1, 0), rgb(1, 0.95, 0.9, 0.14), rgb(1, 1, 1, 0)]] drawInRect:NSMakeRect(lx - 50, pr.origin.y, 100, pr.size.height) angle:0];
            [NSGraphicsContext restoreGraphicsState]; }
    }
    if (!self.open) {   /* the condition monitor: a heart's trace running across, its head bright, its tail fading */
        NSRect m = NSInsetRect(self.monitorRect, 10, 30); double bpm = 72, beat = 60 / bpm, span = 2.4;
        NSBezierPath *tr = [NSBezierPath bezierPath]; tr.lineWidth = 2; tr.lineJoinStyle = NSLineJoinStyleRound;
        int nseg = 160; NSColor *gc = rgb(0.35, 0.95, 0.40, 1); double head = fmod(T, span);
        for (int k = 0; k <= nseg; k++) {
            double u = (double)k / nseg, tt = T - span + u * span, v = ecg(fmod(tt / beat, 1.0));
            NSPoint pt = NSMakePoint(m.origin.x + fmod(u * m.size.width + head / span * m.size.width, m.size.width), NSMaxY(m) - 6 - v * (m.size.height - 10));
            if (k == 0 || pt.x < tr.currentPoint.x) [tr moveToPoint:pt]; else [tr lineToPoint:pt];
        }
        [NSGraphicsContext saveGraphicsState]; NSRectClip(NSInsetRect(m, -2, -6));
        NSShadow *gl = [NSShadow new]; gl.shadowColor = [gc colorWithAlphaComponent:0.8]; gl.shadowBlurRadius = 6; [gl set];
        [[gc colorWithAlphaComponent:0.85] setStroke]; [tr stroke];
        CGFloat hx = m.origin.x + (CGFloat)(head / span) * m.size.width;   /* the sweep: dark just ahead of the head */
        [rgb(0.01, 0.04, 0.02, 1) setFill]; NSRectFill(NSMakeRect(hx + 2, m.origin.y - 6, 18, m.size.height + 12));
        [NSGraphicsContext restoreGraphicsState];
        double hv = ecg(fmod(T / beat, 1.0)); NSPoint hp = NSMakePoint(hx, NSMaxY(m) - 6 - hv * (m.size.height - 10));
        draw_sprite([self sprite:@"ecghead" colors:@[rgb(0.7, 1, 0.7, 1), rgb(0.3, 0.9, 0.35, 0.4), rgb(0, 0, 0, 0)]], hp, 8, 8, 1);
        double pulse = exp(-pow(fmod(T / beat, 1.0) - 0.33, 2) / 0.004);
        text(@"FINE", type(20, YES), [gc colorWithAlphaComponent:0.7 + 0.3 * pulse], NSMakeRect(NSMaxX(self.monitorRect) - 90, self.monitorRect.origin.y + 6, 78, 26), NSTextAlignmentRight);
    }
    if (!self.open) {
        {   /* the item's file, typed out on the save room's typewriter */
            static NSString *const blurbs[] = {@"S.T.A.R.S. Alpha Team flees into a mansion in the Arklay Mountains, and finds Umbrella's secret.",
                                               @"A rookie cop and a student looking for her brother reach Raccoon City the night the dead take it.",
                                               @"Jill Valentine fights her way out of Raccoon City's last days, hunted by the Nemesis.",
                                               @"Regina's team lands on Ibis Island to bring back Dr. Kirk, and finds the facility overrun by dinosaurs.",
                                               @"A rescue team crosses into a jungle out of time to find Edward City's lost research station."};
            NSString *full = self.game < 5 ? blurbs[self.game] : @""; double tt = T - [self.cache[@"typeT"] doubleValue];
            NSUInteger n = MIN(full.length, (NSUInteger)MAX(0.0, (tt - 0.25) * 38));
            NSString *shown = [full substringToIndex:n]; NSRect f = self.fileRect;
            NSMutableParagraphStyle *ps = [NSMutableParagraphStyle new]; ps.lineSpacing = 2;
            NSDictionary *at = @{NSFontAttributeName: type(13, NO), NSForegroundColorAttributeName: paper(), NSParagraphStyleAttributeName: ps};
            NSRect tr = NSMakeRect(f.origin.x + 16, f.origin.y + 58, f.size.width - 32, 44);
            [shown drawWithRect:tr options:NSStringDrawingUsesLineFragmentOrigin attributes:at];
            if (n < full.length || fmod(T, 1.0) < 0.5) {                      /* the carriage: a block where the next letter lands */
                NSTextStorage *ts = [[NSTextStorage alloc] initWithString:shown.length ? shown : @" " attributes:at]; NSLayoutManager *lm = [NSLayoutManager new]; NSTextContainer *tc = [[NSTextContainer alloc] initWithSize:NSMakeSize(tr.size.width, 1000)];
                tc.lineFragmentPadding = 0; [lm addTextContainer:tc]; [ts addLayoutManager:lm];
                NSRect g = shown.length ? [lm boundingRectForGlyphRange:NSMakeRange(MAX(0, (NSInteger)[lm numberOfGlyphs] - 1), 1) inTextContainer:tc] : NSMakeRect(-8, 0, 0, 17);
                [rgb(0.76, 0.09, 0.11, 0.9) setFill]; NSRectFill(NSMakeRect(tr.origin.x + NSMaxX(g) + 1, tr.origin.y + g.origin.y + 2, 7, 14));
            }
        }
        {   /* the grid's cursor: brackets breathing round the chosen slot */
            NSRect r = NSInsetRect([self cardRect:self.game], -4, -4); CGFloat a = (CGFloat)(0.55 + 0.45 * (0.5 + 0.5 * sin(T * 4.2)));
            [[blood() colorWithAlphaComponent:a] setStroke];
            for (int k = 0; k < 4; k++) { CGFloat sx = k % 2 ? -1 : 1, sy = k / 2 ? -1 : 1; NSPoint c = NSMakePoint(k % 2 ? NSMaxX(r) : r.origin.x, k / 2 ? NSMaxY(r) : r.origin.y);
                NSBezierPath *br = [NSBezierPath bezierPath]; br.lineWidth = 3; [br moveToPoint:NSMakePoint(c.x, c.y + sy * 18)]; [br lineToPoint:c]; [br lineToPoint:NSMakePoint(c.x + sx * 18, c.y)]; [br stroke]; }
        }
        NSRect p = self.playRect; NSImage *pg = [self sprite:@"playglow" colors:@[rgb(0.9, 0.08, 0.1, 0.7), rgb(0.5, 0.02, 0.04, 0.2), rgb(0, 0, 0, 0)]];
        draw_sprite(pg, NSMakePoint(NSMidX(p), NSMidY(p)), p.size.width * 0.8, p.size.height * 1.2, 0.20 + 0.18 * (0.5 + 0.5 * sin(T * 2.0)));
    }
    if (flick < 1) { [rgb(0, 0, 0, 0.18) setFill]; NSRectFillUsingOperation(b, NSCompositingOperationSourceOver); }
}
- (void)panelBox:(NSRect)r title:(NSString *)t   /* a box of the inventory: black glass, a fine grey rule, its name on the frame */
{
    [rgb(0.03, 0.025, 0.03, 0.88) setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver);
    [rgb(0.55, 0.52, 0.50, 0.55) setStroke]; NSBezierPath *e = [NSBezierPath bezierPathWithRect:NSInsetRect(r, 0.5, 0.5)]; e.lineWidth = 1; [e stroke];
    [rgb(0.55, 0.52, 0.50, 0.25) setStroke]; NSBezierPath *e2 = [NSBezierPath bezierPathWithRect:NSInsetRect(r, 3.5, 3.5)]; e2.lineWidth = 1; [e2 stroke];
    if (t.length) { NSDictionary *a = @{NSFontAttributeName: type(10.5, YES), NSKernAttributeName: @2.4}; CGFloat w = [t sizeWithAttributes:a].width + 16;
        [rgb(0.02, 0.018, 0.02, 1) setFill]; NSRectFill(NSMakeRect(r.origin.x + 12, r.origin.y - 7, w, 14));
        ktext(t, type(10.5, YES), blood(), 2.4, NSMakeRect(r.origin.x + 20, r.origin.y - 8, w, 15), NSTextAlignmentLeft); }
}
- (void)drawStill
{
    NSRect b = self.bounds; const GameDef *gd = &GAMES[self.game];
    ktext(@"SURVIVAL HORROR  ·  CLASSICS", type(10.5, YES), blood(), 3, NSMakeRect(40, 12, 400, 15), NSTextAlignmentLeft);
    NSImage *logo = self.logos[@(gd->id)];
    {   NSShadow *sh = [NSShadow new]; sh.shadowColor = rgb(0, 0, 0, 0.85); sh.shadowBlurRadius = 10; [NSGraphicsContext saveGraphicsState]; [sh set];
        if (logo) { NSSize ls = logo.size; CGImageRef lc = [logo CGImageForProposedRect:NULL context:nil hints:nil];
                    CGFloat px = lc ? CGImageGetWidth(lc) : ls.width, lw = MIN(520, px * 0.62), lh = lw * ls.height / MAX(1, ls.width);
                    if (lh > 104) { lh = 104; lw = lh * ls.width / MAX(1, ls.height); }
                    [logo drawInRect:NSMakeRect(40, 136 - lh, lw, lh) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}]; }
        else text(@(gd->title), title_font(50), paper(), NSMakeRect(40, 70, 600, 64), NSTextAlignmentLeft);
        [NSGraphicsContext restoreGraphicsState]; }
    ktext([NSString stringWithFormat:@"CAPCOM  ·  %s", gd->year], type(11, YES), ash(), 2.4, NSMakeRect(40, 140, 300, 15), NSTextAlignmentLeft);
    {   /* the monitor's frame (its trace is live) */
        NSRect m = self.monitorRect; [self panelBox:m title:@"CONDITION"];
        NSRect g = NSInsetRect(m, 10, 30); [rgb(0.01, 0.04, 0.02, 1) setFill]; NSRectFill(NSInsetRect(g, -2, -6));
        [rgb(0.2, 0.6, 0.25, 0.18) setFill];
        for (CGFloat x = g.origin.x; x <= NSMaxX(g); x += 10) NSRectFillUsingOperation(NSMakeRect(x, g.origin.y - 6, 1, g.size.height + 12), NSCompositingOperationSourceOver);
        for (CGFloat y = g.origin.y - 6; y <= NSMaxY(g) + 6; y += 10) NSRectFillUsingOperation(NSMakeRect(g.origin.x - 2, y, g.size.width + 4, 1), NSCompositingOperationSourceOver);
        ktext(@"ECG", type(10, YES), ash(), 2, NSMakeRect(m.origin.x + 14, m.origin.y + 12, 60, 14), NSTextAlignmentLeft);
    }
    {   /* Examine's frame */
        NSRect e = self.examineRect; [self panelBox:e title:@"EXAMINE"];
        ktext(@(gd->tag).uppercaseString, type(12, YES), paper(), 2.4, NSMakeRect(e.origin.x, NSMaxY(e) - 24, e.size.width, 16), NSTextAlignmentCenter);
    }
    /* the item grid: each game a slot, its picture, its name, its state; one slot empty, as in the game */
    [self panelBox:NSInsetRect(self.gridRect, -8, -8) title:@"ITEMS"];
    for (NSInteger k = 0; k < 6; k++) {
        NSRect r = [self cardRect:k]; BOOL sel = k == self.game, hov = self.hover == HIT_CARD + k;
        [rgb(0.06, 0.055, 0.06, 1) setFill]; NSRectFill(r);
        [rgb(0.5, 0.48, 0.46, sel ? 0.9 : hov ? 0.7 : 0.35) setStroke]; NSBezierPath *fr = [NSBezierPath bezierPathWithRect:NSInsetRect(r, 0.5, 0.5)]; fr.lineWidth = 1; [fr stroke];
        if (k >= NG) { ktext(@"— EMPTY —", type(11, NO), rgb(0.4, 0.38, 0.37, 1), 2, NSMakeRect(r.origin.x, NSMidY(r) - 8, r.size.width, 16), NSTextAlignmentCenter); continue; }
        NSDictionary *en = [self entry:k]; BOOL have = en != nil, mounted = have && volume_ok(en[@"game"]);
        NSRect ir = NSMakeRect(r.origin.x + 6, r.origin.y + 6, r.size.width - 12, r.size.height - 40);
        NSImage *shot = self.backdrop[@(GAMES[k].id)];
        [NSGraphicsContext saveGraphicsState]; NSRectClip(ir); [rgb(0, 0, 0, 1) setFill]; NSRectFill(ir);
        if (shot) draw_fill(shot, ir, have ? (sel ? 1 : 0.75) : 0.3);
        if (!sel) { [rgb(0, 0, 0, hov ? 0.15 : 0.35) setFill]; NSRectFillUsingOperation(ir, NSCompositingOperationSourceOver); }
        [NSGraphicsContext restoreGraphicsState];
        if (GAMES[k].bonus) { NSRect rb = NSMakeRect(NSMaxX(ir) - 52, ir.origin.y + 6, 46, 15); [blood() setFill]; NSRectFill(rb);
                              text(@"BONUS", type(9.5, YES), paper(), NSMakeRect(rb.origin.x, rb.origin.y + 1, rb.size.width, 13), NSTextAlignmentCenter); }
        text(@(GAMES[k].title).uppercaseString, type(11.5, YES), have ? (sel ? paper() : rgb(0.8, 0.77, 0.72, 1)) : ash(), NSMakeRect(r.origin.x + 8, NSMaxY(r) - 31, r.size.width - 70, 16), NSTextAlignmentLeft);
        NSString *st = !have ? (suite_cfg ? @"SET UP" : @"ABSENT") : !mounted ? @"INSERT" : @"READY";
        NSColor *sc = !have ? ash() : !mounted ? rgb(0.95, 0.6, 0.3, 1) : rgb(0.45, 0.85, 0.45, 1);
        [sc setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMaxX(r) - 60, NSMaxY(r) - 25, 5, 5)] fill];
        ktext(st, type(9.5, YES), sc, 1.4, NSMakeRect(NSMaxX(r) - 52, NSMaxY(r) - 31, 46, 14), NSTextAlignmentRight);
        text([NSString stringWithFormat:@"%s · %s", GAMES[k].tag, GAMES[k].year], type(9.5, NO), ash(), NSMakeRect(r.origin.x + 8, NSMaxY(r) - 16, r.size.width - 16, 13), NSTextAlignmentLeft);
    }
    {   /* the item's file: its name, its place and year; the text is typed live; its versions as the verbs under it */
        NSRect f = self.fileRect; [self panelBox:f title:@"FILE"];
        text(@(gd->title), title_font(22), paper(), NSMakeRect(f.origin.x + 16, f.origin.y + 14, f.size.width - 32, 28), NSTextAlignmentLeft);
        ktext([NSString stringWithFormat:@"%@  ·  %s", @(gd->tag).uppercaseString, gd->year], type(10.5, YES), blood(), 2, NSMakeRect(f.origin.x + 16, f.origin.y + 40, f.size.width - 32, 14), NSTextAlignmentLeft);
        const GameDef *d = gd;
        if (d->nv > 1) for (NSInteger k = 0; k < d->nv; k++) {
            NSRect r = [self verRect:k of:d->nv]; BOOL on = k == self->ver[self.game], hov = self.hover == HIT_VER + k;
            [(on ? rgb(0.45, 0.04, 0.06, 0.9) : rgb(1, 1, 1, hov ? 0.10 : 0.05)) setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver);
            [(on ? blood() : rgb(0.5, 0.48, 0.46, 0.5)) setStroke]; NSBezierPath *vb = [NSBezierPath bezierPathWithRect:NSInsetRect(r, 0.5, 0.5)]; vb.lineWidth = 1; [vb stroke];
            BOOL have = self.installed[@(d->v[k].id)] != nil;
            text([(on ? @"▸ " : @"") stringByAppendingString:@(d->v[k].label)], type(12, on), have ? paper() : ash(), NSMakeRect(r.origin.x + 4, r.origin.y + 5, r.size.width - 8, 17), NSTextAlignmentCenter);
        }
    }
    /* the commands: as the inventory's own list, a cursor at the one under the pointer */
    NSArray *tiles = @[@"OPTIONS", @"MODS", @"SAVES", @"GAME FILES", @"UPDATES"];
    NSUInteger nmods = 0, onmods = 0; hmods_counts(@(GAMES[self.game].id), &nmods, &onmods);
    NSRect cl = NSMakeRect(642, 440, 286, 30 * NTILES + 30); [self panelBox:cl title:@"COMMANDS"];
    for (NSInteger k = 0; k < NTILES; k++) {
        NSRect r = [self tileRect:k]; BOOL hov = self.hover == HIT_OPTIONS + k || (k == 0 && self.open);
        if (hov) { [rgb(0.45, 0.04, 0.06, 0.55) setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver);
                   NSBezierPath *ar = [NSBezierPath bezierPath]; [ar moveToPoint:NSMakePoint(r.origin.x + 8, NSMidY(r) - 5)]; [ar lineToPoint:NSMakePoint(r.origin.x + 15, NSMidY(r))]; [ar lineToPoint:NSMakePoint(r.origin.x + 8, NSMidY(r) + 5)]; [ar closePath]; [paper() setFill]; [ar fill]; }
        ktext(tiles[k], type(12.5, YES), hov ? paper() : rgb(0.8, 0.77, 0.72, 1), 2, NSMakeRect(r.origin.x + 22, r.origin.y + 6, 140, 17), NSTextAlignmentLeft);
        NSString *value = k == 0 ? (self.opts.count ? self.opts[0].choices[self.opts[0].index][1] : @"") : k == 1 ? (nmods ? [NSString stringWithFormat:@"%lu of %lu on", (unsigned long)onmods, (unsigned long)nmods] : @"Add")
                        : k == 4 ? @"Nightly" : @"Open";
        text(value, type(11.5, NO), k == 1 && onmods ? blood() : ash(), NSMakeRect(r.origin.x + 150, r.origin.y + 7, r.size.width - 160, 16), NSTextAlignmentRight);
    }
    {   /* Exit and Use */
        NSRect q = self.quitRect, p = self.playRect; NSDictionary *en = [self entry:self.game]; BOOL can = en && volume_ok(en[@"game"]), ph = self.hover == HIT_PLAY;
        [self panelBox:q title:nil]; ktext(@"EXIT", type(15, YES), self.hover == HIT_QUIT ? paper() : rgb(0.8, 0.77, 0.72, 1), 3, NSMakeRect(q.origin.x, q.origin.y + 16, q.size.width, 20), NSTextAlignmentCenter);
        [[[NSGradient alloc] initWithStartingColor:can ? (ph ? rgb(0.95, 0.20, 0.20, 1) : rgb(0.80, 0.11, 0.13, 1)) : rgb(0.28, 0.11, 0.11, 1) endingColor:can ? rgb(0.42, 0.03, 0.05, 1) : rgb(0.16, 0.06, 0.06, 1)] drawInRect:p angle:-90];
        [rgb(1, 0.55, 0.5, can ? 0.75 : 0.2) setStroke]; NSBezierPath *pe = [NSBezierPath bezierPathWithRect:NSInsetRect(p, 0.5, 0.5)]; pe.lineWidth = 1; [pe stroke];
        ktext(@"USE", title_font(24), can ? paper() : ash(), 4, NSMakeRect(p.origin.x, p.origin.y + 4, p.size.width, 30), NSTextAlignmentCenter);
        ktext(@(gd->title).uppercaseString, type(9, YES), rgb(1, 0.85, 0.82, can ? 0.85 : 0.4), 1.2, NSMakeRect(p.origin.x + 6, p.origin.y + 32, p.size.width - 12, 13), NSTextAlignmentCenter);
        NSString *hint = self.status.length ? self.status : @"←→ choose  ·  ↑↓ version  ·  Return uses  ·  Esc exits";
        text(hint, type(11, NO), ash(), NSMakeRect(NSMaxX(q) + 14, q.origin.y + 17, NSMinX(p) - NSMaxX(q) - 28, 16), NSTextAlignmentCenter);
    }
    (void)b;
    if (self.open) {
        [rgb(0, 0, 0, 0.7) setFill]; NSRectFillUsingOperation(b, NSCompositingOperationSourceOver);
        NSRect pr = self.panelRect; [self panelBox:pr title:@"OPTIONS"];
        for (NSInteger k = 0; k < (NSInteger)self.opts.count; k++) {
            Opt *o = self.opts[k]; NSRect r = [self rowRect:k]; BOOL hov = self.hover == HIT_ROW + k;
            if (hov) { [rgb(0.45, 0.04, 0.06, 0.45) setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver); }
            ktext(o.title.uppercaseString, type(13, YES), paper(), 1.6, NSMakeRect(r.origin.x + 12, r.origin.y + 5, 260, 20), NSTextAlignmentLeft);
            text(o.choices[o.index][1], type(15, YES), blood(), NSMakeRect(r.origin.x + 240, r.origin.y + 4, r.size.width - 252, 22), NSTextAlignmentRight);
            text(o.choices[o.index][2], type(11, NO), ash(), NSMakeRect(r.origin.x + 12, r.origin.y + 28, r.size.width - 24, 16), NSTextAlignmentLeft);
        }
    }
}
@end

static NSAlert *alert(NSString *m, NSString *i) { NSAlert *a = [NSAlert new]; a.messageText = m; a.informativeText = i; return a; }

@interface Launcher : NSObject <NSApplicationDelegate>
@property (strong) NSWindow *window; @property (strong) HView *view; @property (strong) NSTimer *timer;
@end
@implementation Launcher
- (NSString *)prefsPath { return [home_dir() stringByAppendingPathComponent:@"launcher.plist"]; }
- (void)save
{
    NSMutableDictionary *d = [NSMutableDictionary dictionary]; HView *v = self.view;
    d[@"game"] = @(v.game); for (NSInteger k = 0; k < NG; k++) d[[NSString stringWithFormat:@"ver_%s", GAMES[k].id]] = @(v->ver[k]);
    for (Opt *o in v.opts) d[o.key] = o.value;
    [[NSFileManager defaultManager] createDirectoryAtPath:home_dir() withIntermediateDirectories:YES attributes:nil error:nil];
    [d writeToFile:self.prefsPath atomically:YES];
}
- (Opt *)opt:(NSString *)key title:(NSString *)t choices:(NSArray *)c saved:(NSDictionary *)s
{
    Opt *o = [Opt new]; o.key = key; o.title = t; o.choices = c; o.index = 0;
    for (NSInteger k = 0; k < (NSInteger)c.count; k++) if ([c[k][0] isEqual:s[key]]) o.index = k;
    return o;
}
- (void)applicationDidFinishLaunching:(NSNotification *)n
{
    NSString *res = NSBundle.mainBundle.resourcePath;
    NSDictionary *saved = [NSDictionary dictionaryWithContentsOfFile:self.prefsPath] ?: @{};
    HView *v = [[HView alloc] initWithFrame:NSMakeRect(0, 0, 960, 660)]; self.view = v; v.hover = HIT_NONE;
    v.installed = [NSDictionary dictionaryWithContentsOfFile:[res stringByAppendingPathComponent:@"games.plist"]] ?: @{};
    NSFileManager *fm = NSFileManager.defaultManager;
    if (!v.installed.count) {   /* the downloaded app: a version is installed when its setup finished with this app's code */
        NSMutableDictionary *inst = [NSMutableDictionary dictionary];
        for (NSInteger k = 0; k < NG; k++) for (int j = 0; j < GAMES[k].nv; j++) {
            const Version *ver = &GAMES[k].v[j];
            NSData *sj = [NSData dataWithContentsOfFile:[res stringByAppendingPathComponent:[NSString stringWithFormat:@"setup/%s.json", ver->bin]]];
            NSDictionary *c = sj ? [NSJSONSerialization JSONObjectWithData:sj options:0 error:nil] : nil;
            if (!c) continue;
            if (!suite_cfg) suite_cfg = [NSMutableDictionary dictionary];
            suite_cfg[@(ver->id)] = c;
            NSString *root = suite_root(c), *rc = [root stringByAppendingPathComponent:c[@"build_dir"] ?: @"recomp"];
            NSData *rj = [NSData dataWithContentsOfFile:[rc stringByAppendingPathComponent:@"ready.json"]];
            NSDictionary *ready = rj ? [NSJSONSerialization JSONObjectWithData:rj options:0 error:nil] : nil;
            if (![ready[@"code"] isEqual:c[@"code"]] || ![fm fileExistsAtPath:[rc stringByAppendingPathComponent:@"libgame.dylib"]]) continue;   /* Play sets it up */
            inst[@(ver->id)] = @{@"bin": @(ver->bin), @"exe": [rc stringByAppendingPathComponent:c[@"exe"]], @"game": [root stringByAppendingPathComponent:@"game-files"],
                                 @"data": c[@"data_dir"] ? [root stringByAppendingPathComponent:c[@"data_dir"]] : root, @"built": c[@"release"] ?: @"", @"lib": rc};
        }
        v.installed = inst;
    }
    v.art = [NSMutableDictionary dictionary]; v.backdrop = [NSMutableDictionary dictionary]; v.soft = [NSMutableDictionary dictionary]; v.blurred = [NSMutableDictionary dictionary]; v.fade = 1;
    v.slides = [NSMutableDictionary dictionary]; v.portraits = [NSMutableDictionary dictionary]; v.logos = [NSMutableDictionary dictionary];
    for (NSInteger k = 0; k < NG; k++) {
        NSDictionary *sc = suite_cfg[@(GAMES[k].v[0].id)];
        NSString *artdir = sc ? [suite_root(sc) stringByAppendingPathComponent:@"art"] : [res stringByAppendingPathComponent:@"art"];   /* the downloaded app: setup's art */
        NSImage *img = [[NSImage alloc] initWithContentsOfFile:[artdir stringByAppendingPathComponent:[NSString stringWithFormat:@"%s.png", GAMES[k].id]]];
        if (img) v.art[@(GAMES[k].id)] = img;
        /* the backdrop: the game's title screen (captured at install), else GOG's banner */
        NSImage *bd = [[NSImage alloc] initWithContentsOfFile:[artdir stringByAppendingPathComponent:[NSString stringWithFormat:@"%s-title.png", GAMES[k].id]]]
                   ?: [[NSImage alloc] initWithContentsOfFile:[artdir stringByAppendingPathComponent:[NSString stringWithFormat:@"%s-shot-1.jpg", GAMES[k].id]]]
                   ?: [[NSImage alloc] initWithContentsOfFile:[artdir stringByAppendingPathComponent:[NSString stringWithFormat:@"%s-banner.jpg", GAMES[k].id]]];
        if (bd) v.backdrop[@(GAMES[k].id)] = bd;
        {   /* the slides: its title screen, then GOG's screenshots and banner; the box; the title keyed out of the title screen */
            NSString *gid = @(GAMES[k].id); NSImage *(^pic)(NSString *) = ^NSImage *(NSString *n) {
                if (getenv("SHC_LAUNCHER_ART")) { NSImage *i = [[NSImage alloc] initWithContentsOfFile:[@(getenv("SHC_LAUNCHER_ART")) stringByAppendingPathComponent:n]]; return i.size.width > 16 ? i : nil; }
                NSImage *i = [[NSImage alloc] initWithContentsOfFile:[artdir stringByAppendingPathComponent:n]]; return i.size.width > 16 ? i : nil; };
            NSMutableArray *sl = [NSMutableArray array]; NSImage *title = pic([gid stringByAppendingString:@"-title.png"]);
            for (int j = 1; j <= 6; j++) { NSImage *im = ready(pic([NSString stringWithFormat:@"%@-shot-%d.jpg", gid, j]), 1600); if (im) [sl addObject:im]; }
            if (title && sl.count < 2) [sl insertObject:ready(title, 1400) atIndex:0];   /* (with screenshots, the title screen stays out: its title is the logo above) */
            NSImage *ban = pic([gid stringByAppendingString:@"-banner.jpg"]); if (ban) [sl insertObject:ready(ban, 1600) atIndex:MIN(sl.count, (NSUInteger)2)];
            v.slides[gid] = sl;
            NSImage *box = pic([gid stringByAppendingString:@"-store-portrait.jpg"]) ?: title ?: ban; if (box) v.portraits[gid] = ready(box, 900);
            NSImage *lg = title ? key_title(title, GAMES[k].id[0] == 'r') : nil; if (lg) v.logos[gid] = lg;   /* (Resident Evil's red, Dino Crisis' white) */
        }
        v->ver[k] = [saved[[NSString stringWithFormat:@"ver_%s", GAMES[k].id]] integerValue];
    }
    v.game = [saved[@"game"] integerValue] % NG;
    if (getenv("SHC_LAUNCHER_GAME")) v.game = atoi(getenv("SHC_LAUNCHER_GAME")) % NG;
    BOOL notch = NO; if (@available(macOS 12.0, *)) notch = NSScreen.mainScreen.safeAreaInsets.top > 0;
    NSMutableArray *opts = [NSMutableArray array];
    [opts addObject:[self opt:@"display" title:@"Display" choices:@[@[@"fullscreen", @"Full Screen", @"The whole display, the Dock and menu bar out of the way: Game Mode on"],
                                                                    @[@"window", @"Window", @"A window you can move and resize"]] saved:saved]];
    if (notch) [opts addObject:[self opt:@"notch" title:@"Around the Notch" choices:@[@[@"below", @"Below It", @"The picture under the camera housing, the band above it black"],
                                                                                         @[@"around", @"Around It", @"The picture to the top edge, the notch over it"]] saved:saved]];
    [opts addObject:[self opt:@"vsync" title:@"VSync" choices:@[@[@"1", @"On", @"Frames paced to the display: no tearing"], @[@"0", @"Off", @"Frames as soon as they are ready"]] saved:saved]];
    [opts addObject:[self opt:@"aniso" title:@"Texture Filtering" choices:@[@[@"16", @"16x Anisotropic", @"Distant textures steady at an angle"], @[@"4", @"4x Anisotropic", @"Lighter on the GPU"],
                                                                                @[@"1", @"Off (As Released)", @"Textures sampled as the games did"]] saved:saved]];
    [opts addObject:[self opt:@"movies" title:@"Movies" choices:@[@[@"on", @"On", @"The games' movies play"], @[@"off", @"Off", @"Skipped: straight to the game"]] saved:saved]];
    [opts addObject:[self opt:@"aids" title:@"Test Aids" choices:@[@[@"off", @"Off", @"The games as released"],
                                                                    @[@"on", @"On", @"Health held full (Resident Evil 1-3, The Mercenaries, Dino Crisis); Resident Evil 2: GOG's -wepall switch too"]] saved:saved]];
    v.opts = opts;
    __weak Launcher *ws = self;
    v.act = ^(NSInteger h) { [ws hit:h]; };
    if (getenv("SHC_LAUNCHER_OPEN")) v.open = YES;
    if (getenv("SHC_LAUNCHER_SHOT")) {
        v.t = getenv("SHC_LAUNCHER_TIME") ? atof(getenv("SHC_LAUNCHER_TIME")) : 4.0;
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)v.bounds.size.width * 2 pixelsHigh:(NSInteger)v.bounds.size.height * 2
                                                                    bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        rep.size = v.bounds.size;
        [v displayRectIgnoringOpacity:v.bounds inContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:rep]];
        if (getenv("SHC_LAUNCHER_TIMING")) {
            double t0 = CACurrentMediaTime();
            for (int k = 0; k < 60; k++) { v.t += 1.0 / 60; [v displayRectIgnoringOpacity:v.bounds inContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:rep]]; }
            fprintf(stderr, "frame: %.1f ms\n", (CACurrentMediaTime() - t0) * 1000 / 60);
        }
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(getenv("SHC_LAUNCHER_SHOT")) atomically:YES]; exit(0);
    }
    NSWindow *w = [[NSWindow alloc] initWithContentRect:v.frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskFullSizeContentView
                                                backing:NSBackingStoreBuffered defer:NO];
    w.titlebarAppearsTransparent = YES; w.titleVisibility = NSWindowTitleHidden; w.title = @"Survival Horror Classics"; w.backgroundColor = NSColor.blackColor;
    w.contentView = v; [w center]; w.releasedWhenClosed = NO; self.window = w;
    [w makeKeyAndOrderFront:nil]; [w makeFirstResponder:v]; [NSApp activateIgnoringOtherApps:YES];
    __block double t = 0;
    /* a controller: d-pad or left stick to move (held: repeats), A / Start to play (A changes an option), B back or quit,
     * Y the options, X the mods */
    __block int held = 0; __block double next = 0; __block uint32_t was = 0;
    [GCController startWirelessControllerDiscoveryWithCompletionHandler:nil];
    double t0 = CACurrentMediaTime();
    self.timer = [NSTimer timerWithTimeInterval:1.0 / 60 repeats:YES block:^(NSTimer *tm) { t = v.t = CACurrentMediaTime() - t0; v.glow = 0.5 + 0.5 * sin(t * 1.3);
        GCExtendedGamepad *g = (GCController.current ?: GCController.controllers.firstObject).extendedGamepad;
        if (g && NSApp.active && self.window.isKeyWindow) {
            float x = g.leftThumbstick.xAxis.value, y = g.leftThumbstick.yAxis.value;
            int dir = g.dpad.left.pressed || x < -0.6f ? NAV_LEFT : g.dpad.right.pressed || x > 0.6f ? NAV_RIGHT
                    : g.dpad.up.pressed || y > 0.6f ? NAV_UP : g.dpad.down.pressed || y < -0.6f ? NAV_DOWN : 0;
            if (dir != held) { held = dir; next = t + 0.38; if (dir) [v nav:dir]; }
            else if (dir && t >= next) { next = t + 0.14; [v nav:dir]; }
            uint32_t now = (g.buttonA.pressed ? 1u : 0) | (g.buttonB.pressed ? 2u : 0) | (g.buttonY.pressed ? 4u : 0) | (g.buttonMenu.pressed ? 8u : 0) | (g.buttonX.pressed ? 16u : 0),
                     down = now & ~was; was = now;
            if (down & 1) [v nav:NAV_OK]; else if (down & 2) [v nav:NAV_BACK]; else if (down & 4) [v nav:NAV_OPTIONS]; else if (down & 8) [v nav:v.open ? NAV_BACK : NAV_PLAY];
            else if ((down & 16) && !v.open) [v nav:NAV_MODS];
        } else { held = 0; was = 0; }
        if (v.fade < 1) v.fade = MIN(1, v.fade + 1.0 / 60 / 0.5);
        v.needsDisplay = YES;
        static double lastT; if (getenv("SHC_LAUNCHER_TIMING") && t - lastT >= 1) { fprintf(stderr, "fps: %.0f\n", frames_drawn / (t - lastT)); frames_drawn = 0; lastT = t; } }];
    [[NSRunLoop currentRunLoop] addTimer:self.timer forMode:NSRunLoopCommonModes];
    if (getenv("SHC_LAUNCHER_PLAY")) [self hit:HIT_PLAY];
    if (getenv("SHC_LAUNCHER_MODS")) [self hit:HIT_MODS];                                         /* development: the Mods window */
    if (pending_open) { NSArray *p = pending_open; pending_open = nil; dispatch_async(dispatch_get_main_queue(), ^{ [self application:NSApp openFiles:p]; }); }
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
/* the downloaded app: a version's own binary shows its setup window (Drop, Choose, Search; setup/setup_ui.m), given a
 * folder when one was dropped on the app, and comes back here when the game is ready (DS_SUITE). A second version of a
 * game already set up (Claire, The Mercenaries) builds from the copy that is there. */
- (void)setUp:(const Version *)ver folder:(NSString *)folder
{
    [self save];
    setenv("DS_SUITE", NSBundle.mainBundle.executablePath.fileSystemRepresentation, 1);
    if (folder) setenv("DS_SETUP_FOLDER", folder.fileSystemRepresentation, 1); else unsetenv("DS_SETUP_FOLDER");
    NSString *bin = [NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent stringByAppendingPathComponent:@(ver->bin)];
    char *const av[] = {(char *)bin.fileSystemRepresentation, NULL};
    execv(av[0], av);
    [alert(@"Setup did not start", @(strerror(errno))) runModal];
}
static BOOL holds_exe(NSString *dir, NSString *exe, int depth)   /* a dropped folder: the version's executable at most 3 levels down */
{
    NSFileManager *fm = NSFileManager.defaultManager;
    for (NSString *n in [fm contentsOfDirectoryAtPath:dir error:nil]) {
        if ([n caseInsensitiveCompare:exe] == NSOrderedSame) return YES;
        NSString *sub = [dir stringByAppendingPathComponent:n]; BOOL isdir = NO;
        if (depth < 3 && ![n hasPrefix:@"."] && [fm fileExistsAtPath:sub isDirectory:&isdir] && isdir && holds_exe(sub, exe, depth + 1)) return YES;
    }
    return NO;
}
- (void)application:(NSApplication *)app openFiles:(NSArray<NSString *> *)paths
{
    if (!self.view) { pending_open = (NSArray *)paths; return; }      /* the app opened by the drop: once its window is up */
    for (NSString *p in paths) for (NSInteger k = 0; k < NG; k++) {
        const Version *first = NULL;   /* a game's folder: its first version not set up yet (Leon, then Claire), else its card */
        BOOL ours = NO;
        for (int j = 0; j < GAMES[k].nv; j++) {
            NSDictionary *c = suite_cfg[@(GAMES[k].v[j].id)];
            if (!c || !holds_exe(p, c[@"exe"], 0)) continue;
            ours = YES; if (!first && !self.view.installed[@(GAMES[k].v[j].id)]) first = &GAMES[k].v[j];
        }
        if (!ours) continue;
        if (first) { [self setUp:first folder:p]; return; }
        self.view.game = k; self.view.needsDisplay = YES; return;
    }
    [alert(@"That isn't one of the games", @"Drop the folder of your GOG copy of Resident Evil, Resident Evil 2, Resident Evil 3, Dino Crisis or Dino Crisis 2 on the app.") runModal];
    [app replyToOpenOrPrint:NSApplicationDelegateReplyFailure];
}
- (void)hit:(NSInteger)h
{
    HView *v = self.view;
    if (h >= HIT_ROW) { Opt *o = v.opts[h - HIT_ROW]; o.index = (o.index + 1) % (NSInteger)o.choices.count; }
    else if (h >= HIT_VER) v->ver[v.game] = h - HIT_VER;
    else if (h >= HIT_CARD) { if (v.game != h - HIT_CARD) { v.prevGame = v.game; v.fade = 0; } v.game = h - HIT_CARD; }
    else switch (h) {
        case HIT_OPTIONS: v.open = !v.open; break;
        case HIT_MODS: { NSDictionary *e = [v entry:v.game]; const GameDef *d = &GAMES[v.game];
                         if (!e) { v.status = suite_cfg ? @"Not set up yet: Play sets it up" : @"Not installed"; break; }
                         if (!volume_ok(e[@"game"])) { v.status = [NSString stringWithFormat:@"Mods are placed by the game's own files: connect %@ first", volume_name(e[@"game"]) ?: @"its drive"]; break; }
                         hmods_show(self.window, @(d->name), @(d->id), d->gamebanana, e[@"game"], !strcmp([v version:v.game]->id, "re2c") ? @"claire" : @"leon",
                                    ^{ v.needsDisplay = YES; });
                         break; }
        case HIT_QUIT: [self save]; [NSApp terminate:nil]; return;
        case HIT_SAVES: { NSDictionary *e = [v entry:v.game]; NSString *d = e[@"data"] ?: home_dir();
                          [[NSFileManager defaultManager] createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
                          [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:d]]; break; }
        case HIT_FILES: { NSDictionary *e = [v entry:v.game];
                          if (e && volume_ok(e[@"game"])) [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:e[@"game"]]];
                          else v.status = e ? [NSString stringWithFormat:@"%s's folder isn't reachable: %@", GAMES[v.game].title, e[@"game"]] : @"Not installed"; break; }
        case HIT_UPDATES: { NSString *u = [NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"update"];
                            if (![[NSFileManager defaultManager] isExecutableFileAtPath:u]) { v.status = @"Updates: this app was not installed from a checkout"; break; }
                            v.status = suite_cfg ? @"Updating: the newest release is downloaded from GitHub; the app restarts when done"
                                                 : @"Updating: the games are rebuilt from GitHub (a few minutes); the app restarts when done";
                            [v display];
                            NSTask *task = [NSTask new]; task.launchPath = u; task.standardOutput = [NSFileHandle fileHandleWithNullDevice]; task.standardError = [NSFileHandle fileHandleWithNullDevice];
                            task.terminationHandler = ^(NSTask *tk) { dispatch_async(dispatch_get_main_queue(), ^{
                                if (tk.terminationStatus) { v.status = @"The update did not finish: see ~/Games/SurvivalHorrorClassics"; v.needsDisplay = YES; return; }
                                [[NSWorkspace sharedWorkspace] openApplicationAtURL:NSBundle.mainBundle.bundleURL configuration:[NSWorkspaceOpenConfiguration configuration] completionHandler:nil];
                                [NSApp terminate:nil]; }); };
                            [task launch]; break; }
        case HIT_PLAY: [self play]; return;
    }
    [self save]; v.needsDisplay = YES;
}
- (void)play
{
    HView *v = self.view; const Version *ver = [v version:v.game]; NSDictionary *e = [v entry:v.game];
    if (!e && suite_cfg[@(ver->id)]) { [self setUp:ver folder:nil]; return; }
    if (!e) { [alert(@"Not installed", [NSString stringWithFormat:@"Install it with the project's installer: ./install.sh --games %s", GAMES[v.game].id]) runModal]; return; }
    if (!volume_ok(e[@"game"])) {
        NSString *vol = volume_name(e[@"game"]);
        [alert(vol ? [NSString stringWithFormat:@"Insert %@", vol] : @"The game's folder is missing",
               [NSString stringWithFormat:@"%s plays from %@.", GAMES[v.game].title, e[@"game"]]) runModal];
        return;
    }
    NSString *bin = [NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent stringByAppendingPathComponent:e[@"bin"]];
    NSString *exe = [e[@"exe"] isAbsolutePath] ? e[@"exe"] : [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:e[@"exe"]];
    NSString *data = e[@"data"]; NSString *log = [data stringByAppendingPathComponent:[e[@"bin"] stringByAppendingString:@".log"]];
    NSMutableDictionary *env = [NSMutableDictionary dictionary]; for (Opt *o in v.opts) env[o.key] = o.value;
    NSMutableArray *args = [NSMutableArray array];
    if (!strcmp(ver->id, "re2") || !strcmp(ver->id, "re2c")) [args addObject:@"-mode0"];                    /* 640x480 (GOG's mode 1 draws 320x240) */
    if ([env[@"aids"] isEqual:@"on"] && (!strcmp(ver->id, "re2") || !strcmp(ver->id, "re2c"))) [args addObject:@"-wepall"];   /* GOG's own: every weapon */
    NSMutableDictionary *genv = [@{@"DS_NO_LAUNCHER": @"1", @"DS_DISPLAY": env[@"display"], @"DS_NOTCH": env[@"notch"] ?: @"below", @"DSR_VSYNC": env[@"vsync"],
                                   @"DSR_ANISO": env[@"aniso"], @"DS_LOG_FILE": log, @"DS_ARGS": [args componentsJoinedByString:@" "]} mutableCopy];
    if ([env[@"movies"] isEqual:@"off"]) { genv[@"RE_NO_MOVIES"] = @"1"; genv[@"GTA_NO_MOVIES"] = @"1"; }
    if ([env[@"aids"] isEqual:@"on"]) genv[@"SHC_AIDS"] = @"1";                                         /* re_aids.c: health held at its most */
    NSString *mods = hmods_env(@(GAMES[v.game].id)); if (mods.length) genv[@"GTA_MODS"] = mods;        /* the enabled mods (files.c, re_mods.c) */
    if (getenv("SHC_LAUNCHER_DRY")) {
        for (NSString *k in [genv.allKeys sortedArrayUsingSelector:@selector(compare:)]) printf("%s=%s\n", k.UTF8String, [genv[k] UTF8String]);
        printf("'%s' --exe '%s' --game '%s' --data '%s'\n", bin.UTF8String, exe.UTF8String, [e[@"game"] UTF8String], data.UTF8String); fflush(stdout); exit(0);
    }
    [self save];
    NSMutableDictionary *all = [NSProcessInfo.processInfo.environment mutableCopy]; [all addEntriesFromDictionary:genv];
    [all addEntriesFromDictionary:@{@"SHC_PLAY_BIN": bin, @"SHC_PLAY_EXE": exe, @"SHC_PLAY_GAME": e[@"game"], @"SHC_PLAY_DATA": data}];
    if (e[@"lib"]) all[@"SHC_PLAY_LIB"] = e[@"lib"];                     /* the downloaded app: the version's library, set up in its folder */
    [all removeObjectForKey:@"SHC_LAUNCHER_PLAY"];
    NSWorkspaceOpenConfiguration *cfg = [NSWorkspaceOpenConfiguration configuration]; cfg.createsNewApplicationInstance = YES; cfg.environment = all; cfg.activates = YES;
    [self.window orderOut:nil];
    [[NSWorkspace sharedWorkspace] openApplicationAtURL:NSBundle.mainBundle.bundleURL configuration:cfg completionHandler:^(NSRunningApplication *app, NSError *err) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (app) { [NSApp terminate:nil]; return; }
            [self.window makeKeyAndOrderFront:nil]; [alert(@"The game did not start", err.localizedDescription ?: @"") runModal]; });
    }];
}
@end

/* the game's instance (see play): straight into the game, its stderr in the game's log (the last one kept past 5 MB) */
static void exec_game(void)
{
    const char *names[4] = {"SHC_PLAY_BIN", "SHC_PLAY_EXE", "SHC_PLAY_GAME", "SHC_PLAY_DATA"}; char *v[4];
    for (int k = 0; k < 4; k++) { const char *s = getenv(names[k]); if (!s) return; v[k] = strdup(s); }
    mkdir(v[3], 0755);
    const char *log = getenv("DS_LOG_FILE");
    if (log) { struct stat st; if (!stat(log, &st) && st.st_size > 5000000) { char old[2048]; snprintf(old, sizeof old, "%.*s.old.log", (int)(strlen(log) - 4), log); rename(log, old); }
               int fd = open(log, O_WRONLY | O_APPEND | O_CREAT, 0644); if (fd >= 0) { dup2(fd, 2); close(fd); } }
    for (int k = 0; k < 4; k++) unsetenv(names[k]);
    const char *lib = getenv("SHC_PLAY_LIB");                         /* the downloaded app: bind the version's own library */
    if (lib) { setenv("DYLD_LIBRARY_PATH", lib, 1); unsetenv("SHC_PLAY_LIB"); }
    const char *argv[] = {v[0], "--exe", v[1], "--game", v[2], "--data", v[3], 0};
    execv(argv[0], (char *const *)argv);
    fprintf(stderr, "Survival Horror Classics: %s did not start: %s\n", argv[0], strerror(errno));
}
/* --install-mod <re1|re2|re3|dc> <archive> [leon|claire|both], --list-mods <game>: the Mods window's installer, from a
 * terminal (the game's folder from games.plist) */
static int mods_cli(int argc, const char **argv)
{
    NSString *game = argc > 2 ? @(argv[2]) : @"", *dir = nil;
    NSDictionary *inst = [NSDictionary dictionaryWithContentsOfFile:[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"games.plist"]];
    for (int k = 0; k < NG; k++) if ([game isEqualToString:@(GAMES[k].id)]) dir = inst[@(GAMES[k].v[0].id)][@"game"];
    if (!dir) { fprintf(stderr, "usage: %s --install-mod <re1|re2|re3|dc> <archive> [leon|claire|both] | --list-mods <game> (the game must be installed)\n", argv[0]); return 2; }
    if (!strcmp(argv[1], "--list-mods")) return hmods_cli(game, dir, nil, nil);
    if (argc < 4) { fprintf(stderr, "--install-mod: which archive?\n"); return 2; }
    if (!volume_ok(dir)) { fprintf(stderr, "the game's folder isn't reachable: %s\n", dir.UTF8String); return 1; }
    return hmods_cli(game, dir, @(argv[3]), argc > 4 ? @(argv[4]) : nil);
}
int main(int argc, const char **argv)
{
    if (getenv("SHC_PLAY_BIN")) { exec_game(); return 1; }
    if (argc > 1 && (!strcmp(argv[1], "--install-mod") || !strcmp(argv[1], "--list-mods"))) { @autoreleasepool { return mods_cli(argc, argv); } }
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication]; app.activationPolicy = NSApplicationActivationPolicyRegular;
            { NSMenu *bar = [NSMenu new]; NSMenuItem *it = [NSMenuItem new]; [bar addItem:it]; NSMenu *m = [NSMenu new];   /* Command-Q quits the launch window */
            [m addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"]; it.submenu = m; app.mainMenu = bar; }
        Launcher *l = [Launcher new]; app.delegate = l; [app run];
    }
    return 0;
}
