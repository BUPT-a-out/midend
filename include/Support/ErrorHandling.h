#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

namespace midend {

class CompilerError : public std::runtime_error {
   public:
    explicit CompilerError(const std::string& msg) : std::runtime_error(msg) {}
};

template <typename T, typename E = CompilerError>
class Result {
   private:
    std::variant<T, E> data_;

   public:
    Result(T value) : data_(std::move(value)) {}
    Result(E error) : data_(std::move(error)) {}

    bool isOk() const { return std::holds_alternative<T>(data_); }
    bool isError() const { return std::holds_alternative<E>(data_); }

    T& value() { return std::get<T>(data_); }
    const T& value() const { return std::get<T>(data_); }

    E& error() { return std::get<E>(data_); }
    const E& error() const { return std::get<E>(data_); }

    T valueOr(T defaultValue) const {
        return isOk() ? value() : std::move(defaultValue);
    }
};

inline void report_fatal_error(const std::string& msg) {
    throw CompilerError(msg);
}

}  // namespace midend