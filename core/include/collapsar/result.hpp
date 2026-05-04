#pragma once

#include <cassert>
#include <type_traits>
#include <utility>
#include <variant>

#include "collapsar/error.hpp"

namespace collapsar {

// Tiny tagged-union of T or Error. We don't depend on std::expected so the
// header compiles cleanly under the various MSVC / libstdc++ feature levels
// the project targets. Move-only types are supported.
template <typename T>
class Result {
    static_assert(!std::is_same_v<T, Error>, "Use Result<void> instead");

public:
    using value_type = T;

    Result(T value) : data_{std::in_place_index<0>, std::move(value)} {}
    Result(Error err) : data_{std::in_place_index<1>, std::move(err)} {}

    [[nodiscard]] bool has_value() const noexcept { return data_.index() == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & {
        assert(has_value());
        return std::get<0>(data_);
    }
    [[nodiscard]] const T& value() const& {
        assert(has_value());
        return std::get<0>(data_);
    }
    [[nodiscard]] T&& value() && {
        assert(has_value());
        return std::get<0>(std::move(data_));
    }

    [[nodiscard]] const Error& error() const& {
        assert(!has_value());
        return std::get<1>(data_);
    }
    [[nodiscard]] Error error() && {
        assert(!has_value());
        return std::get<1>(std::move(data_));
    }

private:
    std::variant<T, Error> data_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error err) : err_{std::move(err)} {}

    [[nodiscard]] bool has_value() const noexcept { return err_.ok(); }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
    [[nodiscard]] const Error& error() const& noexcept { return err_; }
    [[nodiscard]] Error error() && noexcept { return std::move(err_); }

private:
    Error err_{};
};

} // namespace collapsar
