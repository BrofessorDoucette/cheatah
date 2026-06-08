#include "ndarray.hpp"

#include <algorithm>
#include <stdexcept>

namespace cheatah::ndarray {

// Every other ndarray function is now a template over the element type and lives
// in the header (ndarray.hpp). `broadcast_shapes` is the one shape-only,
// element-type-independent function, so it is compiled here — giving the library a
// translation unit and a real symbol while the templated ops monomorphize at the
// call site.
std::vector<std::size_t> broadcast_shapes(const std::vector<std::size_t>& a,
                                          const std::vector<std::size_t>& b) {
    const std::size_t n = std::max(a.size(), b.size());
    std::vector<std::size_t> r(n);
    for (std::size_t i = 0; i < n; ++i) {
        // Align from the right (trailing dimensions).
        const std::size_t da = (i < n - a.size()) ? 1 : a[i - (n - a.size())];
        const std::size_t db = (i < n - b.size()) ? 1 : b[i - (n - b.size())];
        if (da == db || db == 1) {
            r[i] = da;
        } else if (da == 1) {
            r[i] = db;
        } else {
            throw std::runtime_error("ndarray: operands could not be broadcast together");
        }
    }
    return r;
}

}  // namespace cheatah::ndarray
