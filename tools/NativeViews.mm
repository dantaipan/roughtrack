#include "NativeViews.h"

#import <Cocoa/Cocoa.h>

#include <iostream>

static void printView (NSView* view, int depth)
{
    const auto frame = view.frame;
    const auto bounds = view.bounds;

    std::cout << std::string ((size_t) depth * 2, ' ') << view.className.UTF8String << " frame " << frame.origin.x << "," << frame.origin.y
              << " " << frame.size.width << "x" << frame.size.height << " bounds " << bounds.size.width << "x" << bounds.size.height
              << " hidden " << (view.isHidden ? 1 : 0);

    if (view.layer != nil)
        std::cout << " layer " << view.layer.bounds.size.width << "x" << view.layer.bounds.size.height << " scale " << view.layer.contentsScale;

    std::cout << std::endl;

    for (NSView* child in view.subviews)
        printView (child, depth + 1);
}

void printNativeViewTree (void* nsView)
{
    printView ((__bridge NSView*) nsView, 1);
}
