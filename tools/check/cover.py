"""Cover every screen with a black window for N seconds: cover.py SECONDS.

Needs a Python with PyObjC (reliability.py runs it with COVER_PY). Prints "covered" once the
windows are on screen and "uncovered" once they are gone; reliability.py times both lines.
The windows never take focus, so the game stays the frontmost app.
"""
import importlib
import sys
import time
from typing import Any

# PyObjC builds AppKit's names at run time and ships no stubs, so the module is typed Any.
AppKit: Any = importlib.import_module("AppKit")


def pump(seconds: float) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        AppKit.NSRunLoop.currentRunLoop().runUntilDate_(AppKit.NSDate.dateWithTimeIntervalSinceNow_(0.05))


def main() -> None:
    app = AppKit.NSApplication.sharedApplication()
    app.setActivationPolicy_(AppKit.NSApplicationActivationPolicyAccessory)
    wins = []
    for screen in AppKit.NSScreen.screens():
        w = AppKit.NSWindow.alloc().initWithContentRect_styleMask_backing_defer_(
            screen.frame(), AppKit.NSWindowStyleMaskBorderless, AppKit.NSBackingStoreBuffered, False)
        w.setBackgroundColor_(AppKit.NSColor.blackColor())
        w.setLevel_(AppKit.NSStatusWindowLevel)
        w.orderFrontRegardless()
        wins.append(w)
    pump(0.2)
    print("covered", flush=True)
    pump(float(sys.argv[1]))
    for w in wins:
        w.orderOut_(None)
    pump(0.2)
    print("uncovered", flush=True)


if __name__ == "__main__":
    main()
