APP_STL := c++_static
NDK_TOOLCHAIN_VERSION := 4.9
APP_CPPFLAGS := -std=c++11
APP_CPPFLAGS += -fexceptions
APP_CPPFLAGS += -fPIC -frtti  -fpermissive -fvisibility=hidden
APP_ABI := armeabi-v7a arm64-v8a
APP_OPTIM := release
APP_PLATFORM := android-21