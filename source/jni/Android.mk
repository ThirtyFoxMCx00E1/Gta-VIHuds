LOCAL_PATH := $(call my-dir)

# VIHud: GTA VI style main / pause / settings / loading screens and in-game HUD for GTA San Andreas (Android).
#   armeabi-v7a -> libVIHud32.so (game 2.00)    arm64-v8a -> libVIHud64.so (game 2.10)
# Based on PCMenu (transparent build): the paused game shows behind the pause menu.

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_MODULE := VIHud32
    LOCAL_CFLAGS += -mfloat-abi=softfp
else
    LOCAL_MODULE := VIHud64
endif
LOCAL_SRC_FILES := main.cpp mod/logger.cpp scripting.cpp
LOCAL_CFLAGS += -O2 -DNDEBUG -DPCMENU_TRANSPARENT -DVIHUD -std=c++17 -fno-exceptions -fno-rtti -Wall -Wno-unused-function -Wno-unused-variable
LOCAL_C_INCLUDES += $(LOCAL_PATH)
LOCAL_LDLIBS += -llog -lOpenSLES -ldl
include $(BUILD_SHARED_LIBRARY)
