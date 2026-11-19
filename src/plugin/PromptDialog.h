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

#include "ResultDialog.h"

// A one-line text prompt, same look as the result dialog: caption, edit, OK, Cancel.
// `value` is the initial text and receives what the user typed. Returns false when the
// user cancelled.
bool showPromptDialog(HWND owner, const wchar_t* title, const wchar_t* caption, std::wstring& value,
                      const ResultTheme& theme);
