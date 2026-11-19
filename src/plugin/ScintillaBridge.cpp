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
#include "ScintillaBridge.h"

#include <algorithm>

namespace bridge {

DocumentView view()
{
    HWND sci = currentScintilla();
    if (!sci) return DocumentView{};

    const auto length = static_cast<uint32_t>(::SendMessage(sci, SCI_GETLENGTH, 0, 0));
    const char* text = reinterpret_cast<const char*>(::SendMessage(sci, SCI_GETCHARACTERPOINTER, 0, 0));
    return DocumentView{text, length};
}

uint32_t caret()
{
    HWND sci = currentScintilla();
    if (!sci) return 0;
    return static_cast<uint32_t>(::SendMessage(sci, SCI_GETCURRENTPOS, 0, 0));
}

void selectRange(uint32_t start, uint32_t end, bool center)
{
    HWND sci = currentScintilla();
    if (!sci) return;
    ::SendMessage(sci, SCI_SETSEL, static_cast<WPARAM>(start), static_cast<LPARAM>(end));

    const auto line = static_cast<long>(::SendMessage(sci, SCI_LINEFROMPOSITION, static_cast<WPARAM>(start), 0));
    if (!center) {
        ::SendMessage(sci, SCI_ENSUREVISIBLE, static_cast<WPARAM>(line), 0);
        return;
    }

    // Put the element's first line in the middle of the viewport, even when it was
    // already on screen: "go to this element" should look the same every time.
    const auto visible = static_cast<long>(::SendMessage(sci, SCI_LINESONSCREEN, 0, 0));
    long first = line - (visible > 1 ? visible / 2 : 0);
    if (first < 0) first = 0;
    ::SendMessage(sci, SCI_SETFIRSTVISIBLELINE, static_cast<WPARAM>(first), 0);
}

void applyEdit(const Edit& edit)
{
    HWND sci = currentScintilla();
    if (!sci) return;

    ::SendMessage(sci, SCI_BEGINUNDOACTION, 0, 0);
    if (edit.removeLen > 0) {
        ::SendMessage(sci, SCI_DELETERANGE, static_cast<WPARAM>(edit.start),
                      static_cast<LPARAM>(edit.removeLen));
    }
    if (!edit.insertText.empty()) {
        // SCI_INSERTTEXT writes *before* the given position. After the deletion the
        // replaced range is gone, so the new text goes at `start`: adding removeLen
        // would push the block forward by its own former length, which is what made
        // a replace look like it had slipped.
        ::SendMessage(sci, SCI_INSERTTEXT, static_cast<WPARAM>(edit.start),
                      reinterpret_cast<LPARAM>(edit.insertText.c_str()));
    }
    ::SendMessage(sci, SCI_ENDUNDOACTION, 0, 0);

    selectRange(edit.start, edit.start + static_cast<uint32_t>(edit.insertText.size()));

    // Ctrl+Z belongs to whoever has the keyboard: hand it back to the editor, or the
    // user has to click in the text before the undo works.
    ::SetFocus(sci);
}

bool selection(uint32_t& start, uint32_t& end)
{
    HWND sci = currentScintilla();
    start = end = 0;
    if (!sci) return false;

    start = static_cast<uint32_t>(::SendMessage(sci, SCI_GETSELECTIONSTART, 0, 0));
    end = static_cast<uint32_t>(::SendMessage(sci, SCI_GETSELECTIONEND, 0, 0));
    return end > start;
}

std::string textRange(uint32_t start, uint32_t end)
{
    HWND sci = currentScintilla();
    if (!sci || end <= start) return std::string();

    std::string out(static_cast<size_t>(end - start) + 1, '\0');
    Sci_TextRangeFull range{};
    range.chrg.cpMin = start;
    range.chrg.cpMax = end;
    range.lpstrText = out.data();
    ::SendMessage(sci, SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&range));
    out.resize(static_cast<size_t>(end - start));
    return out;
}

void replaceRange(uint32_t start, uint32_t end, const std::string& text)
{
    HWND sci = currentScintilla();
    if (!sci) return;

    ::SendMessage(sci, SCI_BEGINUNDOACTION, 0, 0);
    ::SendMessage(sci, SCI_SETSEL, static_cast<WPARAM>(start), static_cast<LPARAM>(end));
    ::SendMessage(sci, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(text.c_str()));
    ::SendMessage(sci, SCI_ENDUNDOACTION, 0, 0);

    selectRange(start, start + static_cast<uint32_t>(text.size()));
    ::SetFocus(sci);
}

void applyEdits(std::vector<Edit> edits)
{
    HWND sci = currentScintilla();
    if (!sci || edits.empty()) return;

    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) {
        return a.start > b.start; // highest offset first
    });

    ::SendMessage(sci, SCI_BEGINUNDOACTION, 0, 0);
    for (const Edit& edit : edits) {
        if (edit.removeLen > 0) {
            ::SendMessage(sci, SCI_DELETERANGE, static_cast<WPARAM>(edit.start),
                          static_cast<LPARAM>(edit.removeLen));
        }
        if (!edit.insertText.empty()) {
            ::SendMessage(sci, SCI_INSERTTEXT, static_cast<WPARAM>(edit.start),
                          reinterpret_cast<LPARAM>(edit.insertText.c_str()));
        }
    }
    ::SendMessage(sci, SCI_ENDUNDOACTION, 0, 0);

    const Edit& first = edits.back();
    selectRange(first.start, first.start + static_cast<uint32_t>(first.insertText.size()));
    ::SetFocus(sci);
}

Theme theme()
{
    Theme t;
    HWND sci = currentScintilla();
    if (!sci) return t;

    const auto styleFore = [&](int style) {
        return static_cast<COLORREF>(::SendMessage(sci, SCI_STYLEGETFORE, style, 0));
    };
    const auto styleBack = [&](int style) {
        return static_cast<COLORREF>(::SendMessage(sci, SCI_STYLEGETBACK, style, 0));
    };

    t.background = styleBack(STYLE_DEFAULT);
    t.text = styleFore(STYLE_DEFAULT);
    t.dark = (::SendMessage(nppData._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0);

    // XML lexer styles: 1 = tag name, 2 = attribute name, 6 = quoted value.
    t.tag = styleFore(1);
    t.attribute = styleFore(2);
    t.value = styleFore(6);

    // A style the theme leaves at the default colour would make the three row kinds
    // indistinguishable; fall back to the text colour rather than painting black on
    // a dark background.
    if (t.tag == t.background) t.tag = t.text;
    if (t.attribute == t.background) t.attribute = t.text;
    if (t.value == t.background) t.value = t.text;
    return t;
}

} // namespace bridge
