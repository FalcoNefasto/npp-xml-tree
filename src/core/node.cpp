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
#include "node.h"
#include "scanner.h"

namespace {

constexpr size_t kNone = static_cast<size_t>(-1);

} // namespace

NodeRef nodeAt(DocumentView doc, const ElementIndex& idx, uint32_t offset, NodeKind kind)
{
    NodeRef n;
    n.kind = kind;
    if (!doc.data || doc.length == 0 || offset >= doc.length) return n;

    if (kind == NodeKind::Element) {
        const size_t i = idx.findStart(offset);
        if (i == kNone) return n;

        const ElementPos& e = idx.at(i);
        n.start = e.start;
        n.end = e.end;
        n.nameOff = e.nameOff;
        n.nameLen = e.nameLen;
        n.depth = e.depth;

        // Local scan of this one opening tag: where it ends, and whether it is
        // self-closing. The index records offsets, not shapes.
        XmlScanner s(DocumentView{doc.data + e.start, doc.length - e.start});
        ScanEvent ev;
        if (!s.next(ev) || ev.type != ScanEvent::Type::ElementStart) return n;

        const uint32_t openEnd = e.start + ev.end;
        if (!ev.selfClosing && e.end > openEnd) {
            // Inner text runs from the end of the opening tag to the '<' of the
            // closing one. Scanning back for that '<' keeps `</name >` working.
            uint32_t closeStart = e.end;
            while (closeStart > openEnd && doc.data[closeStart - 1] != '<') --closeStart;
            if (closeStart > openEnd) --closeStart; // now on the '<' itself
            if (closeStart > openEnd) {
                n.valueOff = openEnd;
                n.valueLen = static_cast<uint16_t>(closeStart - openEnd);
            }
        }
        return n;
    }

    // Attribute: attributes only exist inside opening tags, so the owner is the last
    // element that starts at or before the offset (no tag can start inside a tag).
    // The local scan below decides whether the offset really is in that tag; using
    // findContaining here would miss elements that never closed (end == start).
    size_t lo = 0, hi = idx.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (idx.at(mid).start <= offset) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return n;

    const ElementPos& e = idx.at(lo - 1);
    XmlScanner s(DocumentView{doc.data + e.start, doc.length - e.start});
    ScanEvent ev;
    if (!s.next(ev) || ev.type != ScanEvent::Type::ElementStart) return n;
    if (e.start + ev.end <= offset) return n;

    NodeRef first;
    while (s.next(ev)) {
        if (ev.type != ScanEvent::Type::Attribute) break;

        NodeRef a;
        a.kind = NodeKind::Attribute;
        a.start = e.start + ev.start;
        a.end = e.start + ev.end;
        a.nameOff = e.start + ev.nameOff;
        a.nameLen = ev.nameLen;
        a.valueOff = e.start + ev.valueOff;
        a.valueLen = ev.valueLen;
        a.depth = e.depth;
        a.captured.assign(doc.data + a.start, a.end - a.start);

        if (offset >= a.start && offset < a.end) return a;
        if (!first.found()) first = a;
    }
    // Inside the tag but not on an attribute (the '<', say): the first one is the
    // least surprising answer, and the caller still gets a verified node.
    return first;
}
