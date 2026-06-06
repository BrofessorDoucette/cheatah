#include "ndarray.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace cheatah::purrscript::ndarray {

namespace {

std::vector<std::ptrdiff_t> contiguous_strides(const std::vector<std::size_t>& shape) {
    std::vector<std::ptrdiff_t> s(shape.size());
    std::ptrdiff_t step = 1;
    for (std::size_t i = shape.size(); i-- > 0;) {
        s[i] = step;
        step *= static_cast<std::ptrdiff_t>(shape[i]);
    }
    return s;
}

std::size_t product(const std::vector<std::size_t>& shape) {
    std::size_t t = 1;
    for (std::size_t d : shape) t *= d;
    return t;
}

std::vector<std::size_t> to_size(const std::vector<long long>& v) {
    std::vector<std::size_t> out(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) out[i] = static_cast<std::size_t>(v[i]);
    return out;
}

// Advance a C-order multi-index odometer; returns false when it wraps past the end.
bool next_index(std::vector<std::size_t>& idx, const std::vector<std::size_t>& shape) {
    for (std::size_t i = shape.size(); i-- > 0;) {
        if (++idx[i] < shape[i]) return true;
        idx[i] = 0;
    }
    return false;
}

template <typename Op>
NDArray binary_op(const NDArray& a, const NDArray& b, Op op) {
    const std::vector<std::size_t> rshape = broadcast_shapes(a.shape(), b.shape());
    const NDArray av = broadcast_to(a, rshape);
    const NDArray bv = broadcast_to(b, rshape);
    NDArray out(rshape);
    auto& buf = *out.buffer();
    std::vector<std::size_t> idx(rshape.size(), 0);
    std::size_t flat = 0;
    do {
        buf[flat++] = op(av.at(idx), bv.at(idx));  // out is contiguous -> flat order
    } while (!rshape.empty() && next_index(idx, rshape));
    return out;
}

void format_rec(const NDArray& a, std::vector<std::size_t>& idx, std::size_t dim, std::string& out) {
    if (dim == a.ndim()) {
        std::ostringstream os;
        os << a.at(idx);
        out += os.str();
        return;
    }
    out += "[";
    for (std::size_t i = 0; i < a.shape()[dim]; ++i) {
        if (i != 0) out += ", ";
        idx[dim] = i;
        format_rec(a, idx, dim + 1, out);
    }
    out += "]";
}

} // namespace

NDArray::NDArray() : data_(std::make_shared<std::vector<double>>()) {}

NDArray::NDArray(std::vector<std::size_t> shape, double fill)
    : data_(std::make_shared<std::vector<double>>()), shape_(std::move(shape)) {
    data_->assign(product(shape_), fill);
    strides_ = contiguous_strides(shape_);
}

NDArray::NDArray(std::shared_ptr<std::vector<double>> data, std::vector<std::size_t> shape,
                 std::vector<std::ptrdiff_t> strides, std::size_t offset)
    : data_(std::move(data)), shape_(std::move(shape)), strides_(std::move(strides)),
      offset_(offset) {}

std::size_t NDArray::size() const { return product(shape_); }

double NDArray::at(const std::vector<std::size_t>& index) const {
    std::ptrdiff_t off = static_cast<std::ptrdiff_t>(offset_);
    for (std::size_t i = 0; i < index.size(); ++i) {
        off += static_cast<std::ptrdiff_t>(index[i]) * strides_[i];
    }
    return (*data_)[static_cast<std::size_t>(off)];
}

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

NDArray broadcast_to(const NDArray& a, const std::vector<std::size_t>& target) {
    const std::size_t n = target.size();
    if (a.ndim() > n) {
        throw std::runtime_error("ndarray: cannot broadcast to fewer dimensions");
    }
    std::vector<std::ptrdiff_t> ns(n, 0);  // stretched / missing dims -> stride 0
    const std::size_t pad = n - a.ndim();
    for (std::size_t i = 0; i < a.ndim(); ++i) {
        const std::size_t adim = a.shape()[i];
        if (adim == target[pad + i]) {
            ns[pad + i] = a.strides()[i];
        } else if (adim != 1) {
            throw std::runtime_error("ndarray: shape not broadcastable to target");
        }  // adim == 1 -> stride stays 0 (stretch)
    }
    return NDArray(a.buffer(), target, ns, a.offset());
}

NDArray array(const std::vector<double>& values) {
    NDArray a(std::vector<std::size_t>{values.size()});
    *a.buffer() = values;
    return a;
}

NDArray scalar(double value) {
    NDArray a;  // 0-d
    a.buffer()->assign(1, value);
    return a;
}

NDArray zeros(const std::vector<long long>& shape) { return NDArray(to_size(shape), 0.0); }
NDArray ones(const std::vector<long long>& shape) { return NDArray(to_size(shape), 1.0); }
NDArray full(const std::vector<long long>& shape, double value) {
    return NDArray(to_size(shape), value);
}

NDArray arange(double start, double stop, double step) {
    std::vector<double> v;
    if (step == 0.0) throw std::runtime_error("ndarray: arange step must be non-zero");
    for (double x = start; (step > 0) ? (x < stop) : (x > stop); x += step) v.push_back(x);
    return array(v);
}

NDArray reshape(const NDArray& a, const std::vector<long long>& shape) {
    const std::vector<std::size_t> ns = to_size(shape);
    if (product(ns) != a.size()) {
        throw std::runtime_error("ndarray: cannot reshape, size mismatch");
    }
    // Flatten in C-order (handles views), then lay out contiguously.
    NDArray out(ns);
    auto& buf = *out.buffer();
    std::vector<std::size_t> idx(a.ndim(), 0);
    std::size_t flat = 0;
    do {
        buf[flat++] = a.at(idx);
    } while (a.ndim() != 0 && next_index(idx, a.shape()));
    return out;
}

NDArray add(const NDArray& a, const NDArray& b) {
    return binary_op(a, b, [](double x, double y) { return x + y; });
}
NDArray sub(const NDArray& a, const NDArray& b) {
    return binary_op(a, b, [](double x, double y) { return x - y; });
}
NDArray mul(const NDArray& a, const NDArray& b) {
    return binary_op(a, b, [](double x, double y) { return x * y; });
}
NDArray divide(const NDArray& a, const NDArray& b) {
    return binary_op(a, b, [](double x, double y) { return x / y; });
}

double sum(const NDArray& a) {
    double s = 0.0;
    std::vector<std::size_t> idx(a.ndim(), 0);
    do {
        s += a.at(idx);
    } while (a.ndim() != 0 && next_index(idx, a.shape()));
    return s;
}
double mean(const NDArray& a) {
    const std::size_t n = a.size();
    return n == 0 ? 0.0 : sum(a) / static_cast<double>(n);
}

double get(const NDArray& a, const std::vector<long long>& index) { return a.at(to_size(index)); }

std::vector<long long> shape_of(const NDArray& a) {
    std::vector<long long> out(a.ndim());
    for (std::size_t i = 0; i < a.ndim(); ++i) out[i] = static_cast<long long>(a.shape()[i]);
    return out;
}
long long size_of(const NDArray& a) { return static_cast<long long>(a.size()); }

std::string to_string(const NDArray& a) {
    if (a.ndim() == 0) {
        std::ostringstream os;
        os << a.at({});
        return os.str();
    }
    std::vector<std::size_t> idx(a.ndim(), 0);
    std::string out;
    format_rec(a, idx, 0, out);
    return out;
}

} // namespace cheatah::purrscript::ndarray
