/* Hand-written config for the Android arm64 build of tcc (libtcc, in-process use only). */
#define TCC_VERSION "0.9.28rc"
#define CC_NAME CC_clang
#define GCC_MAJOR 4
#define GCC_MINOR 2
#ifndef TCC_TARGET_ARM64
#define TCC_TARGET_ARM64 1
#endif
#define CONFIG_TRIPLET "aarch64-linux-android"
#define CONFIG_TCCDIR "/nonexistent"
#define CONFIG_TCC_PREDEFS 1
#define CONFIG_TCC_SEMLOCK 0
