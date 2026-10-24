// The Lost Age's main() (tla_runner.cpp, compiled with -Dmain=gsr_tla_engine_main_cpp) under a C name that
// dlsym can find (gsr_engine.c, gsr_game.c).
int gsr_tla_engine_main_cpp(int argc, char** argv);
extern "C" int gsr_tla_engine_main(int argc, char** argv) { return gsr_tla_engine_main_cpp(argc, argv); }
