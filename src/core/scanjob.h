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
// Builds the flat index in slices: a 100 MB document is scanned over many timer
// ticks, never in one blocking pass. All the state needed to stop between two bytes
// is the scanner's ScanState plus the stack of index positions of open elements.
#include "document.h"
#include "index.h"
#include "scanner.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class ScanJob {
public:
    ScanJob(DocumentView doc, ElementIndex& out);

    // Consumes events until the scanner has moved `budgetBytes` past where it was.
    // Returns true once the document has been scanned to its end.
    bool step(uint32_t budgetBytes);

    // Drops every entry that is not safely closed at or before `pos` and resumes
    // the scan from the last such boundary. Containers still open there are dropped:
    // re-scanning them would restart from their start, which is not what we need.
    void invalidateFrom(uint32_t pos);

    bool done() const { return done_; }
    float progress() const; // 0..1 over doc.length

private:
    DocumentView doc_{};
    ElementIndex& index_;
    XmlScanner scanner_;
    std::vector<size_t> open_; // index of each open element, outermost first
    uint32_t scanned_ = 0;     // high-water mark, for progress()
    bool done_ = false;
};
