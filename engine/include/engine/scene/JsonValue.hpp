#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <variant>

class JsonValue {
public:
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;

    JsonValue() : data(nullptr) {}
    JsonValue(std::nullptr_t) : data(nullptr) {}
    JsonValue(bool b) : data(b) {}
    JsonValue(double d) : data(d) {}
    JsonValue(int i) : data(static_cast<double>(i)) {}
    JsonValue(const std::string& s) : data(s) {}
    JsonValue(const char* s) : data(std::string(s)) {}
    JsonValue(const Array& a) : data(a) {}
    JsonValue(const Object& o) : data(o) {}

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(data); }
    bool isObject() const { return std::holds_alternative<Object>(data); }
    bool isArray() const { return std::holds_alternative<Array>(data); }
    bool isNumber() const { return std::holds_alternative<double>(data); }
    bool isString() const { return std::holds_alternative<std::string>(data); }

    double asDouble() const { return std::get<double>(data); }
    int asInt() const { return static_cast<int>(std::get<double>(data)); }
    const std::string& asString() const { return std::get<std::string>(data); }
    const Array& asArray() const { return std::get<Array>(data); }
    const Object& asObject() const { return std::get<Object>(data); }

    bool has(const std::string& key) const {
        return isObject() && asObject().count(key) > 0;
    }

    const JsonValue& operator[](const std::string& key) const {
        return asObject().at(key);
    }

    const JsonValue& operator[](size_t index) const {
        return asArray().at(index);
    }

    size_t size() const {
        if (isArray()) return asArray().size();
        if (isObject()) return asObject().size();
        return 0;
    }

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data;
};