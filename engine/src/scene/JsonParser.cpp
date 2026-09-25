#include <engine/scene/JsonParser.hpp>
#include <stdexcept>
#include <cctype>

JsonValue JsonParser::parse(const std::string& text) {
    JsonParser parser(text);
    parser.skipWhitespace();
    JsonValue result = parser.parseValue();
    return result;
}

void JsonParser::skipWhitespace() {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        pos++;
    }
}

char JsonParser::peek() const {
    if (pos >= text.size()) {
        throw std::runtime_error("unexpected end of JSON input");
    }
    return text[pos];
}

char JsonParser::advance() {
    char c = peek();
    pos++;
    return c;
}

void JsonParser::expect(char c) {
    if (advance() != c) {
        throw std::runtime_error(std::string("expected '") + c + "' in JSON input");
    }
}

JsonValue JsonParser::parseValue() {
    skipWhitespace();
    char c = peek();

    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == '"') return parseString();
    if (c == 't' || c == 'f' || c == 'n') return parseLiteral();
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();

    throw std::runtime_error("unexpected character in JSON input");
}

JsonValue JsonParser::parseObject() {
    expect('{');
    JsonValue::Object obj;

    skipWhitespace();
    if (peek() == '}') {
        advance();
        return JsonValue(obj);
    }

    while (true) {
        skipWhitespace();
        JsonValue keyValue = parseString();
        std::string key = keyValue.asString();

        skipWhitespace();
        expect(':');

        JsonValue val = parseValue();
        obj[key] = val;

        skipWhitespace();
        char c = advance();
        if (c == '}') break;
        if (c != ',') throw std::runtime_error("expected ',' or '}' in JSON object");
    }

    return JsonValue(obj);
}

JsonValue JsonParser::parseArray() {
    expect('[');
    JsonValue::Array arr;

    skipWhitespace();
    if (peek() == ']') {
        advance();
        return JsonValue(arr);
    }

    while (true) {
        JsonValue val = parseValue();
        arr.push_back(val);

        skipWhitespace();
        char c = advance();
        if (c == ']') break;
        if (c != ',') throw std::runtime_error("expected ',' or ']' in JSON array");
    }

    return JsonValue(arr);
}

JsonValue JsonParser::parseString() {
    expect('"');
    std::string result;

    while (peek() != '"') {
        char c = advance();
        if (c == '\\') {
            char escaped = advance();
            switch (escaped) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                default: result += escaped; break;
            }
        } else {
            result += c;
        }
    }
    advance(); // consomme le guillemet fermant

    return JsonValue(result);
}

JsonValue JsonParser::parseNumber() {
    size_t start = pos;
    if (peek() == '-') advance();
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) advance();
    if (pos < text.size() && text[pos] == '.') {
        advance();
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) advance();
    }
    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
        advance();
        if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) advance();
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) advance();
    }

    std::string numStr = text.substr(start, pos - start);
    return JsonValue(std::stod(numStr));
}

JsonValue JsonParser::parseLiteral() {
    if (text.compare(pos, 4, "true") == 0) {
        pos += 4;
        return JsonValue(true);
    }
    if (text.compare(pos, 5, "false") == 0) {
        pos += 5;
        return JsonValue(false);
    }
    if (text.compare(pos, 4, "null") == 0) {
        pos += 4;
        return JsonValue(nullptr);
    }
    throw std::runtime_error("invalid literal in JSON input");
}