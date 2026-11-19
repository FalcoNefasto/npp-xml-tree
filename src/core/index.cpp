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
#include "index.h"

void ElementIndex::clear() { elems_.clear(); }

void ElementIndex::append(uint32_t start, uint32_t end, uint32_t nameOff, uint16_t nameLen,
                          uint16_t depth)
{
    ElementPos e;
    e.start = start;
    e.end = end;
    e.nameOff = nameOff;
    e.nameLen = nameLen;
    e.depth = depth;
    elems_.push_back(e);
}

void ElementIndex::setEnd(size_t i, uint32_t end) { elems_[i].end = end; }

bool ElementIndex::valid() const
{
    for (size_t i = 0; i < elems_.size(); ++i) {
        if (elems_[i].end < elems_[i].start) return false;
        if (i > 0 && elems_[i].start < elems_[i - 1].start) return false;
    }
    return true;
}

size_t ElementIndex::findStart(uint32_t offset) const
{
    size_t lo = 0, hi = elems_.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (elems_[mid].start < offset) lo = mid + 1;
        else hi = mid;
    }
    if (lo < elems_.size() && elems_[lo].start == offset) return lo;
    return static_cast<size_t>(-1);
}

size_t ElementIndex::findContaining(uint32_t pos) const
{
    // Last element whose start is <= pos.
    size_t lo = 0, hi = elems_.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (elems_[mid].start <= pos) lo = mid + 1;
        else hi = mid;
    }
    // Walk back to the innermost element that really contains pos: siblings that
    // already closed before pos sit between the container and the caret.
    for (size_t i = lo; i-- > 0;) {
        if (elems_[i].start <= pos && pos < elems_[i].end) return i;
    }
    return static_cast<size_t>(-1);
}

uint32_t ElementIndex::lastCompleteAt(uint32_t pos) const
{
    // The boundary closest to pos wins: the element with the greatest end (ties go
    // to the later start), so a resync rescans as little as possible.
    size_t best = static_cast<size_t>(-1);
    for (size_t i = 0; i < elems_.size(); ++i) {
        const ElementPos& e = elems_[i];
        if (e.end <= e.start || e.end > pos) continue; // unclosed, or still open at pos
        if (best == static_cast<size_t>(-1) || e.end > elems_[best].end ||
            (e.end == elems_[best].end && e.start > elems_[best].start)) {
            best = i;
        }
    }
    return best == static_cast<size_t>(-1) ? 0 : elems_[best].start;
}

void ElementIndex::applyEdit(uint32_t pos, uint32_t removed, uint32_t inserted)
{
    const int64_t delta = static_cast<int64_t>(inserted) - static_cast<int64_t>(removed);
    const uint32_t after = pos + removed;
    const auto shifted = [delta](uint32_t v) {
        return static_cast<uint32_t>(static_cast<int64_t>(v) + delta);
    };

    // One pass, writing over the entries that survive: no reallocation, and the
    // relative order (which is document order) is preserved by construction.
    size_t w = 0;
    for (size_t r = 0; r < elems_.size(); ++r) {
        ElementPos e = elems_[r];

        if (e.end <= pos) {
            // Closed before the edit: untouched.
        } else if (e.start >= after) {
            e.start = shifted(e.start);
            e.end = shifted(e.end);
        } else if (e.start >= pos && e.end <= after) {
            continue; // entirely inside the replaced range: the element is gone
        } else {
            // Straddles a boundary: it survives, clipped to the new range and with
            // its end moved by the size change.
            if (e.start >= pos) e.start = pos;
            e.end = (e.end >= after) ? shifted(e.end) : pos;
        }
        // The name always starts right after the '<'. Recomputing it here is what
        // keeps getPath, filterByName and the tree reading the right bytes after the
        // entries have moved.
        e.nameOff = e.start + 1;
        elems_[w++] = e;
    }
    elems_.resize(w);
}

uint32_t ElementIndex::subtreeEnd(size_t i, uint32_t docLength) const
{
    if (i >= elems_.size()) return docLength;

    const ElementPos& me = elems_[i];
    if (me.end > me.start) return me.end;

    // Never closed: the subtree runs to the next element that is not nested inside it.
    for (size_t j = i + 1; j < elems_.size(); ++j) {
        if (elems_[j].depth <= me.depth) return elems_[j].start;
    }
    return docLength;
}