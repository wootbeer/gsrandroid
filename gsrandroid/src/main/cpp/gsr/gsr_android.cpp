// gsr_android.cpp -- glue that lets GSRecomp's player builder (tools/gsr_builder) and the translator
// (gbarecomp/tools/gba_recompile) run inside the app on Android.
//
// The builder normally starts gba_recompile and a bundled GCC as separate programs; apps cannot start
// programs from their own storage, so builder_main.cpp has a few GSR_ANDROID_INPROCESS hooks:
//   * the translator runs as a function in a forked child process (gba_recompile_main), and
//   * the compile step is gsr_android_compile_and_link below (libtcc, gsr_build.c).
// Neither the translator nor the builder contains any Golden Sun code; that comes from the player's ROM.
#include "gsr_build.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

int gsr_builder_main(int argc, char** argv);  // builder_main.cpp's main(), renamed with -Dmain=

namespace fs = std::filesystem;

static void log_line(const char* line) {
    std::printf("@log %s\n", line);
    std::fflush(stdout);
}

static void progress(int permille) {
    std::printf("@progress %d 1000\n", permille);
    std::fflush(stdout);
}

bool gsr_android_compile_and_link(const std::string& gen, const std::string& out_dir,
                                  std::string* error) {
    std::printf("@stage Compiling the game on this device\n");
    std::fflush(stdout);
    const fs::path work = fs::path(out_dir) / "build_work";
    const std::string work_s = work.string();
    GsrBuildParams p{};
    p.gen_dir = gen.c_str();
    p.work_dir = work_s.c_str();
    p.out_dir = out_dir.c_str();
    p.host_so = "";
    const char* w = std::getenv("GSR_WORKERS");
    p.workers = w ? std::atoi(w) : 4;
    p.free_sources = 1;  // the builder deletes gen/ afterwards anyway
    p.log = log_line;
    p.progress = progress;
    char err[600] = {0};
    const int n = gsr_build_game(&p, err, sizeof err);
    std::error_code ec;
    fs::remove_all(work, ec);  // objects are no longer needed once linked
    if (n <= 0) {
        *error = std::string("compiling the game failed: ") + err;
        return false;
    }
    return true;
}

// For the C side (gsr_spike.c): run the whole builder as if it were `gsr_builder <args>`.
extern "C" int gsr_run_builder(int argc, char** argv) { return gsr_builder_main(argc, argv); }
