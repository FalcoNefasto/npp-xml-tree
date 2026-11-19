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
#include "document.h"

// Decides whether the panel should parse this buffer. An XML-looking extension wins
// without reading a byte; otherwise the first non-blank bytes must be an XML prolog.
// `path` may be null (unsaved buffer). Pure: no Win32, testable in the console.
bool looksLikeXml(DocumentView doc, const wchar_t* path);
