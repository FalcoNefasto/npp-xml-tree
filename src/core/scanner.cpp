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
#include "scanner.h"
#include "encoding.h"

#include <cstring>

namespace {

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Name = everything up to whitespace, '/', '>', '<', '=' or a quote. XML names are
// more restrictive; being permissive here costs nothing and keeps broken markup usable.
inline bool isNameChar(char c)
{
    return !isSpace(c) && c != '/' && c != '>' && c != '<' && c != '=' && c != '"' && c != '\'';
}

} // namespace

void XmlScanner::reset(DocumentView doc)
{
    doc_ = doc;
    pos_ = 0;
    limit_ = doc.length;
    finished_ = false;
    stack_.clear();
    inAttrs_ = false;
    attrPos_ = 0;
    attrEnd_ = 0;
    attrDepth_ = 0;
    inClose_ = false;

    // A UTF-8 BOM is not content: starting after it keeps every offset a byte
    // offset of the document, which is what Scintilla needs.
    if (detectEncoding(doc.data, doc.length) == Encoding::Utf8Bom) pos_ = 3;
}

void XmlScanner::setLimit(uint32_t limit)
{
    limit_ = limit > doc_.length ? doc_.length : limit;
    if (limit_ > pos_) finished_ = false;
}

bool XmlScanner::atLimit() const { return pos_ >= limit_; }

uint32_t XmlScanner::findFrom(uint32_t from, const char* needle, uint32_t needleLen) const
{
    if (needleLen == 0 || from >= doc_.length) return doc_.length;
    for (uint32_t i = from; i + needleLen <= doc_.length; ++i) {
        if (std::memcmp(doc_.data + i, needle, needleLen) == 0) return i;
    }
    return doc_.length;
}

// Finds the '>' that closes the tag starting at `from`, skipping quoted values
// so that a '>' inside an attribute value does not end the tag.
uint32_t XmlScanner::findTagEnd(uint32_t from) const
{
    char quote = 0;
    for (uint32_t i = from; i < doc_.length; ++i) {
        const char c = at(i);
        if (quote) {
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '>') {
            return i + 1;
        } else if (c == '<') {
            return i; // malformed: the tag ends where the next one starts
        }
    }
    return doc_.length;
}

bool XmlScanner::next(ScanEvent& ev)
{
    // Pending orphan closes are emitted before anything else, even at the end of
    // the document: the close tag has been consumed but the stack is not settled.
    if (inClose_) return emitClosing(ev);

    // Same for the attributes of a tag whose '>' ends the document: they are already
    // parsed, so a limit that sits exactly at the tag end must not hide them.
    while (inAttrs_ || !atLimit()) {
        const uint32_t p = pos_;

        // Attributes of the start tag just emitted, in document order, before any
        // content token. pos_ already points past the tag, so this branch never
        // rescans it.
        if (inAttrs_) {
            uint32_t i = attrPos_;
            while (i < attrEnd_ && isSpace(at(i))) ++i;
            const bool tagDone = i >= attrEnd_ || at(i) == '>' ||
                                 (at(i) == '/' && i + 1 < attrEnd_ && at(i + 1) == '>');
            if (tagDone) {
                inAttrs_ = false;
                attrPos_ = i;
                continue;
            }

            const uint32_t nameOff = i;
            uint32_t nameLen = 0;
            while (i < attrEnd_ && isNameChar(at(i))) {
                ++i;
                ++nameLen;
            }

            // Nothing here looks like an attribute name. It happens for real: a tag
            // whose '>' was deleted ends with " /" and the next '<', so the slash sits
            // before the end of the tag with no '>' after it. Skipping the byte keeps
            // the scan moving; without this the same empty attribute came back forever
            // and Notepad++ froze on the file.
            if (nameLen == 0) {
                attrPos_ = nameOff + 1;
                continue;
            }

            uint32_t spanEnd = i;
            uint32_t valueOff = 0;
            uint32_t valueLen = 0;

            uint32_t j = i;
            while (j < attrEnd_ && isSpace(at(j))) ++j;
            if (j < attrEnd_ && at(j) == '=') {
                ++j;
                while (j < attrEnd_ && isSpace(at(j))) ++j;
                if (j < attrEnd_ && (at(j) == '"' || at(j) == '\'')) {
                    const char quote = at(j);
                    ++j;
                    valueOff = j;
                    while (j < attrEnd_ && at(j) != quote) ++j;
                    valueLen = j - valueOff;
                    if (j < attrEnd_) ++j; // closing quote
                } else {
                    valueOff = j;
                    while (j < attrEnd_ && !isSpace(at(j)) && at(j) != '>' && at(j) != '/') ++j;
                    valueLen = j - valueOff;
                }
                spanEnd = j;
            }

            ev = ScanEvent{};
            ev.type = ScanEvent::Type::Attribute;
            ev.start = nameOff;
            ev.end = spanEnd > nameOff ? spanEnd : nameOff + nameLen;
            ev.nameOff = nameOff;
            ev.nameLen = static_cast<uint16_t>(nameLen);
            ev.valueOff = valueOff;
            ev.valueLen = valueLen;
            ev.depth = attrDepth_;
            // Always move: an attribute that parsed to nothing must not be served twice.
            const uint32_t next = i > j ? i : j;
            attrPos_ = next > nameOff ? next : nameOff + nameLen;
            return true;
        }

        if (at(p) != '<') {
            uint32_t i = p;
            while (i < limit_ && at(i) != '<') ++i;
            pos_ = i;
            continue;
        }

        const auto has = [&](uint32_t off, const char* s, uint32_t n) -> bool {
            return p + off + n <= doc_.length && std::memcmp(doc_.data + p + off, s, n) == 0;
        };

        const auto make = [&](ScanEvent::Type t, uint32_t end) {
            ev = ScanEvent{};
            ev.type = t;
            ev.start = p;
            ev.end = end;
            ev.depth = static_cast<uint16_t>(stack_.size());
            pos_ = end;
            return true;
        };

        if (has(0, "<!--", 4)) {
            const uint32_t close = findFrom(p + 4, "-->", 3);
            return make(ScanEvent::Type::Comment, close < doc_.length ? close + 3 : doc_.length);
        }
        if (has(0, "<![CDATA[", 9)) {
            const uint32_t close = findFrom(p + 9, "]]>", 3);
            return make(ScanEvent::Type::CData, close < doc_.length ? close + 3 : doc_.length);
        }
        if (has(0, "<?", 2)) {
            const uint32_t close = findFrom(p + 2, "?>", 2);
            return make(ScanEvent::Type::ProcessingInstruction,
                        close < doc_.length ? close + 2 : doc_.length);
        }
        if (has(0, "<!DOCTYPE", 9)) {
            // The internal subset may contain '>' and quoted strings: track both.
            uint32_t i = p + 9;
            int brackets = 0;
            char quote = 0;
            for (; i < doc_.length; ++i) {
                const char c = at(i);
                if (quote) {
                    if (c == quote) quote = 0;
                    continue;
                }
                if (c == '"' || c == '\'') {
                    quote = c;
                } else if (c == '[') {
                    ++brackets;
                } else if (c == ']') {
                    if (brackets > 0) --brackets;
                } else if (c == '>' && brackets == 0) {
                    ++i;
                    break;
                }
            }
            return make(ScanEvent::Type::DocType, i);
        }
        if (has(0, "<!", 2)) {
            const uint32_t close = findFrom(p + 2, ">", 1);
            pos_ = close < doc_.length ? close + 1 : doc_.length;
            continue;
        }

        const bool closing = has(0, "</", 2);
        if (has(0, "<!DOCTYPE", 9)) {
            // The internal subset may contain '>' and quoted strings: track both.
            uint32_t i = p + 9;
            int brackets = 0;
            char quote = 0;
            for (; i < doc_.length; ++i) {
                const char c = at(i);
                if (quote) {
                    if (c == quote) quote = 0;
                    continue;
                }
                if (c == '"' || c == '\'') {
                    quote = c;
                } else if (c == '[') {
                    ++brackets;
                } else if (c == ']') {
                    if (brackets > 0) --brackets;
                } else if (c == '>' && brackets == 0) {
                    ++i;
                    break;
                }
            }
            return make(ScanEvent::Type::DocType, i);
        }
        if (has(0, "<!", 2)) {
            const uint32_t close = findFrom(p + 2, ">", 1);
            pos_ = close < doc_.length ? close + 1 : doc_.length;
            continue;
        }

        const uint32_t tagEnd = findTagEnd(p + (closing ? 2 : 1));
        const uint32_t nameOff = p + (closing ? 2 : 1);
        uint16_t nameLen = 0;
        while (nameOff + nameLen < tagEnd && isNameChar(at(nameOff + nameLen))) ++nameLen;

        ev = ScanEvent{};
        ev.start = p;
        ev.end = tagEnd > p ? tagEnd : p + 1;
        ev.nameOff = nameOff;
        ev.nameLen = nameLen;
        ev.depth = static_cast<uint16_t>(stack_.size());
        pos_ = ev.end;

        if (closing) {
            inClose_ = true;
            closeStart_ = p;
            closeEnd_ = ev.end;
            closeNameOff_ = nameOff;
            closeNameLen_ = nameLen;
            return emitClosing(ev);
        }

        ev.selfClosing = ev.end >= p + 2 && at(ev.end - 2) == '/';
        ev.type = ScanEvent::Type::ElementStart;
        inAttrs_ = true;
        attrPos_ = nameOff + nameLen;
        attrEnd_ = ev.end;
        attrDepth_ = ev.depth;
        if (!ev.selfClosing) stack_.push_back(OpenElement{nameOff, nameLen});
        return true;
    }

    if (limit_ >= doc_.length) finished_ = true;
    return false;
}

bool XmlScanner::emitClosing(ScanEvent& ev)
{
    size_t match = static_cast<size_t>(-1);
    for (size_t k = stack_.size(); k-- > 0;) {
        if (stack_[k].nameLen == closeNameLen_ &&
            std::memcmp(doc_.data + stack_[k].nameOff, doc_.data + closeNameOff_, closeNameLen_) == 0) {
            match = k;
            break;
        }
    }

    const auto fill = [&](ScanEvent::Type t, uint16_t depth) {
        ev = ScanEvent{};
        ev.type = t;
        ev.start = closeStart_;
        ev.end = closeEnd_;
        ev.nameOff = closeNameOff_;
        ev.nameLen = closeNameLen_;
        ev.depth = depth;
    };

    if (match == static_cast<size_t>(-1)) {
        fill(ScanEvent::Type::OrphanClose, static_cast<uint16_t>(stack_.size()));
        inClose_ = false;
        return true;
    }

    if (match == stack_.size() - 1) {
        fill(ScanEvent::Type::ElementEnd, static_cast<uint16_t>(match));
        stack_.pop_back();
        inClose_ = false;
        return true;
    }

    // The close tag belongs to an element further down: the level above it is
    // reported as an orphan and dropped, one per call, until the owner is on top.
    fill(ScanEvent::Type::OrphanClose, static_cast<uint16_t>(stack_.size() - 1));
    stack_.pop_back();
    return true;
}

ScanState XmlScanner::save() const
{
    ScanState s;
    s.pos = pos_;
    s.finished = finished_;
    s.stack = stack_;
    s.inAttrs = inAttrs_;
    s.attrPos = attrPos_;
    s.attrEnd = attrEnd_;
    s.attrDepth = attrDepth_;
    s.inClose = inClose_;
    s.closeStart = closeStart_;
    s.closeEnd = closeEnd_;
    s.closeNameOff = closeNameOff_;
    s.closeNameLen = closeNameLen_;
    return s;
}

void XmlScanner::load(const ScanState& s)
{
    pos_ = s.pos;
    finished_ = s.finished;
    stack_ = s.stack;
    inAttrs_ = s.inAttrs;
    attrPos_ = s.attrPos;
    attrEnd_ = s.attrEnd;
    attrDepth_ = s.attrDepth;
    inClose_ = s.inClose;
    closeStart_ = s.closeStart;
    closeEnd_ = s.closeEnd;
    closeNameOff_ = s.closeNameOff;
    closeNameLen_ = s.closeNameLen;
    if (pos_ > limit_) limit_ = doc_.length;
}
