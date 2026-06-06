#pragma once

// cheatah hashlib — cryptographic hashing, mirroring the core of
// https://docs.python.org/3/library/hashlib.html. Self-contained SHA-256 (no
// external crypto dependency); returns a lowercase hex digest.
#include <string>
#include <string_view>

namespace cheatah::hashlib {

std::string sha256(std::string_view data);  // 64-char lowercase hex digest

} // namespace cheatah::hashlib
