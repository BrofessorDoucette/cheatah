#pragma once

// cheatah runtime — the host for compiled cheatah programs.
//
// A cheatah program (a .purr file) is compiled by `purrc` into a loadable module
// that exports `extern "C" void purr_main(cheatah::runtime::Runtime&)`. The
// `cheatah-runtime` executable dlopens that module and calls purr_main with a live
// Runtime. The runtime is fully HEADLESS — the language core knows nothing about
// graphics, networking, or any host facility beyond the standard library;
// capabilities like windows are provided by EXTERNAL modules a program imports.
namespace cheatah::runtime {

// The host context handed to a program's purr_main. Minimal today; host services
// (logging, lifecycle, …) live here as the runtime grows.
class Runtime {
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
};

} // namespace cheatah::runtime
