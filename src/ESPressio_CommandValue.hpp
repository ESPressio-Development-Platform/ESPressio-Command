#pragma once

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <ESPressio_Memory.hpp>

namespace ESPressio::Command {

using CommandString = System::Memory::String<
    System::Memory::MemoryPolicy::ExternalPreferred
>;

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

    using Storage = std::variant<
        std::monostate,
        CommandString,
        bool,
        int64_t,
        uint64_t,
        double
    >;

    CommandValue() = default;
    CommandValue(std::nullptr_t) : value_(std::monostate{}) {}
    CommandValue(const char* value)
        : value_(CommandString(value == nullptr ? "" : value)) {}
    CommandValue(CommandString value) : value_(std::move(value)) {}
    CommandValue(const std::string& value)
        : value_(CommandString(value.begin(), value.end())) {}
    CommandValue(bool value) : value_(value) {}

    template<typename T, typename std::enable_if_t<
        std::is_integral_v<std::decay_t<T>> &&
        !std::is_same_v<std::decay_t<T>, bool> &&
        std::is_signed_v<std::decay_t<T>>, int> = 0>
    CommandValue(T value) : value_(static_cast<int64_t>(value)) {}

    template<typename T, typename std::enable_if_t<
        std::is_integral_v<std::decay_t<T>> &&
        !std::is_same_v<std::decay_t<T>, bool> &&
        std::is_unsigned_v<std::decay_t<T>>, int> = 0>
    CommandValue(T value) : value_(static_cast<uint64_t>(value)) {}

    template<typename T, typename std::enable_if_t<
        std::is_floating_point_v<std::decay_t<T>>, int> = 0>
    CommandValue(T value) : value_(static_cast<double>(value)) {}

    CommandValue& operator=(std::nullptr_t) {
        value_ = std::monostate{};
        return *this;
    }
    CommandValue& operator=(const char* value) {
        value_ = CommandString(value == nullptr ? "" : value);
        return *this;
    }
    CommandValue& operator=(CommandString value) {
        value_ = std::move(value);
        return *this;
    }
    CommandValue& operator=(const std::string& value) {
        value_ = CommandString(value.begin(), value.end());
        return *this;
    }
    CommandValue& operator=(bool value) {
        value_ = value;
        return *this;
    }

    template<typename T, typename std::enable_if_t<
        std::is_integral_v<std::decay_t<T>> &&
        !std::is_same_v<std::decay_t<T>, bool> &&
        std::is_signed_v<std::decay_t<T>>, int> = 0>
    CommandValue& operator=(T value) {
        value_ = static_cast<int64_t>(value);
        return *this;
    }

    template<typename T, typename std::enable_if_t<
        std::is_integral_v<std::decay_t<T>> &&
        !std::is_same_v<std::decay_t<T>, bool> &&
        std::is_unsigned_v<std::decay_t<T>>, int> = 0>
    CommandValue& operator=(T value) {
        value_ = static_cast<uint64_t>(value);
        return *this;
    }

    template<typename T, typename std::enable_if_t<
        std::is_floating_point_v<std::decay_t<T>>, int> = 0>
    CommandValue& operator=(T value) {
        value_ = static_cast<double>(value);
        return *this;
    }

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

    bool IsNull() const noexcept {
        return std::holds_alternative<std::monostate>(value_);
    }

    const Storage& Value() const noexcept { return value_; }

    const CommandString* TryGetString() const noexcept {
        return std::get_if<CommandString>(&value_);
    }

    CommandString ToString() const {
        if (std::holds_alternative<CommandString>(value_)) {
            return std::get<CommandString>(value_);
        }
        if (std::holds_alternative<bool>(value_)) {
            return std::get<bool>(value_) ? CommandString("true") : CommandString("false");
        }
        if (std::holds_alternative<int64_t>(value_)) {
            return FormatInteger(std::get<int64_t>(value_));
        }
        if (std::holds_alternative<uint64_t>(value_)) {
            return FormatInteger(std::get<uint64_t>(value_));
        }
        if (std::holds_alternative<double>(value_)) {
            char buffer[48]{};
            const int written = std::snprintf(
                buffer,
                sizeof(buffer),
                "%.17g",
                std::get<double>(value_)
            );
            return written > 0
                ? CommandString(buffer, static_cast<std::size_t>(written))
                : CommandString{};
        }
        return CommandString("null");
    }

    template<typename T>
    T As() const {
        using Target = std::decay_t<T>;
        if constexpr (std::is_same_v<Target, CommandValue>) {
            return *this;
        } else if constexpr (std::is_same_v<Target, CommandString>) {
            if (IsNull()) throw std::invalid_argument("Null command value cannot be converted to string");
            return ToString();
        } else if constexpr (std::is_same_v<Target, std::string>) {
            if (IsNull()) throw std::invalid_argument("Null command value cannot be converted to string");
            const auto text = ToString();
            return std::string(text.begin(), text.end());
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
            if (std::holds_alternative<CommandString>(value_)) {
                const CommandString& value = std::get<CommandString>(value_);
                if (EqualsIgnoreCase(value, "true") || value == "1" ||
                    EqualsIgnoreCase(value, "yes") || EqualsIgnoreCase(value, "on") ||
                    EqualsIgnoreCase(value, "high")) return true;
                if (EqualsIgnoreCase(value, "false") || value == "0" ||
                    EqualsIgnoreCase(value, "no") || EqualsIgnoreCase(value, "off") ||
                    EqualsIgnoreCase(value, "low")) return false;
                throw std::invalid_argument("Expected boolean value");
            }
            throw std::invalid_argument("Command value cannot be converted to boolean");
        } else if constexpr (std::is_integral_v<Target>) {
            long double numeric = 0;
            if (std::holds_alternative<int64_t>(value_)) {
                numeric = static_cast<long double>(std::get<int64_t>(value_));
            } else if (std::holds_alternative<uint64_t>(value_)) {
                numeric = static_cast<long double>(std::get<uint64_t>(value_));
            } else if (std::holds_alternative<bool>(value_)) {
                numeric = std::get<bool>(value_) ? 1 : 0;
            } else if (std::holds_alternative<double>(value_)) {
                const double value = std::get<double>(value_);
                if (!std::isfinite(value) || std::floor(value) != value) {
                    throw std::invalid_argument("Floating-point command value is not an integer");
                }
                numeric = static_cast<long double>(value);
            } else if (std::holds_alternative<CommandString>(value_)) {
                const CommandString& value = std::get<CommandString>(value_);
                char* end = nullptr;
                errno = 0;
                if constexpr (std::is_signed_v<Target>) {
                    const long long parsed = std::strtoll(value.c_str(), &end, 0);
                    if (errno == ERANGE) throw std::out_of_range("Integer command value is out of range");
                    if (end != value.c_str() + value.size()) throw std::invalid_argument("Expected integer value");
                    numeric = static_cast<long double>(parsed);
                } else {
                    if (!value.empty() && value.front() == '-') {
                        throw std::out_of_range("Unsigned integer cannot be negative");
                    }
                    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 0);
                    if (errno == ERANGE) throw std::out_of_range("Integer command value is out of range");
                    if (end != value.c_str() + value.size()) throw std::invalid_argument("Expected integer value");
                    numeric = static_cast<long double>(parsed);
                }
            } else {
                throw std::invalid_argument("Null command value cannot be converted to integer");
            }

            const long double minimum = static_cast<long double>(std::numeric_limits<Target>::min());
            const long double maximum = static_cast<long double>(std::numeric_limits<Target>::max());
            if (numeric < minimum || numeric > maximum) {
                throw std::out_of_range("Integer command value is out of range");
            }
            return static_cast<Target>(numeric);
        } else if constexpr (std::is_floating_point_v<Target>) {
            long double numeric = 0;
            if (std::holds_alternative<double>(value_)) {
                numeric = static_cast<long double>(std::get<double>(value_));
            } else if (std::holds_alternative<int64_t>(value_)) {
                numeric = static_cast<long double>(std::get<int64_t>(value_));
            } else if (std::holds_alternative<uint64_t>(value_)) {
                numeric = static_cast<long double>(std::get<uint64_t>(value_));
            } else if (std::holds_alternative<bool>(value_)) {
                numeric = std::get<bool>(value_) ? 1 : 0;
            } else if (std::holds_alternative<CommandString>(value_)) {
                const CommandString& value = std::get<CommandString>(value_);
                char* end = nullptr;
                errno = 0;
                numeric = std::strtold(value.c_str(), &end);
                if (errno == ERANGE) throw std::out_of_range("Floating-point command value is out of range");
                if (end != value.c_str() + value.size()) throw std::invalid_argument("Expected numeric value");
            } else {
                throw std::invalid_argument("Null command value cannot be converted to number");
            }

            if (numeric < -static_cast<long double>(std::numeric_limits<Target>::max()) ||
                numeric > static_cast<long double>(std::numeric_limits<Target>::max())) {
                throw std::out_of_range("Floating-point command value is out of range");
            }
            return static_cast<Target>(numeric);
        } else {
            static_assert(!sizeof(Target), "Unsupported CommandValue::As<T>() type");
        }
    }

private:
    template<typename TInteger>
    static CommandString FormatInteger(TInteger value) {
        char buffer[32]{};
        const int written = std::snprintf(
            buffer,
            sizeof(buffer),
            std::is_signed_v<TInteger> ? "%lld" : "%llu",
            static_cast<unsigned long long>(value)
        );
        return written > 0
            ? CommandString(buffer, static_cast<std::size_t>(written))
            : CommandString{};
    }

    static bool EqualsIgnoreCase(const CommandString& value, const char* expected) noexcept {
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
