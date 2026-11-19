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
// Structural operations on the document: every one of them produces a single
// Edit {start, removeLen, insertText} that the caller applies to Scintilla inside
// one undo action. Nothing here touches the buffer.
#include "document.h"
#include "index.h"
#include "node.h"

#include <string>
#include <string_view>
#include <vector>

enum class OpStatus {
    Ok,
    Stale,         // the node no longer matches the document: realign and do nothing
    NotApplicable  // the operation does not make sense for this node
};

struct PlannedEdit {
    OpStatus status = OpStatus::NotApplicable;
    Edit edit;
};

// The pre-edit gate. An offset captured earlier may not delete the wrong block.
//
// Element: (1) [start,end) is inside the document; (2) at `start` there is an
// opening tag whose name is the expected one; (3) at `end` there is `</name>`,
// unless the tag is self-closing. The closing tag is the anchor that makes an edit
// inside the block visible: it moves, and the expected slice is no longer there.
//
// Attribute: an attribute has no closing tag, so the check is byte-for-byte
// equality between the current slice and the one captured at resolution.
OpStatus verify(DocumentView doc, const NodeRef& node);

// --- element operations -----------------------------------------------------
// `removeLine` also takes the whole line when the node is alone on it, including
// its indentation and its "\r\n" (or "\n"), so no stray \r is left behind.
PlannedEdit planRemove(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                       bool removeLine);

// Re-inserts the element's own bytes right after its end.
PlannedEdit planDuplicate(DocumentView doc, const ElementIndex& idx, const NodeRef& node);

// Inserts `xml` on the line after the node's line, with that line's indentation.
PlannedEdit planPasteAfter(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                           std::string_view xml);

// Replaces the whole element with `xml`. Empty text is NotApplicable: a paste that
// deletes the block and inserts nothing is Remove wearing another name.
PlannedEdit planPaste(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                      std::string_view xml);

// --- reporting --------------------------------------------------------------
// The values a "sum" or "unique" would look at, in document order.
// Scope: the element's direct children, taking each child's `value` attribute when it
// has one and its text content otherwise. When the element has no element children,
// the scope is every element with the same name in the document, so clicking one
// <F_20 value="…"/> and asking for a sum totals that column across all sections.
std::vector<std::string> collectValues(DocumentView doc, const ElementIndex& idx,
                                       const NodeRef& node);

struct SumResult {
    size_t values = 0;      // values looked at
    size_t numbers = 0;     // values that parsed as a number
    double sum = 0.0;
};

SumResult sumValues(const std::vector<std::string>& values);

// Distinct values, first occurrence wins, document order preserved.
std::vector<std::string> uniqueValues(const std::vector<std::string>& values);

// --- editing ----------------------------------------------------------------
// True when `name` is usable as an XML element or attribute name: letters, digits,
// '-', '_', '.', ':' and not starting with a digit or a hyphen. Deliberately strict:
// a name we cannot write correctly is refused before anything is touched.
bool isValidXmlName(std::string_view name);

struct RenamePlan {
    OpStatus status = OpStatus::NotApplicable;
    Edit open;   // the name inside the opening tag
    Edit close;  // the name inside the closing tag (empty when self-closing)
    bool hasClose = false;
};

RenamePlan planRename(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                      std::string_view newName);

// Inserts `<name/>` as the last child, indented two spaces past the parent.
PlannedEdit planAddChild(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                         std::string_view name);

// Adds the attribute, or replaces its value when it already exists.
PlannedEdit planSetAttribute(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                             std::string_view name, std::string_view value);

// Replaces the value of an existing attribute, quotes excluded.
PlannedEdit planSetAttributeValue(DocumentView doc, const NodeRef& attr, std::string_view value);

std::string copyElement(DocumentView doc, const NodeRef& node); // "<tag ...>...</tag>"
std::string copyData(DocumentView doc, const NodeRef& node);    // inner text only

// "/root/child", or "/root/child[@id='x']" when withPredicates and the element has
// attributes (the first one is the predicate).
std::string getPath(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                    bool withPredicates);

// --- attribute operations ---------------------------------------------------
// The removed range includes the whitespace that separates the attribute from the
// previous token, so the tag does not keep a stray blank; it never includes '>' or
// '/>', so the tag stays closed.
PlannedEdit planRemoveAttribute(DocumentView doc, const NodeRef& attr);

std::string copyAttributeValue(DocumentView doc, const NodeRef& attr); // value, no quotes
