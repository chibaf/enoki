#include <enoki/cuda.h>
#include <enoki/autodiff.h>
#include <pybind11/pybind11.h>
#include <pybind11/operators.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <sstream>

using namespace enoki;
namespace py = pybind11;
using namespace py::literals;

using Float     = float;
using FloatC    = CUDAArray<Float>;
using UInt32C   = CUDAArray<uint32_t>;
using UInt64C   = CUDAArray<uint64_t>;
using BoolC     = CUDAArray<bool>;

using FloatD    = DiffArray<FloatC>;
using UInt32D   = DiffArray<UInt32C>;
using UInt64D   = DiffArray<UInt64C>;
using BoolD     = DiffArray<BoolC>;

using Vector2fC = Array<FloatC, 2>;
using Vector2fD = Array<FloatD, 2>;
using Vector2uC = Array<UInt32C, 2>;
using Vector2uD = Array<UInt32D, 2>;
using Vector2bC = mask_t<Vector2fC>;
using Vector2bD = mask_t<Vector2fD>;

using Vector3fC = Array<FloatC, 3>;
using Vector3fD = Array<FloatD, 3>;
using Vector3uC = Array<UInt32C, 3>;
using Vector3uD = Array<UInt32D, 3>;
using Vector3bC = mask_t<Vector3fC>;
using Vector3bD = mask_t<Vector3fD>;

using Vector4fC = Array<FloatC, 4>;
using Vector4fD = Array<FloatD, 4>;
using Vector4uC = Array<UInt32C, 4>;
using Vector4uD = Array<UInt32D, 4>;
using Vector4bC = mask_t<Vector4fC>;
using Vector4bD = mask_t<Vector4fD>;

struct CUDAManagedBuffer {
    CUDAManagedBuffer(size_t size) {
        ptr = cuda_managed_malloc(size);
    }

    ~CUDAManagedBuffer() {
        cuda_free(ptr);
    }

    void *ptr = nullptr;
};

template <typename Array> py::object enoki_to_torch(const Array &array, bool eval);
template <typename Array> py::object enoki_to_numpy(const Array &array, bool eval);
template <typename Array> Array torch_to_enoki(py::object src);

template <typename Array>
py::class_<Array> bind(py::module &m, const char *name) {
    using Scalar = scalar_t<Array>;
    using Value  = value_t<Array>;

    constexpr bool IsMask  = std::is_same_v<Scalar, bool>;
    constexpr bool IsFloat = std::is_floating_point_v<Scalar>;

    py::class_<Array> cl(m, name);

    cl.def(py::init<>())
      .def(py::init<const Array &>())
      .def(py::init<const Value &>())
      .def(py::self == py::self)
      .def(py::self != py::self)
      .def("__repr__", [](const Array &a) {
          std::ostringstream oss;
          oss << a;
          return oss.str();
      })
      .def_static("zero", [](size_t size) { return zero<Array>(size); });

    if constexpr (!is_diff_array_v<Array>) {
        cl.def(py::init([](const py::object &obj) {
              return torch_to_enoki<Array>(obj);
          }))
          .def("torch", &enoki_to_torch<Array>, "eval"_a = true)
          .def("numpy", &enoki_to_numpy<Array>, "eval"_a = true);
    }

    if constexpr (!IsMask) {
        cl.def(py::self + py::self)
          .def(py::self - py::self)
          .def(py::self / py::self)
          .def(py::self * py::self)
          .def(py::self < py::self)
          .def(py::self > py::self)
          .def(py::self >= py::self)
          .def(py::self <= py::self)
          .def(-py::self);
    } else {
        cl.def(py::self | py::self)
          .def(py::self & py::self)
          .def(py::self ^ py::self)
          .def(!py::self)
          .def(~py::self);
    }

    if constexpr (!IsMask && array_depth_v<Array> == 1) {
        if (IsFloat)
            cl.def_static("linspace",
                  [](Scalar min, Scalar max, size_t size) {
                      return linspace<Array>(min, max, size);
                  },
                  "min"_a, "max"_a, "size"_a);

        cl.def_static("arange",
              [](size_t size) { return arange<Array>(size); }, "size"_a);

        cl.def_static("full",
                      [](const Scalar &value, size_t size) {
                          return full<Array>(value, size);
                      }, "value"_a, "size"_a);
    }

    cl.def("__getitem__", [](const Array &a, size_t index) -> Value {
        if (index >= a.size())
            throw py::index_error();
        return a.coeff(index);
    });

    cl.def("__len__", [](const Array &a) { return a.size(); });
    cl.def("resize", [](Array &a, size_t size) { enoki::set_slices(a, size); });

    if constexpr (array_depth_v<Array> > 1) {
        cl.def("__setitem__", [](Array &a, size_t index, const Value &b) {
            if (index >= Array::Size)
                throw py::index_error();
            a.coeff(index) = b;
        });

        if constexpr (array_size_v<Array> == 2)
            cl.def(py::init<Value, Value>());
        else if constexpr (array_size_v<Array> == 3)
            cl.def(py::init<Value, Value, Value>());
        else if constexpr (array_size_v<Array> == 4)
            cl.def(py::init<Value, Value, Value, Value>());

        if constexpr (array_size_v<Array> >= 1)
            cl.def_property("x", [](const Array &a) { return a.x(); },
                                 [](Array &a, const Value &v) { a.x() = v; });
        if constexpr (array_size_v<Array> >= 2)
            cl.def_property("y", [](const Array &a) { return a.y(); },
                                 [](Array &a, const Value &v) { a.y() = v; });
        if constexpr (array_size_v<Array> >= 3)
            cl.def_property("z", [](const Array &a) { return a.z(); },
                                 [](Array &a, const Value &v) { a.z() = v; });
        if constexpr (array_size_v<Array> >= 4)
            cl.def_property("w", [](const Array &a) { return a.w(); },
                                 [](Array &a, const Value &v) { a.w() = v; });

        if constexpr (!IsMask) {
            m.def("dot", [](const Array &a, const Array &b) { return enoki::dot(a, b); });
            m.def("abs_dot", [](const Array &a, const Array &b) { return enoki::abs_dot(a, b); });
            m.def("normalize", [](const Array &a) { return enoki::normalize(a); });

            if constexpr (array_size_v<Array> == 3)
                m.def("cross", [](const Array &a, const Array &b) { return enoki::cross(a, b); });
        }
    }

    if constexpr (array_depth_v<Array> == 1) {
        m.def("gather",
              [](const Array &source, const uint32_array_t<Array> &index, mask_t<Array> &mask) {
                  return gather<Array>(source, index, mask);
              },
              "source"_a, "index"_a, "mask"_a = true);

        m.def("scatter",
              [](Array &target, const Array &source,
                 const uint32_array_t<Array> &index,
                 mask_t<Array> &mask) { scatter(target, source, index, mask); },
              "target"_a, "source"_a, "index"_a, "mask"_a = true);

        m.def("scatter_add",
              [](Array &target, const Array &source,
                 const uint32_array_t<Array> &index,
                 mask_t<Array> &mask) { scatter_add(target, source, index, mask); },
              "target"_a, "source"_a, "index"_a, "mask"_a = true);
    }

    if constexpr (IsFloat) {
        m.def("abs", [](const Array &a) { return enoki::abs(a); });
        m.def("sqrt", [](const Array &a) { return enoki::sqrt(a); });
        m.def("rcp", [](const Array &a) { return enoki::rcp(a); });
        m.def("rsqrt", [](const Array &a) { return enoki::rsqrt(a); });

        m.def("ceil", [](const Array &a) { return enoki::ceil(a); });
        m.def("floor", [](const Array &a) { return enoki::floor(a); });
        m.def("round", [](const Array &a) { return enoki::round(a); });
        m.def("trunc", [](const Array &a) { return enoki::trunc(a); });

        m.def("sin", [](const Array &a) { return enoki::sin(a); });
        m.def("cos", [](const Array &a) { return enoki::cos(a); });
        m.def("sincos", [](const Array &a) { return enoki::sincos(a); });
        m.def("tan", [](const Array &a) { return enoki::tan(a); });
        m.def("sec", [](const Array &a) { return enoki::sec(a); });
        m.def("csc", [](const Array &a) { return enoki::csc(a); });
        m.def("cot", [](const Array &a) { return enoki::cot(a); });
        m.def("asin", [](const Array &a) { return enoki::asin(a); });
        m.def("acos", [](const Array &a) { return enoki::acos(a); });
        m.def("atan", [](const Array &a) { return enoki::atan(a); });
        m.def("atan2", [](const Array &a, const Array &b) {
            return enoki::atan2(a, b);
        });

        m.def("sinh", [](const Array &a) { return enoki::sinh(a); });
        m.def("cosh", [](const Array &a) { return enoki::cosh(a); });
        m.def("sincosh", [](const Array &a) { return enoki::sincosh(a); });
        m.def("tanh", [](const Array &a) { return enoki::tanh(a); });
        m.def("sech", [](const Array &a) { return enoki::sech(a); });
        m.def("csch", [](const Array &a) { return enoki::csch(a); });
        m.def("coth", [](const Array &a) { return enoki::coth(a); });
        m.def("asinh", [](const Array &a) { return enoki::asinh(a); });
        m.def("acosh", [](const Array &a) { return enoki::acosh(a); });
        m.def("atanh", [](const Array &a) { return enoki::atanh(a); });

        m.def("log", [](const Array &a) { return enoki::log(a); });
        m.def("exp", [](const Array &a) { return enoki::exp(a); });
        m.def("pow", [](const Array &a, const Array &b) {
            return enoki::pow(a, b);
        });
    }

    if constexpr (!IsMask) {
        m.def("max", [](const Array &a, const Array &b) { return enoki::max(a, b); });
        m.def("min", [](const Array &a, const Array &b) { return enoki::min(a, b); });

        m.def("hsum", [](const Array &a) { return enoki::hsum(a); });
        m.def("hprod", [](const Array &a) { return enoki::hprod(a); });
        m.def("hmin", [](const Array &a) { return enoki::hmin(a); });
        m.def("hmax", [](const Array &a) { return enoki::hmax(a); });

        m.def("fmadd", [](const Array &a, const Array &b, const Array &c) {
            return enoki::fmadd(a, b, c);
        });
        m.def("fmsub", [](const Array &a, const Array &b, const Array &c) {
            return enoki::fmsub(a, b, c);
        });
        m.def("fnmadd", [](const Array &a, const Array &b, const Array &c) {
            return enoki::fnmadd(a, b, c);
        });
        m.def("fnmsub", [](const Array &a, const Array &b, const Array &c) {
            return enoki::fnmsub(a, b, c);
        });
    } else {
        m.def("any", [](const Array &a) { return enoki::any(a); });
        m.def("none", [](const Array &a) { return enoki::none(a); });
        m.def("all", [](const Array &a) { return enoki::all(a); });
    }

    m.def("eq", [](const Array &a, const Array &b) { return eq(a, b); });
    m.def("neq", [](const Array &a, const Array &b) { return neq(a, b); });

    m.def("select", [](const mask_t<Array> &a, const Array &b, const Array &c) {
        return enoki::select(a, b, c);
    });

    if constexpr (IsFloat && is_diff_array_v<Array>) {
        m.def("detach", [](const Array &a) { return detach(a); });
        m.def("requires_gradient",
              [](const Array &a) { return requires_gradient(a); },
              "array"_a);

        m.def("set_requires_gradient",
              [](Array &a, bool value) { set_requires_gradient(a, value); },
              "array"_a, "value"_a = true);

        m.def("gradient", [](Array &a) { return gradient(a); });
        m.def("set_gradient",
              [](Array &a, const Array &g) { set_gradient(a, detach(g)); });

        m.def("graphviz", [](const Array &a) { return graphviz(a); });

        if constexpr (array_depth_v<Array> == 1) {
            m.def("backward", [](Array &a) { return backward(a); });
            cl.def_static("backward", []() { return backward<Array>(); });
        }
    }

    m.def("set_label", [](const Array &a, const char *label) {
        set_label(a, label);
    });

    py::implicitly_convertible<Value, Array>();

    if constexpr (IsFloat)
        py::implicitly_convertible<int, Array>();

    if constexpr (!std::is_same_v<Value, Scalar>)
        py::implicitly_convertible<Scalar, Array>();

    return cl;
}

template <typename Scalar> py::object torch_dtype() {
    py::object torch = py::module::import("torch");
    const char *name = nullptr;

    if (std::is_same_v<Scalar, enoki::half>) {
        name = "float16";
    } else if (std::is_same_v<Scalar, float>) {
        name = "float32";
    } else if (std::is_same_v<Scalar, double>) {
        name = "float64";
    } else if (std::is_integral_v<Scalar>) {
        if (sizeof(Scalar) == 1)
            name = std::is_signed_v<Scalar> ? "int8" : "uint8";
        else if (sizeof(Scalar) == 2)
            name = "int16";
        else if (sizeof(Scalar) == 4)
            name = "int32";
        else if (sizeof(Scalar) == 8)
            name = "int64";
    }

    if (name == nullptr)
        throw std::runtime_error("pytorch_dtype(): Unsupported type");

    return torch.attr(name);
}

template <size_t Index, size_t Dim, typename Source, typename Target>
static void copy_array_gather(size_t offset,
                              const std::array<size_t, Dim> &shape,
                              const std::array<size_t, Dim> &strides,
                              const Source &source, Target &target) {
    using namespace enoki;
    if constexpr (Index == Dim - 1) {
        using UInt32 = uint32_array_t<Source>;
        UInt32 index = fmadd(arange<UInt32>((uint32_t) shape[Index]),
                             (uint32_t) strides[Index], (uint32_t) offset);
        target = gather<Target>(source, index);
    } else {
        const size_t step = strides[Index];
        for (size_t i = 0; i < shape[Index]; ++i) {
            copy_array_gather<Index + 1, Dim>(offset, shape, strides, source,
                                              target.coeff(i));
            offset += step;
        }
    }
}

template <size_t Index, size_t Dim, typename Source, typename Target>
static void copy_array_scatter(size_t offset,
                               const std::array<size_t, Dim> &shape,
                               const std::array<size_t, Dim> &strides,
                               const Source &source, Target &target) {
    using namespace enoki;
    if constexpr (Index == Dim - 1) {
        using UInt32 = uint32_array_t<Source>;
        UInt32 index = fmadd(arange<UInt32>((uint32_t) shape[Index]),
                             (uint32_t) strides[Index], (uint32_t) offset);
        scatter(target, source, index);
    } else {
        const size_t step = strides[Index];
        for (size_t i = 0; i < shape[Index]; ++i) {
            copy_array_scatter<Index + 1, Dim>(offset, shape, strides,
                                               source.coeff(i), target);
            offset += step;
        }
    }
}

template <typename Array>
py::object enoki_to_torch(const Array &src, bool eval) {
    constexpr size_t Depth = array_depth_v<Array>;
    using Scalar = scalar_t<Array>;

    std::array<size_t, Depth> shape = enoki::shape(src),
                              shape_rev = shape,
                              strides;
    std::reverse(shape_rev.begin(), shape_rev.end());

    py::object torch = py::module::import("torch");
    py::object dtype_obj = torch_dtype<Scalar>();

    py::object result = torch.attr("empty")(
        py::cast(shape_rev),
        "dtype"_a = dtype_obj,
        "device"_a = "cuda");

    size_t size = 1;
    for (size_t i : shape)
        size *= i;

    strides = py::cast<std::array<size_t, Depth>>(result.attr("stride")());
    std::reverse(strides.begin(), strides.end());
    CUDAArray<Scalar> target = CUDAArray<Scalar>::map(
        (Scalar *) py::cast<uintptr_t>(result.attr("data_ptr")()), size);
    copy_array_scatter<0>(0, shape, strides, src, target);
    if (eval)
        cuda_eval();
    return result;
}

template <typename Array> Array torch_to_enoki(py::object src) {
    constexpr size_t Depth = array_depth_v<Array>;
    using Scalar = scalar_t<Array>;
    std::string type_name = py::cast<std::string>(src.get_type().attr("__name__"));
    if (type_name.find("Tensor") == std::string::npos)
        throw py::reference_cast_error();

    py::tuple shape_obj = src.attr("shape");
    py::object dtype_obj = src.attr("dtype");
    py::object target_dtype = torch_dtype<Scalar>();

    if (shape_obj.size() != Depth || !dtype_obj.is(target_dtype))
        throw py::reference_cast_error();

    auto shape = py::cast<std::array<size_t, Depth>>(shape_obj);
    auto strides = py::cast<std::array<size_t, Depth>>(src.attr("stride")());
    std::reverse(shape.begin(), shape.end());
    std::reverse(strides.begin(), strides.end());

    size_t size = 1;
    for (size_t i : shape)
        size *= i;

    CUDAArray<Scalar> source = CUDAArray<Scalar>::map(
        (Scalar *) py::cast<uintptr_t>(src.attr("data_ptr")()), size);

    Array result;
    copy_array_gather<0>(0, shape, strides, source, result);
    return result;
}

template <typename Array>
py::object enoki_to_numpy(const Array &src, bool eval) {
    constexpr size_t Depth = array_depth_v<Array>;
    using Scalar = scalar_t<Array>;

    std::array<size_t, Depth> shape = enoki::shape(src),
                              shape_rev = shape, strides;
    std::reverse(shape_rev.begin(), shape_rev.end());

    size_t size = 1, stride = sizeof(Scalar);
    for (ssize_t i = (ssize_t) Depth - 1; i >= 0; --i) {
        size *= shape_rev[i];
        strides[i] = stride;
        stride *= shape_rev[i];
    }

    CUDAManagedBuffer *buf = new CUDAManagedBuffer(stride);
    py::object buf_py = py::cast(buf, py::return_value_policy::take_ownership);

    py::array result(py::dtype::of<Scalar>(), shape_rev, strides, buf->ptr, buf_py);
    CUDAArray<Scalar> target = CUDAArray<Scalar>::map(buf->ptr, stride);
    for (ssize_t i = (ssize_t) Depth - 1; i >= 0; --i)
        strides[i] /= sizeof(Scalar);
    std::reverse(strides.begin(), strides.end());
    copy_array_scatter<0>(0, shape, strides, src, target);
    if (eval)
        cuda_eval();

    return result;
}