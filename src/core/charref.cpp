// XML Tree -- a dockable XML tree panel for Notepad++.
// Copyright (C) 2026 FalcoNefasto
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
#include "charref.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint32_t kMaxCodePoint = 0x10FFFF;

bool isSurrogate(uint32_t cp) { return cp >= 0xD800 && cp <= 0xDFFF; }

void appendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// One UTF-8 sequence at `i`: returns the code point and how many bytes it took, or
// 0 for an invalid sequence (the caller copies the byte through).
uint32_t decodeUtf8(std::string_view text, size_t i, size_t& length)
{
    const auto byte = [&](size_t k) { return static_cast<unsigned char>(text[k]); };
    const unsigned char b0 = byte(i);

    if (b0 < 0x80) {
        length = 1;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && i + 1 < text.size()) {
        length = 2;
        return ((b0 & 0x1Fu) << 6) | (byte(i + 1) & 0x3Fu);
    }
    if ((b0 & 0xF0) == 0xE0 && i + 2 < text.size()) {
        length = 3;
        return ((b0 & 0x0Fu) << 12) | ((byte(i + 1) & 0x3Fu) << 6) | (byte(i + 2) & 0x3Fu);
    }
    if ((b0 & 0xF8) == 0xF0 && i + 3 < text.size()) {
        length = 4;
        return ((b0 & 0x07u) << 18) | ((byte(i + 1) & 0x3Fu) << 12) |
               ((byte(i + 2) & 0x3Fu) << 6) | (byte(i + 3) & 0x3Fu);
    }
    length = 0;
    return 0;
}

bool digitValue(char c, bool hex, uint32_t& value)
{
    if (c >= '0' && c <= '9') {
        value = static_cast<uint32_t>(c - '0');
        return true;
    }
    if (!hex) return false;
    if (c >= 'a' && c <= 'f') {
        value = static_cast<uint32_t>(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        value = static_cast<uint32_t>(c - 'A' + 10);
        return true;
    }
    return false;
}

} // namespace

std::string charRefsToChars(std::string_view text, size_t* converted)
{
    std::string out;
    out.reserve(text.size());
    size_t done = 0;

    size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '&' || i + 2 >= text.size() || text[i + 1] != '#') {
            out += text[i++];
            continue;
        }

        size_t j = i + 2;
        bool hex = false;
        if (text[j] == 'x' || text[j] == 'X') {
            hex = true;
            ++j;
        }

        uint32_t value = 0;
        size_t digits = 0;
        uint32_t digit = 0;
        while (j < text.size() && digitValue(text[j], hex, digit)) {
            if (value > kMaxCodePoint) break; // keeps the shift from overflowing
            value = value * (hex ? 16u : 10u) + digit;
            ++j;
            ++digits;
        }

        const bool valid = digits > 0 && j < text.size() && text[j] == ';' && value > 0 &&
                           value <= kMaxCodePoint && !isSurrogate(value);
        if (!valid) {
            out += text[i++]; // not a usable reference: leave it alone
            continue;
        }

        appendUtf8(out, value);
        ++done;
        i = j + 1;
    }

    if (converted) *converted = done;
    return out;
}

std::string charsToCharRefs(std::string_view text, size_t* converted)
{
    std::string out;
    out.reserve(text.size());
    size_t done = 0;

    size_t i = 0;
    while (i < text.size()) {
        size_t length = 0;
        const uint32_t cp = decodeUtf8(text, i, length);
        if (length == 0) {
            out += text[i++];
            continue;
        }
        if (cp < 0x80) {
            out += text[i];
        } else {
            char ref[16];
            std::snprintf(ref, sizeof ref, "&#x%X;", cp);
            out += ref;
            ++done;
        }
        i += length;
    }

    if (converted) *converted = done;
    return out;
}
