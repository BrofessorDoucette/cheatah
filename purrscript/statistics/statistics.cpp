#include "statistics.hpp"

// Header-only module (all functions are templates constrained by NumericRange).
// This translation unit exists so the module builds as a (near-empty) library,
// consistent with every other purrscript module.
namespace cheatah::purrscript::statistics {}  // namespace anchor
