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
// A materialized node: everything the panel and the edit operations need about one
// element or one attribute. This is NOT an index entry — it is built on demand by
// re-scanning the single tag it points at, which is why ElementPos can stay 16 bytes.
#include "document.h"
#include "index.h"

#include <cstdint>
#include <string>

enum class NodeKind : uint8_t { Element, Attribute };

struct NodeRef {
    NodeKind kind = NodeKind::Element;
    uint32_t start = 0;    // offset of '<' (element) or of the attribute name
    uint32_t end = 0;      // one past the closing tag / end of the attribute span
    uint32_t nameOff = 0;  // tag name / attribute name
    uint32_t valueOff = 0; // inner text (element) / unquoted value (attribute)
    uint16_t nameLen = 0;
    uint32_t valueLen = 0;
    uint16_t depth = 0;

    // Exact bytes of [start,end) taken when the node was resolved. Attributes need
    // them: an attribute has no closing tag, so the integrity gate compares bytes.
    std::string captured;

    bool found() const { return nameLen != 0; }
};

// Resolves the node at `offset`. For NodeKind::Element, `offset` is the '<' of the
// element; for NodeKind::Attribute it is the attribute name (an offset inside the
// tag resolves to the attribute containing it, else to the first attribute).
NodeRef nodeAt(DocumentView doc, const ElementIndex& idx, uint32_t offset, NodeKind kind);
