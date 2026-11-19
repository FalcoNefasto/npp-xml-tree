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
#include "encoding.h"

namespace {
inline unsigned char byte(const char* p) { return static_cast<unsigned char>(*p); }

// A UTF-16 document with no BOM is still UTF-16: half of its bytes are zeros, all on the
// same side of each pair. Notepad++ normally decodes such a file for us, so this only
// catches the case where it did not -- and it must say so, because scanning those bytes
// as UTF-8 produces a tree of garbage.
Encoding sniffUtf16(const char* data, uint32_t length)
{
    const uint32_t sample = length < 4096 ? length : 4096;
    if (sample < 4) return Encoding::Utf8;

    uint32_t zeros = 0;
    uint32_t zerosEven = 0;
    for (uint32_t i = 0; i < sample; ++i) {
        if (data[i] == 0) {
            ++zeros;
            if ((i % 2) == 0) ++zerosEven;
        }
    }

    if (zeros * 5 < sample) return Encoding::Utf8; // fewer than 20% zeros: not UTF-16
    const uint32_t zerosOdd = zeros - zerosEven;
    if (zerosEven * 4 >= zeros * 3) return Encoding::Utf16Be;
    if (zerosOdd * 4 >= zeros * 3) return Encoding::Utf16Le;
    return Encoding::Utf8;
}
} // namespace

Encoding detectEncoding(const char* data, uint32_t length)
{
    if (data == nullptr || length < 2) return Encoding::Utf8;

    if (length >= 3 && byte(data) == 0xEF && byte(data + 1) == 0xBB && byte(data + 2) == 0xBF)
        return Encoding::Utf8Bom;

    if (byte(data) == 0xFF && byte(data + 1) == 0xFE) return Encoding::Utf16Le;
    if (byte(data) == 0xFE && byte(data + 1) == 0xFF) return Encoding::Utf16Be;

    return sniffUtf16(data, length);
}

uint32_t bomLength(Encoding e)
{
    switch (e) {
    case Encoding::Utf8Bom: return 3;
    case Encoding::Utf16Le:
    case Encoding::Utf16Be: return 2;
    case Encoding::Utf8: break;
    }
    return 0;
}
