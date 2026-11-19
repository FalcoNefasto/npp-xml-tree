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

#include <functional>
#include <string>
#include <vector>

// A modeless dialog in the spirit of Find & Replace: it stays next to the editor and
// lists what a strict parser would refuse, one line per thing to look at. It only
// reports: the repair is made by hand in the text, so there is no Fix button.
//
// It is a real dialog built from a dialog template, so Notepad++ themes it the way it
// themes its own dialogs and it looks native in both light and dark mode. The window is
// resizable and the list scrolls both ways: a long message must be readable in full.
class ProblemsDialog {
public:
    bool create(HWND nppOwner);
    void show();
    void hide();
    bool visible() const;
    HWND handle() const { return hwnd_; }

    // Replaces the list. `navigable` turns Previous/Next on.
    void setProblems(const std::vector<std::wstring>& items, bool navigable);
    void select(int index);

    // Reported as a list index; -1 when nothing is selected.
    std::function<void(int)> onSelect;
    std::function<void()> onPrevious;
    std::function<void()> onNext;
    std::function<void()> onClose;

private:
    static INT_PTR CALLBACK dlgProc(HWND, UINT, WPARAM, LPARAM);

    void layout() const;
    // Wide enough for the longest row, so the horizontal scrollbar has somewhere to go.
    void updateHorizontalExtent() const;

    HWND hwnd_ = nullptr;
    HWND list_ = nullptr;
    HWND previous_ = nullptr;
    HWND next_ = nullptr;
    HWND close_ = nullptr;
    std::vector<std::wstring> items_;
};
