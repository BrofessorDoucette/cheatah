#pragma once

// purrscript ndarray — our own numpy-flavored N-dimensional array (of doubles),
// with NumPy broadcasting. See numpy.org/doc/stable/user/basics.broadcasting.html.
//
// Design (the "pointers + a bit of thinking"): the elements live in a shared
// buffer (std::shared_ptr<std::vector<double>>) and an array is a VIEW into it —
// {shape, strides, offset}. That makes reshape and **broadcast** zero-copy: to
// stretch a dimension of size 1, we just give it a stride of 0, so every index
// along it reads the same element. Shared ownership = memory-safe, no manual frees.
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace cheatah::purrscript::ndarray {

class NDArray {
public:
    NDArray();
    explicit NDArray(std::vector<std::size_t> shape, double fill = 0.0);  // contiguous
    NDArray(std::shared_ptr<std::vector<double>> data, std::vector<std::size_t> shape,
            std::vector<std::ptrdiff_t> strides, std::size_t offset);

    const std::vector<std::size_t>& shape() const { return shape_; }
    const std::vector<std::ptrdiff_t>& strides() const { return strides_; }
    std::size_t ndim() const { return shape_.size(); }
    std::size_t size() const;  // product of dims (1 for a 0-d scalar)

    double at(const std::vector<std::size_t>& index) const;  // element via strides
    const std::shared_ptr<std::vector<double>>& buffer() const { return data_; }
    std::size_t offset() const { return offset_; }

private:
    std::shared_ptr<std::vector<double>> data_;
    std::vector<std::size_t> shape_;
    std::vector<std::ptrdiff_t> strides_;  // element strides
    std::size_t offset_ = 0;
};

// The broadcast result shape of two shapes (NumPy rules), or throws if incompatible.
std::vector<std::size_t> broadcast_shapes(const std::vector<std::size_t>& a,
                                          const std::vector<std::size_t>& b);
// A zero-copy view of `a` stretched to `target` (size-1 / missing dims get stride 0).
NDArray broadcast_to(const NDArray& a, const std::vector<std::size_t>& target);

// ---- factories (shapes arrive from purrscript as list[int]) ----
NDArray array(const std::vector<double>& values);  // 1-D from a list[float]
NDArray scalar(double value);                      // 0-D (broadcasts to anything)
NDArray zeros(const std::vector<long long>& shape);
NDArray ones(const std::vector<long long>& shape);
NDArray full(const std::vector<long long>& shape, double value);
NDArray arange(double start, double stop, double step);
NDArray reshape(const NDArray& a, const std::vector<long long>& shape);

// ---- element-wise ops (broadcasting) ----
NDArray add(const NDArray& a, const NDArray& b);
NDArray sub(const NDArray& a, const NDArray& b);
NDArray mul(const NDArray& a, const NDArray& b);
NDArray divide(const NDArray& a, const NDArray& b);

// ---- reductions / access / display ----
double sum(const NDArray& a);
double mean(const NDArray& a);
double get(const NDArray& a, const std::vector<long long>& index);
std::vector<long long> shape_of(const NDArray& a);
long long size_of(const NDArray& a);
std::string to_string(const NDArray& a);  // e.g. "[[1, 2], [3, 4]]"

} // namespace cheatah::purrscript::ndarray
