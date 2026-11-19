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
// Core document primitives. No Win32, no NPP: this header is used by the
// console tests as well as by the plugin.
#include <cstdint>
#include <string>

// A read-only window on the document bytes. Offsets everywhere in this codebase
// are byte offsets into this buffer, which are the same byte offsets Scintilla
// uses (SCI_SETSEL, SCI_DELETERANGE), so no translation layer exists.
struct DocumentView {
    const char* data = nullptr;
    uint32_t length = 0;
};

// One document mutation: bytes [start, start + removeLen) are replaced by insertText.
struct Edit {
    uint32_t start = 0;
    uint32_t removeLen = 0;
    std::string insertText;
};
