#pragma once
// Generated header for pkg.textlex (folded base + .purr types), included by the pkg umbrella.
#include <string>

namespace cheatah { namespace pkg {

enum class LexKind { WORD, NUMBER, PUNCT };

// A header-only helper (no .purr definition) — resolves via the header fallback.
inline std::string kind_name(LexKind k) { return "word"; }

// A cheatah interface compiles to a C++20 concept — resolves via `concept` recognition.
template <class Self>
concept Drawable = requires(Self s) { s.draw(); };

struct StrText { std::string s; };
inline int scan(int n) { return n; }

}} // namespace cheatah::pkg
