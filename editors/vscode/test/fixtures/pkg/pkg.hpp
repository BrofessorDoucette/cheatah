#pragma once
// The pkg umbrella header: purrc resolves `import pkg.*` by the FIRST segment to this file, which
// #includes every submodule's generated header (re-exporting their symbols into ::cheatah::pkg).
#include "text/lexer.gen.hpp"

namespace cheatah { namespace pkg = ::cheatah::pkg; }
