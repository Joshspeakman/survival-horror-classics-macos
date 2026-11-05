/* GTA Classics: the Mods window (the launch window's Mods tile), in the manner of Dusklight's mod manager: the chosen
 * game's installed mods (on/off, priority, remove), a browser of GameBanana's mods for the game (search, most
 * downloaded, newest; Install downloads and installs), and archives dropped on the window or picked with Install from
 * File.
 *
 * Each mod lives in <data>/mods/<folder>/, listed in priority order (first wins) in <data>/mods/index.plist. What an
 * archive holds decides its kind:
 *   files  game files at their game-folder paths (data/, models/, audio/, text/ ...): read in place of the game's own
 *          (runtime/win32/files.c, GTA_MODS) - on when installed
 *   img    loose models and textures (.dff, .txd ...) meant to replace entries inside the game's .img archives: kept,
 *          off until archive replacement is supported
 *   code   ASI plugins, DLLs or CLEO scripts: native Windows code, which needs recompiling with the game (to come)
 *   saves  savegames: copied into the game's user files folder
 * Nothing in the game folder is changed. Archives: zip (ditto), rar and 7z (bsdtar). */
#import <Cocoa/Cocoa.h>

static NSString *mods_dir(NSString *data) { return [data stringByAppendingPathComponent:@"mods"]; }
static NSString *index_path(NSString *data) { return [mods_dir(data) stringByAppendingPathComponent:@"index.plist"]; }
static NSMutableArray<NSMutableDictionary *> *load_index(NSString *data)
{
    NSMutableArray *a = [NSMutableArray array];
    for (NSDictionary *d in [NSArray arrayWithContentsOfFile:index_path(data)] ?: @[]) if ([d isKindOfClass:NSDictionary.class]) [a addObject:[d mutableCopy]];
    return a;
}
static void save_index(NSString *data, NSArray *mods)
{
    [[NSFileManager defaultManager] createDirectoryAtPath:mods_dir(data) withIntermediateDirectories:YES attributes:nil error:nil];
    [mods writeToFile:index_path(data) atomically:YES];
}
/* GTA_MODS for the game: the enabled file mods' folders, highest priority first, one per line */
NSString *gcm_env(NSString *data)
{
    NSMutableArray *dirs = [NSMutableArray array];
    for (NSDictionary *m in load_index(data))
        if ([m[@"enabled"] boolValue] && [m[@"kind"] isEqualToString:@"files"]) [dirs addObject:[mods_dir(data) stringByAppendingPathComponent:m[@"folder"]]];
    return [dirs componentsJoinedByString:@"\n"];
}
/* installed, and how many are on */
void gcm_counts(NSString *data, NSUInteger *installed, NSUInteger *on)
{
    NSArray *a = load_index(data); *installed = a.count; *on = 0;
    for (NSDictionary *m in a) if ([m[@"enabled"] boolValue]) (*on)++;
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
static NSSet *GAME_DIRS(void) { return [NSSet setWithArray:@[@"data", @"models", @"audio", @"text", @"anim", @"txd", @"movies", @"skins", @"mp3", @"mss"]]; }
static NSSet *CODE_EXT(void) { return [NSSet setWithArray:@[@"asi", @"dll", @"cs", @"cleo", @"csa", @"csi", @"lua", @"fxt"]]; }
static NSSet *IMG_EXT(void) { return [NSSet setWithArray:@[@"dff", @"txd", @"col", @"ifp", @"ipl", @"ide", @"img"]]; }
/* the folder whose children look like the game folder's (any case), the shallowest */
static NSString *game_root(NSString *top)
{
    NSFileManager *fm = NSFileManager.defaultManager; NSMutableArray *queue = [NSMutableArray arrayWithObject:top];
    while (queue.count) {
        NSString *d = queue[0]; [queue removeObjectAtIndex:0]; BOOL isdir;
        for (NSString *c in [fm contentsOfDirectoryAtPath:d error:nil] ?: @[]) {
            NSString *p = [d stringByAppendingPathComponent:c];
            if (![fm fileExistsAtPath:p isDirectory:&isdir] || !isdir || [c isEqualToString:@"__MACOSX"]) continue;
            if ([GAME_DIRS() containsObject:c.lowercaseString]) return d;
            if (p.pathComponents.count - top.pathComponents.count < 4) [queue addObject:p];
        }
    }
    return nil;
}
/* unpacks and sorts an archive (or a single file) into <data>/mods; returns the index entry, or nil with *why */
static NSMutableDictionary *install_archive(NSString *path, NSString *name, NSString *source, NSString *data, NSString *userFiles, NSString *gameTag, NSString **why)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *incoming = [mods_dir(data) stringByAppendingPathComponent:[@".incoming-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    [fm createDirectoryAtPath:incoming withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *ext = path.pathExtension.lowercaseString; int rc;
    if ([ext isEqualToString:@"zip"]) rc = run(@"/usr/bin/ditto", @[@"-x", @"-k", path, incoming]);
    else if ([@[@"rar", @"7z", @"tar", @"gz", @"tgz", @"xz", @"bz2"] containsObject:ext]) rc = run(@"/usr/bin/bsdtar", @[@"-xf", path, @"-C", incoming]);
    else rc = [fm copyItemAtPath:path toPath:[incoming stringByAppendingPathComponent:path.lastPathComponent] error:nil] ? 0 : -1;
    if (rc != 0) { [fm removeItemAtPath:incoming error:nil]; *why = [NSString stringWithFormat:@"%@ couldn't be unpacked (zip, rar, 7z or a single file).", path.lastPathComponent]; return nil; }
    /* what is in it */
    BOOL code = NO, imgs = NO; NSMutableArray *saves = [NSMutableArray array]; NSUInteger nfiles = 0;
    NSRegularExpression *saveRe = [NSRegularExpression regularExpressionWithPattern:@"^gta(3|vc|sa)sf[0-9]+\\.b$" options:NSRegularExpressionCaseInsensitive error:nil];
    NSDirectoryEnumerator *en = [fm enumeratorAtPath:incoming];
    for (NSString *rel in en) {
        if ([rel hasPrefix:@"__MACOSX"] || [rel.lastPathComponent isEqualToString:@".DS_Store"]) continue;
        BOOL isdir; [fm fileExistsAtPath:[incoming stringByAppendingPathComponent:rel] isDirectory:&isdir]; if (isdir) continue;
        nfiles++; NSString *e = rel.pathExtension.lowercaseString, *fn = rel.lastPathComponent;
        if ([CODE_EXT() containsObject:e]) code = YES;
        if ([IMG_EXT() containsObject:e]) imgs = YES;
        if ([saveRe numberOfMatchesInString:fn options:0 range:NSMakeRange(0, fn.length)]) [saves addObject:rel];
    }
    if (!nfiles) { [fm removeItemAtPath:incoming error:nil]; *why = @"The archive is empty."; return nil; }
    NSString *root = game_root(incoming), *kind, *note;
    if (code) { kind = @"code"; note = @"Native Windows code (ASI, DLL or CLEO): needs recompiling with the game, which is coming"; }
    else if (root) { kind = @"files"; note = @"Game files: read in place of the game's own"; }
    else if (saves.count) {
        kind = @"saves"; NSUInteger copied = 0;
        [fm createDirectoryAtPath:userFiles withIntermediateDirectories:YES attributes:nil error:nil];
        for (NSString *rel in saves) {
            NSString *dst = [userFiles stringByAppendingPathComponent:rel.lastPathComponent];
            if ([fm fileExistsAtPath:dst]) { NSString *bak = [dst stringByAppendingString:@".before-mod"]; [fm removeItemAtPath:bak error:nil]; [fm moveItemAtPath:dst toPath:bak error:nil]; }
            if ([fm copyItemAtPath:[incoming stringByAppendingPathComponent:rel] toPath:dst error:nil]) copied++;
        }
        note = [NSString stringWithFormat:@"%lu savegame%s copied to your saves (any it replaced kept as .before-mod)", (unsigned long)copied, copied == 1 ? "" : "s"];
    }
    else if (imgs) { kind = @"img"; note = @"Models and textures for the game's .img archives: archive replacement is coming"; }
    else { kind = @"other"; note = @"Nothing the game reads directly (kept, off)"; }
    /* into its folder: a files mod's game-folder root, else everything */
    NSString *folder = slug(name), *dest = [mods_dir(data) stringByAppendingPathComponent:folder];
    for (int k = 2; [fm fileExistsAtPath:dest]; k++) { folder = [NSString stringWithFormat:@"%@-%d", slug(name), k]; dest = [mods_dir(data) stringByAppendingPathComponent:folder]; }
    NSString *from = [kind isEqualToString:@"files"] ? root : incoming;
    if (![fm moveItemAtPath:from toPath:dest error:nil]) { [fm removeItemAtPath:incoming error:nil]; *why = @"The mod couldn't be stored in the mods folder."; return nil; }
    [fm removeItemAtPath:incoming error:nil];
    return [@{@"name": name, @"folder": folder, @"kind": kind, @"note": note, @"source": source ?: @"", @"game": gameTag,
              @"enabled": @([kind isEqualToString:@"files"]), @"added": NSDate.date} mutableCopy];
}

// ---------------------------------------------------------------- the window
@interface GCMods : NSObject <NSTableViewDataSource, NSTableViewDelegate, NSWindowDelegate, NSSearchFieldDelegate>
@property NSPanel *panel, *browser; @property NSTableView *table, *btable; @property NSTextField *status, *bstatus; @property NSProgressIndicator *spin;
@property NSSearchField *search; @property NSPopUpButton *sort; @property NSImageView *preview; @property NSTextField *pinfo;
@property NSString *data, *userFiles, *title, *gameTag; @property int gb;
@property NSMutableArray<NSMutableDictionary *> *mods; @property NSArray<NSDictionary *> *results; @property NSUInteger busy;
@property (copy) void (^changed)(void);
- (void)browse:(id)s;
- (void)download:(NSDictionary *)f name:(NSString *)name page:(NSString *)page;
- (void)installPath:(NSString *)path name:(NSString *)name source:(NSString *)source done:(void (^)(BOOL))done;
@end

static GCMods *current;
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

@implementation GCMods
- (void)show:(NSWindow *)parent
{
    NSPanel *p = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 720, 470) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                                              backing:NSBackingStoreBuffered defer:NO];
    p.title = [NSString stringWithFormat:@"Mods — %@", self.title]; p.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    p.delegate = self; p.releasedWhenClosed = NO; p.minSize = NSMakeSize(620, 360); self.panel = p;
    NSView *cv = p.contentView;
    NSTextField *head = [NSTextField labelWithString:@"Installed mods: the first one wins where two change the same file. Drop a zip, rar or 7z here to install it."];
    head.frame = NSMakeRect(16, 438, 690, 20); head.textColor = NSColor.secondaryLabelColor; head.autoresizingMask = NSViewMinYMargin | NSViewWidthSizable; [cv addSubview:head];
    NSTableView *t = [NSTableView new]; self.table = t;
    [t addTableColumn:column(@"on", @"On", 34)]; [t addTableColumn:column(@"name", @"Mod", 250)]; [t addTableColumn:column(@"kind", @"Status", 380)];
    t.dataSource = self; t.delegate = self; t.usesAlternatingRowBackgroundColors = YES; t.rowHeight = 22;
    [t registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    NSScrollView *sv = scroller(t, NSMakeRect(16, 64, 688, 368)); sv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable; [cv addSubview:sv];
    NSArray *row = @[button(@"Browse GameBanana…", self, @selector(browse:), NSMakeRect(12, 22, 170, 32)),
                     button(@"Install from File…", self, @selector(installFile:), NSMakeRect(184, 22, 150, 32)),
                     button(@"Up", self, @selector(up:), NSMakeRect(344, 22, 54, 32)), button(@"Down", self, @selector(down:), NSMakeRect(398, 22, 64, 32)),
                     button(@"Remove", self, @selector(remove:), NSMakeRect(466, 22, 84, 32)), button(@"Open Folder", self, @selector(openFolder:), NSMakeRect(552, 22, 108, 32))];
    for (NSButton *b in row) [cv addSubview:b];
    NSButton *done = button(@"Done", self, @selector(done:), NSMakeRect(616, 22, 92, 32)); done.keyEquivalent = @"\r"; done.autoresizingMask = NSViewMinXMargin;
    ((NSButton *)row[5]).frame = NSMakeRect(552, 22, 64, 32); ((NSButton *)row[5]).title = @"Folder";
    [cv addSubview:done];
    NSTextField *st = [NSTextField labelWithString:@""]; st.frame = NSMakeRect(16, 4, 690, 16); st.font = [NSFont systemFontOfSize:11]; st.textColor = NSColor.secondaryLabelColor;
    st.autoresizingMask = NSViewWidthSizable; self.status = st; [cv addSubview:st];
    self.mods = load_index(self.data); [t reloadData];
    if (parent) [p setFrameOrigin:NSMakePoint(NSMidX(parent.frame) - 360, NSMidY(parent.frame) - 235)]; else [p center];
    [p makeKeyAndOrderFront:nil];
}
- (void)saveAndNotify { save_index(self.data, self.mods); [self.table reloadData]; if (self.changed) self.changed(); }
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
    [[NSFileManager defaultManager] trashItemAtURL:[NSURL fileURLWithPath:[mods_dir(self.data) stringByAppendingPathComponent:m[@"folder"]]] resultingItemURL:nil error:nil];
    [self.mods removeObjectAtIndex:(NSUInteger)r]; [self saveAndNotify];
}
- (void)openFolder:(id)s
{
    [[NSFileManager defaultManager] createDirectoryAtPath:mods_dir(self.data) withIntermediateDirectories:YES attributes:nil error:nil];
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:mods_dir(self.data) isDirectory:YES]];
}
/* install (off the main thread), then into the list at the top */
- (void)installPath:(NSString *)path name:(NSString *)name source:(NSString *)source done:(void (^)(BOOL))done
{
    self.busy++; self.status.stringValue = [NSString stringWithFormat:@"Installing %@…", name];
    NSString *data = self.data, *uf = self.userFiles, *tag = self.gameTag;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString *why = nil; NSMutableDictionary *m = install_archive(path, name, source, data, uf, tag, &why);
        dispatch_async(dispatch_get_main_queue(), ^{
            self.busy--;
            if (m) { [self.mods insertObject:m atIndex:0]; [self saveAndNotify]; self.status.stringValue = [NSString stringWithFormat:@"Installed %@: %@", name, m[@"note"]]; }
            else { self.status.stringValue = why ?: @"Not installed."; NSBeep(); }
            if (done) done(m != nil);
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
    NSTextField *pi = [NSTextField wrappingLabelWithString:@"Pick a mod to see it. Mods that are game files work now; ASI and CLEO mods need the mod recompiler, which is coming."];
    pi.frame = NSMakeRect(612, 110, 232, 150); pi.font = [NSFont systemFontOfSize:11]; pi.textColor = NSColor.secondaryLabelColor; pi.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    self.pinfo = pi; [cv addSubview:pi];
    NSButton *inst = button(@"Install", self, @selector(installPick:), NSMakeRect(608, 60, 120, 32)); inst.keyEquivalent = @"\r"; inst.autoresizingMask = NSViewMinXMargin;
    NSButton *page = button(@"Open Page", self, @selector(openPage:), NSMakeRect(730, 60, 118, 32)); page.autoresizingMask = NSViewMinXMargin;
    [cv addSubview:inst]; [cv addSubview:page];
    NSProgressIndicator *sp = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(16, 30, 16, 16)]; sp.style = NSProgressIndicatorStyleSpinning; sp.displayedWhenStopped = NO;
    self.spin = sp; [cv addSubview:sp];
    NSTextField *st = [NSTextField labelWithString:@"Mods from gamebanana.com: check each mod's page for its author's instructions."]; st.frame = NSMakeRect(40, 30, 560, 16);
    st.font = [NSFont systemFontOfSize:11]; st.textColor = NSColor.secondaryLabelColor; st.autoresizingMask = NSViewWidthSizable; self.bstatus = st; [cv addSubview:st];
    [b setFrameOrigin:NSMakePoint(NSMaxX(self.panel.frame) - 120, NSMinY(self.panel.frame) - 40)];
    [b makeKeyAndOrderFront:nil];
    [self query:nil];
}
static void fetch_json(NSString *url, void (^done)(id))
{
    NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:url]]; [rq setValue:@"GTA-Classics-macOS/1.0" forHTTPHeaderField:@"User-Agent"];
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
/* the mod's files: one is downloaded (the user picks when there are several), then installed */
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
- (void)download:(NSDictionary *)f name:(NSString *)name page:(NSString *)page
{
    [self.spin startAnimation:nil]; self.bstatus.stringValue = [NSString stringWithFormat:@"Downloading %@ (%.1f MB)…", f[@"_sFile"], [f[@"_nFilesize"] doubleValue] / 1048576.0];
    NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:f[@"_sDownloadUrl"]]]; [rq setValue:@"GTA-Classics-macOS/1.0" forHTTPHeaderField:@"User-Agent"];
    [[NSURLSession.sharedSession downloadTaskWithRequest:rq completionHandler:^(NSURL *loc, NSURLResponse *resp, NSError *e) {
        NSString *dst = nil;
        if (loc) { dst = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@", NSUUID.UUID.UUIDString, f[@"_sFile"]]];
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
@end

/* the Mods window for a game (one at a time); changed() runs whenever the list changes (the launcher's tile) */
void gcm_show(NSWindow *parent, NSString *title, NSString *gameTag, int gamebanana, NSString *data, NSString *userFiles, void (^changed)(void))
{
    if (current) { [current.panel makeKeyAndOrderFront:nil]; return; }
    GCMods *m = [GCMods new]; m.title = title; m.gameTag = gameTag; m.gb = gamebanana; m.data = data; m.userFiles = userFiles; m.changed = changed;
    current = m; [m show:parent];
    /* development: GTAC_MODS_INSTALL=<archive> installs it; GTAC_MODS_GB=<GameBanana mod id> downloads its first file */
    if (getenv("GTAC_MODS_INSTALL")) [m installPath:@(getenv("GTAC_MODS_INSTALL")) name:@(getenv("GTAC_MODS_INSTALL")).lastPathComponent.stringByDeletingPathExtension source:@"test" done:nil];
    if (getenv("GTAC_MODS_GB")) {
        fetch_json([NSString stringWithFormat:@"https://gamebanana.com/apiv11/Mod/%s?_csvProperties=_aFiles,_sName,_sProfileUrl", getenv("GTAC_MODS_GB")], ^(id j) {
            NSArray *files = [j isKindOfClass:NSDictionary.class] ? j[@"_aFiles"] : nil;
            if ([files isKindOfClass:NSArray.class] && files.count) { [m browse:nil]; [m download:files[0] name:j[@"_sName"] page:j[@"_sProfileUrl"]]; }
        });
    }
}
void gcm_browse(void) { if (current) [current browse:nil]; }   /* development (GTAC_MODS_BROWSE) */
