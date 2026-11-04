/* GTA Classics launch window: one app for the natively recompiled Grand Theft Auto III, Vice City and San Andreas. The
 * look is the GTA III launch window's (gta3_launcher.m): Steam's library art under the chosen game's logo, the game's
 * menu amber, a row of tiles (Graphics Options, Saves, Game Files, Updates) and Quit / Play, with a card per game where
 * GTA III had its strip of Liberty City. Choosing a card changes the art; a game that isn't installed is dimmed.
 *
 * This is the app's own executable (Contents/MacOS/GTAClassics), a standalone program like the Homeworld port's
 * launcher: Play execv()s the chosen game's binary beside it (Contents/MacOS/GTA3, GTAVC, GTASA) in this same process,
 * so the app in the Dock is the game and macOS keeps Game Mode on. The game is told the choices in its environment:
 *   DS_NO_LAUNCHER=1                        the game's own launch window (gta3_launcher.m) is not shown again
 *   DS_DISPLAY=window|fullscreen, DS_NOTCH=around|below   main.m (the game's window)
 *   GTA_MODE=WxH                            runtime/win32/gta3_mode.c, d3d9.c (the video mode; "" = as set in the game)
 *   DSR_VSYNC=1|0                           main.m (the Metal layer)
 *   DS_LOG_FILE=<data>/<BIN>.log            the game's log (stderr goes there too; over 5 MB it moves to <BIN>.old.log)
 * install.sh writes Contents/Resources/games.plist: each installed game's binary, executable, game and data folders
 * and the commit it was built from. The choices and the last game are kept in ~/Games/GTAClassics/launcher.plist.
 * Art: Contents/Resources/art/<gta3|vc|sa>/ (tools/gta_art.py, fetched at install time), else ~/Games/GTAClassics/art/
 * <id>/ or the game's data folder's art/; without it the titles are written.
 *
 * Development: GTAC_LAUNCHER_SHOT=<png> renders the window to an image and exits (GTAC_LAUNCHER_GAME=0/1/2 the chosen
 * card, also without a shot; GTAC_LAUNCHER_OPEN=1 the Graphics Options panel open, GTAC_LAUNCHER_HOVER=<code> an item lit);
 * GTAC_LAUNCHER_PLAY=1 presses Play at once; GTAC_LAUNCHER_DRY=1 prints what Play would run instead of running it. */
#import <Cocoa/Cocoa.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static NSColor *accent(void) { return [NSColor colorWithSRGBRed:0.98 green:0.70 blue:0.20 alpha:1]; }       /* the games' menu amber */
static NSColor *accent_dim(void) { return [NSColor colorWithSRGBRed:0.55 green:0.38 blue:0.12 alpha:1]; }
static NSColor *text_c(void) { return [NSColor colorWithSRGBRed:0.97 green:0.95 blue:0.91 alpha:1]; }
static NSColor *dim(void) { return [NSColor colorWithSRGBRed:0.66 green:0.64 blue:0.60 alpha:1]; }
static NSColor *ink(void) { return [NSColor colorWithSRGBRed:0.03 green:0.04 blue:0.06 alpha:1]; }
static NSFont *heavy(CGFloat size) { return [NSFont fontWithName:@"Futura-CondensedExtraBold" size:size] ?: [NSFont systemFontOfSize:size weight:NSFontWeightHeavy]; }
static NSFont *body(CGFloat size, BOOL bold)
{ return [NSFont fontWithName:bold ? @"AvenirNext-DemiBold" : @"AvenirNext-Medium" size:size] ?: [NSFont systemFontOfSize:size weight:bold ? NSFontWeightSemibold : NSFontWeightRegular]; }
static void on_main(dispatch_block_t b) { CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, b); CFRunLoopWakeUp(CFRunLoopGetMain()); }

// ---------------------------------------------------------------- the games
typedef struct {
    const char *id, *title, *city, *place, *bin, *exe, *dataDir, *folder, *userFiles;
    CGFloat heroX, heroY; CGFloat r, g, b;                    /* where the hero's crop sits; a colour when there is no art */
} GameDef;
static const GameDef GAMES[3] = {
    {"gta3", "Grand Theft Auto III", "LIBERTY CITY", "Liberty City", "GTA3", "gta3.exe", "GTA3Native", "GTAIII", "GTA3 User Files", 0.78, 0.3, 0.02, 0.21, 0.33},
    {"vc", "Grand Theft Auto: Vice City", "VICE CITY", "Vice City", "GTAVC", "gta-vc.exe", "GTAVCNative", "Grand Theft Auto Vice City", "GTA Vice City User Files", 0.62, 0.25, 0.45, 0.12, 0.10},
    {"sa", "Grand Theft Auto: San Andreas", "SAN ANDREAS", "San Andreas", "GTASA", "gta_sa.exe", "GTASANative", "GTA San Andreas", "GTA San Andreas User Files", 0.62, 0.2, 0.50, 0.44, 0.28},
};
enum { NGAMES = 3 };

@interface GCGame : NSObject
@property const GameDef *def; @property BOOL installed;
@property NSString *game, *data, *built, *binPath, *exePath;   /* the game's folder, its data folder, the commit it was built from */
@property NSImage *hero, *logo, *card;
@end
@implementation GCGame @end

// ---------------------------------------------------------------- settings tiles
@interface GCTile : NSObject
@property NSString *title; @property NSArray<NSDictionary *> *choices; @property NSInteger index;
@property (copy) void (^activate)(void);            /* a tile that runs something instead of cycling */
@property (copy) NSString *(^summary)(void);        /* a tile showing others' values (Graphics Options) */
@property (copy) NSDictionary *(^current)(void);    /* a tile whose label and note follow the chosen game */
@property BOOL wide;
@end
@implementation GCTile @end

// ---------------------------------------------------------------- the view
enum { HIT_NONE = -1, HIT_PLAY = 100, HIT_QUIT = 101, HIT_BACK = 102, HIT_CARD = 200, HIT_TILE = 300, HIT_ROW = 400 };   /* tiles, rows: + 10*k + 0 left / 1 value / 2 right */
enum { W = 960, H = 660 };

@interface GCLaunchView : NSView
@property NSArray<GCGame *> *games; @property NSInteger game;
@property NSArray<GCTile *> *tiles; @property NSInteger hover, pressed, focus;   /* focus: -1 the cards, else a tile */
@property NSArray<GCTile *> *rows; @property BOOL open; @property NSInteger rowFocus;   /* the Graphics Options panel */
@property NSString *hint;
@property (copy) void (^onPlay)(void); @property (copy) void (^onQuit)(void); @property (copy) void (^onChange)(void);
@end

@implementation GCLaunchView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (GCGame *)chosen { return self.games[self.game]; }
- (NSRect)cardRect:(NSInteger)i { return NSMakeRect(60 + i * 288, 318, 264, 149); }
- (NSRect)tileRect:(NSInteger)k   /* the tiles in a row (a wide one counts twice) */
{
    NSInteger units = 0, at = 0, n = (NSInteger)self.tiles.count;
    for (NSInteger j = 0; j < n; j++) { if (j == k) at = units; units += self.tiles[j].wide ? 2 : 1; }
    CGFloat u = (840 - 16 * (units - 1)) / (CGFloat)units;
    return NSMakeRect(60 + at * (u + 16), 494, self.tiles[k].wide ? 2 * u + 16 : u, 66);
}
- (NSRect)playRect { return NSMakeRect(W - 60 - 200, H - 66, 200, 46); }
- (NSRect)quitRect { return NSMakeRect(W - 60 - 200 - 20 - 150, H - 66, 150, 46); }
- (NSRect)panelRect { CGFloat h = 86 + 60 * (CGFloat)self.rows.count; return NSMakeRect(130, MAX(40, 486 - h), 700, h); }
- (NSRect)rowRect:(NSInteger)k { NSRect p = self.panelRect; return NSMakeRect(p.origin.x + 30, p.origin.y + 72 + 60 * k, p.size.width - 60, 52); }
- (NSRect)rowArrow:(NSInteger)k right:(BOOL)right { NSRect r = [self rowRect:k]; return NSMakeRect(right ? NSMaxX(r) - 34 : NSMaxX(r) - 330, r.origin.y + 3, 30, 30); }
- (NSRect)rowValue:(NSInteger)k { NSRect r = [self rowRect:k]; return NSMakeRect(NSMaxX(r) - 300, r.origin.y + 4, 262, 28); }
- (void)openPanel { self.open = YES; self.rowFocus = 0; self.hover = HIT_NONE; self.needsDisplay = YES; }
- (void)closePanel { self.open = NO; self.hover = HIT_NONE; self.needsDisplay = YES; }
- (void)choose:(NSInteger)g { if (g == self.game) return; self.game = g; self.needsDisplay = YES; if (self.onChange) self.onChange(); }
- (NSInteger)hit:(NSPoint)p
{
    if (NSPointInRect(p, self.playRect)) return HIT_PLAY;
    if (NSPointInRect(p, self.quitRect)) return self.open ? HIT_BACK : HIT_QUIT;
    if (self.open) {
        for (NSInteger k = 0; k < (NSInteger)self.rows.count; k++) {
            if (NSPointInRect(p, [self rowArrow:k right:NO])) return HIT_ROW + 10 * k;
            if (NSPointInRect(p, [self rowArrow:k right:YES])) return HIT_ROW + 10 * k + 2;
            if (NSPointInRect(p, [self rowRect:k])) return HIT_ROW + 10 * k + 1;
        }
        return NSPointInRect(p, self.panelRect) ? HIT_NONE : HIT_BACK;    /* a click outside the panel closes it */
    }
    for (NSInteger i = 0; i < NGAMES; i++) if (NSPointInRect(p, [self cardRect:i])) return HIT_CARD + i;
    for (NSInteger k = 0; k < (NSInteger)self.tiles.count; k++) if (NSPointInRect(p, [self tileRect:k])) return HIT_TILE + 10 * k + 1;
    return HIT_NONE;
}
- (void)updateTrackingAreas
{
    for (NSTrackingArea *t in self.trackingAreas) [self removeTrackingArea:t];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect owner:self userInfo:nil]];
}
- (void)mouseMoved:(NSEvent *)e { NSInteger h = [self hit:[self convertPoint:e.locationInWindow fromView:nil]]; if (h != self.hover) { self.hover = h; self.needsDisplay = YES; } }
- (void)mouseExited:(NSEvent *)e { self.hover = HIT_NONE; self.needsDisplay = YES; }
- (void)mouseDown:(NSEvent *)e
{
    NSInteger h = [self hit:[self convertPoint:e.locationInWindow fromView:nil]];
    if (h >= HIT_CARD && h < HIT_CARD + NGAMES && e.clickCount == 2) { [self choose:h - HIT_CARD]; self.pressed = HIT_NONE; if (self.onPlay) self.onPlay(); return; }   /* double-click: play it */
    self.pressed = h; self.needsDisplay = YES;
}
- (void)mouseUp:(NSEvent *)e
{
    NSInteger h = [self hit:[self convertPoint:e.locationInWindow fromView:nil]], p = self.pressed; self.pressed = HIT_NONE; self.needsDisplay = YES;
    if (h != p || h == HIT_NONE) return;
    if (h == HIT_PLAY) { if (self.onPlay) self.onPlay(); return; }
    if (h == HIT_QUIT) { if (self.onQuit) self.onQuit(); return; }
    if (h == HIT_BACK) { [self closePanel]; return; }
    if (h >= HIT_ROW) { NSInteger k = (h - HIT_ROW) / 10; self.rowFocus = k; [self stepRow:k by:(h - HIT_ROW) % 10 == 0 ? -1 : 1]; return; }
    if (h >= HIT_TILE) { NSInteger k = (h - HIT_TILE) / 10; self.focus = k; [self activate:k]; return; }
    if (h >= HIT_CARD) { self.focus = -1; [self choose:h - HIT_CARD]; }
}
- (void)activate:(NSInteger)k
{
    if (k < 0 || k >= (NSInteger)self.tiles.count) return;
    GCTile *t = self.tiles[k]; if (t.activate) t.activate(); self.needsDisplay = YES;
}
- (void)stepRow:(NSInteger)k by:(NSInteger)dir
{
    GCTile *t = self.rows[k]; NSInteger n = (NSInteger)t.choices.count; t.index = (t.index + dir + n) % n;
    self.needsDisplay = YES; if (self.onChange) self.onChange();
}
- (void)keyDown:(NSEvent *)e
{
    if (self.open) {
        NSInteger rn = (NSInteger)self.rows.count;
        switch (e.keyCode) {
        case 0x24: case 0x4c: case 0x35: [self closePanel]; return;                          /* Return, Escape: back */
        case 0x7b: [self stepRow:self.rowFocus by:-1]; return;
        case 0x7c: [self stepRow:self.rowFocus by:1]; return;
        case 0x7e: self.rowFocus = (self.rowFocus + rn - 1) % rn; self.needsDisplay = YES; return;
        case 0x7d: case 0x30: self.rowFocus = (self.rowFocus + 1) % rn; self.needsDisplay = YES; return;
        }
        [super keyDown:e]; return;
    }
    NSInteger n = (NSInteger)self.tiles.count;
    switch (e.keyCode) {
    case 0x24: case 0x4c: if (self.focus >= 0) { [self activate:self.focus]; return; }        /* Return: the tile, else Play */
                          if (self.onPlay) self.onPlay(); return;
    case 0x35: if (self.onQuit) self.onQuit(); return;                                       /* Escape */
    case 0x7b: [self choose:(self.game + NGAMES - 1) % NGAMES]; return;                     /* left / right: the games */
    case 0x7c: [self choose:(self.game + 1) % NGAMES]; return;
    case 0x7e: self.focus = self.focus < 0 ? n - 1 : self.focus - 1; self.needsDisplay = YES; return;   /* up / down: the cards, then each tile */
    case 0x7d: case 0x30: self.focus = self.focus + 1 >= n ? -1 : self.focus + 1; self.needsDisplay = YES; return;   /* also Tab */
    }
    [super keyDown:e];
}

static void draw_text(NSString *s, NSFont *f, NSColor *c, NSRect r, NSTextAlignment al, NSShadow *sh, CGFloat kern)
{
    NSMutableParagraphStyle *ps = [NSMutableParagraphStyle new]; ps.alignment = al; ps.lineBreakMode = NSLineBreakByTruncatingTail;
    NSMutableDictionary *a = [@{NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSParagraphStyleAttributeName: ps, NSKernAttributeName: @(kern)} mutableCopy];
    if (sh) a[NSShadowAttributeName] = sh;
    [s drawInRect:r withAttributes:a];
}
static void fill_over(NSRect r) { NSRectFillUsingOperation(r, NSCompositingOperationSourceOver); }
/* an image filling a rectangle, aspect kept (cropped), focusX/focusY: where the crop sits (0 left/top .. 1 right/bottom) */
static void draw_cover(NSImage *img, NSRect r, CGFloat fraction, CGFloat focusX, CGFloat focusY)
{
    NSSize is = img.size;
    CGFloat sc = MAX(r.size.width / is.width, r.size.height / is.height), w = r.size.width / sc, h = r.size.height / sc;
    NSRect from = NSMakeRect((is.width - w) * focusX, (is.height - h) * (1 - focusY), w, h);   /* NSImage rows: bottom-up */
    [img drawInRect:r fromRect:from operation:NSCompositingOperationSourceOver fraction:fraction respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
}
static void draw_fit(NSImage *img, NSRect r, CGFloat fraction)           /* aspect kept, inside r, left-aligned */
{
    CGFloat sc = MIN(r.size.width / img.size.width, r.size.height / img.size.height), w = img.size.width * sc, h = img.size.height * sc;
    [img drawInRect:NSMakeRect(r.origin.x, NSMidY(r) - h / 2, w, h) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:fraction respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
}
static void stroke_rect(NSRect r, NSColor *c, CGFloat w) { [c setStroke]; NSBezierPath *b = [NSBezierPath bezierPathWithRect:NSInsetRect(r, w / 2, w / 2)]; b.lineWidth = w; [b stroke]; }
- (void)drawButton:(NSRect)r title:(NSString *)title code:(NSInteger)code primary:(BOOL)primary enabled:(BOOL)enabled
{
    BOOL hot = self.hover == code && enabled, down = self.pressed == code && enabled;
    if (primary && !enabled) {   /* Play for a game that isn't installed: the bar, unlit */
        [[accent_dim() colorWithAlphaComponent:0.45] setFill]; fill_over(r);
        draw_text(title, heavy(25), [ink() colorWithAlphaComponent:0.75], NSMakeRect(r.origin.x, r.origin.y + 7, r.size.width, 34), NSTextAlignmentCenter, nil, 4);
    } else if (primary) {   /* the game's amber bar: solid, black lettering */
        NSShadow *glow = [NSShadow new]; glow.shadowColor = [accent() colorWithAlphaComponent:hot ? 0.75 : 0.4]; glow.shadowBlurRadius = hot ? 22 : 14; glow.shadowOffset = NSZeroSize;
        [NSGraphicsContext saveGraphicsState]; [glow set];
        [(down ? accent_dim() : hot ? [NSColor colorWithSRGBRed:1 green:0.80 blue:0.36 alpha:1] : accent()) setFill]; NSRectFill(r);
        [NSGraphicsContext restoreGraphicsState];
        draw_text(title, heavy(25), ink(), NSMakeRect(r.origin.x, r.origin.y + 7, r.size.width, 34), NSTextAlignmentCenter, nil, 4);
    } else {
        [[NSColor colorWithWhite:0.02 alpha:down ? 0.95 : 0.7] setFill]; fill_over(r);
        stroke_rect(r, hot ? accent() : [text_c() colorWithAlphaComponent:0.35], hot ? 2 : 1);
        draw_text(title, heavy(21), hot ? accent() : text_c(), NSMakeRect(r.origin.x, r.origin.y + 10, r.size.width, 30), NSTextAlignmentCenter, nil, 4);
    }
}
- (void)drawArrow:(NSRect)r right:(BOOL)right lit:(BOOL)lit
{
    NSBezierPath *p = [NSBezierPath bezierPath]; CGFloat x0 = NSMidX(r) - 4, x1 = NSMidX(r) + 4, y0 = NSMidY(r) - 7, y1 = NSMidY(r) + 7, ym = NSMidY(r);
    if (right) { [p moveToPoint:NSMakePoint(x0, y0)]; [p lineToPoint:NSMakePoint(x1, ym)]; [p lineToPoint:NSMakePoint(x0, y1)]; }
    else { [p moveToPoint:NSMakePoint(x1, y0)]; [p lineToPoint:NSMakePoint(x0, ym)]; [p lineToPoint:NSMakePoint(x1, y1)]; }
    p.lineWidth = 2.5; p.lineCapStyle = NSLineCapStyleSquare; p.lineJoinStyle = NSLineJoinStyleMiter;
    [(lit ? NSColor.whiteColor : accent()) setStroke]; [p stroke];
}
- (void)drawCard:(NSInteger)i shadow:(NSShadow *)sh
{
    GCGame *g = self.games[i]; NSRect r = [self cardRect:i];
    BOOL sel = self.game == i, hot = self.hover == HIT_CARD + i, foc = sel && self.focus == -1;
    if (sel) {                                                           /* the chosen game: an amber glow behind the card */
        NSShadow *glow = [NSShadow new]; glow.shadowColor = [accent() colorWithAlphaComponent:foc ? 0.6 : 0.4]; glow.shadowBlurRadius = 22; glow.shadowOffset = NSZeroSize;
        [NSGraphicsContext saveGraphicsState]; [glow set]; [NSColor.blackColor setFill]; NSRectFill(r); [NSGraphicsContext restoreGraphicsState];
    }
    [NSColor.blackColor setFill]; NSRectFill(r);
    if (g.card) draw_cover(g.card, r, 1, 0.5, 0.5);
    else { [[NSColor colorWithSRGBRed:g.def->r green:g.def->g blue:g.def->b alpha:1] setFill]; NSRectFill(r); }
    if (!g.installed) {                                                  /* not installed: the colour taken out */
        [[NSColor colorWithWhite:0.5 alpha:1] setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSaturation);
    }
    NSGradient *foot = [[NSGradient alloc] initWithColorsAndLocations:[ink() colorWithAlphaComponent:0], 0.0, [ink() colorWithAlphaComponent:0.45], 0.5, [ink() colorWithAlphaComponent:0.92], 1.0, nil];
    [foot drawInRect:NSMakeRect(r.origin.x, NSMidY(r) - 10, r.size.width, r.size.height / 2 + 10) angle:90];
    CGFloat shade = !g.installed ? (hot ? 0.5 : 0.62) : sel ? 0 : hot ? 0.12 : 0.36;
    if (shade > 0) { [[ink() colorWithAlphaComponent:shade] setFill]; fill_over(r); }
    NSRect lr = NSMakeRect(r.origin.x + 12, NSMaxY(r) - 60, 118, 50);
    if (g.logo) {
        NSShadow *ls = [NSShadow new]; ls.shadowColor = [NSColor colorWithWhite:0 alpha:0.9]; ls.shadowOffset = NSMakeSize(0, -2); ls.shadowBlurRadius = 6;
        [NSGraphicsContext saveGraphicsState]; [ls set]; draw_fit(g.logo, lr, g.installed ? (sel || hot ? 1 : 0.8) : 0.55); [NSGraphicsContext restoreGraphicsState];
    } else draw_text(@(g.def->title).uppercaseString, heavy(16), text_c(), NSMakeRect(lr.origin.x, NSMaxY(r) - 34, r.size.width - 24, 22), NSTextAlignmentLeft, sh, 1.5);
    draw_text(@(g.def->city), heavy(12), sel ? accent() : g.installed ? text_c() : dim(), NSMakeRect(NSMaxX(r) - 150, NSMaxY(r) - 26, 138, 16), NSTextAlignmentRight, sh, 2.5);
    if (!g.installed) {                                                  /* a tag across the top */
        NSRect tag = NSMakeRect(NSMaxX(r) - 124, r.origin.y + 10, 114, 22);
        [[NSColor colorWithWhite:0 alpha:0.75] setFill]; fill_over(tag); stroke_rect(tag, [dim() colorWithAlphaComponent:0.6], 1);
        draw_text(@"NOT INSTALLED", heavy(12), dim(), NSMakeRect(tag.origin.x, tag.origin.y + 3, tag.size.width, 16), NSTextAlignmentCenter, nil, 2);
    }
    stroke_rect(r, sel ? accent() : hot ? [accent() colorWithAlphaComponent:0.8] : [text_c() colorWithAlphaComponent:0.22], sel ? 3 : hot ? 2 : 1);
}
- (void)drawTile:(NSInteger)k shadow:(NSShadow *)sh
{
    GCTile *t = self.tiles[k]; NSRect r = [self tileRect:k]; NSDictionary *c = t.current ? t.current() : t.choices[t.index];
    BOOL hot = self.hover == HIT_TILE + 10 * k + 1, foc = self.focus == k, special = [c[@"accent"] boolValue], off = [c[@"off"] boolValue];
    [[NSColor colorWithWhite:0.03 alpha:hot ? 0.92 : 0.78] setFill]; fill_over(r);
    stroke_rect(r, (hot || foc) ? accent() : [text_c() colorWithAlphaComponent:0.16], foc ? 2 : 1);
    [((hot || foc) ? accent() : accent_dim()) setFill]; NSRectFill(NSMakeRect(r.origin.x, r.origin.y, 4, r.size.height));   /* an amber edge */
    draw_text(t.title.uppercaseString, heavy(13), (hot || foc) ? accent() : dim(), NSMakeRect(r.origin.x + 4, r.origin.y + 9, r.size.width - 4, 18), NSTextAlignmentCenter, nil, 2.5);
    NSColor *vc = off ? dim() : special ? accent() : hot ? NSColor.whiteColor : text_c();
    draw_text(t.summary ? t.summary() : c[@"label"], body(15, YES), vc, NSMakeRect(r.origin.x + 21, r.origin.y + 33, r.size.width - 38, 24), NSTextAlignmentCenter, sh, 0.2);
}
- (void)drawPanel:(NSShadow *)sh                        /* Graphics Options: a panel over the window, a row per setting */
{
    [[NSColor colorWithWhite:0 alpha:0.66] setFill]; fill_over(NSMakeRect(0, 0, W, H - 90));
    NSRect p = self.panelRect;
    NSShadow *glow = [NSShadow new]; glow.shadowColor = [NSColor colorWithWhite:0 alpha:0.9]; glow.shadowBlurRadius = 30; glow.shadowOffset = NSMakeSize(0, -8);
    [NSGraphicsContext saveGraphicsState]; [glow set]; [[NSColor colorWithSRGBRed:0.05 green:0.05 blue:0.06 alpha:0.98] setFill]; NSRectFill(p); [NSGraphicsContext restoreGraphicsState];
    [accent() setFill]; NSRectFill(NSMakeRect(p.origin.x, p.origin.y, p.size.width, 50));   /* the game's menu title bar */
    draw_text(@"FOR EVERY GAME", heavy(14), [ink() colorWithAlphaComponent:0.7], NSMakeRect(p.origin.x + 30, p.origin.y + 18, 300, 20), NSTextAlignmentLeft, nil, 2.5);
    draw_text(@"GRAPHICS OPTIONS", heavy(30), ink(), NSMakeRect(p.origin.x + 30, p.origin.y + 6, p.size.width - 60, 40), NSTextAlignmentRight, nil, 3);
    for (NSInteger k = 0; k < (NSInteger)self.rows.count; k++) {
        GCTile *t = self.rows[k]; NSRect r = [self rowRect:k]; NSDictionary *c = t.choices[t.index];
        BOOL hot = self.hover >= HIT_ROW + 10 * k && self.hover <= HIT_ROW + 10 * k + 2, foc = self.rowFocus == k;
        if (foc || hot) { [[accent() colorWithAlphaComponent:foc ? 0.16 : 0.08] setFill]; fill_over(NSInsetRect(r, -10, -2));   /* the game's highlight band */
                          if (foc) { [accent() setFill]; NSRectFill(NSMakeRect(r.origin.x - 10, r.origin.y - 2, 4, r.size.height + 4)); } }
        draw_text(t.title.uppercaseString, heavy(21), foc || hot ? accent() : text_c(), NSMakeRect(r.origin.x + 6, r.origin.y + 4, 260, 28), NSTextAlignmentLeft, sh, 2.5);
        [self drawArrow:[self rowArrow:k right:NO] right:NO lit:self.hover == HIT_ROW + 10 * k];
        [self drawArrow:[self rowArrow:k right:YES] right:YES lit:self.hover == HIT_ROW + 10 * k + 2];
        draw_text(c[@"label"], body(18, YES), self.hover == HIT_ROW + 10 * k + 1 ? NSColor.whiteColor : text_c(), [self rowValue:k], NSTextAlignmentCenter, sh, 0.3);
        draw_text(c[@"note"] ?: @"", body(12, NO), dim(), NSMakeRect(r.origin.x + 6, r.origin.y + 32, r.size.width - 12, 18), NSTextAlignmentRight, nil, 0);
        if (k + 1 < (NSInteger)self.rows.count) { [[text_c() colorWithAlphaComponent:0.1] setFill]; fill_over(NSMakeRect(r.origin.x, NSMaxY(r) + 4, r.size.width, 1)); }
    }
}
- (NSString *)cardHint:(NSInteger)i
{
    GCGame *g = self.games[i];
    if (!g.installed) return [NSString stringWithFormat:@"%s isn't installed: ./install.sh --games %s", g.def->place, g.def->id];
    return [NSString stringWithFormat:@"%s · double-click or Return to play", g.def->title];
}
- (NSString *)currentHint
{
    if (self.open) return @"←/→ change  ·  ↑/↓ move  ·  Return or Esc goes back";
    if (self.hover >= HIT_CARD && self.hover < HIT_CARD + NGAMES) return [self cardHint:self.hover - HIT_CARD];
    NSInteger k = -1;
    if (self.hover >= HIT_TILE && self.hover < HIT_ROW) k = (self.hover - HIT_TILE) / 10; else if (self.hover == HIT_NONE && self.focus >= 0) k = self.focus;
    if (k >= 0 && k < (NSInteger)self.tiles.count) { GCTile *t = self.tiles[k]; return (t.current ? t.current() : t.choices[t.index])[@"note"]; }
    if (self.hover == HIT_PLAY && !self.chosen.installed) return [self cardHint:self.game];
    return self.hint;
}
- (void)drawRect:(NSRect)dirty
{
    NSRect b = self.bounds; GCGame *g = self.chosen;
    NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.85]; sh.shadowOffset = NSMakeSize(0, -1); sh.shadowBlurRadius = 3;
    [ink() setFill]; NSRectFill(b);
    /* the chosen game's Steam library art across the top; it fades into the dark below and behind the logo */
    NSRect band = NSMakeRect(0, 0, W, 360);
    if (g.hero) draw_cover(g.hero, band, 1, g.def->heroX, g.def->heroY);
    else { [[NSColor colorWithSRGBRed:g.def->r green:g.def->g blue:g.def->b alpha:1] setFill]; NSRectFill(band); }
    if (!g.installed) { [[NSColor colorWithWhite:0.5 alpha:1] setFill]; NSRectFillUsingOperation(band, NSCompositingOperationSaturation);
                        [[ink() colorWithAlphaComponent:0.35] setFill]; fill_over(band); }
    NSGradient *left = [[NSGradient alloc] initWithColorsAndLocations:[ink() colorWithAlphaComponent:0.92], 0.0, [ink() colorWithAlphaComponent:0.55], 0.42, [ink() colorWithAlphaComponent:0], 0.7, nil];
    [left drawInRect:band angle:0];
    NSGradient *foot = [[NSGradient alloc] initWithColorsAndLocations:[ink() colorWithAlphaComponent:0], 0.0, [ink() colorWithAlphaComponent:0.7], 0.6, ink(), 1.0, nil];
    [foot drawInRect:NSMakeRect(0, 150, W, 212) angle:90];
    /* the logo and what this is */
    if (g.logo) {
        NSShadow *ls = [NSShadow new]; ls.shadowColor = [NSColor colorWithWhite:0 alpha:0.9]; ls.shadowOffset = NSMakeSize(0, -4); ls.shadowBlurRadius = 14;
        [NSGraphicsContext saveGraphicsState]; [ls set]; draw_fit(g.logo, NSMakeRect(56, 30, 330, 190), 1); [NSGraphicsContext restoreGraphicsState];
    } else draw_text(@(g.def->title).uppercaseString, heavy(46), text_c(), NSMakeRect(60, 100, 700, 60), NSTextAlignmentLeft, sh, 2);
    [accent() setFill]; NSRectFill(NSMakeRect(60, 240, 4, 38));
    draw_text(@"NATIVE FOR APPLE SILICON", heavy(19), text_c(), NSMakeRect(74, 238, 420, 24), NSTextAlignmentLeft, sh, 2.5);
    NSString *tag = g.installed ? [NSString stringWithFormat:@"%s on Metal · your Steam copy, recompiled", g.def->place]
                                : [NSString stringWithFormat:@"Not installed · ./install.sh --games %s", g.def->id];
    draw_text(tag, body(13, NO), accent(), NSMakeRect(74, 261, 520, 20), NSTextAlignmentLeft, sh, 0.3);
    /* the games */
    draw_text(@"THE CLASSICS", heavy(13), dim(), NSMakeRect(60, 296, 300, 18), NSTextAlignmentLeft, nil, 3);
    draw_text(@(g.def->title).uppercaseString, heavy(13), accent(), NSMakeRect(W - 60 - 500, 296, 500, 18), NSTextAlignmentRight, nil, 3);
    NSInteger hov = self.hover, foc = self.focus; if (self.open) self.hover = self.focus = HIT_NONE;   /* under the panel: nothing lit */
    for (NSInteger i = 0; i < NGAMES; i++) [self drawCard:i shadow:sh];
    for (NSInteger k = 0; k < (NSInteger)self.tiles.count; k++) [self drawTile:k shadow:sh];
    self.hover = hov; self.focus = foc;
    [[text_c() colorWithAlphaComponent:0.12] setFill]; fill_over(NSMakeRect(60, H - 90, 840, 1));
    if (self.open) [self drawPanel:sh];
    NSString *hint = [self currentHint];
    if (hint.length) draw_text(hint, body(12.5, NO), dim(), NSMakeRect(60, H - 58, 420, 36), NSTextAlignmentLeft, sh, 0.2);
    [self drawButton:self.quitRect title:self.open ? @"BACK" : @"QUIT" code:self.open ? HIT_BACK : HIT_QUIT primary:NO enabled:YES];
    [self drawButton:self.playRect title:@"PLAY" code:HIT_PLAY primary:YES enabled:g.installed];
}
@end

// ---------------------------------------------------------------- updates (the other ports' updater)
enum { UPD_NONE, UPD_CHECKING, UPD_CURRENT, UPD_AVAILABLE, UPD_ERROR };
static NSString *short_commit(NSString *c) { return c.length > 7 ? [c substringToIndex:7] : (c ?: @""); }
static NSString *update_check(NSString *updater)     /* the newest commit of the branch on GitHub; nil if it can't be reached */
{
    NSTask *t = [NSTask new]; t.executableURL = [NSURL fileURLWithPath:@"/bin/bash"]; t.arguments = @[updater, @"--check"];
    NSPipe *p = [NSPipe pipe]; t.standardOutput = p; t.standardError = [NSFileHandle fileHandleWithNullDevice];
    if (![t launchAndReturnError:nil]) return nil;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC), dispatch_get_global_queue(0, 0), ^{ if (t.running) [t terminate]; });
    NSData *d = [p.fileHandleForReading readDataToEndOfFile]; [t waitUntilExit];
    NSString *out = [[[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    return t.terminationStatus == 0 && out.length >= 40 ? [out substringToIndex:40] : nil;
}
static NSArray<NSDictionary *> *update_choice(int state, NSString *branch, NSString *built, NSString *latest)
{
    NSString *label = @"Unavailable", *note = @"This copy wasn't installed from the GitHub repository"; BOOL lit = NO;
    switch (state) {
    case UPD_CHECKING: label = @"Checking…"; note = [NSString stringWithFormat:@"Asking GitHub for the newest %@", branch]; break;
    case UPD_CURRENT: label = @"Up to Date"; note = [NSString stringWithFormat:@"%@ %@ · click to check again", branch, short_commit(built)]; break;
    case UPD_AVAILABLE: label = @"Update Ready"; lit = YES; note = [NSString stringWithFormat:@"%@ → %@: click to install (every installed game is rebuilt)", short_commit(built), short_commit(latest)]; break;
    case UPD_ERROR: label = @"Can’t Check"; note = @"GitHub unreachable or not signed in: click to retry"; break;
    }
    return @[@{@"value": @"", @"label": label, @"note": note, @"accent": @(lit)}];
}
static NSString *update_phase(NSString *line, NSString *branch)
{
    if ([line hasPrefix:@"== fetching"]) return @"Downloading the update from GitHub";
    if ([line hasPrefix:[NSString stringWithFormat:@"== %@:", branch]]) return [@"Got " stringByAppendingString:[line substringFromIndex:branch.length + 4]];
    if ([line hasPrefix:@"== recompiling "]) {                          /* "== recompiling <game> from <folder> (...)" */
        NSString *rest = [line substringFromIndex:15]; NSRange f = [rest rangeOfString:@" from "];
        return [NSString stringWithFormat:@"Rebuilding %@ from your copy: translating it to native code", f.location != NSNotFound ? [rest substringToIndex:f.location] : rest];
    }
    if ([line hasPrefix:@"== installing"]) return @"Installing the new app";
    return nil;
}
@interface GCBlockTarget : NSObject
@property (copy) void (^fire)(void);
- (void)clicked:(id)b;
@end
@implementation GCBlockTarget
- (void)clicked:(id)b { (void)b; if (self.fire) self.fire(); }
@end
static BOOL update_run(NSWindow *parent, NSString *updater, NSString *branch, NSString *logPath)   /* modal progress; YES when rebuilt */
{
    NSPanel *pn = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 520, 170) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    pn.title = @"Updating";
    NSTextField *title = [NSTextField labelWithString:@"Updating to the newest version"]; title.font = [NSFont boldSystemFontOfSize:14]; title.frame = NSMakeRect(20, 128, 480, 22);
    NSTextField *status = [NSTextField wrappingLabelWithString:@"Starting…"]; status.frame = NSMakeRect(20, 64, 480, 56);
    NSProgressIndicator *spin = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 44, 480, 12)]; spin.indeterminate = YES; [spin startAnimation:nil];
    NSTextField *time = [NSTextField labelWithString:@"A few minutes for each game. Your saves and settings are kept."]; time.font = [NSFont systemFontOfSize:11]; time.textColor = NSColor.secondaryLabelColor; time.frame = NSMakeRect(20, 14, 370, 18);
    NSButton *btn = [NSButton buttonWithTitle:@"Close" target:nil action:nil]; btn.frame = NSMakeRect(400, 8, 100, 30); btn.hidden = YES;
    for (NSView *x in @[title, status, spin, time, btn]) [pn.contentView addSubview:x];
    if (parent) [pn setFrameOrigin:NSMakePoint(NSMidX(parent.frame) - 260, NSMidY(parent.frame) - 85)]; else [pn center];
    [[NSFileManager defaultManager] createFileAtPath:logPath contents:nil attributes:nil];
    NSFileHandle *log = [NSFileHandle fileHandleForWritingAtPath:logPath];
    NSTask *t = [NSTask new]; t.executableURL = [NSURL fileURLWithPath:@"/bin/bash"]; t.arguments = @[updater];
    NSPipe *pipe = [NSPipe pipe]; t.standardOutput = pipe; t.standardError = pipe;
    NSMutableString *pending = [NSMutableString new], *tail = [NSMutableString new];
    pipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle *h) {
        NSData *d = h.availableData; if (!d.length) return;
        [log writeData:d];
        NSString *chunk = [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] ?: @"";
        @synchronized (pending) {
            [pending appendString:chunk]; NSRange nl;
            while ((nl = [pending rangeOfString:@"\n"]).location != NSNotFound) {
                NSString *line = [pending substringToIndex:nl.location]; [pending deleteCharactersInRange:NSMakeRange(0, nl.location + 1)];
                [tail appendFormat:@"%@\n", line]; if (tail.length > 600) [tail deleteCharactersInRange:NSMakeRange(0, tail.length - 600)];
                NSString *ph = update_phase(line, branch); if (ph) on_main(^{ status.stringValue = ph; });
            }
        }
    };
    __block BOOL ok = NO;
    __attribute__((objc_precise_lifetime)) GCBlockTarget *bt = [GCBlockTarget new];   /* a button's target is weak: kept here */
    bt.fire = ^{ [NSApp stopModal]; }; btn.target = bt; btn.action = @selector(clicked:);
    t.terminationHandler = ^(NSTask *task) {
        int code = task.terminationStatus;
        on_main(^{
            pipe.fileHandleForReading.readabilityHandler = nil; [log closeFile];
            [spin stopAnimation:nil]; spin.hidden = YES; btn.hidden = NO;
            if (code == 0) { ok = YES; title.stringValue = @"Updated"; status.stringValue = @"The new version is installed. The app restarts when you press Restart."; btn.title = @"Restart"; }
            else {
                NSString *last; @synchronized (pending) { last = [[tail componentsSeparatedByString:@"\n"] componentsJoinedByString:@" "]; }
                title.stringValue = @"The update didn't finish";
                status.stringValue = [NSString stringWithFormat:@"Nothing was changed if it stopped before installing. Details: %@  (%@)", logPath, last.length > 160 ? [last substringFromIndex:last.length - 160] : last];
                time.stringValue = @"";
            }
        });
    };
    if (![t launchAndReturnError:nil]) { [log closeFile]; return NO; }
    [pn makeKeyAndOrderFront:nil]; [NSApp runModalForWindow:pn]; [pn orderOut:nil];
    return ok;
}

// ---------------------------------------------------------------- images
static NSImage *image_at(NSArray<NSString *> *dirs, NSString *name)
{
    for (NSString *d in dirs) { NSImage *i = [[NSImage alloc] initWithContentsOfFile:[d stringByAppendingPathComponent:name]]; if (i.size.width > 16) return i; }
    return nil;
}
static NSImage *trimmed(NSImage *img)   /* a logo without its empty margins (Steam's logos sit in 640x360 with differing room) */
{
    CGImageRef cg = [img CGImageForProposedRect:NULL context:nil hints:nil]; if (!cg) return img;
    size_t w = CGImageGetWidth(cg), h = CGImageGetHeight(cg);
    uint8_t *px = calloc(w * h, 4); if (!px) return img;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(px, w, h, 8, w * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast); CGColorSpaceRelease(cs);
    if (!ctx) { free(px); return img; }
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cg);
    size_t x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (size_t y = 0; y < h; y++) for (size_t x = 0; x < w; x++) if (px[(y * w + x) * 4 + 3] > 8) {
        if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
    }
    CGContextRelease(ctx); free(px);
    if (x1 <= x0 || y1 <= y0) return img;
    CGImageRef sub = CGImageCreateWithImageInRect(cg, CGRectMake(x0, y0, x1 - x0 + 1, y1 - y0 + 1));   /* rows from the top */
    NSImage *out = [[NSImage alloc] initWithCGImage:sub size:NSMakeSize(x1 - x0 + 1, y1 - y0 + 1)]; CGImageRelease(sub);
    return out;
}

// ---------------------------------------------------------------- the launcher
static NSInteger index_of(NSArray<NSDictionary *> *c, id v, NSInteger dflt)
{ for (NSUInteger k = 0; k < c.count; k++) if ([c[k][@"value"] isEqual:v]) return (NSInteger)k; return dflt; }
static NSString *val(GCTile *t) { return t.choices[t.index][@"value"]; }
/* the game's video mode for these choices (gta3_launcher.m mode_for): 4:3, as tall as the screen (below the notch's band
 * if so) or the window, in points or (Retina) pixels; "" = as set in the game */
static NSString *mode_for(NSString *display, NSString *notch, NSString *res)
{
    if ([res isEqualToString:@"game"]) return @"";
    NSScreen *s = NSScreen.mainScreen; CGFloat h = s.frame.size.height;
    if ([display isEqualToString:@"window"]) h = floor(s.visibleFrame.size.height * 0.78) - 28;   /* main.m's window, less its title bar */
    else if (![notch isEqualToString:@"around"]) { if (@available(macOS 12.0, *)) h -= s.safeAreaInsets.top; }
    if ([res isEqualToString:@"retina"]) h *= s.backingScaleFactor;
    int ih = (int)floor(h) & ~1, iw = (int)lround(ih * 4.0 / 3) & ~1;
    if (ih < 480) { ih = 480; iw = 640; }
    return [NSString stringWithFormat:@"%dx%d", iw, ih];
}
static NSAlert *alert(NSString *msg, NSString *info) { NSAlert *a = [NSAlert new]; a.messageText = msg; a.informativeText = info; return a; }

@interface Launcher : NSObject <NSApplicationDelegate>
@property (strong) NSWindow *window; @property (strong) GCLaunchView *view;
@property (strong) GCTile *display, *notch, *resolution, *vsync, *updates;
@property NSString *home, *plistPath;
@end

@implementation Launcher
- (void)applicationDidFinishLaunching:(NSNotification *)n
{
    self.home = [NSHomeDirectory() stringByAppendingPathComponent:@"Games/GTAClassics"];
    self.plistPath = [self.home stringByAppendingPathComponent:@"launcher.plist"];
    NSDictionary *saved = [NSDictionary dictionaryWithContentsOfFile:self.plistPath] ?: @{};
    NSString *macos = NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent, *res = NSBundle.mainBundle.resourcePath ?: @"";
    NSDictionary *installed = [NSDictionary dictionaryWithContentsOfFile:[res stringByAppendingPathComponent:@"games.plist"]] ?: @{};
    NSFileManager *fm = NSFileManager.defaultManager;

    NSMutableArray<GCGame *> *games = [NSMutableArray array];
    for (int i = 0; i < NGAMES; i++) {
        const GameDef *d = &GAMES[i]; NSDictionary *e = installed[@(d->id)];
        GCGame *g = [GCGame new]; g.def = d;
        g.data = e[@"data"] ?: [NSHomeDirectory() stringByAppendingPathComponent:[@"Games" stringByAppendingPathComponent:@(d->dataDir)]];
        g.game = e[@"game"] ?: [g.data stringByAppendingPathComponent:[@"game-files" stringByAppendingPathComponent:@(d->folder)]];
        g.built = e[@"built"] ?: @"";
        g.binPath = [macos stringByAppendingPathComponent:e[@"bin"] ?: @(d->bin)];
        g.exePath = [res stringByAppendingPathComponent:e[@"exe"] ?: @(d->exe)];
        g.installed = e && [fm isExecutableFileAtPath:g.binPath] && [fm fileExistsAtPath:g.exePath];
        NSArray *art = @[[res stringByAppendingPathComponent:[@"art" stringByAppendingPathComponent:@(d->id)]],
                         [self.home stringByAppendingPathComponent:[@"art" stringByAppendingPathComponent:@(d->id)]], [g.data stringByAppendingPathComponent:@"art"]];
        g.hero = image_at(art, @"hero.jpg"); g.card = image_at(art, @"shot1.jpg");
        NSImage *logo = image_at(art, @"logo.png"); g.logo = logo ? trimmed(logo) : nil;
        [games addObject:g];
    }

    GCLaunchView *v = [[GCLaunchView alloc] initWithFrame:NSMakeRect(0, 0, W, H)]; self.view = v;
    v.hover = v.pressed = HIT_NONE; v.focus = -1; v.games = games;
    NSInteger first = 0; while (first < NGAMES - 1 && !games[first].installed) first++;
    v.game = 0; for (int i = 0; i < NGAMES; i++) if ([saved[@"game"] isEqual:@(GAMES[i].id)]) v.game = i;
    if (!saved[@"game"] || !games[v.game].installed) v.game = games[first].installed ? first : v.game;
    v.hint = @"←/→ choose a game · ↑/↓ settings · Return plays · Esc quits";

    NSScreen *scr = NSScreen.mainScreen;
    CGFloat notchH = 0; if (@available(macOS 12.0, *)) notchH = scr.safeAreaInsets.top;
    GCTile *display = self.display = [GCTile new], *notch = self.notch = [GCTile new], *resTile = self.resolution = [GCTile new], *vsync = self.vsync = [GCTile new];
    display.title = @"Display";
    display.choices = @[@{@"value": @"fullscreen", @"label": @"Full Screen", @"note": @"The whole display, the Dock and menu bar out of the way: Game Mode on"},
                        @{@"value": @"window", @"label": @"Window", @"note": @"A window on the desktop (4:3, resizable); ⌘-Tab and the Dock as usual"}];
    display.index = index_of(display.choices, saved[@"display"], 0);
    notch.title = @"Notch";
    notch.choices = @[@{@"value": @"below", @"label": @"Below It", @"note": @"The picture under the camera housing, the band above it black"},
                      @{@"value": @"around", @"label": @"Around It", @"note": @"The whole display's height: the notch over the top of the picture"}];
    notch.index = notchH > 0 ? index_of(notch.choices, saved[@"notch"], 0) : 0;
    resTile.title = @"Resolution";
    resTile.choices = @[@{@"value": @"standard", @"label": @"Standard", @"note": [NSString stringWithFormat:@"Display height in points (%@), scaled up by macOS", mode_for(@"fullscreen", @"around", @"standard")]},
                        @{@"value": @"retina", @"label": @"Retina", @"note": [NSString stringWithFormat:@"Every pixel (%@ full screen): the sharpest picture", mode_for(@"fullscreen", @"around", @"retina")]},
                        @{@"value": @"game", @"label": @"As Set in Game", @"note": @"Each game's own display menu decides (640 × 480 at first)"}];
    if (scr.backingScaleFactor <= 1) resTile.choices = @[resTile.choices[0], resTile.choices[2]];
    resTile.index = index_of(resTile.choices, saved[@"resolution"], 0);
    NSInteger hz = scr.maximumFramesPerSecond; if (hz <= 0) hz = 60;
    vsync.title = @"VSync";
    vsync.choices = @[@{@"value": @"1", @"label": @"On", @"short": @"VSync", @"note": [NSString stringWithFormat:@"Frames paced to the display's %ld Hz: no tearing (the games' own limiters keep 30)", (long)hz]},
                      @{@"value": @"0", @"label": @"Off", @"short": @"No VSync", @"note": @"Frames shown as soon as they're drawn: lowest latency, may tear"}];
    vsync.index = index_of(vsync.choices, saved[@"vsync"], 0);
    v.rows = notchH > 0 ? @[display, notch, resTile, vsync] : @[display, resTile, vsync];

    GCTile *gfx = [GCTile new]; gfx.title = @"Graphics Options"; gfx.wide = YES;
    gfx.choices = @[@{@"value": @"", @"label": @"", @"note": notchH > 0 ? @"Display, notch, resolution and VSync, for every game" : @"Display, resolution and VSync, for every game"}];
    gfx.summary = ^NSString * {
        NSMutableArray *a = [NSMutableArray array];
        for (GCTile *t in @[display, resTile, vsync]) [a addObject:t.choices[t.index][@"short"] ?: t.choices[t.index][@"label"]];
        return [a componentsJoinedByString:@"  ·  "];
    };
    __weak GCLaunchView *wv = v; gfx.activate = ^{ [wv openPanel]; };
    NSString *(^userFiles)(GCGame *) = ^NSString *(GCGame *g) {
        return [g.data stringByAppendingPathComponent:[@"drive_c/Users/player/Documents" stringByAppendingPathComponent:@(g.def->userFiles)]];
    };
    GCTile *saves = [GCTile new]; saves.title = @"Saves";
    saves.current = ^NSDictionary * {
        GCGame *g = wv.chosen;
        if (!g.installed) return @{@"label": @"—", @"off": @YES, @"note": [NSString stringWithFormat:@"%s isn't installed", g.def->place]};
        return @{@"label": @"Open", @"note": [@"Saved games and settings: " stringByAppendingString:userFiles(g).stringByAbbreviatingWithTildeInPath]};
    };
    saves.activate = ^{
        GCGame *g = wv.chosen; if (!g.installed) { NSBeep(); return; }
        NSString *u = userFiles(g); [[NSFileManager defaultManager] createDirectoryAtPath:u withIntermediateDirectories:YES attributes:nil error:nil];
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:u isDirectory:YES]];
    };
    GCTile *files = [GCTile new]; files.title = @"Game Files";
    files.current = ^NSDictionary * {
        GCGame *g = wv.chosen;
        if (!g.installed) return @{@"label": @"—", @"off": @YES, @"note": [NSString stringWithFormat:@"%s isn't installed", g.def->place]};
        return @{@"label": @"Open", @"note": [@"The game's own folder (models, audio, skins): " stringByAppendingString:g.game.stringByAbbreviatingWithTildeInPath]};
    };
    files.activate = ^{
        GCGame *g = wv.chosen; if (!g.installed || ![[NSFileManager defaultManager] fileExistsAtPath:g.game]) { NSBeep(); return; }
        [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:g.game isDirectory:YES]];
    };
    [self setupUpdates];
    v.tiles = @[gfx, saves, files, self.updates];
    __weak Launcher *ws = self;
    v.onPlay = ^{ [ws play]; };
    v.onQuit = ^{ [NSApp terminate:nil]; };
    v.onChange = ^{ [ws save]; };

    const char *shot = getenv("GTAC_LAUNCHER_SHOT");
    if (getenv("GTAC_LAUNCHER_GAME")) v.game = MAX(0, MIN(NGAMES - 1, atoi(getenv("GTAC_LAUNCHER_GAME"))));   /* development */
    if (shot) {                                                          /* development: the window as an image */
        if (getenv("GTAC_LAUNCHER_HOVER")) v.hover = atoi(getenv("GTAC_LAUNCHER_HOVER"));
        if (getenv("GTAC_LAUNCHER_OPEN")) v.open = YES;
        NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds]; [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(shot) atomically:YES];
        exit(0);
    }
    NSMenu *bar = [NSMenu new], *appm = [NSMenu new]; NSMenuItem *it = [NSMenuItem new]; it.submenu = appm; [bar addItem:it];
    [appm addItemWithTitle:@"Quit GTA Classics" action:@selector(terminate:) keyEquivalent:@"q"]; NSApp.mainMenu = bar;
    NSWindow *w = [[NSWindow alloc] initWithContentRect:v.frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskFullSizeContentView
                                                backing:NSBackingStoreBuffered defer:NO];
    w.releasedWhenClosed = NO; w.titlebarAppearsTransparent = YES; w.titleVisibility = NSWindowTitleHidden; w.movableByWindowBackground = YES;
    w.title = @"GTA Classics"; w.backgroundColor = NSColor.blackColor; w.contentView = v;
    self.window = w; [w center]; [w makeKeyAndOrderFront:nil]; [w makeFirstResponder:v];
    [NSApp activateIgnoringOtherApps:YES];
    if (getenv("GTAC_LAUNCHER_PLAY")) dispatch_async(dispatch_get_main_queue(), ^{ [ws play]; });   /* development: Play at once */
}
- (void)setupUpdates
{
    NSDictionary *info = NSBundle.mainBundle.infoDictionary;
    NSString *updater = [NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"update"];
    if (![[NSFileManager defaultManager] isExecutableFileAtPath:updater]) updater = nil;
    NSString *branch = info[@"GTACBranch"] ?: @"main", *built = info[@"GTACBuilt"] ?: @"";
    GCTile *u = [GCTile new]; u.title = @"Updates"; self.updates = u;
    __block int state = updater ? UPD_CHECKING : UPD_NONE; __block NSString *latest = nil;
    u.choices = update_choice(state, branch, built, nil);
    __weak GCTile *wu = u; __weak Launcher *ws = self;
    void (^check)(void) = ^{
        if (!updater) return;
        state = UPD_CHECKING; wu.choices = update_choice(state, branch, built, nil); ws.view.needsDisplay = YES;
        void (^done)(NSString *) = ^(NSString *l) {
            latest = l; state = !l ? UPD_ERROR : [l isEqualToString:built] ? UPD_CURRENT : UPD_AVAILABLE;
            wu.choices = update_choice(state, branch, built, l); ws.view.needsDisplay = YES;
        };
        if (getenv("GTAC_LAUNCHER_SHOT")) { done(update_check(updater)); return; }
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ NSString *l = update_check(updater); on_main(^{ done(l); }); });
    };
    check();
    u.activate = ^{
        if (state == UPD_CHECKING || state == UPD_NONE) return;
        if (state != UPD_AVAILABLE) { check(); return; }
        NSAlert *a = alert([NSString stringWithFormat:@"Update to the newest %@?", [branch isEqualToString:@"nightly"] ? @"Nightly" : @"version"],
                           [NSString stringWithFormat:@"The %@ branch (%@) is downloaded from GitHub and every installed game is rebuilt from your own copy, "
                            "which takes a few minutes for each. Your saves and settings are kept. The app restarts when it is done.", branch, short_commit(latest)]);
        [a addButtonWithTitle:@"Update"]; [a addButtonWithTitle:@"Cancel"];
        if ([a runModal] != NSAlertFirstButtonReturn) return;
        [[NSFileManager defaultManager] createDirectoryAtPath:ws.home withIntermediateDirectories:YES attributes:nil error:nil];
        if (update_run(ws.window, updater, branch, [ws.home stringByAppendingPathComponent:@"update.log"])) {
            NSTask *o = [NSTask new]; o.executableURL = [NSURL fileURLWithPath:@"/usr/bin/open"]; o.arguments = @[@"-n", NSBundle.mainBundle.bundlePath];
            [o launchAndReturnError:nil]; [o waitUntilExit];                /* the rebuilt app is at the same path */
            exit(0);
        }
        check();
    };
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
- (NSDictionary *)choices
{
    return @{@"game": @(GAMES[self.view.game].id), @"display": val(self.display), @"notch": val(self.notch), @"resolution": val(self.resolution), @"vsync": val(self.vsync)};
}
- (void)save
{
    [[NSFileManager defaultManager] createDirectoryAtPath:self.home withIntermediateDirectories:YES attributes:nil error:nil];
    [self.choices writeToFile:self.plistPath atomically:YES];
}
- (void)play
{
    [self save];
    GCGame *g = self.view.chosen; NSFileManager *fm = NSFileManager.defaultManager;
    if (!g.installed) {
        [alert([NSString stringWithFormat:@"%s isn't installed", g.def->title],
               [NSString stringWithFormat:@"Install it from your Steam copy with the project's installer:\n\n./install.sh --games %s\n\n"
                "The games already in the app are kept.", g.def->id]) runModal];
        return;
    }
    if (![fm fileExistsAtPath:[g.game stringByAppendingPathComponent:@(g.def->exe)]]) {
        [alert(@"The game files are missing", [NSString stringWithFormat:@"Expected %s in %@. Run install.sh again.", g.def->exe, g.game]) runModal];
        return;
    }
    NSDictionary *c = self.choices;
    NSString *log = [g.data stringByAppendingPathComponent:[@(g.def->bin) stringByAppendingString:@".log"]];
    NSDictionary *env = @{@"DS_NO_LAUNCHER": @"1", @"DS_DISPLAY": c[@"display"], @"DS_NOTCH": c[@"notch"], @"DSR_VSYNC": c[@"vsync"],
                          /* always set, "" for as set in the game: the game's own launch code keeps a value already set,
                           * and would otherwise take one from an older launcher.plist in its data folder */
                          @"GTA_MODE": mode_for(c[@"display"], c[@"notch"], c[@"resolution"]), @"DS_LOG_FILE": log};
    NSArray<NSString *> *args = @[g.binPath, @"--exe", g.exePath, @"--game", g.game, @"--data", g.data];
    if (getenv("GTAC_LAUNCHER_DRY")) {                                   /* development: what Play runs */
        for (NSString *k in [env.allKeys sortedArrayUsingSelector:@selector(compare:)]) printf("%s=%s\n", k.UTF8String, [env[k] UTF8String]);
        for (NSString *a in args) printf("'%s' ", a.UTF8String);
        printf("2>>'%s'\n", log.UTF8String); fflush(stdout); exit(0);
    }
    /* The game runs in a fresh instance of the app that never shows a window: it execs the game at once (main below).
     * A full-screen game window from the process that showed this window can stay black and off the front (as in the
     * Diablo II port), and opened through LaunchServices the instance is still this app, so Game Mode turns on. */
    NSMutableDictionary *all = [NSProcessInfo.processInfo.environment mutableCopy];
    [all addEntriesFromDictionary:env];
    [all addEntriesFromDictionary:@{@"GTAC_PLAY_BIN": g.binPath, @"GTAC_PLAY_EXE": g.exePath, @"GTAC_PLAY_GAME": g.game, @"GTAC_PLAY_DATA": g.data}];
    [all removeObjectForKey:@"GTAC_LAUNCHER_PLAY"];
    NSWorkspaceOpenConfiguration *cfg = [NSWorkspaceOpenConfiguration configuration];
    cfg.createsNewApplicationInstance = YES; cfg.environment = all; cfg.activates = YES;
    [self.window orderOut:nil];
    [[NSWorkspace sharedWorkspace] openApplicationAtURL:NSBundle.mainBundle.bundleURL configuration:cfg completionHandler:^(NSRunningApplication *app, NSError *err) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (app) { [NSApp terminate:nil]; return; }
            [self.window makeKeyAndOrderFront:nil];
            [alert(@"The game did not start", err.localizedDescription ?: @"") runModal];
        });
    }];
}
@end

/* the game's instance (see play): straight into the game, its stderr in the game's log (the previous one kept as .old
 * past 5 MB) */
static void exec_game(void)
{
    const char *bin = getenv("GTAC_PLAY_BIN"), *log = getenv("DS_LOG_FILE");
    const char *argv[] = {bin, "--exe", getenv("GTAC_PLAY_EXE"), "--game", getenv("GTAC_PLAY_GAME"), "--data", getenv("GTAC_PLAY_DATA"), 0};
    for (int k = 2; k < 7; k += 2) if (!argv[k]) return;
    mkdir(argv[6], 0755);
    if (log) {
        struct stat st;
        if (!stat(log, &st) && st.st_size > 5000000) { char old[2048]; snprintf(old, sizeof old, "%.*s.old.log", (int)(strlen(log) - 4), log); rename(log, old); }
        int fd = open(log, O_WRONLY | O_APPEND | O_CREAT, 0644); if (fd >= 0) { dup2(fd, 2); close(fd); }
    }
    char *keep[4]; const char *names[4] = {"GTAC_PLAY_BIN", "GTAC_PLAY_EXE", "GTAC_PLAY_GAME", "GTAC_PLAY_DATA"};
    for (int k = 0; k < 4; k++) { keep[k] = strdup(getenv(names[k])); }
    argv[0] = keep[0]; argv[2] = keep[1]; argv[4] = keep[2]; argv[6] = keep[3];
    for (int k = 0; k < 4; k++) unsetenv(names[k]);
    execv(argv[0], (char *const *)argv);
    fprintf(stderr, "GTA Classics: %s did not start: %s\n", argv[0], strerror(errno));
}

int main(int argc, const char **argv)
{
    (void)argc; (void)argv;
    if (getenv("GTAC_PLAY_BIN")) { exec_game(); return 1; }
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        Launcher *l = [Launcher new]; app.delegate = l;
        [app run];
    }
    return 0;
}
