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
#include "index.h"
#include "ScintillaBridge.h"
#include "node.h"

#include <commctrl.h>

#include <cstdint>
#include <functional>

// The tree is virtual: a TreeView item holds nothing but the document offset of the
// node it stands for and its kind. Text, icon and child count are answered on demand
// in TVN_GETDISPINFO, and children are materialized only when a branch is expanded.
class TreeView {
public:
    bool create(HWND parent, int id);
    HWND handle() const { return tree_; }

    void setSource(DocumentView doc, const ElementIndex* idx);
    void setTheme(const Theme& t);
    void destroy();
    void clear();
    void rebuild();

    // Offset and kind of the focused row; offset == kNone when nothing is selected.
    uint32_t selectedOffset() const;
    NodeKind selectedKind() const;

    // Selects the row of the element starting at `offset`, expanding its ancestors.
    bool selectElement(uint32_t offset);

    // Row under a screen point, or nullptr.
    HTREEITEM itemFromPoint(POINT screen) const;
    void selectItem(HTREEITEM item);
    HTREEITEM itemFromOffset(uint32_t offset) const;
    LPARAM paramOfItem(HTREEITEM item) const { return paramOf(item); }

    // Returns true when the notification was consumed.
    bool handleNotify(NMHDR* hdr, LRESULT* result);

    // Del on a selected row: the same command the context menu offers.
    std::function<void()> onDeleteKey;
    std::function<void(bool /*removeLine*/)> onRemoveKey;
    std::function<void(bool /*cut*/)> onCopyKey;
    // Enter on a selected row: same as the double click.
    std::function<void()> onActivate;
    std::function<void()> onRenameKey;   // F2

    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    // Item params are (kind << 56) | offset. Offset 0 is a legitimate value (a file
    // that starts with '<root>'), so "no node here" needs its own bit rather than 0.
    static constexpr LPARAM kHintBit = 1LL << 40;

    static uint32_t offsetOf(LPARAM param)
    {
        return (param & kHintBit) ? kNone : static_cast<uint32_t>(param & 0x00FFFFFFFFull);
    }
    static bool isHint(LPARAM param) { return (param & kHintBit) != 0; }
    static NodeKind kindOf(LPARAM param) { return static_cast<NodeKind>(param >> 56); }
    static LPARAM packNode(uint32_t offset, NodeKind kind)
    {
        return static_cast<LPARAM>(offset) | (static_cast<LPARAM>(kind) << 56);
    }

private:
    struct AddContext {
        HTREEITEM parent = nullptr;
        uint32_t parentStart = 0;
        uint16_t depth = 0;
        int added = 0;
        bool truncated = false;
    };

    void addChildren(HTREEITEM parent, uint32_t parentStart, uint32_t parentEnd, uint16_t depth);
    HTREEITEM insert(LPARAM param, HTREEITEM parent, HTREEITEM after, bool hasChildren);
    bool elementHasChildren(const ElementPos& e) const;
    bool hasElementChild(const ElementPos& e) const;
    void fillDispInfo(NMTVDISPINFO* info) const;
    void drawItem(NMTVCUSTOMDRAW* cd) const;
    HTREEITEM findItemForOffset(HTREEITEM parent, uint32_t offset, uint32_t rangeEnd);
    LPARAM paramOf(HTREEITEM item) const;
    static LRESULT CALLBACK subclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    Theme theme_{};
    HWND tree_ = nullptr;
    HIMAGELIST icons_ = nullptr;
    HFONT bold_ = nullptr;
    DocumentView doc_{};
    const ElementIndex* index_ = nullptr;
};
