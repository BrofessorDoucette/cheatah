// purrc — the cheatah compiler.
//
// Compiles a .purr program into a loadable module that the cheatah runtime runs:
//   purrc <input.purr> [-o <output>]
//
// Pipeline: read -> lex -> parse -> codegen C++ -> invoke the C++ backend to build
// a shared module (exporting purr_main). The module links the stdlib libraries the
// program imported (static); the Runtime symbols stay undefined and resolve from
// the cheatah-runtime host at load time.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "codegen.hpp"
#include "parser.hpp"
#include "version.hpp"

#ifndef CHEATAH_ROOT
#define CHEATAH_ROOT ""
#endif
#ifndef CHEATAH_RUNTIME_INCLUDE
#define CHEATAH_RUNTIME_INCLUDE ""
#endif
#ifndef CHEATAH_LIB_DIR
#define CHEATAH_LIB_DIR ""
#endif
#ifndef CHEATAH_CXX
#define CHEATAH_CXX "c++"
#endif

using namespace cheatah;

namespace {

// Run a program directly (fork + execvp, NO shell) so arguments are passed
// verbatim — file paths can't be interpreted as shell syntax. Returns the exit
// code, or -1 on failure.
int run_process(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv.data());
        std::perror("purrc: exec C++ compiler");
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Default output: drop a trailing ".purr", append ".so".
std::string default_output(const std::string& input) {
    std::string base = input;
    const std::string ext = ".purr";
    if (base.size() >= ext.size() && base.compare(base.size() - ext.size(), ext.size(), ext) == 0) {
        base.erase(base.size() - ext.size());
    }
    return base + ".so";
}

} // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string output;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--version" || a == "-v") {
            std::cout << "cheatah purrc " << version() << "\n";
            return 0;
        } else if (a == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else if (input.empty()) {
            input = a;
        }
    }
    if (input.empty()) {
        std::cerr << "usage: purrc <input.purr> [-o <output>]\n"
                     "       purrc --version\n";
        return 2;
    }
    if (output.empty()) {
        output = default_output(input);
    }

    std::ifstream in(input);
    if (!in) {
        std::cerr << "purrc: cannot open '" << input << "'\n";
        return 1;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string source = buf.str();

    const ParseResult pr = parse_source(source);
    if (!pr.ok()) {
        for (const Diagnostic& d : pr.diagnostics) {
            std::cerr << input << ":" << d.pos.line << ":" << d.pos.column << ": error: "
                      << d.message << "\n";
        }
        return 1;
    }

    const CodegenResult cg = codegen(pr.program);
    if (!cg.ok()) {
        for (const std::string& d : cg.diagnostics) {
            std::cerr << "purrc: " << d << "\n";
        }
        return 1;
    }

    // Emit the generated C++ next to the output (kept, so it can be inspected).
    const std::string gen_path = output + ".gen.cpp";
    {
        std::ofstream gen(gen_path);
        if (!gen) {
            std::cerr << "purrc: cannot write '" << gen_path << "'\n";
            return 1;
        }
        gen << cg.source;
    }

    // The modules to compile/link: the imported ones, plus `builtins` which is
    // always available (no import) — so every program sees len(), hex(), etc.
    std::vector<std::string> modules = {"builtins"};
    modules.insert(modules.end(), cg.modules.begin(), cg.modules.end());

    // Transitive module dependencies (a module whose public API uses another).
    static const std::map<std::string, std::vector<std::string>> kModuleDeps = {
        {"linalg", {"ndarray"}},  // the linalg routines operate on ndarray
    };
    std::set<std::string> seen(modules.begin(), modules.end());
    for (std::size_t i = 0; i < modules.size(); ++i) {
        const auto it = kModuleDeps.find(modules[i]);
        if (it == kModuleDeps.end()) continue;
        for (const std::string& dep : it->second) {
            if (seen.insert(dep).second) modules.push_back(dep);
        }
    }

    // Build the C++ backend argv as a LIST and exec it directly — NO shell — so
    // file paths can never be interpreted as shell metacharacters (no command
    // injection). Maximum optimization: programs run on the same machine, so
    // -march=native unlocks the host's SIMD; -w because generated code legitimately
    // has redundant parens (the language guarantees correctness).
    std::vector<std::string> args = {
        CHEATAH_CXX, "-std=c++20", "-O3", "-march=native", "-DNDEBUG",
        "-fno-semantic-interposition", "-fPIC", "-shared", "-pthread", "-w",
        std::string("-I") + CHEATAH_RUNTIME_INCLUDE,
    };
    for (const std::string& m : modules) {
        args.push_back(std::string("-I") + CHEATAH_ROOT + "/" + m);
    }
    args.push_back(gen_path);
    for (const std::string& m : modules) {
        args.push_back(std::string(CHEATAH_LIB_DIR) + "/libcheatah_" + m + ".a");
    }
    // -shared leaves the Runtime symbols undefined; they resolve from the
    // cheatah-runtime host at dlopen time.
    args.push_back("-o");
    args.push_back(output);

    const int rc = run_process(args);
    if (rc != 0) {
        std::cerr << "purrc: C++ backend failed (exit " << rc << ")\n";
        return 1;
    }
    std::cerr << "purrc: " << input << " -> " << output << "\n";
    return 0;
}
