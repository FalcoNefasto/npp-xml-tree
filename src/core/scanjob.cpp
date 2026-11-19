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
#include "scanjob.h"

#include <cstddef>

ScanJob::ScanJob(DocumentView doc, ElementIndex& out) : doc_(doc), index_(out), scanner_(doc)
{
    index_.clear();
    open_.clear();
    scanned_ = 0;
    done_ = false;
}

float ScanJob::progress() const
{
    if (doc_.length == 0) return 1.0f;
    return static_cast<float>(scanned_) / static_cast<float>(doc_.length);
}

bool ScanJob::step(uint32_t budgetBytes)
{
    if (done_) return true;

    const uint32_t from = scanner_.pos();
    uint32_t limit = from + budgetBytes;
    if (limit < from || limit > doc_.length) limit = doc_.length; // overflow, or past the end
    scanner_.setLimit(limit);

    ScanEvent ev;
    while (scanner_.next(ev)) {
        switch (ev.type) {
        case ScanEvent::Type::ElementStart:
            index_.append(ev.start, ev.selfClosing ? ev.end : ev.start, ev.nameOff, ev.nameLen,
                          ev.depth);
            if (!ev.selfClosing) open_.push_back(index_.size() - 1);
            break;
        case ScanEvent::Type::ElementEnd:
            if (!open_.empty()) {
                index_.setEnd(open_.back(), ev.end);
                open_.pop_back();
            }
            break;
        default:
            // Comments, CDATA, PIs, DOCTYPE, orphan closes: no index entry.
            break;
        }
    }

    if (scanner_.pos() > scanned_) scanned_ = scanner_.pos();
    done_ = limit >= doc_.length;
    return done_;
}

void ScanJob::invalidateFrom(uint32_t pos)
{
    // The boundary closest to pos: the end of the last element that had closed
    // there. `lastCompleteAt` returns its start, which findStart turns into the end.
    const uint32_t anchor = index_.lastCompleteAt(pos);
    uint32_t resumeAt = 0;
    const size_t ai = index_.findStart(anchor);
    if (ai != static_cast<size_t>(-1)) {
        const ElementPos& a = index_.at(ai);
        if (a.end > a.start && a.end <= pos) resumeAt = a.end;
    }

    // Keep exactly what is complete at or before that boundary. A parent whose end
    // lies past it cannot be trusted, and is dropped with everything after it.
    size_t keep = 0;
    for (size_t i = 0; i < index_.size(); ++i) {
        const ElementPos& e = index_.at(i);
        if (e.end > e.start && e.end <= resumeAt) index_.setAt(keep++, e);
    }
    index_.truncate(keep);

    open_.clear();
    scanner_ = XmlScanner(doc_);
    if (resumeAt > 0) {
        ScanState s;
        s.pos = resumeAt;
        scanner_.load(s);
    }
    scanned_ = resumeAt;
    done_ = false;
}
