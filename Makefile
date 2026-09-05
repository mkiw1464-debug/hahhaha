TARGET  := iphone:clang:16.5:15.0
ARCHS   := arm64

include $(THEOS)/makefiles/common.mk

LIBRARY_NAME := FFNET_IOS

FFNET_IOS_FILES := \
    src/Main.mm   \
    src/fishhook.c

FFNET_IOS_CFLAGS  := \
    -Isrc              \
    -fobjc-arc         \
    -O2                \
    -std=c11           \
    -DTARGET_OS_IPHONE=1

FFNET_IOS_CCFLAGS := \
    -Isrc              \
    -fobjc-arc         \
    -O2                \
    -std=c++17         \
    -fno-exceptions    \
    -DTARGET_OS_IPHONE=1

FFNET_IOS_FRAMEWORKS := UIKit Foundation CoreGraphics QuartzCore Metal

FFNET_IOS_LIBRARIES  := substrate

FFNET_IOS_LDFLAGS    := \
    -weak-lsubstrate    \
    -Wl,-undefined,dynamic_lookup

include $(THEOS)/makefiles/library.mk
