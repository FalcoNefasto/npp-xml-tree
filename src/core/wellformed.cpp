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
#include "wellformed.h"

#include "scanner.h"

#include <algorithm>
#include <vector>

namespace {

struct Open {
    std::string name;
    uint32_t start = 0;
    uint32_t length = 0;
};

std::string nameOf(DocumentView doc, const ScanEvent& ev)
{
    if (!doc.data || ev.nameOff + ev.nameLen > doc.length) return std::string();
    return std::string(doc.data + ev.nameOff, ev.nameLen);
}

} // namespace

std::vector<XmlProblem> checkWellFormed(DocumentView doc, size_t maxProblems)
{
    std::vector<XmlProblem> problems;
    if (!doc.data) return problems;

    XmlScanner scanner(doc);
    std::vector<Open> stack;
    ScanEvent ev;
    bool rootClosed = false;
    // Orphan events arrive one per level the scanner drops; the first one already
    // reports the whole chain, so the rest are skipped.
    int skipOrphanEvents = 0;

    const auto report = [&](XmlProblem problem) {
        if (problems.size() < maxProblems) problems.push_back(std::move(problem));
    };

    while (scanner.next(ev)) {
        if (problems.size() >= maxProblems) break;

        switch (ev.type) {
        case ScanEvent::Type::ElementStart: {
            const std::string name = nameOf(doc, ev);
            if (ev.depth == 0 && rootClosed) {
                XmlProblem p;
                p.offset = ev.start;
                p.length = ev.end - ev.start;
                p.kind = ProblemKind::SecondRoot;
                p.message = "<" + name + "> is a second root element";
                report(std::move(p));
            }
            if (ev.selfClosing) {
                if (ev.depth == 0) rootClosed = true;
            } else {
                stack.push_back(Open{name, ev.start, ev.end - ev.start});
            }
            break;
        }

        case ScanEvent::Type::ElementEnd:
            if (!stack.empty()) stack.pop_back();
            if (stack.empty()) rootClosed = true;
            break;

        case ScanEvent::Type::OrphanClose: {
            if (skipOrphanEvents > 0) {
                --skipOrphanEvents;
                break;
            }

            const std::string closing = nameOf(doc, ev);

            // Everything still open above the element this tag does close. They are one
            // problem, not one each: they are closed by the same missing tags in the same
            // place, and one line of the list is one thing to look at.
            size_t match = stack.size();
            for (size_t i = stack.size(); i-- > 0;) {
                if (stack[i].name == closing) {
                    match = i;
                    break;
                }
            }

            XmlProblem p;
            if (match == stack.size()) {
                // Nothing at all: the closing tag closes nothing.
                p.offset = ev.start;
                p.length = ev.end - ev.start;
                p.kind = ProblemKind::OrphanClose;
                p.message = "</" + closing + "> does not match any open element";
            } else {
                const size_t firstOpen = match + 1;
                p.offset = stack[firstOpen].start;
                p.length = stack[firstOpen].length;
                p.kind = ProblemKind::MismatchedClose;

                if (stack.size() - firstOpen == 1) {
                    p.message = "<" + stack[firstOpen].name + "> is still open when </" +
                                closing + "> arrives";
                } else {
                    p.message = std::to_string(stack.size() - firstOpen) +
                                " elements are still open when </" + closing + "> arrives: ";
                    for (size_t i = firstOpen; i < stack.size(); ++i) {
                        if (i > firstOpen) p.message += ", ";
                        p.message += "<" + stack[i].name + ">";
                    }
                }

                skipOrphanEvents = static_cast<int>(stack.size() - firstOpen) - 1;
                stack.resize(firstOpen);
            }
            report(std::move(p));
            break;
        }

        default:
            break;
        }
    }

    // Whatever is still open at the end: also one line, listing them in order.
    if (!stack.empty()) {
        XmlProblem p;
        p.offset = stack.front().start;
        p.length = stack.front().length;
        p.kind = ProblemKind::Unclosed;

        if (stack.size() == 1) {
            p.message = "<" + stack.front().name + "> is never closed";
        } else {
            p.message = std::to_string(stack.size()) + " elements are never closed: ";
            for (size_t i = 0; i < stack.size(); ++i) {
                if (i > 0) p.message += ", ";
                p.message += "<" + stack[i].name + ">";
            }
        }
        report(std::move(p));
    }

    std::sort(problems.begin(), problems.end(),
              [](const XmlProblem& a, const XmlProblem& b) { return a.offset < b.offset; });
    return problems;
}
