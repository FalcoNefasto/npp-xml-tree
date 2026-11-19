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
#include <windows.h>

#include <string>

// Colours for the result dialog, taken from the same place the tree takes them:
// the active document's Scintilla styles.
struct ResultTheme {
    COLORREF background = RGB(255, 255, 255);
    COLORREF text = RGB(0, 0, 0);
    bool dark = false;
};

// A small modal result window: a caption, a read-only multiline body and two buttons
// (Copy, Close). Built with plain Win32 calls like the rest of the plugin -- no dialog
// resource, no MFC. Painted with the given theme so it does not glare inside a dark
// Notepad++. Blocks until the user closes it.
void showResultDialog(HWND owner, const wchar_t* title, const wchar_t* caption,
                      const std::wstring& body, const ResultTheme& theme);
