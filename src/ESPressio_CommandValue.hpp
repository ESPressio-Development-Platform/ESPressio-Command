#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace ESPressio::Command {

class CommandValue {
public:
    enum class Type {
        Null,
        String,
        Boolean,
        SignedInteger,
        UnsignedInteger,
        FloatingPoint
    };

    using Storage = std::variant<std::monostate, std::string, bool, int64_t, uint64_t, double>;

    CommandValue() = default;
    CommandValue(std::nullptr_t) : value_(std::monostate{}) {}
    CommandValue(const char* value) : value_(std::string(value == nullptr ? "" : value)) {}
    CommandValue(std::string value) : value_(std::move(value)) {}
    CommandValue(bool value) : value_(value) {}

    template<typename T, typename std::enable_if_t<std::is_integral_v<std::decay_t<T>> && !std::is_same_v<std::decay_t<T>, bool> && std::is_signed_v<std::decay_t<T>>, int> = 0>
    CommandValue(T value) : value_(static_cast<int64_t>(value)) {}

    template<typename T, typename std::enable_if_t<std::is_integral_v<std::decay_t<T>> && !std::is_same_v<std::decay_t<T>, bool> && std::is_unsigned_v<std::decay_t<T>>, int> = 0>
    CommandValue(T value) : value_(static_cast<uint64_t>(value)) {}

    template<typename T, typename std::enable_if_t<std::is_floating_point_v<std::decay_t<T>>, int> = 0>
    CommandValue(T value) : value_(static_cast<double>(value)) {}

    CommandValue& operator=(std::nullptr_t) { value_ = std::monostate{}; return *this; }
    CommandValue& operator=(const char* value) { value_ = std::string(value == nullptr ? "" : value); return *this; }
    CommandValue& operator=(std::string value) { value_ = std::move(value); return *this; }
    CommandValue& operator=(bool value) { value_ = value; return *this; }

    template<typename T, typename std::enable_if_t<std::is_integral_v<std::decay_t<T>> && !std::is_same_v<std::decay_t<T>, bool> && std::is_signed_v<std::decay_t<T>>, int> = 0>
    CommandValue& operator=(T value) { value_ = static_cast<int64_t>(value); return *this; }

    template<typename T, typename std::enable_if_t<std::is_integral_v<std::decay_t<T>> && !std::is_same_v<std::decay_t<T>, bool> && std::is_unsigned_v<std::decay_t<T>>, int> = 0>
    CommandValue& operator=(T value) { value_ = static_cast<uint64_t>(value); return *this; }

    template<typename T, typename std::enable_if_t<std::is_floating_point_v<std::decay_t<T>>, int> = 0>
    CommandValue& operator=(T value) { value_ = static_cast<double>(value); return *this; }

    Type GetType() const noexcept {
        switch (value_.index()) {
            case 0: return Type::Null;
            case 1: return Type::String;
            case 2: return Type::Boolean;
            case 3: return Type::SignedInteger;
            case 4: return Type::UnsignedInteger;
            default: return Type::FloatingPoint;
        }
    }

    bool IsNull() const noexcept { return std::holds_alternative<std::monostate>(value_); }
    const Storage& Value() const noexcept { return value_; }
    const std::string* TryGetString() const noexcept {
        return std::get_if<std::string>(&value_);
    }

    std::string ToString() const {
        if (std::holds_alternative<std::string>(value_)) return std::get<std::string>(value_);
        if (std::holds_alternative<bool>(value_)) return std::get<bool>(value_) ? "true" : "false";
        if (std::holds_alternative<int64_t>(value_)) return std::to_string(std::get<int64_t>(value_));
        if (std::holds_alternative<uint64_t>(value_)) return std::to_string(std::get<uint64_t>(value_));
        if (std::holds_alternative<double>(value_)) {
            std::ostringstream stream;
            stream << std::setprecision(std::numeric_limits<double>::max_digits10) << std::get<double>(value_);
            return stream.str();
        }
        return "null";
    }

    template<typename T>
    T As() const {
        using Target = std::decay_t<T>;
        if constexpr (std::is_same_v<Target, CommandValue>) {
            return *this;
        } else if constexpr (std::is_same_v<Target, std::string>) {
            if (IsNull()) throw std::invalid_argument("Null command value cannot be converted to string");
            return ToString();
        } else if constexpr (std::is_same_v<Target, bool>) {
            if (std::holds_alternative<bool>(value_)) return std::get<bool>(value_);
            if (std::holds_alternative<int64_t>(value_)) {
                const auto value = std::get<int64_t>(value_);
                if (value == 0 || value == 1) return value == 1;
                throw std::invalid_argument("Expected boolean-compatible integer 0 or 1");
            }
            if (std::holds_alternative<uint64_t>(value_)) {
                const auto value = std::get<uint64_t>(value_);
                if (value == 0 || value == 1) return value == 1;
                throw std::invalid_argument("Expected boolean-compatible integer 0 or 1");
            }
            if (std::holds_alternative<std::string>(value_)) {
                const std::string& value = std::get<std::string>(value_);
                if (EqualsIgnoreCase(value, "true") || value == "1" ||
                    EqualsIgnoreCase(value, "yes") || EqualsIgnoreCase(value, "on") ||
                    EqualsIgnoreCase(value, "high")) return true;
                if (EqualsIgnoreCase(value, "false") || value == "0" ||
                    EqualsIgnoreCase(value, "no") || EqualsIgnoreCase(value, "off") ||
                    EqualsIgnoreCase(value, "low")) return false;
                throw std::invalid_argument("Expected boolean value, got '" + value + "'");
            }
            throw std::invalid_argument("Command value cannot be converted to boolean");
        } else if constexpr (std::is_integral_v<Target>) {
            long double numeric = 0;
            if (std::holds_alternative<int64_t>(value_)) numeric = static_cast<long double>(std::get<int64_t>(value_));
            else if (std::holds_alternative<uint64_t>(value_)) numeric = static_cast<long double>(std::get<uint64_t>(value_));
            else if (std::holds_alternative<bool>(value_)) numeric = std::get<bool>(value_) ? 1 : 0;
            else if (std::holds_alternative<double>(value_)) {
                const double value = std::get<double>(value_);
                if (!std::isfinite(value) || std::floor(value) != value) throw std::invalid_argument("Floating-point command value is not an integer");
                numeric = static_cast<long double>(value);
            } else if (std::holds_alternative<std::string>(value_)) {
                const std::string& value = std::get<std::string>(value_);
                std::size_t used = 0;
                if constexpr (std::is_signed_v<Target>) {
                    const long long parsed = std::stoll(value, &used, 0);
                    if (used != value.size()) throw std::invalid_argument("Expected integer value: " + value);
                    numeric = static_cast<long double>(parsed);
                } else {
                    if (!value.empty() && value.front() == '-') throw std::out_of_range("Unsigned integer cannot be negative: " + value);
                    const unsigned long long parsed = std::stoull(value, &used, 0);
                    if (used != value.size()) throw std::invalid_argument("Expected integer value: " + value);
                    numeric = static_cast<long double>(parsed);
                }
            } else throw std::invalid_argument("Null command value cannot be converted to integer");

            const long double minimum = static_cast<long double>(std::numeric_limits<Target>::min());
            const long double maximum = static_cast<long double>(std::numeric_limits<Target>::max());
            if (numeric < minimum || numeric > maximum) throw std::out_of_range("Integer command value is out of range");
            return static_cast<Target>(numeric);
        } else if constexpr (std::is_floating_point_v<Target>) {
            long double numeric = 0;
            if (std::holds_alternative<double>(value_)) numeric = static_cast<long double>(std::get<double>(value_));
            else if (std::holds_alternative<int64_t>(value_)) numeric = static_cast<long double>(std::get<int64_t>(value_));
            else if (std::holds_alternative<uint64_t>(value_)) numeric = static_cast<long double>(std::get<uint64_t>(value_));
            else if (std::holds_alternative<bool>(value_)) numeric = std::get<bool>(value_) ? 1 : 0;
            else if (std::holds_alternative<std::string>(value_)) {
                const std::string& value = std::get<std::string>(value_);
                std::size_t used = 0;
                numeric = std::stold(value, &used);
                if (used != value.size()) throw std::invalid_argument("Expected numeric value: " + value);
            } else throw std::invalid_argument("Null command value cannot be converted to number");

            if (numeric < -static_cast<long double>(std::numeric_limits<Target>::max()) || numeric > static_cast<long double>(std::numeric_limits<Target>::max())) throw std::out_of_range("Floating-point command value is out of range");
            return static_cast<Target>(numeric);
        } else {
            static_assert(!sizeof(Target), "Unsupported CommandValue::As<T>() type");
        }
    }

private:
    static bool EqualsIgnoreCase(const std::string& value, const char* expected) noexcept {
        if (expected == nullptr) return false;
        std::size_t index = 0;
        for (; index < value.size() && expected[index] != '\0'; ++index) {
            const auto left = static_cast<unsigned char>(value[index]);
            const auto right = static_cast<unsigned char>(expected[index]);
            if (std::tolower(left) != std::tolower(right)) return false;
        }
        return index == value.size() && expected[index] == '\0';
    }

    Storage value_;
};

} // namespace ESPressio::Command
