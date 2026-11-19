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

#include <cstdint>
#include <vector>

// One structural token found in the document. Offsets are byte offsets into the
// DocumentView, i.e. the same values Scintilla uses for SCI_SETSEL and
// SCI_DELETERANGE.
struct ScanEvent {
    enum class Type : uint8_t {
        ElementStart,   // <tag ...>   (selfClosing when <tag .../>)
        ElementEnd,     // </tag>
        Attribute,      // name="value" inside the start tag
        Comment,        // <!-- ... -->
        CData,          // <![CDATA[ ... ]]>
        ProcessingInstruction, // <? ... ?>
        DocType,        // <!DOCTYPE ... >, internal subset included
        OrphanClose,    // </tag> with no matching open element
    };

    Type type = Type::ElementStart;
    uint32_t start = 0;   // first byte of the whole token
    uint32_t end = 0;     // one past the last byte of the whole token
    uint32_t nameOff = 0; // tag or attribute name
    uint16_t nameLen = 0;
    uint32_t valueOff = 0; // attribute value / inner text, when it applies
    uint32_t valueLen = 0; // 32 bit: base64 and MTOM values run past 64 KB
    uint16_t depth = 0;    // depth of the element the token belongs to
    bool selfClosing = false;
};

struct OpenElement {
    uint32_t nameOff = 0;
    uint16_t nameLen = 0;
};

// Everything the scanner needs to stop between two bytes and resume later.
struct ScanState {
    uint32_t pos = 0;
    bool finished = false;
    std::vector<OpenElement> stack;
    bool inAttrs = false;
    uint32_t attrPos = 0;
    uint32_t attrEnd = 0;
    uint16_t attrDepth = 0;
    bool inClose = false;
    uint32_t closeStart = 0;
    uint32_t closeEnd = 0;
    uint32_t closeNameOff = 0;
    uint16_t closeNameLen = 0;
};

class XmlScanner {
public:
    XmlScanner() = default;
    explicit XmlScanner(DocumentView doc) { reset(doc); }

    void reset(DocumentView doc);

    // Emits the next token. Returns false when the current limit is reached
    // (the caller may raise it and call again) or at the end of the document.
    bool next(ScanEvent& ev);

    void setLimit(uint32_t limit);
    uint32_t limit() const { return limit_; }
    uint32_t pos() const { return pos_; }
    uint16_t depth() const { return static_cast<uint16_t>(stack_.size()); }

    ScanState save() const;
    void load(const ScanState& s);

private:
    bool atLimit() const;
    char at(uint32_t i) const { return doc_.data[i]; }
    uint32_t findFrom(uint32_t from, const char* needle, uint32_t needleLen) const;
    uint32_t findTagEnd(uint32_t from) const;
    bool skipTo(uint32_t end) { pos_ = end; return true; }

    DocumentView doc_{};
    uint32_t pos_ = 0;
    uint32_t limit_ = 0;
    bool finished_ = false;
    std::vector<OpenElement> stack_;

    // Attributes of the tag at pos_: emitted one per next() call, then cleared.
    bool inAttrs_ = false;
    uint32_t attrPos_ = 0;
    uint32_t attrEnd_ = 0;
    uint16_t attrDepth_ = 0;

    // A close tag that matched an element further down the stack: the levels above
    // are reported as orphan closes one per call, then the real ElementEnd.
    bool inClose_ = false;
    uint32_t closeStart_ = 0;
    uint32_t closeEnd_ = 0;
    uint32_t closeNameOff_ = 0;
    uint16_t closeNameLen_ = 0;
    bool emitClosing(ScanEvent& ev);
};
