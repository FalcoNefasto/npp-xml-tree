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
#include <cstdint>

// Only UTF-8 is parsed. The BOM is reported so the scanner can skip it; UTF-16
// is detected so the panel can say "not supported" instead of showing a wrong tree.
enum class Encoding { Utf8, Utf8Bom, Utf16Le, Utf16Be };

Encoding detectEncoding(const char* data, uint32_t length);
uint32_t bomLength(Encoding e);
