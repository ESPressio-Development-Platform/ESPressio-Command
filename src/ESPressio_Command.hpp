#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ESPressio::Command {

struct CommandResult {
    bool success{true};
    int code{0};
    std::string message;
    static CommandResult Ok(std::string message = {}) { return {true, 0, std::move(message)}; }
    static CommandResult Error(std::string message, int code = 1) { return {false, code, std::move(message)}; }
};

struct CommandInvocation {
    std::vector<std::string> path;
    std::vector<std::string> positional;
    std::map<std::string, std::string> named;
    std::string raw;
};

class CommandContext {
public:
    bool Has(const std::string& name) const { return values_.find(name) != values_.end(); }
    const std::string& Raw(const std::string& name) const {
        auto it = values_.find(name);
        if (it == values_.end()) throw std::out_of_range("Unknown command parameter: " + name);
        return it->second;
    }
    const CommandInvocation& Invocation() const { return invocation_; }

    template<typename T> T Get(const std::string& name) const { return Convert<T>(Raw(name)); }

private:
    friend class CommandRegistry;
    std::map<std::string, std::string> values_;
    CommandInvocation invocation_;

    static std::string Lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return value;
    }

    template<typename T> static T Convert(const std::string& value) {
        if constexpr (std::is_same_v<T, std::string>) return value;
        else if constexpr (std::is_same_v<T, bool>) {
            const auto v = Lower(value);
            if (v == "true" || v == "1" || v == "yes" || v == "on" || v == "high") return true;
            if (v == "false" || v == "0" || v == "no" || v == "off" || v == "low") return false;
            throw std::invalid_argument("Expected boolean value, got '" + value + "'");
        } else if constexpr (std::is_integral_v<T>) {
            std::size_t used = 0;
            if constexpr (std::is_signed_v<T>) {
                long long parsed = std::stoll(value, &used, 0);
                if (used != value.size() || parsed < static_cast<long long>(std::numeric_limits<T>::min()) || parsed > static_cast<long long>(std::numeric_limits<T>::max())) throw std::out_of_range("Integer out of range: " + value);
                return static_cast<T>(parsed);
            } else {
                if (!value.empty() && value.front() == '-') throw std::out_of_range("Unsigned integer cannot be negative: " + value);
                unsigned long long parsed = std::stoull(value, &used, 0);
                if (used != value.size() || parsed > static_cast<unsigned long long>(std::numeric_limits<T>::max())) throw std::out_of_range("Integer out of range: " + value);
                return static_cast<T>(parsed);
            }
        } else if constexpr (std::is_floating_point_v<T>) {
            std::size_t used = 0; long double parsed = std::stold(value, &used);
            if (used != value.size()) throw std::invalid_argument("Expected numeric value: " + value);
            return static_cast<T>(parsed);
        } else {
            static_assert(!sizeof(T), "Unsupported CommandContext::Get<T>() type");
        }
    }
};

enum class ParameterKind { String, Boolean, SignedInteger, UnsignedInteger, FloatingPoint, Enumeration };

class CommandParameter {
public:
    CommandParameter(std::string name, ParameterKind kind = ParameterKind::String) : name_(std::move(name)), kind_(kind) {}
    CommandParameter& Description(std::string v) { description_ = std::move(v); return *this; }
    CommandParameter& Required(bool v = true) { required_ = v; return *this; }
    CommandParameter& Optional() { required_ = false; return *this; }
    CommandParameter& Default(std::string v) { default_ = std::move(v); hasDefault_ = true; required_ = false; return *this; }
    CommandParameter& Alias(std::string v) { aliases_.push_back(std::move(v)); return *this; }
    CommandParameter& NamedOnly(bool v = true) { namedOnly_ = v; return *this; }
    CommandParameter& Range(long double min, long double max) { hasRange_ = true; min_ = min; max_ = max; return *this; }
    CommandParameter& OneOf(std::vector<std::string> values) { choices_ = std::move(values); return *this; }
    CommandParameter& Validator(std::function<bool(const std::string&)> fn, std::string message = "Validation failed") { validator_ = std::move(fn); validatorMessage_ = std::move(message); return *this; }

    const std::string& Name() const { return name_; }
    const std::string& DescriptionText() const { return description_; }
    bool IsRequired() const { return required_; }
    bool IsNamedOnly() const { return namedOnly_; }
    bool HasDefault() const { return hasDefault_; }
    const std::string& DefaultValue() const { return default_; }
    ParameterKind Kind() const { return kind_; }
    const std::vector<std::string>& Aliases() const { return aliases_; }
    const std::vector<std::string>& Choices() const { return choices_; }

    bool Matches(const std::string& key) const {
        if (key == name_) return true;
        return std::find(aliases_.begin(), aliases_.end(), key) != aliases_.end();
    }

    std::string Validate(const std::string& value) const {
        try {
            switch (kind_) {
                case ParameterKind::Boolean: (void)CommandContext::Convert<bool>(value); break;
                case ParameterKind::SignedInteger: {
                    auto v = CommandContext::Convert<long long>(value); if (hasRange_ && (v < min_ || v > max_)) return "Value for '" + name_ + "' is outside the allowed range"; break;
                }
                case ParameterKind::UnsignedInteger: {
                    auto v = CommandContext::Convert<unsigned long long>(value); if (hasRange_ && (v < min_ || v > max_)) return "Value for '" + name_ + "' is outside the allowed range"; break;
                }
                case ParameterKind::FloatingPoint: {
                    auto v = CommandContext::Convert<long double>(value); if (hasRange_ && (v < min_ || v > max_)) return "Value for '" + name_ + "' is outside the allowed range"; break;
                }
                default: break;
            }
        } catch (const std::exception& e) { return "Invalid value for '" + name_ + "': " + e.what(); }
        if (!choices_.empty() && std::find(choices_.begin(), choices_.end(), value) == choices_.end()) return "Invalid value for '" + name_ + "'";
        if (validator_ && !validator_(value)) return validatorMessage_;
        return {};
    }

private:
    friend class CommandContext;
    std::string name_, description_, default_, validatorMessage_;
    ParameterKind kind_{ParameterKind::String};
    bool required_{true}, namedOnly_{false}, hasDefault_{false}, hasRange_{false};
    long double min_{0}, max_{0};
    std::vector<std::string> aliases_, choices_;
    std::function<bool(const std::string&)> validator_;
};

class CommandNode {
public:
    using Callback = std::function<CommandResult(const CommandContext&)>;
    explicit CommandNode(std::string name = {}) : name_(std::move(name)) {}
    CommandNode& Description(std::string v) { description_ = std::move(v); return *this; }
    CommandNode& Alias(std::string v) { aliases_.push_back(std::move(v)); return *this; }
    CommandNode& Hidden(bool v = true) { hidden_ = v; return *this; }
    CommandNode& Deprecated(std::string message = {}) { deprecated_ = true; deprecationMessage_ = std::move(message); return *this; }
    CommandNode& OnExecute(Callback cb) { callback_ = std::move(cb); return *this; }
    CommandNode& Before(Callback cb) { before_.push_back(std::move(cb)); return *this; }
    CommandNode& After(Callback cb) { after_.push_back(std::move(cb)); return *this; }

    CommandNode& Command(std::string name) {
        for (auto& child : children_) if (child->Matches(name)) return *child;
        children_.push_back(std::make_unique<CommandNode>(std::move(name)));
        return *children_.back();
    }
    CommandParameter& Parameter(std::string name, ParameterKind kind = ParameterKind::String) { parameters_.emplace_back(std::move(name), kind); return parameters_.back(); }
    template<typename T> CommandParameter& Parameter(std::string name) {
        if constexpr (std::is_same_v<T, bool>) return Parameter(std::move(name), ParameterKind::Boolean);
        else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return Parameter(std::move(name), ParameterKind::SignedInteger);
        else if constexpr (std::is_integral_v<T>) return Parameter(std::move(name), ParameterKind::UnsignedInteger);
        else if constexpr (std::is_floating_point_v<T>) return Parameter(std::move(name), ParameterKind::FloatingPoint);
        else return Parameter(std::move(name), ParameterKind::String);
    }

    bool Matches(const std::string& value) const { return value == name_ || std::find(aliases_.begin(), aliases_.end(), value) != aliases_.end(); }
    const std::string& Name() const { return name_; }
    const std::string& DescriptionText() const { return description_; }
    const std::vector<std::unique_ptr<CommandNode>>& Children() const { return children_; }
    const std::vector<CommandParameter>& Parameters() const { return parameters_; }
    bool IsHidden() const { return hidden_; }
    bool IsDeprecated() const { return deprecated_; }
    const std::string& DeprecationMessage() const { return deprecationMessage_; }

private:
    friend class CommandRegistry;
    std::string name_, description_, deprecationMessage_;
    bool hidden_{false}, deprecated_{false};
    std::vector<std::string> aliases_;
    std::vector<std::unique_ptr<CommandNode>> children_;
    std::vector<CommandParameter> parameters_;
    Callback callback_;
    std::vector<Callback> before_, after_;
};

class TextCommandParser {
public:
    static std::vector<std::string> Tokenize(const std::string& input, std::string* error = nullptr) {
        std::vector<std::string> out; std::string current; char quote = 0; bool escape = false;
        for (char c : input) {
            if (escape) { current.push_back(c); escape = false; continue; }
            if (c == '\\') { escape = true; continue; }
            if (quote) { if (c == quote) quote = 0; else current.push_back(c); continue; }
            if (c == '\'' || c == '"') { quote = c; continue; }
            if (std::isspace(static_cast<unsigned char>(c))) { if (!current.empty()) { out.push_back(current); current.clear(); } continue; }
            current.push_back(c);
        }
        if (escape) current.push_back('\\');
        if (quote) { if (error) *error = "Unterminated quoted string"; return {}; }
        if (!current.empty()) out.push_back(current);
        return out;
    }
};

class CommandRegistry {
public:
    using Middleware = std::function<CommandResult(const CommandInvocation&, const std::function<CommandResult()>&)>;
    CommandRegistry() : root_("") {}
    static CommandRegistry& GetInstance() { static CommandRegistry instance; return instance; }
    CommandNode& Command(std::string name) { return root_.Command(std::move(name)); }
    CommandRegistry& Use(Middleware middleware) { middleware_.push_back(std::move(middleware)); return *this; }

    CommandResult Invoke(const std::string& input) const {
        std::string parseError; auto tokens = TextCommandParser::Tokenize(input, &parseError);
        if (!parseError.empty()) return CommandResult::Error(parseError);
        if (tokens.empty()) return CommandResult::Error("No command supplied");
        if (tokens[0] == "help" || tokens[0] == "?") { tokens.erase(tokens.begin()); return CommandResult::Ok(Help(tokens)); }
        return InvokeTokens(tokens, input);
    }

    CommandResult Invoke(const CommandInvocation& invocation) const {
        std::vector<std::string> tokens = invocation.path;
        for (const auto& p : invocation.positional) tokens.push_back(p);
        for (const auto& p : invocation.named) { tokens.push_back("--" + p.first); tokens.push_back(p.second); }
        return InvokeTokens(tokens, invocation.raw);
    }

    std::vector<std::string> Complete(const std::string& input) const {
        std::string error; auto tokens = TextCommandParser::Tokenize(input, &error); if (!error.empty()) return {};
        const bool endsSpace = !input.empty() && std::isspace(static_cast<unsigned char>(input.back()));
        std::string prefix; if (!endsSpace && !tokens.empty()) { prefix = tokens.back(); tokens.pop_back(); }
        const CommandNode* node = &root_;
        for (const auto& token : tokens) {
            const CommandNode* next = FindChild(*node, token); if (!next) return {}; node = next;
        }
        std::vector<std::string> result;
        for (const auto& child : node->children_) if (!child->hidden_ && child->name_.compare(0, prefix.size(), prefix) == 0) result.push_back(child->name_);
        return result;
    }

    std::string Help(const std::vector<std::string>& path = {}) const {
        const CommandNode* node = &root_; std::string full;
        for (const auto& token : path) { node = FindChild(*node, token); if (!node) return "Unknown command path"; if (!full.empty()) full += ' '; full += node->name_; }
        std::ostringstream os;
        if (!node->name_.empty()) { os << full; if (!node->description_.empty()) os << "\n" << node->description_; os << "\n\nUsage:\n  " << full; if (!node->children_.empty()) os << " <command>"; for (const auto& p : node->parameters_) os << (p.required_ ? " <" : " [") << p.name_ << (p.required_ ? ">" : "]"); os << "\n"; }
        if (!node->children_.empty()) { os << "\nCommands:\n"; for (const auto& c : node->children_) if (!c->hidden_) os << "  " << c->name_ << (c->description_.empty() ? "" : "\t" + c->description_) << "\n"; }
        if (!node->parameters_.empty()) { os << "\nParameters:\n"; for (const auto& p : node->parameters_) { os << "  " << p.name_ << (p.required_ ? " (required)" : " (optional)"); if (!p.description_.empty()) os << "\t" << p.description_; if (p.hasDefault_) os << " [default: " << p.default_ << "]"; os << "\n"; } }
        if (node == &root_) os << "Commands:\n" << HelpChildren(root_);
        return os.str();
    }

private:
    CommandNode root_;
    std::vector<Middleware> middleware_;

    static const CommandNode* FindChild(const CommandNode& node, const std::string& name) { for (const auto& child : node.children_) if (child->Matches(name)) return child.get(); return nullptr; }
    static std::string HelpChildren(const CommandNode& node) { std::ostringstream os; for (const auto& c : node.children_) if (!c->hidden_) os << "  " << c->name_ << (c->description_.empty() ? "" : "\t" + c->description_) << "\n"; return os.str(); }

    CommandResult InvokeTokens(const std::vector<std::string>& tokens, const std::string& raw) const {
        const CommandNode* node = &root_; std::size_t index = 0; CommandInvocation invocation; invocation.raw = raw;
        while (index < tokens.size()) { const auto* child = FindChild(*node, tokens[index]); if (!child) break; node = child; invocation.path.push_back(tokens[index++]); }
        if (node == &root_) return CommandResult::Error("Unknown command '" + tokens.front() + "'.\n" + Suggest(root_, tokens.front()));
        if (!node->children_.empty() && !node->callback_ && index == tokens.size()) return CommandResult::Error("Incomplete command.\n" + Help(invocation.path));

        std::map<std::string, std::string> supplied; std::vector<std::string> positional;
        while (index < tokens.size()) {
            const std::string token = tokens[index++];
            if (token.rfind("--", 0) == 0) {
                auto eq = token.find('='); std::string key = token.substr(2, eq == std::string::npos ? std::string::npos : eq - 2); std::string value;
                if (eq != std::string::npos) value = token.substr(eq + 1); else { if (index >= tokens.size()) return CommandResult::Error("Missing value for --" + key); value = tokens[index++]; }
                supplied[key] = value; invocation.named[key] = value;
            } else positional.push_back(token);
        }
        invocation.positional = positional;
        CommandContext context; context.invocation_ = invocation; std::size_t pos = 0;
        for (const auto& parameter : node->parameters_) {
            std::string value; bool found = false;
            for (const auto& pair : supplied) if (parameter.Matches(pair.first)) { value = pair.second; found = true; break; }
            if (!found && !parameter.namedOnly_ && pos < positional.size()) { value = positional[pos++]; found = true; }
            if (!found && parameter.hasDefault_) { value = parameter.default_; found = true; }
            if (!found && parameter.required_) return CommandResult::Error("Missing required parameter '" + parameter.name_ + "'.\n" + Help(invocation.path));
            if (found) { auto validation = parameter.Validate(value); if (!validation.empty()) return CommandResult::Error(validation); context.values_[parameter.name_] = value; }
        }
        if (pos < positional.size()) return CommandResult::Error("Too many positional parameters");
        for (const auto& pair : supplied) { bool known = false; for (const auto& p : node->parameters_) if (p.Matches(pair.first)) { known = true; break; } if (!known) return CommandResult::Error("Unknown parameter '--" + pair.first + "'"); }
        if (!node->callback_) return CommandResult::Error("Command is not executable.\n" + Help(invocation.path));

        auto execute = [&]() -> CommandResult {
            for (const auto& before : node->before_) { auto r = before(context); if (!r.success) return r; }
            CommandResult result;
            try { result = node->callback_(context); } catch (const std::exception& e) { return CommandResult::Error(e.what()); }
            for (const auto& after : node->after_) { auto r = after(context); if (!r.success && result.success) result = r; }
            if (node->deprecated_) { const std::string warning = "Deprecated command" + (node->deprecationMessage_.empty() ? std::string{} : ": " + node->deprecationMessage_); result.message = warning + (result.message.empty() ? "" : "\n" + result.message); }
            return result;
        };
        std::function<CommandResult(std::size_t)> chain = [&](std::size_t i) -> CommandResult { if (i == middleware_.size()) return execute(); return middleware_[i](invocation, [&]{ return chain(i + 1); }); };
        return chain(0);
    }

    static std::size_t Distance(const std::string& a, const std::string& b) {
        std::vector<std::size_t> prev(b.size()+1), cur(b.size()+1); for (std::size_t j=0;j<=b.size();++j) prev[j]=j;
        for (std::size_t i=1;i<=a.size();++i) { cur[0]=i; for (std::size_t j=1;j<=b.size();++j) cur[j]=std::min({cur[j-1]+1,prev[j]+1,prev[j-1]+(a[i-1]==b[j-1]?0:1)}); prev.swap(cur); } return prev.back();
    }
    static std::string Suggest(const CommandNode& node, const std::string& token) {
        const CommandNode* best = nullptr; std::size_t score = std::numeric_limits<std::size_t>::max();
        for (const auto& c : node.children_) { auto d = Distance(c->name_, token); if (d < score) { score=d; best=c.get(); } }
        return best && score <= std::max<std::size_t>(2, token.size()/2) ? "Did you mean '" + best->name_ + "'?" : "";
    }
};

} // namespace ESPressio::Command
