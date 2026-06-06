#pragma once

#include <string>
#include <vector>

#include "ast.hpp"

// cheatah codegen — AST -> C++ source for a loadable program module.
//
// The emitted translation unit exports `extern "C" void purr_main(Runtime&)`,
// which the cheatah runtime dlopens and calls. Imports become #includes; module
// calls (io.print) become namespace-qualified calls (cheatah::io::
// print). `modules` lists the imported module roots so purrc knows which stdlib
// libraries to link.
namespace cheatah {

struct CodegenResult {
    std::string source;                    // generated C++
    std::vector<std::string> modules;      // imported module roots (for linking)
    std::vector<std::string> diagnostics;  // empty on success

    bool ok() const { return diagnostics.empty(); }
};

CodegenResult codegen(const Program& program);

} // namespace cheatah
