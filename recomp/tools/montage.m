// montage <out.png> <columns> <tile width> <image>...: a contact sheet of screenshots, each labelled with its file name
// (QA runs: tools/re_qa.sh). Build: clang -fobjc-arc -framework Cocoa -o montage montage.m
#import <Cocoa/Cocoa.h>

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc < 5) { fprintf(stderr, "usage: montage out.png columns tile_width image...\n"); return 1; }
        int cols = atoi(argv[2]), tw = atoi(argv[3]), n = argc - 4, th = tw * 3 / 4, lab = 18;
        int rows = (n + cols - 1) / cols, W = cols * tw, H = rows * (th + lab);
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:W pixelsHigh:H bitsPerSample:8 samplesPerPixel:4
            hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext *g = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep]; [NSGraphicsContext setCurrentContext:g];
        [[NSColor colorWithWhite:0.12 alpha:1] setFill]; NSRectFill(NSMakeRect(0, 0, W, H));
        for (int k = 0; k < n; k++) {
            int c = k % cols, r = k / cols; CGFloat x = c * tw, y = H - (r + 1) * (th + lab);
            NSImage *img = [[NSImage alloc] initWithContentsOfFile:@(argv[4 + k])];
            if (img) [img drawInRect:NSMakeRect(x, y, tw, th) fromRect:NSZeroRect operation:NSCompositingOperationCopy fraction:1];
            NSString *name = [@(argv[4 + k]) lastPathComponent];
            [name drawAtPoint:NSMakePoint(x + 4, y + th + 2) withAttributes:@{NSFontAttributeName: [NSFont systemFontOfSize:12], NSForegroundColorAttributeName: NSColor.whiteColor}];
        }
        [NSGraphicsContext restoreGraphicsState];
        NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        return [png writeToFile:@(argv[1]) atomically:YES] ? 0 : 1;
    }
}
