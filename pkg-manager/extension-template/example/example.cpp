// cheatah `example` template extension — see example.hpp for the interface.

#include "example.hpp"

namespace cheatah::example {

std::string greet(const std::string& who) {
    return "hello, " + who + ", from the cheatah-example extension";
}

} // namespace cheatah::example
