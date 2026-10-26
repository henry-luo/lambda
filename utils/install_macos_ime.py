#!/usr/bin/env python3
"""Install or remove the local Lambda Chinese input source for this user."""

import argparse
import ctypes
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
STAGED = ROOT / "temp/ime_chinese/m5/LambdaChineseIME.app"
INSTALLED = Path.home() / "Library/Input Methods/LambdaChineseIME.app"
SOURCE_ID = "org.lambda-script.ChineseIME.Simplified"


def input_sources():
    cf = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
    carbon = ctypes.CDLL("/System/Library/Frameworks/Carbon.framework/Carbon")
    cf.CFStringCreateWithCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
    cf.CFStringCreateWithCString.restype = ctypes.c_void_p
    cf.CFURLCreateWithFileSystemPath.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                                 ctypes.c_int, ctypes.c_bool]
    cf.CFURLCreateWithFileSystemPath.restype = ctypes.c_void_p
    cf.CFRelease.argtypes = [ctypes.c_void_p]
    cf.CFArrayGetCount.argtypes = [ctypes.c_void_p]
    cf.CFArrayGetCount.restype = ctypes.c_long
    cf.CFArrayGetValueAtIndex.argtypes = [ctypes.c_void_p, ctypes.c_long]
    cf.CFArrayGetValueAtIndex.restype = ctypes.c_void_p
    cf.CFEqual.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    cf.CFEqual.restype = ctypes.c_bool
    carbon.TISRegisterInputSource.argtypes = [ctypes.c_void_p]
    carbon.TISRegisterInputSource.restype = ctypes.c_int32
    carbon.TISCreateInputSourceList.argtypes = [ctypes.c_void_p, ctypes.c_bool]
    carbon.TISCreateInputSourceList.restype = ctypes.c_void_p
    carbon.TISGetInputSourceProperty.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    carbon.TISGetInputSourceProperty.restype = ctypes.c_void_p
    carbon.TISDisableInputSource.argtypes = [ctypes.c_void_p]
    carbon.TISDisableInputSource.restype = ctypes.c_int32
    key = ctypes.c_void_p.in_dll(carbon, "kTISPropertyInputSourceID").value
    wanted = cf.CFStringCreateWithCString(None, SOURCE_ID.encode(), 0x08000100)

    def find():
        sources = carbon.TISCreateInputSourceList(None, True)
        if not sources:
            return None
        found = None
        for index in range(cf.CFArrayGetCount(sources)):
            source = cf.CFArrayGetValueAtIndex(sources, index)
            value = carbon.TISGetInputSourceProperty(source, key)
            if value and cf.CFEqual(value, wanted):
                found = source
                break
        # callers use the source before releasing its containing array.
        return sources, found

    return cf, carbon, wanted, find


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("install", "status", "remove"))
    args = parser.parse_args()
    if args.action == "install":
        if not STAGED.is_dir():
            parser.error(f"build and stage the app first: {STAGED}")
        subprocess.run(["codesign", "--verify", "--strict", str(STAGED)], check=True)
        INSTALLED.parent.mkdir(parents=True, exist_ok=True)
        if INSTALLED.exists():
            shutil.rmtree(INSTALLED)
        shutil.copytree(STAGED, INSTALLED)
    cf, carbon, wanted, find = input_sources()
    try:
        if args.action == "install":
            path = cf.CFStringCreateWithCString(None, str(INSTALLED).encode(), 0x08000100)
            try:
                url = cf.CFURLCreateWithFileSystemPath(None, path, 0, True)
                try:
                    status = carbon.TISRegisterInputSource(url)
                    if status:
                        raise RuntimeError(f"TISRegisterInputSource failed: {status}")
                finally:
                    cf.CFRelease(url)
            finally:
                cf.CFRelease(path)
        listing = find()
        found = listing is not None and listing[1] is not None
        pending = args.action == "install" and not found
        if args.action == "remove":
            if found:
                status = carbon.TISDisableInputSource(listing[1])
                if status:
                    raise RuntimeError(f"TISDisableInputSource failed: {status}")
            if INSTALLED.exists():
                shutil.rmtree(INSTALLED)
            print("Lambda Chinese input source removed")
        else:
            print(f"installed={INSTALLED.is_dir()} registered={found} path={INSTALLED}")
        if listing is not None:
            cf.CFRelease(listing[0])
        if pending:
            print("Registration was accepted, but this login session has not listed the "
                  "input source. Sign out and back in, then run 'status'.", file=sys.stderr)
            return 2
    finally:
        cf.CFRelease(wanted)


if __name__ == "__main__":
    sys.exit(main())
