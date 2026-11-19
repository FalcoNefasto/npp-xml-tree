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
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

// Character references, the way XML and FOXE treat them.
//
// charRefsToChars decodes `&#123;` and `&#x1F600;` into UTF-8. A reference that is
// malformed, out of range (> U+10FFFF), a lone surrogate, or unterminated is copied
// through exactly as it was: a converter that quietly eats bad input is worse than one
// that leaves it alone.
std::string charRefsToChars(std::string_view text, size_t* converted = nullptr);

// charsToCharRefs encodes every non-ASCII code point as `&#xNNNN;` (uppercase hex, no
// leading zeros). ASCII stays literal and invalid UTF-8 bytes are copied through.
std::string charsToCharRefs(std::string_view text, size_t* converted = nullptr);
