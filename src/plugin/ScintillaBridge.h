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
#include "PluginDefinition.h"

#include "document.h"

#include <cstdint>
#include <vector>

// Colours for the panel, taken from the active document's Scintilla styles.
// Reading them from the editor is what makes the tree follow whatever theme the
// user picked (default, dark, Zenburn, ...) instead of imposing its own palette.
struct Theme {
    COLORREF background = RGB(255, 255, 255);
    COLORREF text = RGB(0, 0, 0);
    COLORREF tag = RGB(0, 0, 160);       // element names
    COLORREF attribute = RGB(128, 0, 0); // attribute names
    COLORREF value = RGB(0, 0, 0);       // attribute values / inner text
    bool dark = false;
};

// Every call into Scintilla for the active view. Kept in one place so the panel never
// sends SCI_* itself, and so the plugin/tests boundary stays visible: this is the only
// file that turns offsets into Scintilla calls.
namespace bridge {

DocumentView view();
uint32_t caret();

// Selects [start, end). With `center`, the selection is scrolled to the middle of the
// viewport instead of merely being brought into view.
void selectRange(uint32_t start, uint32_t end, bool center = false);

// Applies one edit as a single undo action, then moves the caret to its end.
void applyEdit(const Edit& edit);

// The editor's selection: [start, end). True when it is not empty.
bool selection(uint32_t& start, uint32_t& end);
std::string textRange(uint32_t start, uint32_t end);

// Replaces [start, end) with `text` as one undo action.
void replaceRange(uint32_t start, uint32_t end, const std::string& text);

// Applies several edits as one undo action. They are applied from the highest offset
// down, so an edit never moves the offsets of one not yet applied.
void applyEdits(std::vector<Edit> edits);

Theme theme();

} // namespace bridge
