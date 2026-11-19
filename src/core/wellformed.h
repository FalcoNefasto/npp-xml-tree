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
#include "document.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// What a strict XML parser would refuse, found with the same tolerant scanner the tree
// uses: elements that are never closed, closing tags that close nothing, a second root.
// This is not schema validation -- it says nothing about attributes or content models.
//
// It only reports: the repair is the user's to make in the text, by hand.
enum class ProblemKind : uint8_t {
    Unclosed,         // <a> ... and the document ends
    MismatchedClose,  // <a><b></a> : b is still open when </a> arrives
    OrphanClose,      // </b> with nothing open
    SecondRoot,       // a second element at depth 0
};

struct XmlProblem {
    uint32_t offset = 0; // what the user should look at
    uint32_t length = 0; // length of the offending token, for highlighting
    ProblemKind kind = ProblemKind::Unclosed;
    std::string message; // UTF-8, with the tag name already in it
};

std::vector<XmlProblem> checkWellFormed(DocumentView doc, size_t maxProblems = 200);
