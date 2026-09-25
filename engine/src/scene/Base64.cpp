#include <engine/scene/Base64.hpp>

namespace {
    const char* base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string Base64::encode(const std::vector<uint8_t>& data) {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < data.size()) {
        uint32_t chunk = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        result += base64Chars[(chunk >> 18) & 0x3F];
        result += base64Chars[(chunk >> 12) & 0x3F];
        result += base64Chars[(chunk >> 6) & 0x3F];
        result += base64Chars[chunk & 0x3F];
        i += 3;
    }

    size_t remaining = data.size() - i;
    if (remaining == 1) {
        uint32_t chunk = data[i] << 16;
        result += base64Chars[(chunk >> 18) & 0x3F];
        result += base64Chars[(chunk >> 12) & 0x3F];
        result += "==";
    } else if (remaining == 2) {
        uint32_t chunk = (data[i] << 16) | (data[i + 1] << 8);
        result += base64Chars[(chunk >> 18) & 0x3F];
        result += base64Chars[(chunk >> 12) & 0x3F];
        result += base64Chars[(chunk >> 6) & 0x3F];
        result += "=";
    }

    return result;
}

namespace {
    int base64CharValue(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }
}

std::vector<uint8_t> Base64::decode(const std::string& encoded) {
    std::vector<uint8_t> result;
    result.reserve((encoded.size() / 4) * 3);

    int val = 0, bits = -8;
    for (char c : encoded) {
        if (c == '=') break;
        int digit = base64CharValue(c);
        if (digit == -1) continue;

        val = (val << 6) + digit;
        bits += 6;

        if (bits >= 0) {
            result.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }

    return result;
}