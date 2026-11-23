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
#include "Notepad_plus_msgs.h"
#include "PluginInterface.h"
#include "Scintilla.h"

#include <windows.h>

#define PLUGIN_MODULE_NAME L"XML Tree"
// Docking.h requires the plugin's DLL file name here, not the display name.
#define PLUGIN_DLL_NAME L"NppXmlTree.dll"
#define PLUGIN_VERSION L"1.1.0"
#define PLUGIN_AUTHOR L"FalcoNefasto"

extern NppData nppData;

// The DLL's own HINSTANCE. Inside a DLL, GetModuleHandle(nullptr) returns the host
// executable -- which is how a dialog resource id ended up resolving against
// notepad++.exe and opening its Go To dialog.
HINSTANCE pluginInstance();

// Scintilla handle of the active view and its buffer, resolved on demand.
HWND currentScintilla();
UINT_PTR currentBufferId();
void currentFullPath(wchar_t* out, int outChars);
