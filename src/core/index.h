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
// Flat element index: one 16-byte entry per element, in document order. This is
// the whole model of the document; there is no tree of objects.
#include <cstddef>
#include <cstdint>
#include <vector>

struct ElementPos {
    uint32_t start = 0;   // offset of '<'
    uint32_t end = 0;     // one past "</tag>" or "/>"; == start while the element is unclosed
    uint32_t nameOff = 0; // offset of the name inside the opening tag
    uint16_t nameLen = 0;
    uint16_t depth = 0;
};

// One million elements are 16 MB: this size is part of the design, not an accident.
static_assert(sizeof(ElementPos) == 16, "ElementPos must stay 16 bytes");

class ElementIndex {
public:
    void clear();
    void append(uint32_t start, uint32_t end, uint32_t nameOff, uint16_t nameLen, uint16_t depth);
    void setEnd(size_t i, uint32_t end);

    size_t size() const { return elems_.size(); }
    const ElementPos& at(size_t i) const { return elems_[i]; }
    ElementPos& at(size_t i) { return elems_[i]; }
    void setAt(size_t i, const ElementPos& e) { elems_[i] = e; }
    void truncate(size_t n) { elems_.resize(n); }

    // Innermost element containing pos ([start,end)), or SIZE_MAX. A closed sibling
    // that ended before pos does not contain it, whatever its start says.
    size_t findContaining(uint32_t pos) const;

    // Index of the element whose start is exactly offset, or SIZE_MAX.
    size_t findStart(uint32_t offset) const;

    // Start of the last element that closed at or before pos, or 0 when none.
    // An unclosed element (end == start) is never a safe boundary.
    uint32_t lastCompleteAt(uint32_t pos) const;

    // Applies one document mutation: bytes [pos, pos+removed) become `inserted` bytes.
    // Every offset in the index moves with it, in place, without reallocating.
    void applyEdit(uint32_t pos, uint32_t removed, uint32_t inserted);

    // True when starts are monotonic and no entry ends before it starts.
    bool valid() const;

    // End of the region an element owns when enumerating children: its own end when it
    // closed, otherwise the next element at the same or a shallower depth (or
    // `docLength`). An element that never closed still shows its children.
    uint32_t subtreeEnd(size_t i, uint32_t docLength) const;

private:
    std::vector<ElementPos> elems_;
};
