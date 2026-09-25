#pragma once

#include <engine/scene/JsonValue.hpp>
#include <string>

class JsonParser {
public:
    static JsonValue parse(const std::string& text);

private:
    JsonParser(const std::string& text) : text(text), pos(0) {}

    JsonValue parseValue();
    JsonValue parseObject();
    JsonValue parseArray();
    JsonValue parseString();
    JsonValue parseNumber();
    JsonValue parseLiteral();

    void skipWhitespace();
    char peek() const;
    char advance();
    void expect(char c);

    const std::string& text;
    size_t pos;
};