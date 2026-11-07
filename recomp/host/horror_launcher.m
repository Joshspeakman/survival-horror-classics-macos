/* Survival Horror Classics launch window: one app for the natively recompiled Resident Evil, Resident Evil 2,
 * Resident Evil 3: Nemesis and Dino Crisis (the player's own GOG copies). A card per game (its GOG icon, from the game's
 * folder at install time), the chosen game's versions under it (Resident Evil 2: Leon or Claire; Resident Evil 3: the
 * game or The Mercenaries), a row of tiles (Options, Saves, Game Files, Updates) and Quit / Play. The look is drawn
 * here: a dark field, a slow red bloom behind the chosen card, typewriter captions.
 *
 * This is the app's executable (Contents/MacOS/SurvivalHorrorClassics). Play opens a fresh instance of the app that
 * execs the chosen game's binary beside it (ResidentEvil, ResidentEvil2Leon, ResidentEvil2Claire, ResidentEvil3,
 * ResidentEvil3Mercenaries, DinoCrisis), so the app in the Dock is the game and Game Mode stays on. The game is told:
 *   DS_NO_LAUNCHER=1, DS_DISPLAY=window|fullscreen, DS_NOTCH=below|around, DSR_VSYNC=1|0   main.m
 *   DSR_ANISO=16|4|1                     renderer.m (texture filtering)
 *   RE_NO_MOVIES=1, GTA_NO_MOVIES=1      the movies skipped (amstream.inc, dshow.c)
 *   DS_ARGS="..."                        the game's command line: Resident Evil 2's display mode (-mode0) and its GOG
 *                                        test options (-life1 -wepall1: Test Aids)
 *   DS_LOG_FILE=<data>/<BIN>.log
 * The games stay where they were installed from (by default the Expansion Pak SD card): a game whose folder is on a
 * volume that is not mounted says so and does not play. Saves and settings: ~/Games/SurvivalHorrorClassics/<id>.
 * install.sh writes Contents/Resources/games.plist (each version's binary, executable, game and data folders, commit).
 * The choices are kept in ~/Games/SurvivalHorrorClassics/launcher.plist.
 * Development: SHC_LAUNCHER_SHOT=<png> renders the window to an image and exits (SHC_LAUNCHER_GAME=0..3 the card,
 * SHC_LAUNCHER_OPEN=1 the options open); SHC_LAUNCHER_DRY=1 prints what Play would run. */
#import <Cocoa/Cocoa.h>
#import <CoreImage/CoreImage.h>
#import <GameController/GameController.h>
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
static NSFont *title_font(CGFloat s) { return [NSFont fontWithName:@"Didot-Bold" size:s] ?: [NSFont fontWithName:@"Georgia-Bold" size:s] ?: [NSFont boldSystemFontOfSize:s]; }
static NSString *home_dir(void) { return [NSHomeDirectory() stringByAppendingPathComponent:@"Games/SurvivalHorrorClassics"]; }

/* ---- the games and their versions ---- */
typedef struct { const char *id, *label, *bin; } Version;
typedef struct { const char *id, *title, *year, *tag; int nv; Version v[2]; } GameDef;
static const GameDef GAMES[4] = {
    {"re1", "RESIDENT EVIL", "1997", "Spencer Mansion", 1, {{"re1", "Resident Evil", "ResidentEvil"}}},
    {"re2", "RESIDENT EVIL 2", "1999", "Raccoon City", 2, {{"re2", "Leon", "ResidentEvil2Leon"}, {"re2c", "Claire", "ResidentEvil2Claire"}}},
    {"re3", "RESIDENT EVIL 3", "2000", "Nemesis", 2, {{"re3", "The Game", "ResidentEvil3"}, {"re3m", "The Mercenaries", "ResidentEvil3Mercenaries"}}},
    {"dc", "DINO CRISIS", "2000", "Ibis Island", 1, {{"dc", "Dino Crisis", "DinoCrisis"}}},
};
enum { NG = 4 };

@interface Opt : NSObject
@property NSString *key, *title; @property NSArray<NSArray<NSString *> *> *choices; @property NSInteger index;   /* {value, label, note} */
@end
@implementation Opt
- (NSString *)value { return self.choices[self.index][0]; }
@end

enum { HIT_NONE = -1, HIT_PLAY = 1, HIT_QUIT, HIT_OPTIONS, HIT_SAVES, HIT_FILES, HIT_UPDATES, HIT_CARD = 100, HIT_VER = 200, HIT_ROW = 300 };
@interface HView : NSView { @public NSInteger ver[NG]; }
@property NSInteger game, hover; @property BOOL open; @property NSArray<Opt *> *opts;
@property NSDictionary *installed; @property NSMutableDictionary *art, *backdrop, *soft, *blurred; @property CGFloat glow, fade; @property NSInteger prevGame; @property NSString *status;
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
    NSImage *bl = self.blurred[@(GAMES[self.game].id)];
    if (bl) draw_fill(bl, self.bounds, 1); else { [rgb(0.08, 0.07, 0.08, 1) setFill]; NSRectFill(r); }
    [rgb(0.02, 0.015, 0.02, 0.58) setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver);   /* smoked glass: readable on light art too */
    [tint ?: rgb(1, 1, 1, 0.07) setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver);
    NSGradient *hi = [[NSGradient alloc] initWithStartingColor:rgb(1, 1, 1, 0.12) endingColor:rgb(1, 1, 1, 0)];
    [hi drawInRect:NSMakeRect(r.origin.x, r.origin.y, r.size.width, MIN(40, r.size.height)) angle:90];
    [NSGraphicsContext restoreGraphicsState];
    NSBezierPath *e = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, ew / 2, ew / 2) xRadius:rad yRadius:rad]; e.lineWidth = ew;
    [edge ?: rgb(1, 1, 1, 0.16) setStroke]; [e stroke];
}
- (BOOL)acceptsFirstResponder { return YES; }
- (NSRect)cardRect:(NSInteger)k { CGFloat w = 200, gap = 22, x0 = (self.bounds.size.width - (4 * w + 3 * gap)) / 2; return NSMakeRect(x0 + k * (w + gap), 150, w, 250); }
- (NSRect)verRect:(NSInteger)k of:(NSInteger)n { CGFloat w = 210, gap = 14, x0 = (self.bounds.size.width - (n * w + (n - 1) * gap)) / 2; return NSMakeRect(x0 + k * (w + gap), 430, w, 40); }
- (NSRect)tileRect:(NSInteger)k { CGFloat w = 122, x0 = 40; return NSMakeRect(x0 + k * (w + 12), 520, w, 48); }
- (NSRect)playRect { return NSMakeRect(self.bounds.size.width - 40 - 170, 516, 170, 56); }
- (NSRect)quitRect { return NSMakeRect(self.bounds.size.width - 40 - 170 - 14 - 110, 516, 110, 56); }
- (NSRect)panelRect { CGFloat h = 70 + 58 * (CGFloat)self.opts.count; return NSMakeRect(150, MAX(60, 300 - h / 2), self.bounds.size.width - 300, h); }
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
    for (NSInteger k = 0; k < 4; k++) if (NSPointInRect(p, [self tileRect:k])) return HIT_OPTIONS + k;
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
enum { NAV_LEFT = 1, NAV_RIGHT, NAV_UP, NAV_DOWN, NAV_OK, NAV_BACK, NAV_OPTIONS, NAV_PLAY };
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
    else if (n == NAV_BACK) self.act(HIT_QUIT);
}
- (void)keyDown:(NSEvent *)e
{
    unichar ch = e.charactersIgnoringModifiers.length ? [e.charactersIgnoringModifiers characterAtIndex:0] : 0;
    if (ch == 27) { if (self.act) self.act(self.open ? HIT_OPTIONS : HIT_QUIT); return; }
    if (ch == '\r' || ch == 3) { if (self.act && !self.open) self.act(HIT_PLAY); return; }
    if (self.open && (ch == NSUpArrowFunctionKey || ch == NSDownArrowFunctionKey || ch == NSLeftArrowFunctionKey || ch == NSRightArrowFunctionKey || ch == ' ')) {
        [self nav:ch == NSUpArrowFunctionKey ? NAV_UP : ch == NSDownArrowFunctionKey ? NAV_DOWN : ch == NSLeftArrowFunctionKey ? NAV_LEFT : ch == ' ' ? NAV_OK : NAV_RIGHT]; return; }
    if (!self.open && (ch == NSLeftArrowFunctionKey || ch == NSRightArrowFunctionKey)) { if (self.act) self.act(HIT_CARD + (self.game + (ch == NSLeftArrowFunctionKey ? NG - 1 : 1)) % NG); return; }
    if (!self.open && (ch == NSUpArrowFunctionKey || ch == NSDownArrowFunctionKey) && GAMES[self.game].nv > 1) { if (self.act) self.act(HIT_VER + (self->ver[self.game] + 1) % GAMES[self.game].nv); return; }
    [super keyDown:e];
}
- (void)drawRect:(NSRect)dirty
{
    NSRect b = self.bounds;
    [rgb(0.035, 0.03, 0.035, 1) setFill]; NSRectFill(b);
    /* the chosen game's own title screen, crossfading from the last one, darkened toward the foot */
    NSImage *now = self.soft[@(GAMES[self.game].id)] ?: self.backdrop[@(GAMES[self.game].id)], *was = self.fade < 1 ? (self.soft[@(GAMES[self.prevGame].id)] ?: self.backdrop[@(GAMES[self.prevGame].id)]) : nil;
    if (was && was != now) draw_fill(was, b, 1 - self.fade);
    draw_fill(now, b, was ? self.fade : 1);
    NSGradient *shade = [[NSGradient alloc] initWithColorsAndLocations:rgb(0, 0, 0, 0.62), 0.0, rgb(0, 0, 0, 0.42), 0.35, rgb(0, 0, 0, 0.85), 1.0, nil];
    [shade drawInRect:b angle:-90];
    NSGradient *vig = [[NSGradient alloc] initWithStartingColor:rgb(0, 0, 0, 0) endingColor:rgb(0, 0, 0, 0.55)];
    [vig drawInRect:b relativeCenterPosition:NSZeroPoint];
    /* the red bloom behind the chosen card, breathing slowly */
    NSRect cr = [self cardRect:self.game];
    NSGradient *bloom = [[NSGradient alloc] initWithColors:@[rgb(0.55, 0.04, 0.06, 0.32 + 0.12 * self.glow), rgb(0.2, 0.01, 0.02, 0.10), rgb(0, 0, 0, 0)]];
    [bloom drawFromCenter:NSMakePoint(NSMidX(cr), NSMidY(cr)) radius:0 toCenter:NSMakePoint(NSMidX(cr), NSMidY(cr)) radius:420 options:0];
    /* a grain of horizontal scan lines */
    [rgb(1, 1, 1, 0.018) setFill]; for (CGFloat y = 0; y < b.size.height; y += 3) NSRectFillUsingOperation(NSMakeRect(0, y, b.size.width, 1), NSCompositingOperationSourceOver);
    /* the title */
    { NSShadow *sh = [NSShadow new]; sh.shadowColor = rgb(0, 0, 0, 0.8); sh.shadowBlurRadius = 12; [NSGraphicsContext saveGraphicsState]; [sh set];
      text(@"SURVIVAL HORROR", title_font(40), paper(), NSMakeRect(0, 34, b.size.width, 50), NSTextAlignmentCenter); [NSGraphicsContext restoreGraphicsState]; }
    text(@"C L A S S I C S", type(15, YES), blood(), NSMakeRect(0, 86, b.size.width, 22), NSTextAlignmentCenter);
    [blood() setFill]; NSRectFill(NSMakeRect(b.size.width / 2 - 60, 116, 120, 2));
    /* the cards */
    for (NSInteger k = 0; k < NG; k++) {
        NSRect r = [self cardRect:k]; BOOL sel = k == self.game, hov = self.hover == HIT_CARD + k;
        NSDictionary *en = [self entry:k]; BOOL have = en != nil, mounted = have && volume_ok(en[@"game"]);
        [self glass:r radius:10 tint:rgb(1, 1, 1, sel ? 0.10 : hov ? 0.08 : 0.05) edge:sel ? blood() : hov ? rgb(0.8, 0.25, 0.25, 0.7) : rgb(1, 1, 1, 0.16) width:sel ? 2 : 1];
        /* the game's own title screen, framed (its GOG icon when there is none) */
        NSRect ir = NSMakeRect(r.origin.x + 12, r.origin.y + 12, r.size.width - 24, 146);
        NSImage *shot = self.backdrop[@(GAMES[k].id)], *icon = self.art[@(GAMES[k].id)];
        [NSGraphicsContext saveGraphicsState]; [[NSBezierPath bezierPathWithRoundedRect:ir xRadius:6 yRadius:6] addClip];
        [rgb(0, 0, 0, 0.5) setFill]; NSRectFillUsingOperation(ir, NSCompositingOperationSourceOver);
        if (shot) draw_fill(shot, ir, have ? (sel ? 1 : 0.8) : 0.3);
        else if (icon) [icon drawInRect:NSMakeRect(NSMidX(ir) - 60, NSMidY(ir) - 60, 120, 120) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:have ? 1 : 0.3 respectFlipped:YES hints:nil];
        if (!sel) { [rgb(0, 0, 0, 0.25) setFill]; NSRectFillUsingOperation(ir, NSCompositingOperationSourceOver); }
        [NSGraphicsContext restoreGraphicsState];
        [rgb(1, 1, 1, 0.14) setStroke]; [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(ir, 0.5, 0.5) xRadius:6 yRadius:6] stroke];
        text(@(GAMES[k].title), type(sel ? 16 : 15, YES), have ? paper() : ash(), NSMakeRect(r.origin.x + 8, r.origin.y + 168, r.size.width - 16, 22), NSTextAlignmentCenter);
        text([NSString stringWithFormat:@"%s  ·  %s", GAMES[k].tag, GAMES[k].year], type(12, NO), ash(), NSMakeRect(r.origin.x + 8, r.origin.y + 192, r.size.width - 16, 18), NSTextAlignmentCenter);
        NSString *st = !have ? @"Not installed" : !mounted ? [NSString stringWithFormat:@"Insert %@", volume_name(en[@"game"]) ?: @"its drive"] : @"Ready";
        text(st, type(11, NO), !have ? ash() : !mounted ? rgb(0.95, 0.6, 0.3, 1) : rgb(0.55, 0.75, 0.55, 1), NSMakeRect(r.origin.x + 8, r.origin.y + 218, r.size.width - 16, 16), NSTextAlignmentCenter);
    }
    /* the chosen game's versions */
    const GameDef *d = &GAMES[self.game];
    if (d->nv > 1) for (NSInteger k = 0; k < d->nv; k++) {
        NSRect r = [self verRect:k of:d->nv]; BOOL on = k == self->ver[self.game], hov = self.hover == HIT_VER + k;
        [self glass:r radius:8 tint:on ? rgb(0.75, 0.06, 0.09, 0.55) : rgb(1, 1, 1, hov ? 0.10 : 0.06) edge:on ? blood() : nil width:1];
        BOOL have = self.installed[@(d->v[k].id)] != nil;
        text(@(d->v[k].label), type(15, on), have ? paper() : ash(), NSMakeRect(r.origin.x, r.origin.y + 10, r.size.width, 22), NSTextAlignmentCenter);
    }
    else text(@"", type(12, NO), ash(), NSZeroRect, NSTextAlignmentCenter);
    /* tiles and buttons */
    NSArray *tiles = @[@"Options", @"Saves", @"Game Files", @"Updates"];
    for (NSInteger k = 0; k < 4; k++) {
        NSRect r = [self tileRect:k]; BOOL hov = self.hover == HIT_OPTIONS + k || (k == 0 && self.open);
        [self glass:r radius:8 tint:rgb(1, 1, 1, hov ? 0.11 : 0.06) edge:hov ? blood() : nil width:1];
        text(tiles[k], type(14, YES), paper(), NSMakeRect(r.origin.x, r.origin.y + 14, r.size.width, 20), NSTextAlignmentCenter);
    }
    NSRect q = self.quitRect, p = self.playRect;
    [self glass:q radius:8 tint:rgb(1, 1, 1, self.hover == HIT_QUIT ? 0.11 : 0.06) edge:nil width:1];
    text(@"Quit", type(16, YES), paper(), NSMakeRect(q.origin.x, q.origin.y + 16, q.size.width, 24), NSTextAlignmentCenter);
    NSDictionary *en = [self entry:self.game]; BOOL can = en && volume_ok(en[@"game"]);
    [(can ? (self.hover == HIT_PLAY ? rgb(0.86, 0.12, 0.14, 1) : blood()) : rgb(0.25, 0.1, 0.1, 1)) setFill]; [[NSBezierPath bezierPathWithRoundedRect:p xRadius:8 yRadius:8] fill];
    text(@"PLAY", title_font(24), can ? paper() : ash(), NSMakeRect(p.origin.x, p.origin.y + 12, p.size.width, 32), NSTextAlignmentCenter);
    if (self.status.length) text(self.status, type(12, NO), ash(), NSMakeRect(40, 584, b.size.width - 80, 18), NSTextAlignmentLeft);
    /* the options panel */
    if (self.open) {
        [rgb(0, 0, 0, 0.6) setFill]; NSRectFillUsingOperation(b, NSCompositingOperationSourceOver);
        NSRect pr = self.panelRect; [self glass:pr radius:14 tint:rgb(0.05, 0.04, 0.05, 0.55) edge:rgb(0.76, 0.09, 0.11, 0.8) width:1.5];
        text(@"OPTIONS", type(16, YES), blood(), NSMakeRect(pr.origin.x, pr.origin.y + 18, pr.size.width, 22), NSTextAlignmentCenter);
        for (NSInteger k = 0; k < (NSInteger)self.opts.count; k++) {
            Opt *o = self.opts[k]; NSRect r = [self rowRect:k]; BOOL hov = self.hover == HIT_ROW + k;
            if (hov) { [rgb(0.3, 0.05, 0.07, 0.35) setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver); }
            text(o.title, type(15, YES), paper(), NSMakeRect(r.origin.x + 12, r.origin.y + 4, 220, 22), NSTextAlignmentLeft);
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
    HView *v = [[HView alloc] initWithFrame:NSMakeRect(0, 0, 960, 610)]; self.view = v; v.hover = HIT_NONE;
    v.installed = [NSDictionary dictionaryWithContentsOfFile:[res stringByAppendingPathComponent:@"games.plist"]] ?: @{};
    v.art = [NSMutableDictionary dictionary]; v.backdrop = [NSMutableDictionary dictionary]; v.soft = [NSMutableDictionary dictionary]; v.blurred = [NSMutableDictionary dictionary]; v.fade = 1;
    for (NSInteger k = 0; k < NG; k++) {
        NSImage *img = [[NSImage alloc] initWithContentsOfFile:[res stringByAppendingPathComponent:[NSString stringWithFormat:@"art/%s.png", GAMES[k].id]]];
        if (img) v.art[@(GAMES[k].id)] = img;
        /* the backdrop: the game's title screen (captured at install), else GOG's banner */
        NSImage *bd = [[NSImage alloc] initWithContentsOfFile:[res stringByAppendingPathComponent:[NSString stringWithFormat:@"art/%s-title.png", GAMES[k].id]]]
                   ?: [[NSImage alloc] initWithContentsOfFile:[res stringByAppendingPathComponent:[NSString stringWithFormat:@"art/%s-banner.jpg", GAMES[k].id]]];
        if (bd) { v.backdrop[@(GAMES[k].id)] = bd; NSImage *f = frost(bd, 70), *g = frost(bd, 320); if (f) v.blurred[@(GAMES[k].id)] = f; if (g) v.soft[@(GAMES[k].id)] = g; }
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
                                                                    @[@"on", @"Invincible + All Weapons", @"Resident Evil 2: GOG's test options (-life, -wepall); the other games: not yet"]] saved:saved]];
    v.opts = opts;
    __weak Launcher *ws = self;
    v.act = ^(NSInteger h) { [ws hit:h]; };
    if (getenv("SHC_LAUNCHER_OPEN")) v.open = YES;
    if (getenv("SHC_LAUNCHER_SHOT")) {
        NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds]; [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(getenv("SHC_LAUNCHER_SHOT")) atomically:YES]; exit(0);
    }
    NSWindow *w = [[NSWindow alloc] initWithContentRect:v.frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskFullSizeContentView
                                                backing:NSBackingStoreBuffered defer:NO];
    w.titlebarAppearsTransparent = YES; w.titleVisibility = NSWindowTitleHidden; w.title = @"Survival Horror Classics"; w.backgroundColor = NSColor.blackColor;
    w.contentView = v; [w center]; w.releasedWhenClosed = NO; self.window = w;
    [w makeKeyAndOrderFront:nil]; [w makeFirstResponder:v]; [NSApp activateIgnoringOtherApps:YES];
    __block double t = 0;
    /* a controller: d-pad or left stick to move (held: repeats), A / Start to play (A changes an option), B back or quit,
     * Y the options */
    __block int held = 0; __block double next = 0; __block uint32_t was = 0;
    [GCController startWirelessControllerDiscoveryWithCompletionHandler:nil];
    self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30 repeats:YES block:^(NSTimer *tm) { t += 1.0 / 30; v.glow = 0.5 + 0.5 * sin(t * 1.3);
        GCExtendedGamepad *g = (GCController.current ?: GCController.controllers.firstObject).extendedGamepad;
        if (g && NSApp.active && self.window.isKeyWindow) {
            float x = g.leftThumbstick.xAxis.value, y = g.leftThumbstick.yAxis.value;
            int dir = g.dpad.left.pressed || x < -0.6f ? NAV_LEFT : g.dpad.right.pressed || x > 0.6f ? NAV_RIGHT
                    : g.dpad.up.pressed || y > 0.6f ? NAV_UP : g.dpad.down.pressed || y < -0.6f ? NAV_DOWN : 0;
            if (dir != held) { held = dir; next = t + 0.38; if (dir) [v nav:dir]; }
            else if (dir && t >= next) { next = t + 0.14; [v nav:dir]; }
            uint32_t now = (g.buttonA.pressed ? 1u : 0) | (g.buttonB.pressed ? 2u : 0) | (g.buttonY.pressed ? 4u : 0) | (g.buttonMenu.pressed ? 8u : 0), down = now & ~was; was = now;
            if (down & 1) [v nav:NAV_OK]; else if (down & 2) [v nav:NAV_BACK]; else if (down & 4) [v nav:NAV_OPTIONS]; else if (down & 8) [v nav:v.open ? NAV_BACK : NAV_PLAY];
        } else { held = 0; was = 0; }
        if (v.fade < 1) { v.fade = MIN(1, v.fade + 1.0 / 30 / 0.4); v.needsDisplay = YES; } else [v setNeedsDisplayInRect:NSInsetRect([v cardRect:v.game], -260, -140)]; }];
    if (getenv("SHC_LAUNCHER_PLAY")) [self hit:HIT_PLAY];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
- (void)hit:(NSInteger)h
{
    HView *v = self.view;
    if (h >= HIT_ROW) { Opt *o = v.opts[h - HIT_ROW]; o.index = (o.index + 1) % (NSInteger)o.choices.count; }
    else if (h >= HIT_VER) v->ver[v.game] = h - HIT_VER;
    else if (h >= HIT_CARD) { if (v.game != h - HIT_CARD) { v.prevGame = v.game; v.fade = 0; } v.game = h - HIT_CARD; }
    else switch (h) {
        case HIT_OPTIONS: v.open = !v.open; break;
        case HIT_QUIT: [self save]; [NSApp terminate:nil]; return;
        case HIT_SAVES: { NSDictionary *e = [v entry:v.game]; NSString *d = e[@"data"] ?: home_dir();
                          [[NSFileManager defaultManager] createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
                          [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:d]]; break; }
        case HIT_FILES: { NSDictionary *e = [v entry:v.game];
                          if (e && volume_ok(e[@"game"])) [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:e[@"game"]]];
                          else v.status = e ? [NSString stringWithFormat:@"%s's folder isn't reachable: %@", GAMES[v.game].title, e[@"game"]] : @"Not installed"; break; }
        case HIT_UPDATES: { NSString *u = [NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"update"];
                            if (![[NSFileManager defaultManager] isExecutableFileAtPath:u]) { v.status = @"Updates: this app was not installed from a checkout"; break; }
                            v.status = @"Updating: the games are rebuilt from GitHub (a few minutes); the app restarts when done";
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
    if (!e) { [alert(@"Not installed", [NSString stringWithFormat:@"Install it with the project's installer: ./install.sh --games %s", GAMES[v.game].id]) runModal]; return; }
    if (!volume_ok(e[@"game"])) {
        NSString *vol = volume_name(e[@"game"]);
        [alert(vol ? [NSString stringWithFormat:@"Insert %@", vol] : @"The game's folder is missing",
               [NSString stringWithFormat:@"%s plays from %@.", GAMES[v.game].title, e[@"game"]]) runModal];
        return;
    }
    NSString *bin = [NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent stringByAppendingPathComponent:e[@"bin"]];
    NSString *exe = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:e[@"exe"]];
    NSString *data = e[@"data"]; NSString *log = [data stringByAppendingPathComponent:[e[@"bin"] stringByAppendingString:@".log"]];
    NSMutableDictionary *env = [NSMutableDictionary dictionary]; for (Opt *o in v.opts) env[o.key] = o.value;
    NSMutableArray *args = [NSMutableArray array];
    if (!strcmp(ver->id, "re2") || !strcmp(ver->id, "re2c")) [args addObject:@"-mode0"];                    /* 640x480 (GOG's mode 1 draws 320x240) */
    if ([env[@"aids"] isEqual:@"on"] && (!strcmp(ver->id, "re2") || !strcmp(ver->id, "re2c"))) [args addObjectsFromArray:@[@"-life1", @"-wepall1"]];
    NSMutableDictionary *genv = [@{@"DS_NO_LAUNCHER": @"1", @"DS_DISPLAY": env[@"display"], @"DS_NOTCH": env[@"notch"] ?: @"below", @"DSR_VSYNC": env[@"vsync"],
                                   @"DSR_ANISO": env[@"aniso"], @"DS_LOG_FILE": log, @"DS_ARGS": [args componentsJoinedByString:@" "]} mutableCopy];
    if ([env[@"movies"] isEqual:@"off"]) { genv[@"RE_NO_MOVIES"] = @"1"; genv[@"GTA_NO_MOVIES"] = @"1"; }
    if (getenv("SHC_LAUNCHER_DRY")) {
        for (NSString *k in [genv.allKeys sortedArrayUsingSelector:@selector(compare:)]) printf("%s=%s\n", k.UTF8String, [genv[k] UTF8String]);
        printf("'%s' --exe '%s' --game '%s' --data '%s'\n", bin.UTF8String, exe.UTF8String, [e[@"game"] UTF8String], data.UTF8String); fflush(stdout); exit(0);
    }
    [self save];
    NSMutableDictionary *all = [NSProcessInfo.processInfo.environment mutableCopy]; [all addEntriesFromDictionary:genv];
    [all addEntriesFromDictionary:@{@"SHC_PLAY_BIN": bin, @"SHC_PLAY_EXE": exe, @"SHC_PLAY_GAME": e[@"game"], @"SHC_PLAY_DATA": data}];
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
    const char *argv[] = {v[0], "--exe", v[1], "--game", v[2], "--data", v[3], 0};
    execv(argv[0], (char *const *)argv);
    fprintf(stderr, "Survival Horror Classics: %s did not start: %s\n", argv[0], strerror(errno));
}
int main(int argc, const char **argv)
{
    (void)argc; (void)argv;
    if (getenv("SHC_PLAY_BIN")) { exec_game(); return 1; }
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication]; app.activationPolicy = NSApplicationActivationPolicyRegular;
        Launcher *l = [Launcher new]; app.delegate = l; [app run];
    }
    return 0;
}
