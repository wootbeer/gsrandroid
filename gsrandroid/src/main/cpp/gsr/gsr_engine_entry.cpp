// The engine's main() (runner_main.cpp, compiled with -Dmain=gsr_engine_main_cpp) under a C name
// that dlsym can find.
int gsr_engine_main_cpp(int argc, char** argv);
extern "C" int gsr_engine_main(int argc, char** argv) { return gsr_engine_main_cpp(argc, argv); }
