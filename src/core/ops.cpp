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
#include "ops.h"

#include "scanner.h"

#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

// One past the '>' of the tag starting at `from`, skipping quoted values; 0 when
// the tag is not terminated inside the document.
uint32_t tagEnd(DocumentView doc, uint32_t from)
{
    char quote = 0;
    for (uint32_t i = from + 1; i < doc.length; ++i) {
        const char c = doc.data[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '>') {
            return i + 1;
        }
    }
    return 0;
}

bool isNameTerminator(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/' || c == '>';
}

bool isBlank(char c) { return c == ' ' || c == '\t'; }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool blankBetween(DocumentView doc, uint32_t from, uint32_t to)
{
    for (uint32_t i = from; i < to && i < doc.length; ++i) {
        if (!isBlank(doc.data[i])) return false;
    }
    return true;
}

// The '@name='value'' predicate of the first attribute of the element at `e`.
// Empty when the element has no attributes.
std::string firstPredicate(DocumentView doc, const ElementPos& e)
{
    XmlScanner s(DocumentView{doc.data + e.start, doc.length - e.start});
    ScanEvent ev;
    if (!s.next(ev)) return std::string();
    while (s.next(ev)) {
        if (ev.type != ScanEvent::Type::Attribute || ev.nameLen == 0) return std::string();
        const std::string name(doc.data + e.start + ev.nameOff, ev.nameLen);
        const std::string value(doc.data + e.start + ev.valueOff, ev.valueLen);
        // Wrap in the quote the value does not contain, so the path stays readable.
        const char quote = value.find('\'') == std::string::npos ? '\'' : '"';
        return "[@" + name + "=" + quote + value + quote + "]";
    }
    return std::string();
}

// One past the closing tag of the element that starts at `start`, read from the
// current text. 0 when the element never closes. This is the authoritative end: the
// one remembered in the index may be stale, and in a document full of identical
// blocks a stale end still lands on a `</name>` that looks right.
uint32_t elementEndFromText(DocumentView doc, uint32_t start)
{
    if (!doc.data || start >= doc.length) return 0;

    XmlScanner scanner(DocumentView{doc.data + start, doc.length - start});
    ScanEvent ev;
    if (!scanner.next(ev) || ev.type != ScanEvent::Type::ElementStart) return 0;
    if (ev.selfClosing) return start + ev.end;

    while (scanner.next(ev)) {
        if (ev.type == ScanEvent::Type::ElementEnd && ev.depth == 0) return start + ev.end;
    }
    return 0;
}

} // namespace

OpStatus verify(DocumentView doc, const NodeRef& node)
{
    if (!doc.data || node.nameLen == 0) return OpStatus::NotApplicable;

    if (node.kind == NodeKind::Attribute) {
        const uint32_t len = node.end > node.start ? node.end - node.start : 0;
        if (len == 0 || node.end > doc.length) return OpStatus::Stale;
        if (node.captured.size() != len) return OpStatus::Stale;
        if (std::memcmp(doc.data + node.start, node.captured.data(), len) != 0) {
            return OpStatus::Stale;
        }
        return OpStatus::Ok;
    }

    // (1) the start is inside the document
    if (node.start >= doc.length) return OpStatus::Stale;

    // (2) an opening tag starts here, and its name is exactly nameLen bytes long
    if (doc.data[node.start] != '<') return OpStatus::Stale;
    const uint32_t nameAt = node.start + 1;
    if (nameAt >= doc.length || doc.data[nameAt] == '/') return OpStatus::Stale;
    const uint32_t openEnd = tagEnd(doc, node.start);
    if (openEnd == 0) return OpStatus::Stale;
    if (nameAt + node.nameLen >= openEnd) return OpStatus::Stale;
    if (!isNameTerminator(doc.data[nameAt + node.nameLen])) return OpStatus::Stale;

    // (3) the element closes somewhere in the text. The end remembered in the index
    // is not consulted: in a document full of identical blocks a stale end still
    // lands on a `</name>` that looks right, and the block would be cut in half.
    // The operations re-derive the range from the buffer before touching it.
    if (elementEndFromText(doc, node.start) == 0) return OpStatus::Stale;
    return OpStatus::Ok;
}

PlannedEdit planRemove(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                       bool removeLine)
{
    PlannedEdit out;
    out.status = verify(doc, node);
    if (out.status != OpStatus::Ok) return out;

    uint32_t start = node.start;
    uint32_t end = elementEndFromText(doc, start);

    // Removing the last child would leave the parent holding nothing between two lines.
    // Collapsing the parent to `<name .../>` is what the sibling lines look like, and it
    // is a single edit, so a single undo.
    if (idx.size() > 0) {
        const size_t parentIndex = idx.findContaining(start > 0 ? start - 1 : 0);
        if (parentIndex != static_cast<size_t>(-1)) {
            const ElementPos& parent = idx.at(parentIndex);
            const uint32_t parentOpenEnd = tagEnd(doc, parent.start);
            const uint32_t parentEnd = elementEndFromText(doc, parent.start);

            if (parentOpenEnd > 0 && parentEnd > parentOpenEnd && doc.data[parentEnd - 2] != '/') {
                const uint32_t closeStart = parentEnd - parent.nameLen - 3;

                bool onlyWhitespace = true;
                for (uint32_t i = parentOpenEnd; i < closeStart && onlyWhitespace;) {
                    if (i >= start && i < end) {
                        i = end; // the element being removed
                        continue;
                    }
                    if (isSpace(doc.data[i])) {
                        ++i;
                    } else {
                        onlyWhitespace = false;
                    }
                }

                bool otherChild = false;
                for (size_t k = 0; k < idx.size() && !otherChild; ++k) {
                    const ElementPos& e = idx.at(k);
                    if (e.start <= parent.start || e.start >= parentEnd) continue;
                    if (e.start >= start && e.start < end) continue; // ours, being removed
                    if (e.depth == static_cast<uint16_t>(parent.depth + 1)) otherChild = true;
                }

                if (onlyWhitespace && !otherChild) {
                    out.edit.start = parent.start;
                    out.edit.removeLen = parentEnd - parent.start;
                    out.edit.insertText.assign(doc.data + parent.start, parentOpenEnd - parent.start - 1);
                    out.edit.insertText += "/>";
                    return out;
                }
            }
        }
    }

    if (removeLine) {
        // Only a node that sits alone on its line takes the line with it.
        uint32_t lineStart = start;
        while (lineStart > 0 && doc.data[lineStart - 1] != '\n') --lineStart;

        uint32_t tail = end;
        while (tail < doc.length && isBlank(doc.data[tail])) ++tail;

        uint32_t after = tail;
        bool lineEndsThere = false;
        if (after + 1 < doc.length && doc.data[after] == '\r' && doc.data[after + 1] == '\n') {
            after += 2; // CRLF counts as one unit: no orphan \r
            lineEndsThere = true;
        } else if (after < doc.length && (doc.data[after] == '\n' || doc.data[after] == '\r')) {
            after += 1;
            lineEndsThere = true;
        } else if (after >= doc.length) {
            lineEndsThere = true; // last line, no trailing newline: stop at the end
            after = doc.length;
        }

        if (lineEndsThere && blankBetween(doc, lineStart, start)) {
            start = lineStart;
            end = after;
        }
    }

    out.edit.start = start;
    out.edit.removeLen = end - start;
    out.edit.insertText.clear();
    return out;
}

PlannedEdit planDuplicate(DocumentView doc, const ElementIndex& idx, const NodeRef& node)
{
    // A duplicate is the element pasted after itself: same line rules, same
    // indentation, own line when the original has one.
    return planPasteAfter(doc, idx, node, copyElement(doc, node));
}

PlannedEdit planPaste(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                      std::string_view xml)
{
    (void)idx;
    PlannedEdit out;
    if (xml.empty()) {
        out.status = OpStatus::NotApplicable;
        return out;
    }
    out.status = verify(doc, node);
    if (out.status != OpStatus::Ok) return out;

    const uint32_t end = elementEndFromText(doc, node.start);
    out.edit.start = node.start;
    out.edit.removeLen = end - node.start;
    out.edit.insertText = std::string(xml);
    return out;
}

PlannedEdit planPasteAfter(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                           std::string_view xml)
{
    (void)idx;
    PlannedEdit out;
    if (xml.empty()) {
        out.status = OpStatus::NotApplicable;
        return out;
    }
    out.status = verify(doc, node);
    if (out.status != OpStatus::Ok) return out;

    // Indentation of the reference node: the blank run that opens its line.
    uint32_t lineStart = node.start;
    while (lineStart > 0 && doc.data[lineStart - 1] != '\n') --lineStart;
    uint32_t indentEnd = lineStart;
    while (indentEnd < node.start && isBlank(doc.data[indentEnd])) ++indentEnd;
    const std::string indent(doc.data + lineStart, indentEnd - lineStart);

    // Where the node's line ends, and whether there is anything else on it.
    const uint32_t nodeEnd = elementEndFromText(doc, node.start);
    uint32_t lineEnd = nodeEnd;
    while (lineEnd < doc.length && doc.data[lineEnd] != '\n' && doc.data[lineEnd] != '\r') ++lineEnd;
    bool aloneOnLine = true;
    for (uint32_t i = nodeEnd; i < lineEnd; ++i) {
        if (!isBlank(doc.data[i])) {
            aloneOnLine = false;
            break;
        }
    }

    if (!aloneOnLine) {
        // The node shares its line with other markup: inserting at the start of the
        // next line would place the copy outside the parent (a one-line document has
        // its closing tag on that next line). Sit right after the node instead.
        out.edit.start = nodeEnd;
        out.edit.insertText = std::string(xml);
        out.edit.removeLen = 0;
        return out;
    }

    std::string nl = "\n";
    uint32_t after = lineEnd;
    if (after + 1 < doc.length && doc.data[after] == '\r' && doc.data[after + 1] == '\n') {
        nl = "\r\n";
        after += 2;
    } else if (after < doc.length) {
        if (doc.data[after] == '\r') nl = "\r";
        after += 1;
    }

    if (after > lineEnd) {
        // Node alone on its line: the copy takes a whole line of its own, indented
        // like the node, and the line that followed keeps its own indentation.
        out.edit.start = after;
        out.edit.insertText = indent + std::string(xml) + nl;
    } else {
        // Last line, no trailing newline: open one for the copy.
        out.edit.start = nodeEnd;
        out.edit.insertText = nl + indent + std::string(xml);
    }
    out.edit.removeLen = 0;
    return out;
}

bool isValidXmlName(std::string_view name)
{
    if (name.empty()) return false;

    const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    const auto isLetter = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || static_cast<unsigned char>(c) >= 0x80;
    };

    if (!isLetter(name[0]) && name[0] != '_') return false;
    for (char c : name) {
        if (isLetter(c) || isDigit(c) || c == '-' || c == '_' || c == '.' || c == ':') continue;
        return false;
    }
    return true;
}

RenamePlan planRename(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                      std::string_view newName)
{
    (void)idx;
    RenamePlan out;
    if (!isValidXmlName(newName)) {
        out.status = OpStatus::NotApplicable;
        return out;
    }
    out.status = verify(doc, node);
    if (out.status != OpStatus::Ok) return out;

    out.open.start = node.nameOff;
    out.open.removeLen = node.nameLen;
    out.open.insertText = std::string(newName);

    const uint32_t end = elementEndFromText(doc, node.start);
    if (end > node.start && end > static_cast<uint32_t>(node.nameLen) + 1 &&
        doc.data[end - 2] != '/') {
        // `</name>`: '<' '/' name '>'
        out.close.start = end - node.nameLen - 1;
        out.close.removeLen = node.nameLen;
        out.close.insertText = std::string(newName);
        out.hasClose = true;
    }
    return out;
}

PlannedEdit planAddChild(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                         std::string_view name)
{
    (void)idx;
    PlannedEdit out;
    if (!isValidXmlName(name)) {
        out.status = OpStatus::NotApplicable;
        return out;
    }
    out.status = verify(doc, node);
    if (out.status != OpStatus::Ok) return out;

    const uint32_t openEnd = tagEnd(doc, node.start);
    if (openEnd == 0) {
        out.status = OpStatus::Stale;
        return out;
    }

    // Indentation of the element's own line, plus two spaces for the child.
    uint32_t lineStart = node.start;
    while (lineStart > 0 && doc.data[lineStart - 1] != '\n') --lineStart;
    uint32_t indentEnd = lineStart;
    while (indentEnd < node.start && isBlank(doc.data[indentEnd])) ++indentEnd;
    const std::string indent(doc.data + lineStart, indentEnd - lineStart);
    const std::string childIndent = indent + "  ";

    std::string nl = "\n";
    if (node.start > 0 && doc.data[node.start - 1] == '\r') nl = "\r\n";

    const std::string tag = "<" + std::string(name) + "/>";

    const bool selfClosing = doc.data[openEnd - 2] == '/';
    if (selfClosing) {
        // `<a/>` becomes `<a>` + child + `</a>`, keeping the indentation.
        const std::string tagName(doc.data + node.nameOff, node.nameLen);
        out.edit.start = openEnd - 2;
        out.edit.removeLen = 2;
        out.edit.insertText = ">" + nl + childIndent + tag + nl + indent + "</" + tagName + ">";
        return out;
    }

    const uint32_t end = elementEndFromText(doc, node.start);
    if (end <= node.start) {
        out.status = OpStatus::Stale;
        return out;
    }
    const uint32_t closeStart = end - node.nameLen - 3;

    out.edit.start = closeStart;
    out.edit.removeLen = 0;
    // On a line of its own, unless there is text just before the closing tag.
    const bool lineStartBefore = closeStart > openEnd && doc.data[closeStart - 1] == '\n';
    out.edit.insertText = (lineStartBefore ? std::string() : nl) + childIndent + tag + nl;
    return out;
}

PlannedEdit planSetAttribute(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                             std::string_view name, std::string_view value)
{
    (void)idx;
    PlannedEdit out;
    if (!isValidXmlName(name)) {
        out.status = OpStatus::NotApplicable;
        return out;
    }
    out.status = verify(doc, node);
    if (out.status != OpStatus::Ok) return out;

    // Walk the attributes of this tag looking for the name.
    XmlScanner scanner(DocumentView{doc.data + node.start, doc.length - node.start});
    ScanEvent ev;
    if (!scanner.next(ev) || ev.type != ScanEvent::Type::ElementStart) {
        out.status = OpStatus::Stale;
        return out;
    }
    while (scanner.next(ev) && ev.type == ScanEvent::Type::Attribute) {
        const std::string attrName(doc.data + node.start + ev.nameOff, ev.nameLen);
        if (attrName == name) {
            out.edit.start = node.start + ev.valueOff;
            out.edit.removeLen = ev.valueLen;
            out.edit.insertText = std::string(value);
            return out;
        }
    }

    const uint32_t openEnd = tagEnd(doc, node.start);
    if (openEnd == 0) {
        out.status = OpStatus::Stale;
        return out;
    }
    const bool selfClosing = doc.data[openEnd - 2] == '/';
    out.edit.start = openEnd - (selfClosing ? 2u : 1u);
    out.edit.removeLen = 0;
    out.edit.insertText = " " + std::string(name) + "=\"" + std::string(value) + "\"";
    return out;
}

PlannedEdit planSetAttributeValue(DocumentView doc, const NodeRef& attr, std::string_view value)
{
    PlannedEdit out;
    out.status = verify(doc, attr);
    if (out.status != OpStatus::Ok) return out;

    out.edit.start = attr.valueOff;
    out.edit.removeLen = attr.valueLen;
    out.edit.insertText = std::string(value);
    return out;
}

std::string copyElement(DocumentView doc, const NodeRef& node)
{
    if (!node.found() || node.kind != NodeKind::Element) return std::string();
    const uint32_t end = elementEndFromText(doc, node.start);
    if (end <= node.start) return std::string();
    return std::string(doc.data + node.start, end - node.start);
}

std::string copyData(DocumentView doc, const NodeRef& node)
{
    if (!node.found() || node.kind != NodeKind::Element) return std::string();

    const uint32_t end = elementEndFromText(doc, node.start);
    if (end <= node.start) return std::string();

    const uint32_t openEnd = tagEnd(doc, node.start);
    if (openEnd == 0 || openEnd > end) return std::string();
    if (openEnd >= 2 && doc.data[openEnd - 2] == '/') return std::string(); // self-closing

    // The closing tag is `</name>`: nameLen + 3 bytes right before `end`.
    const uint32_t close = end - node.nameLen - 3;
    if (close <= openEnd) return std::string();
    return std::string(doc.data + openEnd, close - openEnd);
}

namespace {

// The value a reporting command reads from an element: its `value` attribute when it
// has one (that is how these files carry their numbers), its trimmed text otherwise.
std::string firstValueOf(DocumentView doc, const NodeRef& node)
{
    if (!doc.data || node.end <= node.start) return std::string();

    const DocumentView sub{doc.data + node.start, node.end - node.start};
    XmlScanner scanner(sub);
    ScanEvent ev;
    if (scanner.next(ev) && ev.type == ScanEvent::Type::ElementStart) {
        while (scanner.next(ev) && ev.type == ScanEvent::Type::Attribute) {
            const std::string name(doc.data + node.start + ev.nameOff, ev.nameLen);
            if (name == "value" || name == "Value") {
                return std::string(doc.data + node.start + ev.valueOff, ev.valueLen);
            }
        }
    }

    const uint32_t openEnd = tagEnd(doc, node.start);
    if (openEnd == 0 || openEnd >= node.end) return std::string();
    const uint32_t close = node.end - node.nameLen - 3;
    if (close <= openEnd) return std::string();

    uint32_t b = openEnd;
    uint32_t e = close;
    while (b < e && isSpace(doc.data[b])) ++b;
    while (e > b && isSpace(doc.data[e - 1])) --e;
    return std::string(doc.data + b, e - b);
}

} // namespace

std::vector<std::string> collectValues(DocumentView doc, const ElementIndex& idx,
                                       const NodeRef& node)
{
    std::vector<std::string> out;
    if (!doc.data || !node.found() || node.kind != NodeKind::Element) return out;

    const size_t self = idx.findStart(node.start);
    if (self == static_cast<size_t>(-1)) return out;
    const ElementPos& me = idx.at(self);

    // Direct children first.
    for (size_t i = self + 1; i < idx.size(); ++i) {
        const ElementPos& c = idx.at(i);
        if (c.start >= me.end) break;
        if (c.depth <= me.depth) break;
        if (c.depth != me.depth + 1) continue;

        const NodeRef child = nodeAt(doc, idx, c.start, NodeKind::Element);
        if (!child.found()) continue;
        std::string value = firstValueOf(doc, child);
        if (!value.empty()) out.push_back(value);
    }
    if (!out.empty()) return out;

    // A leaf carries nothing under itself: the useful set is every element with the
    // same name in the document -- the "sum this column" case, where the field is
    // repeated under each of the many sections.
    for (size_t i = 0; i < idx.size(); ++i) {
        const ElementPos& c = idx.at(i);
        if (c.nameLen != me.nameLen) continue;
        if (std::memcmp(doc.data + c.nameOff, doc.data + me.nameOff, me.nameLen) != 0) continue;

        const NodeRef sibling = nodeAt(doc, idx, c.start, NodeKind::Element);
        if (!sibling.found()) continue;
        std::string value = firstValueOf(doc, sibling);
        if (!value.empty()) out.push_back(value);
    }
    return out;
}

SumResult sumValues(const std::vector<std::string>& values)
{
    SumResult r;
    r.values = values.size();
    for (const std::string& v : values) {
        const char* begin = v.c_str();
        char* end = nullptr;
        const double d = std::strtod(begin, &end);
        if (end != begin && *end == '\0') {
            r.sum += d;
            ++r.numbers;
        }
    }
    return r;
}

std::vector<std::string> uniqueValues(const std::vector<std::string>& values)
{
    std::vector<std::string> out;
    for (const std::string& v : values) {
        bool seen = false;
        for (const std::string& kept : out) {
            if (kept == v) {
                seen = true;
                break;
            }
        }
        if (!seen) out.push_back(v);
    }
    return out;
}

std::string getPath(DocumentView doc, const ElementIndex& idx, const NodeRef& node,
                    bool withPredicates)
{
    if (!node.found() || node.kind != NodeKind::Element) return std::string();

    const size_t i = idx.findStart(node.start);
    if (i == static_cast<size_t>(-1)) return std::string();

    // Ancestors are those entries with a smaller depth whose range contains the
    // node's start; the index is in document order, so walking back finds them.
    std::vector<size_t> chain;
    chain.push_back(i);
    uint16_t want = idx.at(i).depth;
    for (size_t k = i; k-- > 0 && want > 0;) {
        const ElementPos& e = idx.at(k);
        if (e.depth + 1 == want && e.start <= node.start && node.start < e.end) {
            chain.push_back(k);
            want = e.depth;
        }
    }

    std::string path;
    for (size_t k = chain.size(); k-- > 0;) {
        const ElementPos& e = idx.at(chain[k]);
        if (e.nameLen == 0 || e.nameOff + e.nameLen > doc.length) return std::string();
        path += '/';
        path.append(doc.data + e.nameOff, e.nameLen);
        if (withPredicates) path += firstPredicate(doc, e);
    }
    return path;
}

PlannedEdit planRemoveAttribute(DocumentView doc, const NodeRef& attr)
{
    PlannedEdit out;
    out.status = verify(doc, attr);
    if (out.status != OpStatus::Ok) return out;

    // The whitespace that belongs with the attribute is everything between the end
    // of the previous token and the attribute name. Stopping at the first
    // non-blank cannot touch the previous attribute or the element name.
    uint32_t start = attr.start;
    while (start > 0 && isSpace(doc.data[start - 1])) --start;

    // Trailing blanks go too, but only when the tag ends right after them: between
    // two attributes they are the next attribute's separator.
    uint32_t end = attr.end;
    uint32_t k = end;
    while (k < doc.length && isSpace(doc.data[k])) ++k;
    const bool tagEnds = k < doc.length &&
                         (doc.data[k] == '>' ||
                          (doc.data[k] == '/' && k + 1 < doc.length && doc.data[k + 1] == '>'));
    if (tagEnds) end = k;

    out.edit.start = start;
    out.edit.removeLen = end - start;
    out.edit.insertText.clear();
    return out;
}

std::string copyAttributeValue(DocumentView doc, const NodeRef& attr)
{
    if (attr.kind != NodeKind::Attribute || attr.valueLen == 0) return std::string();
    if (attr.valueOff >= doc.length || attr.valueOff + attr.valueLen > doc.length) {
        return std::string();
    }
    return std::string(doc.data + attr.valueOff, attr.valueLen);
}
