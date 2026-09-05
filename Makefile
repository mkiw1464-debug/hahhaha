TARGET  := iphone:clang:16.5:15.0
ARCHS   := arm64

include $(THEOS)/makefiles/common.mk

DYLIB_NAME := FFNET_IOS

$(DYLIB_NAME)_FILES := \
    src/Main.mm   \
    src/fishhook.c

$(DYLIB_NAME)_CFLAGS  := \
    -Isrc              \
    -fobjc-arc         \
    -O2                \
    -std=c11           \
    -DTARGET_OS_IPHONE=1

$(DYLIB_NAME)_CCFLAGS := \
    -Isrc              \
    -fobjc-arc         \
    -O2                \
    -std=c++17         \
    -fno-exceptions    \
    -DTARGET_OS_IPHONE=1

$(DYLIB_NAME)_FRAMEWORKS := UIKit Foundation CoreGraphics QuartzCore Metal

$(DYLIB_NAME)_LIBRARIES  := substrate

$(DYLIB_NAME)_LDFLAGS    := \
    -weak-lsubstrate         \
    -Wl,-undefined,dynamic_lookup

include $(THEOS)/makefiles/dylib.mk
