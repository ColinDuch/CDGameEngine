#pragma once

#include <sstream>
#include <string>
#include <vector>

class JsonWriter {
public:
    JsonWriter() { oss.precision(9); }   // evite de perdre des decimales

    void beginObject();
    void endObject();
    void beginArray();
    void endArray();
    void key(const std::string& name);
    void value(const std::string& str);
    void value(double number);
    void value(int number);
    void rawValue(const std::string& raw);

    std::string toString() const { return oss.str(); }

private:
    void beforeValue();
    void newlineIndent();
    void beginContainer(char open);
    void endContainer(char close);

    std::ostringstream oss;
    std::vector<bool> needsComma;
    std::vector<bool> multiline;   // ce conteneur met ses enfants sur des lignes separees
    bool afterKey = false;         // la valeur suit une cle : elle reste sur la meme ligne
};