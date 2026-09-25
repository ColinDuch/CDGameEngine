#include <engine/scene/JsonWriter.hpp>

#include <cmath>

void JsonWriter::newlineIndent() {
    oss << "\n";
    for (size_t i = 0; i < needsComma.size(); ++i) oss << "  ";
}

void JsonWriter::beforeValue() {
    if (afterKey) {                       // juste apres "cle": on reste sur la ligne
        afterKey = false;
        if (!needsComma.empty()) needsComma.back() = true;
        return;
    }
    if (needsComma.empty()) return;

    if (needsComma.back()) oss << ",";
    bool ml = multiline.back();
    needsComma.back() = true;
    if (ml) newlineIndent();
    else    oss << " ";               // tableau compact : [ 1, 2, 3 ]
}

void JsonWriter::beginContainer(char open) {
    if (afterKey) {
        afterKey = false;
        if (!needsComma.empty()) needsComma.back() = true;
    } else if (!needsComma.empty()) {
        if (needsComma.back()) oss << ",";
        needsComma.back() = true;
        multiline.back() = true;      // un conteneur imbrique force le multiligne
        newlineIndent();
    }
    oss << open;
    needsComma.push_back(false);
    multiline.push_back(open == '{');  // objet = multiligne, tableau = compact au depart
}

void JsonWriter::endContainer(char close) {
    bool hadContent = needsComma.back();
    bool ml = multiline.back();
    needsComma.pop_back();
    multiline.pop_back();
    if (hadContent) {
        if (ml) newlineIndent();
        else    oss << " ";
    }
    oss << close;
}

void JsonWriter::beginObject() { beginContainer('{'); }
void JsonWriter::endObject()   { endContainer('}');  }
void JsonWriter::beginArray()  { beginContainer('['); }
void JsonWriter::endArray()    { endContainer(']');  }

void JsonWriter::key(const std::string& name) {
    beforeValue();
    needsComma.back() = false;
    oss << "\"" << name << "\": ";
    afterKey = true;
}

void JsonWriter::value(const std::string& str) {
    beforeValue();
    oss << "\"" << str << "\"";
}

void JsonWriter::value(double number) {
    beforeValue();
    if (!std::isfinite(number)) oss << 0;
    else oss << number;
}

void JsonWriter::value(int number) {
    beforeValue();
    oss << number;
}

void JsonWriter::rawValue(const std::string& raw) {
    beforeValue();
    oss << raw;
}