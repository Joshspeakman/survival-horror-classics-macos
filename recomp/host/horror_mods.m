/* Survival Horror Classics: the Mods window (the launch window's Mods tile; X on a controller). Per game (Resident Evil
 * 2's Leon and Claire share one list, Resident Evil 3 and The Mercenaries another): the installed mods (on/off, order,
 * remove), a browser of GameBanana's mods for the game (Resident Evil 1-3: search, most downloaded, newest, most liked;
 * Install downloads and installs), and archives dropped on the window or picked with From File.
 *
 * Nothing in the game's folder is ever changed. Each mod lives in ~/Games/SurvivalHorrorClassics/mods/<game>/<folder>/,
 * its files at the game's own paths, listed in priority order (first wins) in that folder's index.plist; the game reads
 * an enabled mod's copy of a file instead of its own (runtime/win32/files.c, and for Resident Evil 3's Rofs archives
 * runtime/win32/re_mods.c), so switching a mod off or removing it brings the original straight back.
 *
 * Mods for these games come in every shape: a tree like the game's (Pl0/PLD/PL00.PLD), a Classic REbirth-style folder
 * relative to Resident Evil's USA folder (ENEMY/EM1003.EMD), or bare files (PL00.PLD). Installing places each file by
 * the game's own layout: its name is looked up among the game's files (Resident Evil 3: the files inside its Rofs
 * archives, from their directories), the folders it came in break ties, and a new file goes to the game folder its own
 * folder names. A name the game has in more than one place goes to all of them, except Resident Evil 2's Leon and
 * Claire folders (Pl0, Pl1), which the player picks. Kinds:
 *   files     placed game files: on when installed
 *   code      DLLs or programs (Classic REbirth, texture-pack loaders): Windows code the port can't run: kept, off
 *   textures  image packs for Classic REbirth's texture replacement, which the port doesn't have: kept, off
 *   other     nothing the game reads: kept, off
 * Files that don't belong to the game (readmes, pictures) are kept in the mod's _extras folder.
 * Archives: zip (ditto), rar and 7z (bsdtar).
 * Command line (also for testing): SurvivalHorrorClassics --install-mod <re1|re2|re3|dc> <archive> [leon|claire|both]
 *                                  SurvivalHorrorClassics --list-mods <game> */
#import <Cocoa/Cocoa.h>

NSString *hmods_root(void)   /* SHC_MODS_ROOT: development (tests keep their mods apart) */
{ const char *e = getenv("SHC_MODS_ROOT"); return e ? @(e) : [NSHomeDirectory() stringByAppendingPathComponent:@"Games/SurvivalHorrorClassics/mods"]; }
static NSString *mods_dir(NSString *game) { return [hmods_root() stringByAppendingPathComponent:game]; }
static NSString *index_path(NSString *game) { return [mods_dir(game) stringByAppendingPathComponent:@"index.plist"]; }
static NSMutableArray<NSMutableDictionary *> *load_index(NSString *game)
{
    NSMutableArray *a = [NSMutableArray array];
    for (NSDictionary *d in [NSArray arrayWithContentsOfFile:index_path(game)] ?: @[]) if ([d isKindOfClass:NSDictionary.class]) [a addObject:[d mutableCopy]];
    return a;
}
static void save_index(NSString *game, NSArray *mods)
{
    [[NSFileManager defaultManager] createDirectoryAtPath:mods_dir(game) withIntermediateDirectories:YES attributes:nil error:nil];
    [mods writeToFile:index_path(game) atomically:YES];
}
/* GTA_MODS for the game (the runtime's name for it): the enabled file mods' folders, highest priority first, one per line */
NSString *hmods_env(NSString *game)
{
    NSMutableArray *dirs = [NSMutableArray array];
    for (NSDictionary *m in load_index(game))
        if ([m[@"enabled"] boolValue] && [m[@"kind"] isEqualToString:@"files"]) [dirs addObject:[mods_dir(game) stringByAppendingPathComponent:m[@"folder"]]];
    return [dirs componentsJoinedByString:@"\n"];
}
void hmods_counts(NSString *game, NSUInteger *installed, NSUInteger *on)   /* the launcher's tile, each redraw: read again only when the list changed */
{
    static NSMutableDictionary *seen; if (!seen) seen = [NSMutableDictionary dictionary];
    NSDictionary *at = [[NSFileManager defaultManager] attributesOfItemAtPath:index_path(game) error:nil];
    NSArray *key = @[at.fileModificationDate ?: NSDate.distantPast, @(at.fileSize)], *c = seen[game];
    if (!c || ![c[0] isEqual:key]) {
        NSArray *a = load_index(game); NSUInteger n = 0; for (NSDictionary *m in a) if ([m[@"enabled"] boolValue]) n++;
        seen[game] = c = @[key, @(a.count), @(n)];
    }
    *installed = [c[1] unsignedIntegerValue]; *on = [c[2] unsignedIntegerValue];
}

// ---------------------------------------------------------------- the game's layout
/* every file the game reads, by lower-case name -> its paths below the game folder (lower case), and its folders */
@interface Layout : NSObject
@property NSMutableDictionary<NSString *, NSMutableArray<NSString *> *> *byName; @property NSMutableSet<NSString *> *dirs;
@end
@implementation Layout
@end
static void add_path(Layout *L, NSString *rel)
{
    rel = rel.lowercaseString; NSString *n = rel.lastPathComponent;
    NSMutableArray *a = L.byName[n]; if (!a) L.byName[n] = a = [NSMutableArray array];
    if (![a containsObject:rel]) [a addObject:rel];
    for (NSString *d = rel.stringByDeletingLastPathComponent; d.length; d = d.stringByDeletingLastPathComponent) [L.dirs addObject:d];
}
/* Resident Evil 3's Rofs<n>.dat: a header naming its folder (DATA, PLD), then the folder's directory (at 8 x its offset): a
 * count, then per file its offset, size and name; the game asks for /data/pld/pl00.pld */
static void add_rofs(Layout *L, NSString *path)
{
    NSData *dd = [NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:nil]; const uint8_t *d = dd.bytes; NSUInteger n = dd.length, p = 0x14;
    #define NEED(x) if ((x) > n) return
    NEED(0x40); while (p < 0x40 && d[p] == 0) p++;
    NSUInteger e = p; while (e < n && d[e]) e++; NEED(e + 9);
    NSString *base = [[NSString alloc] initWithBytes:d + p length:e - p encoding:NSASCIIStringEncoding]; p = e + 1;
    uint32_t off; memcpy(&off, d + p, 4); p += 8; e = p; while (e < n && d[e]) e++; NEED(e);
    NSString *sub = [[NSString alloc] initWithBytes:d + p length:e - p encoding:NSASCIIStringEncoding];
    NSUInteger q = (NSUInteger)off * 8; NEED(q + 4); uint32_t cnt; memcpy(&cnt, d + q, 4); q += 4;
    if (!base.length || !sub.length || cnt > 100000) return;
    for (uint32_t k = 0; k < cnt; k++) {
        NEED(q + 9); q += 8; e = q; while (e < n && d[e]) e++; NEED(e);
        NSString *name = [[NSString alloc] initWithBytes:d + q length:e - q encoding:NSISOLatin1StringEncoding]; q = e + 1;
        if (name.length) add_path(L, [NSString stringWithFormat:@"%@/%@/%@", base, sub, name]);
    }
    #undef NEED
}
static NSSet *SKIP_TOP(void) { return [NSSet setWithArray:@[@"__redist", @"app", @"commonappdata", @"__macosx"]]; }
static NSSet *SKIP_EXT(void) { return [NSSet setWithArray:@[@"exe", @"dll", @"hashdb", @"info", @"script", @"ico", @"dat-old"]]; }
static Layout *layout_for(NSString *gameDir)
{
    static NSMutableDictionary *cache; static dispatch_once_t once; dispatch_once(&once, ^{ cache = [NSMutableDictionary dictionary]; });
    @synchronized (cache) { if (cache[gameDir]) return cache[gameDir]; }
    Layout *L = [Layout new]; L.byName = [NSMutableDictionary dictionary]; L.dirs = [NSMutableSet set];
    NSFileManager *fm = NSFileManager.defaultManager; NSDirectoryEnumerator *en = [fm enumeratorAtPath:gameDir];
    for (NSString *rel in en) {
        NSString *top = [rel.pathComponents.firstObject lowercaseString];
        if ([SKIP_TOP() containsObject:top]) { if (rel.pathComponents.count == 1) [en skipDescendants]; continue; }
        BOOL isdir = [en.fileAttributes.fileType isEqualToString:NSFileTypeDirectory];
        if (isdir) { [L.dirs addObject:rel.lowercaseString]; continue; }
        NSString *ext = rel.pathExtension.lowercaseString;
        if ([SKIP_EXT() containsObject:ext] || [rel.lastPathComponent hasPrefix:@"goggame"]) continue;
        if (rel.pathComponents.count == 1 && [rel.lowercaseString hasPrefix:@"rofs"] && [ext isEqualToString:@"dat"]) { add_rofs(L, [gameDir stringByAppendingPathComponent:rel]); continue; }
        add_path(L, rel);
    }
    @synchronized (cache) { if (L.byName.count) cache[gameDir] = L; }
    return L;
}

// ---------------------------------------------------------------- installing an archive
static int run(NSString *tool, NSArray *args)
{
    NSTask *t = [NSTask new]; t.executableURL = [NSURL fileURLWithPath:tool]; t.arguments = args;
    t.standardOutput = t.standardError = [NSFileHandle fileHandleWithNullDevice];
    if (![t launchAndReturnError:nil]) return -1;
    [t waitUntilExit]; return t.terminationStatus;
}
static NSString *slug(NSString *name)
{
    NSMutableString *s = [NSMutableString string];
    for (NSUInteger k = 0; k < name.length && s.length < 48; k++) { unichar c = [name characterAtIndex:k];
        [s appendString:(isalnum(c) && c < 128) ? [NSString stringWithCharacters:&c length:1] : (s.length && ![s hasSuffix:@"-"] ? @"-" : @"")]; }
    while ([s hasSuffix:@"-"]) [s deleteCharactersInRange:NSMakeRange(s.length - 1, 1)];
    return s.length ? s.lowercaseString : @"mod";
}
static NSSet *CODE_EXT(void) { return [NSSet setWithArray:@[@"dll", @"asi", @"exe"]]; }
static NSSet *DOC_EXT(void) { return [NSSet setWithArray:@[@"txt", @"md", @"pdf", @"url", @"nfo", @"rtf", @"htm", @"html", @"doc", @"docx"]]; }
static NSSet *IMAGE_EXT(void) { return [NSSet setWithArray:@[@"png", @"dds", @"bmp", @"jpg", @"jpeg"]]; }
/* how many of a's last folders match b's last folders */
static NSUInteger tail_match(NSArray *a, NSArray *b)
{
    NSUInteger k = 0; while (k < a.count && k < b.count && [[a[a.count - 1 - k] lowercaseString] isEqualToString:b[b.count - 1 - k]]) k++;
    return k;
}
/* a mod being installed: unpacked, and where each of its files goes */
@interface Plan : NSObject
@property NSString *incoming, *name, *source, *game, *why;
@property NSMutableDictionary<NSString *, NSArray<NSString *> *> *place;   /* file (below incoming) -> game paths */
@property NSMutableDictionary<NSString *, NSArray<NSString *> *> *ambiguous;
@property NSMutableArray<NSString *> *extras; @property BOOL code, images;
@end
@implementation Plan
@end
static Plan *plan_archive(NSString *path, NSString *name, NSString *source, NSString *game, NSString *gameDir)
{
    Plan *P = [Plan new]; P.name = name; P.source = source; P.game = game;
    P.place = [NSMutableDictionary dictionary]; P.ambiguous = [NSMutableDictionary dictionary]; P.extras = [NSMutableArray array];
    NSFileManager *fm = NSFileManager.defaultManager;
    [fm createDirectoryAtPath:mods_dir(game) withIntermediateDirectories:YES attributes:nil error:nil];
    P.incoming = [mods_dir(game) stringByAppendingPathComponent:[@".incoming-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    [fm createDirectoryAtPath:P.incoming withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *ext = path.pathExtension.lowercaseString; int rc;
    if ([ext isEqualToString:@"zip"]) rc = run(@"/usr/bin/ditto", @[@"-x", @"-k", path, P.incoming]);
    else if ([@[@"rar", @"7z", @"tar", @"gz", @"tgz", @"xz", @"bz2"] containsObject:ext]) rc = run(@"/usr/bin/bsdtar", @[@"-xf", path, @"-C", P.incoming]);
    else rc = [fm copyItemAtPath:path toPath:[P.incoming stringByAppendingPathComponent:path.lastPathComponent] error:nil] ? 0 : -1;
    if (rc != 0) { P.why = [NSString stringWithFormat:@"%@ couldn't be unpacked (zip, rar, 7z or a single file).", path.lastPathComponent]; return P; }
    Layout *L = layout_for(gameDir);
    if (!L.byName.count) { P.why = @"The game's folder isn't reachable, so the mod can't be placed: connect its drive and try again."; return P; }
    NSUInteger nfiles = 0; NSDirectoryEnumerator *en = [fm enumeratorAtPath:P.incoming];
    for (NSString *rel in en) {
        if ([rel hasPrefix:@"__MACOSX"] || [rel.lastPathComponent isEqualToString:@".DS_Store"]) continue;
        if ([en.fileAttributes.fileType isEqualToString:NSFileTypeDirectory]) continue;
        nfiles++;
        NSString *e = rel.pathExtension.lowercaseString; NSArray *dirs = rel.stringByDeletingLastPathComponent.pathComponents;
        if ([dirs.firstObject isEqualToString:@"/"] || [dirs.firstObject isEqualToString:@""]) dirs = @[];
        if ([CODE_EXT() containsObject:e]) { P.code = YES; [P.extras addObject:rel]; continue; }
        NSArray *cands = L.byName[rel.lastPathComponent.lowercaseString];
        if ([DOC_EXT() containsObject:e]) {                  /* a readme: the game's own are only in its top folder */
            NSMutableArray *deep = [NSMutableArray array]; for (NSString *c in cands) if (c.pathComponents.count > 1) [deep addObject:c];
            cands = deep;
        }
        if (cands.count) {                                   /* the game has it: the best match of the folders it came in */
            NSUInteger best = 0; NSMutableArray *top = [NSMutableArray array];
            for (NSString *c in cands) {
                NSUInteger s = tail_match(dirs, c.stringByDeletingLastPathComponent.pathComponents);
                if (s > best) { best = s; [top removeAllObjects]; }
                if (s == best) [top addObject:c];
            }
            P.place[rel] = top; if (top.count > 1) P.ambiguous[rel] = top;
            continue;
        }
        /* new to the game: into the game folder its own folders name (the deepest such match), if just one */
        NSString *home = nil; NSUInteger best = 0; BOOL tie = NO;
        for (NSString *d in L.dirs) {
            NSUInteger s = tail_match(dirs, d.pathComponents);
            if (s > best) { best = s; home = d; tie = NO; } else if (s && s == best) tie = YES;
        }
        if (home && !tie && ![IMAGE_EXT() containsObject:e] && ![DOC_EXT() containsObject:e] && ![e isEqualToString:@"ini"])
            P.place[rel] = @[[home stringByAppendingPathComponent:rel.lastPathComponent.lowercaseString]];
        else { if ([IMAGE_EXT() containsObject:e]) P.images = YES; [P.extras addObject:rel]; }
    }
    if (!nfiles) P.why = @"The archive is empty.";
    return P;
}
/* RE2's Leon and Claire folders: choice "leon" | "claire" | "both" narrows the ambiguous files' places */
static void plan_choose(Plan *P, NSString *choice)
{
    for (NSString *rel in P.ambiguous.allKeys) {
        NSArray *c = P.ambiguous[rel]; NSMutableArray *keep = [NSMutableArray array];
        for (NSString *g in c) {
            NSString *top = g.pathComponents.firstObject;
            if ([choice isEqualToString:@"both"] || ([choice isEqualToString:@"leon"] && [top isEqualToString:@"pl0"]) || ([choice isEqualToString:@"claire"] && [top isEqualToString:@"pl1"])
                || (![top isEqualToString:@"pl0"] && ![top isEqualToString:@"pl1"])) [keep addObject:g];
        }
        P.place[rel] = keep.count ? keep : c;
    }
}
static BOOL plan_asks(Plan *P)   /* Resident Evil 2: a file in both Leon's and Claire's folders */
{
    for (NSArray *c in P.ambiguous.allValues) {
        BOOL l = NO, cl = NO; for (NSString *g in c) { if ([g hasPrefix:@"pl0/"]) l = YES; if ([g hasPrefix:@"pl1/"]) cl = YES; }
        if (l && cl) return YES;
    }
    return NO;
}
/* the planned files into the mod's folder; returns the index entry, or nil with P.why */
static NSMutableDictionary *plan_commit(Plan *P)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    if (P.why) { [fm removeItemAtPath:P.incoming error:nil]; return nil; }
    NSString *folder = slug(P.name), *dest = [mods_dir(P.game) stringByAppendingPathComponent:folder];
    for (int k = 2; [fm fileExistsAtPath:dest]; k++) { folder = [NSString stringWithFormat:@"%@-%d", slug(P.name), k]; dest = [mods_dir(P.game) stringByAppendingPathComponent:folder]; }
    NSUInteger placed = 0;
    for (NSString *rel in P.place) for (NSString *g in P.place[rel]) {
        NSString *to = [dest stringByAppendingPathComponent:g];
        [fm createDirectoryAtPath:to.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
        [fm removeItemAtPath:to error:nil];
        if ([fm copyItemAtPath:[P.incoming stringByAppendingPathComponent:rel] toPath:to error:nil]) placed++;
    }
    for (NSString *rel in P.extras) {
        NSString *to = [[dest stringByAppendingPathComponent:@"_extras"] stringByAppendingPathComponent:rel];
        [fm createDirectoryAtPath:to.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
        [fm copyItemAtPath:[P.incoming stringByAppendingPathComponent:rel] toPath:to error:nil];
    }
    [fm removeItemAtPath:P.incoming error:nil];
    if (!placed && !P.extras.count) { [fm removeItemAtPath:dest error:nil]; P.why = @"The mod couldn't be stored in the mods folder."; return nil; }
    NSString *kind, *note;
    NSString *skipped = P.code ? @" (its DLLs or programs are Windows code the port can't run: kept, unused)" : @"";
    if (placed) {
        kind = @"files";
        NSMutableSet *where = [NSMutableSet set]; for (NSArray *gs in P.place.allValues) for (NSString *g in gs) [where addObject:g.stringByDeletingLastPathComponent];
        NSArray *w = [where.allObjects sortedArrayUsingSelector:@selector(compare:)];
        note = [NSString stringWithFormat:@"%lu game file%@ (%@%@)%@", (unsigned long)placed, placed == 1 ? @"" : @"s",
                [[w subarrayWithRange:NSMakeRange(0, MIN(3, w.count))] componentsJoinedByString:@", "], w.count > 3 ? @", …" : @"", skipped];
    } else if (P.code) { kind = @"code"; note = @"Windows code (DLLs or programs, such as Classic REbirth): the port can't run it (kept, off)"; }
    else if (P.images) { kind = @"textures"; note = @"A texture pack for Classic REbirth's texture replacement, which the port doesn't have (kept, off)"; }
    else { kind = @"other"; note = @"Nothing the game reads (kept, off)"; }
    return [@{@"name": P.name, @"folder": folder, @"kind": kind, @"note": note, @"source": P.source ?: @"", @"game": P.game,
              @"enabled": @([kind isEqualToString:@"files"]), @"added": NSDate.date} mutableCopy];
}

// ---------------------------------------------------------------- the window
@interface SHMods : NSObject <NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate>
@property NSPanel *panel, *browser; @property NSTableView *table, *btable; @property NSTextField *status, *bstatus; @property NSProgressIndicator *spin;
@property NSSearchField *search; @property NSPopUpButton *sort; @property NSImageView *preview; @property NSTextField *pinfo;
@property NSString *title, *game, *gameDir, *prefer; @property int gb;
@property NSMutableArray<NSMutableDictionary *> *mods; @property NSArray<NSDictionary *> *results;
@property (copy) void (^changed)(void);
- (void)browse:(id)s;
- (void)installPath:(NSString *)path name:(NSString *)name source:(NSString *)source done:(void (^)(BOOL))done;
@end

static SHMods *current;
static NSButton *button(NSString *title, id target, SEL action, NSRect f)
{
    NSButton *b = [NSButton buttonWithTitle:title target:target action:action]; b.frame = f; b.bezelStyle = NSBezelStyleRounded; return b;
}
static NSTableColumn *column(NSString *ident, NSString *title, CGFloat w)
{
    NSTableColumn *c = [[NSTableColumn alloc] initWithIdentifier:ident]; c.title = title; c.width = w; return c;
}
static NSScrollView *scroller(NSTableView *t, NSRect f)
{
    NSScrollView *s = [[NSScrollView alloc] initWithFrame:f]; s.documentView = t; s.hasVerticalScroller = YES; s.borderType = NSBezelBorder; return s;
}
static NSString *ask_place(NSString *modName, NSString *prefer)   /* Resident Evil 2: whose game the files are for */
{
    const char *env = getenv("SHC_MODS_PLACE"); if (env) return @(env);
    NSAlert *a = [NSAlert new]; a.messageText = [NSString stringWithFormat:@"Which game is %@ for?", modName];
    a.informativeText = @"Resident Evil 2 keeps Leon's game (Pl0) and Claire's game (Pl1) apart, and this mod has files both have.";
    NSArray *keys = @[@"leon", @"claire", @"both"]; NSArray *titles = @[@"Leon's Game", @"Claire's Game", @"Both"];
    NSUInteger first = [keys indexOfObject:prefer ?: @"leon"]; if (first == NSNotFound) first = 0;
    [a addButtonWithTitle:titles[first]]; for (NSUInteger k = 0; k < 3; k++) if (k != first) [a addButtonWithTitle:titles[k]];
    NSModalResponse r = [a runModal]; NSUInteger pick = (NSUInteger)(r - NSAlertFirstButtonReturn);
    NSMutableArray *order = [NSMutableArray arrayWithObject:keys[first]]; for (NSUInteger k = 0; k < 3; k++) if (k != first) [order addObject:keys[k]];
    return pick < order.count ? order[pick] : keys[first];
}

@implementation SHMods
- (void)show:(NSWindow *)parent
{
    NSPanel *p = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 760, 470) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                                              backing:NSBackingStoreBuffered defer:NO];
    p.title = [NSString stringWithFormat:@"Mods — %@", self.title]; p.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    p.delegate = self; p.releasedWhenClosed = NO; p.minSize = NSMakeSize(660, 360); self.panel = p;
    NSView *cv = p.contentView;
    NSTextField *head = [NSTextField wrappingLabelWithString:@"The first mod wins where two change the same file. The game's own files are never changed: switching a mod off or removing it brings them back. Drop a zip, rar or 7z here to install it."];
    head.frame = NSMakeRect(16, 424, 728, 36); head.textColor = NSColor.secondaryLabelColor; head.font = [NSFont systemFontOfSize:12];
    head.autoresizingMask = NSViewMinYMargin | NSViewWidthSizable; [cv addSubview:head];
    NSTableView *t = [NSTableView new]; self.table = t;
    [t addTableColumn:column(@"on", @"On", 34)]; [t addTableColumn:column(@"name", @"Mod", 250)]; [t addTableColumn:column(@"kind", @"What it changes", 420)];
    t.dataSource = self; t.delegate = self; t.usesAlternatingRowBackgroundColors = YES; t.rowHeight = 22;
    [t registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    NSScrollView *sv = scroller(t, NSMakeRect(16, 64, 728, 352)); sv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable; [cv addSubview:sv];
    CGFloat x = 12; NSMutableArray *row = [NSMutableArray array];
    if (self.gb) { [row addObject:button(@"Browse GameBanana…", self, @selector(browse:), NSMakeRect(x, 22, 170, 32))]; x += 172; }
    [row addObject:button(@"From File…", self, @selector(installFile:), NSMakeRect(x, 22, 104, 32))]; x += 106;
    [row addObject:button(@"Show Folder", self, @selector(openFolder:), NSMakeRect(x, 22, 110, 32))]; x += 112;
    [row addObject:button(@"Up", self, @selector(up:), NSMakeRect(x, 22, 46, 32))]; x += 46;
    [row addObject:button(@"Down", self, @selector(down:), NSMakeRect(x, 22, 58, 32))]; x += 60;
    [row addObject:button(@"Remove", self, @selector(remove:), NSMakeRect(x, 22, 80, 32))];
    for (NSButton *b in row) [cv addSubview:b];
    NSButton *done = button(@"Done", self, @selector(done:), NSMakeRect(664, 22, 84, 32)); done.keyEquivalent = @"\r"; done.autoresizingMask = NSViewMinXMargin;
    [cv addSubview:done];
    NSTextField *st = [NSTextField labelWithString:self.gb ? @"" : @"GameBanana has no Dino Crisis section: install mods from a file or by dropping them here."];
    st.frame = NSMakeRect(16, 4, 728, 16); st.font = [NSFont systemFontOfSize:11]; st.textColor = NSColor.secondaryLabelColor;
    st.autoresizingMask = NSViewWidthSizable; self.status = st; [cv addSubview:st];
    self.mods = load_index(self.game); [t reloadData];
    if (parent) [p setFrameOrigin:NSMakePoint(NSMidX(parent.frame) - 380, NSMidY(parent.frame) - 235)]; else [p center];
    [p makeKeyAndOrderFront:nil];
}
- (void)saveAndNotify { save_index(self.game, self.mods); [self.table reloadData]; if (self.changed) self.changed(); }
- (void)done:(id)s { [self.browser orderOut:nil]; [self.panel close]; }
- (void)windowWillClose:(NSNotification *)n { if (n.object == self.panel) { [self.browser orderOut:nil]; if (current == self) dispatch_async(dispatch_get_main_queue(), ^{ current = nil; }); } }
- (NSInteger)numberOfRowsInTableView:(NSTableView *)t { return t == self.table ? (NSInteger)self.mods.count : (NSInteger)self.results.count; }
- (NSView *)tableView:(NSTableView *)t viewForTableColumn:(NSTableColumn *)col row:(NSInteger)r
{
    if (t == self.table) {
        NSMutableDictionary *m = self.mods[(NSUInteger)r];
        if ([col.identifier isEqualToString:@"on"]) {
            NSButton *b = [NSButton checkboxWithTitle:@"" target:self action:@selector(toggle:)]; b.tag = r; b.state = [m[@"enabled"] boolValue] ? NSControlStateValueOn : NSControlStateValueOff;
            b.enabled = [m[@"kind"] isEqualToString:@"files"]; return b;
        }
        NSString *txt = [col.identifier isEqualToString:@"name"] ? m[@"name"] : m[@"note"];
        NSTextField *f = [NSTextField labelWithString:txt ?: @""]; f.lineBreakMode = NSLineBreakByTruncatingTail; f.toolTip = txt;
        if (![col.identifier isEqualToString:@"name"] && ![m[@"kind"] isEqualToString:@"files"]) f.textColor = NSColor.secondaryLabelColor;
        return f;
    }
    NSDictionary *x = self.results[(NSUInteger)r];
    NSString *v = [col.identifier isEqualToString:@"name"] ? x[@"_sName"] : [col.identifier isEqualToString:@"cat"] ? (x[@"_aRootCategory"][@"_sName"] ?: @"")
                : [col.identifier isEqualToString:@"likes"] ? [x[@"_nLikeCount"] description] : [x[@"_nViewCount"] description];
    NSTextField *f = [NSTextField labelWithString:v ?: @""]; f.lineBreakMode = NSLineBreakByTruncatingTail; f.toolTip = v; return f;
}
- (void)toggle:(NSButton *)b { NSMutableDictionary *m = self.mods[(NSUInteger)b.tag]; m[@"enabled"] = @(b.state == NSControlStateValueOn); [self saveAndNotify]; }
- (void)move:(NSInteger)dir
{
    NSInteger r = self.table.selectedRow, to = r + dir; if (r < 0 || to < 0 || to >= (NSInteger)self.mods.count) { NSBeep(); return; }
    [self.mods exchangeObjectAtIndex:(NSUInteger)r withObjectAtIndex:(NSUInteger)to]; [self saveAndNotify];
    [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)to] byExtendingSelection:NO];
}
- (void)up:(id)s { [self move:-1]; }
- (void)down:(id)s { [self move:1]; }
- (void)remove:(id)s
{
    NSInteger r = self.table.selectedRow; if (r < 0) { NSBeep(); return; }
    NSDictionary *m = self.mods[(NSUInteger)r];
    NSAlert *a = [NSAlert new]; a.messageText = [NSString stringWithFormat:@"Remove %@?", m[@"name"]];
    a.informativeText = @"Its files go to the Trash. The game's own files are not affected."; [a addButtonWithTitle:@"Remove"]; [a addButtonWithTitle:@"Cancel"];
    if ([a runModal] != NSAlertFirstButtonReturn) return;
    [[NSFileManager defaultManager] trashItemAtURL:[NSURL fileURLWithPath:[mods_dir(self.game) stringByAppendingPathComponent:m[@"folder"]]] resultingItemURL:nil error:nil];
    [self.mods removeObjectAtIndex:(NSUInteger)r]; [self saveAndNotify];
}
- (void)openFolder:(id)s
{
    [[NSFileManager defaultManager] createDirectoryAtPath:mods_dir(self.game) withIntermediateDirectories:YES attributes:nil error:nil];
    NSInteger r = self.table.selectedRow;
    NSString *p = r >= 0 ? [mods_dir(self.game) stringByAppendingPathComponent:self.mods[(NSUInteger)r][@"folder"]] : mods_dir(self.game);
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:p isDirectory:YES]];
}
/* unpack and place (off the main thread), ask about Leon / Claire when needed, then into the list at the top */
- (void)installPath:(NSString *)path name:(NSString *)name source:(NSString *)source done:(void (^)(BOOL))done
{
    self.status.stringValue = [NSString stringWithFormat:@"Installing %@…", name];
    NSString *game = self.game, *gameDir = self.gameDir, *prefer = self.prefer;
    void (^finish)(NSMutableDictionary *, NSString *) = ^(NSMutableDictionary *m, NSString *why) {
        if (m) { [self.mods insertObject:m atIndex:0]; [self saveAndNotify]; self.status.stringValue = [NSString stringWithFormat:@"Installed %@: %@", name, m[@"note"]]; }
        else { self.status.stringValue = why ?: @"Not installed."; NSBeep(); }
        if (done) done(m != nil);
    };
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        Plan *P = plan_archive(path, name, source, game, gameDir);
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!P.why && plan_asks(P)) plan_choose(P, ask_place(name, prefer));
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
                NSMutableDictionary *m = plan_commit(P);
                dispatch_async(dispatch_get_main_queue(), ^{ finish(m, P.why); });
            });
        });
    });
}
- (void)installFile:(id)s
{
    NSOpenPanel *o = [NSOpenPanel openPanel]; o.allowsMultipleSelection = YES; o.message = @"A mod's archive (zip, rar, 7z) or file";
    [o beginSheetModalForWindow:self.panel completionHandler:^(NSModalResponse r) {
        if (r != NSModalResponseOK) return;
        for (NSURL *u in o.URLs) [self installPath:u.path name:u.lastPathComponent.stringByDeletingPathExtension source:u.path done:nil];
    }];
}
- (NSDragOperation)tableView:(NSTableView *)t validateDrop:(id<NSDraggingInfo>)info proposedRow:(NSInteger)r proposedDropOperation:(NSTableViewDropOperation)op
{ return t == self.table ? NSDragOperationCopy : NSDragOperationNone; }
- (BOOL)tableView:(NSTableView *)t acceptDrop:(id<NSDraggingInfo>)info row:(NSInteger)r dropOperation:(NSTableViewDropOperation)op
{
    NSArray<NSURL *> *urls = [info.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    for (NSURL *u in urls) [self installPath:u.path name:u.lastPathComponent.stringByDeletingPathExtension source:u.path done:nil];
    return urls.count > 0;
}

// ---- GameBanana
- (void)browse:(id)s
{
    if (!self.gb) { NSBeep(); return; }
    if (self.browser) { [self.browser makeKeyAndOrderFront:nil]; return; }
    NSPanel *b = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 860, 540) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                                              backing:NSBackingStoreBuffered defer:NO];
    b.title = [NSString stringWithFormat:@"GameBanana — %@", self.title]; b.appearance = self.panel.appearance; b.releasedWhenClosed = NO; self.browser = b;
    NSView *cv = b.contentView;
    NSSearchField *sf = [[NSSearchField alloc] initWithFrame:NSMakeRect(16, 500, 380, 26)]; sf.placeholderString = @"Search mods"; sf.target = self; sf.action = @selector(query:);
    sf.sendsSearchStringImmediately = NO; sf.autoresizingMask = NSViewMinYMargin; self.search = sf; [cv addSubview:sf];
    NSPopUpButton *pop = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(404, 499, 180, 28) pullsDown:NO];
    [pop addItemsWithTitles:@[@"Most downloaded", @"Newest", @"Most liked"]]; pop.target = self; pop.action = @selector(query:); pop.autoresizingMask = NSViewMinYMargin;
    self.sort = pop; [cv addSubview:pop];
    NSTableView *t = [NSTableView new]; self.btable = t;
    [t addTableColumn:column(@"name", @"Mod", 330)]; [t addTableColumn:column(@"cat", @"Category", 110)];
    [t addTableColumn:column(@"likes", @"Likes", 50)]; [t addTableColumn:column(@"views", @"Views", 70)];
    t.dataSource = self; t.delegate = self; t.usesAlternatingRowBackgroundColors = YES; t.rowHeight = 22; t.target = self; t.doubleAction = @selector(installPick:);
    NSScrollView *sv = scroller(t, NSMakeRect(16, 64, 584, 426)); sv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable; [cv addSubview:sv];
    NSImageView *iv = [[NSImageView alloc] initWithFrame:NSMakeRect(612, 270, 232, 220)]; iv.imageScaling = NSImageScaleProportionallyUpOrDown; iv.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    self.preview = iv; [cv addSubview:iv];
    NSTextField *pi = [NSTextField wrappingLabelWithString:@"Pick a mod to see it. Mods made of game files (models, rooms, sounds, pictures) work; DLL-based mods and Classic REbirth texture packs are kept but can't run."];
    pi.frame = NSMakeRect(612, 110, 232, 150); pi.font = [NSFont systemFontOfSize:11]; pi.textColor = NSColor.secondaryLabelColor; pi.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    self.pinfo = pi; [cv addSubview:pi];
    NSButton *inst = button(@"Install", self, @selector(installPick:), NSMakeRect(608, 60, 120, 32)); inst.keyEquivalent = @"\r"; inst.autoresizingMask = NSViewMinXMargin;
    NSButton *page = button(@"Open Page", self, @selector(openPage:), NSMakeRect(730, 60, 118, 32)); page.autoresizingMask = NSViewMinXMargin;
    [cv addSubview:inst]; [cv addSubview:page];
    NSProgressIndicator *sp = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(16, 30, 16, 16)]; sp.style = NSProgressIndicatorStyleSpinning; sp.displayedWhenStopped = NO;
    self.spin = sp; [cv addSubview:sp];
    NSTextField *st = [NSTextField labelWithString:@"Mods from gamebanana.com: check each mod's page for its author's notes."]; st.frame = NSMakeRect(40, 30, 560, 16);
    st.font = [NSFont systemFontOfSize:11]; st.textColor = NSColor.secondaryLabelColor; st.autoresizingMask = NSViewWidthSizable; self.bstatus = st; [cv addSubview:st];
    [b setFrameOrigin:NSMakePoint(NSMaxX(self.panel.frame) - 120, NSMinY(self.panel.frame) - 40)];
    [b makeKeyAndOrderFront:nil];
    [self query:nil];
}
static void fetch_json(NSString *url, void (^done)(id))
{
    NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:url]]; [rq setValue:@"Survival-Horror-Classics-macOS/1.0" forHTTPHeaderField:@"User-Agent"];
    [[NSURLSession.sharedSession dataTaskWithRequest:rq completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
        id j = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
        dispatch_async(dispatch_get_main_queue(), ^{ done(j); });
    }] resume];
}
- (void)query:(id)s
{
    NSString *q = [self.search.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet], *url;
    if (q.length) url = [NSString stringWithFormat:@"https://gamebanana.com/apiv11/Util/Search/Results?_sModelName=Mod&_sOrder=best_match&_idGameRow=%d&_nPage=1&_sSearchString=%@",
                         self.gb, [q stringByAddingPercentEncodingWithAllowedCharacters:NSCharacterSet.URLQueryAllowedCharacterSet]];
    else {
        NSArray *sorts = @[@"Generic_MostDownloaded", @"Generic_Newest", @"Generic_MostLiked"];
        url = [NSString stringWithFormat:@"https://gamebanana.com/apiv11/Mod/Index?_aFilters%%5BGeneric_Game%%5D=%d&_sSort=%@&_nPerpage=50&_nPage=1", self.gb, sorts[(NSUInteger)MAX(0, self.sort.indexOfSelectedItem)]];
    }
    [self.spin startAnimation:nil]; self.bstatus.stringValue = @"Asking GameBanana…";
    fetch_json(url, ^(id j) {
        [self.spin stopAnimation:nil];
        NSArray *recs = [j isKindOfClass:NSDictionary.class] ? j[@"_aRecords"] : nil;
        if (![recs isKindOfClass:NSArray.class]) { self.results = @[]; self.bstatus.stringValue = @"GameBanana couldn't be reached."; [self.btable reloadData]; return; }
        NSMutableArray *mods = [NSMutableArray array]; for (NSDictionary *r in recs) if ([r[@"_sModelName"] isEqual:@"Mod"]) [mods addObject:r];
        self.results = mods; [self.btable reloadData];
        self.bstatus.stringValue = [NSString stringWithFormat:@"%lu mods%@", (unsigned long)mods.count, q.length ? [NSString stringWithFormat:@" for “%@”", q] : @""];
    });
}
- (void)tableViewSelectionDidChange:(NSNotification *)n
{
    if (n.object != self.btable) return;
    NSInteger r = self.btable.selectedRow; if (r < 0) return;
    NSDictionary *x = self.results[(NSUInteger)r]; NSArray *imgs = x[@"_aPreviewMedia"][@"_aImages"];
    self.pinfo.stringValue = [NSString stringWithFormat:@"%@\n%@ · by %@", x[@"_sName"], x[@"_aRootCategory"][@"_sName"] ?: @"", x[@"_aSubmitter"][@"_sName"] ?: @"?"];
    self.preview.image = nil;
    if ([imgs isKindOfClass:NSArray.class] && imgs.count) {
        NSString *u = [NSString stringWithFormat:@"%@/%@", imgs[0][@"_sBaseUrl"], imgs[0][@"_sFile530"] ?: imgs[0][@"_sFile"]];
        [[NSURLSession.sharedSession dataTaskWithURL:[NSURL URLWithString:u] completionHandler:^(NSData *d, NSURLResponse *rs, NSError *e) {
            NSImage *im = d ? [[NSImage alloc] initWithData:d] : nil;
            dispatch_async(dispatch_get_main_queue(), ^{ if (self.btable.selectedRow == r) self.preview.image = im; });
        }] resume];
    }
}
- (void)openPage:(id)s
{
    NSInteger r = self.btable.selectedRow; if (r < 0) { NSBeep(); return; }
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:self.results[(NSUInteger)r][@"_sProfileUrl"]]];
}
- (void)download:(NSDictionary *)f name:(NSString *)name page:(NSString *)page
{
    [self.spin startAnimation:nil]; self.bstatus.stringValue = [NSString stringWithFormat:@"Downloading %@ (%.1f MB)…", f[@"_sFile"], [f[@"_nFilesize"] doubleValue] / 1048576.0];
    NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:f[@"_sDownloadUrl"]]]; [rq setValue:@"Survival-Horror-Classics-macOS/1.0" forHTTPHeaderField:@"User-Agent"];
    [[NSURLSession.sharedSession downloadTaskWithRequest:rq completionHandler:^(NSURL *loc, NSURLResponse *resp, NSError *e) {
        NSString *dst = nil;
        if (loc) { dst = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@", NSUUID.UUID.UUIDString, [f[@"_sFile"] lastPathComponent]]];
                   if (![[NSFileManager defaultManager] moveItemAtPath:loc.path toPath:dst error:nil]) dst = nil; }
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.spin stopAnimation:nil];
            if (!dst) { self.bstatus.stringValue = [NSString stringWithFormat:@"The download failed: %@", e.localizedDescription ?: @"?"]; NSBeep(); return; }
            self.bstatus.stringValue = [NSString stringWithFormat:@"Installing %@…", name];
            [self installPath:dst name:name source:page done:^(BOOL ok) {
                [[NSFileManager defaultManager] removeItemAtPath:dst error:nil];
                self.bstatus.stringValue = ok ? [NSString stringWithFormat:@"%@ installed: see the Mods window.", name] : @"Not installed: see the Mods window.";
            }];
        });
    }] resume];
}
/* the mod's files: one is downloaded (the player picks when there are several), then installed */
- (void)installPick:(id)s
{
    NSInteger r = self.btable.selectedRow; if (r < 0) { NSBeep(); return; }
    NSDictionary *x = self.results[(NSUInteger)r]; NSString *name = x[@"_sName"];
    [self.spin startAnimation:nil]; self.bstatus.stringValue = [NSString stringWithFormat:@"Getting %@'s files…", name];
    fetch_json([NSString stringWithFormat:@"https://gamebanana.com/apiv11/Mod/%@?_csvProperties=_aFiles,_sProfileUrl", x[@"_idRow"]], ^(id j) {
        [self.spin stopAnimation:nil];
        NSArray *files = [j isKindOfClass:NSDictionary.class] ? j[@"_aFiles"] : nil;
        if (![files isKindOfClass:NSArray.class] || !files.count) { self.bstatus.stringValue = @"This mod has no files to download."; NSBeep(); return; }
        NSDictionary *f = files[0];
        if (files.count > 1) {
            NSAlert *a = [NSAlert new]; a.messageText = [NSString stringWithFormat:@"%@ has %lu files", name, (unsigned long)files.count];
            a.informativeText = @"Which one?"; NSPopUpButton *pb = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 320, 26) pullsDown:NO];
            for (NSDictionary *ff in files) [pb addItemWithTitle:[NSString stringWithFormat:@"%@ (%.1f MB)", ff[@"_sFile"], [ff[@"_nFilesize"] doubleValue] / 1048576.0]];
            a.accessoryView = pb; [a addButtonWithTitle:@"Install"]; [a addButtonWithTitle:@"Cancel"];
            if ([a runModal] != NSAlertFirstButtonReturn) return;
            f = files[(NSUInteger)pb.indexOfSelectedItem];
        }
        [self download:f name:name page:x[@"_sProfileUrl"]];
    });
}
@end

/* the Mods window for a game (one at a time); prefer "leon" | "claire" (Resident Evil 2: the version chosen, for the
 * question); changed() runs whenever the list changes (the launcher's tile) */
void hmods_show(NSWindow *parent, NSString *title, NSString *game, int gamebanana, NSString *gameDir, NSString *prefer, void (^changed)(void))
{
    if (current) { [current.panel makeKeyAndOrderFront:nil]; return; }
    SHMods *m = [SHMods new]; m.title = title; m.game = game; m.gb = gamebanana; m.gameDir = gameDir; m.prefer = prefer; m.changed = changed;
    current = m; [m show:parent];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ (void)layout_for(gameDir); });   /* ready before the first install */
    /* development: SHC_MODS_BROWSE=1 opens the browser; SHC_MODS_SHOT=<png> renders the window (and <png>-browser.png the
     * browser, once GameBanana answers) and exits */
    if (getenv("SHC_MODS_BROWSE")) [m browse:nil];
    if (getenv("SHC_MODS_SHOT")) {
        NSString *shot = @(getenv("SHC_MODS_SHOT"));
        void (^render)(NSView *, NSString *) = ^(NSView *v, NSString *path) {
            NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds]; [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
            [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
        };
        __block int tries = 0; __block void (^wait)(void);
        void (^w)(void) = ^{
            if (m.browser && !m.results.count && ++tries < 40) { dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC), dispatch_get_main_queue(), wait); return; }
            render(m.panel.contentView, shot);
            if (m.browser) { if (m.results.count) [m.btable selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
                             dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                                 render(m.browser.contentView, [shot.stringByDeletingPathExtension stringByAppendingString:@"-browser.png"]); exit(0); }); }
            else exit(0);
        };
        wait = w; dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), wait);
    }
}
BOOL hmods_open(void) { return current != nil; }

/* the command line: install an archive, or list the mods (returns the exit status) */
int hmods_cli(NSString *game, NSString *gameDir, NSString *archive, NSString *place)
{
    if (!archive) {
        for (NSDictionary *m in load_index(game)) printf("%s  %-28s %-8s %s\n", [m[@"enabled"] boolValue] ? "on " : "off", [m[@"name"] UTF8String], [m[@"kind"] UTF8String], [m[@"note"] UTF8String]);
        return 0;
    }
    Plan *P = plan_archive(archive, archive.lastPathComponent.stringByDeletingPathExtension, archive, game, gameDir);
    if (!P.why && plan_asks(P)) plan_choose(P, place ?: @"both");
    NSMutableDictionary *m = plan_commit(P);
    if (!m) { fprintf(stderr, "not installed: %s\n", P.why.UTF8String); return 1; }
    NSMutableArray *mods = load_index(game); [mods insertObject:m atIndex:0]; save_index(game, mods);
    printf("installed %s (%s): %s\n", [m[@"name"] UTF8String], [m[@"kind"] UTF8String], [m[@"note"] UTF8String]);
    for (NSString *rel in [P.place.allKeys sortedArrayUsingSelector:@selector(compare:)]) printf("  %s -> %s\n", rel.UTF8String, [[P.place[rel] componentsJoinedByString:@", "] UTF8String]);
    for (NSString *rel in P.extras) printf("  %s -> _extras\n", rel.UTF8String);
    return 0;
}
