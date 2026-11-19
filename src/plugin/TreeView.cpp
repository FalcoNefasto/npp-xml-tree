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
#include "TreeView.h"
#include "PluginDefinition.h"

#include "scanner.h"

#include <commctrl.h>
#include <string>
#include <vector>

namespace {

constexpr int kIconElement = 0;
constexpr int kIconAttribute = 1;
constexpr int kIconIncomplete = 2;

// A flat document can have a million siblings. Materializing them all freezes the
// TreeView; past this many we show a hint row and let the filter find the rest.
constexpr int kMaxChildren = 1000;

std::wstring widen(const char* data, uint32_t len)
{
    if (!data || len == 0) return std::wstring();
    const int chars = ::MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(len), nullptr, 0);
    std::wstring out(static_cast<size_t>(chars), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(len), out.data(), chars);
    return out;
}

bool isBlank(const char* data, uint32_t from, uint32_t to)
{
    for (uint32_t i = from; i < to; ++i) {
        const char c = data[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    }
    return true;
}

int luminance(COLORREF c)
{
    return (GetRValue(c) + GetGValue(c) + GetBValue(c)) / 3;
}

void drawIcon(HDC dc, int index, COLORREF background)
{
    RECT rc{0, 0, 16, 16};
    if (index == kIconAttribute) {
        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, RGB(150, 40, 40));
        ::DrawTextW(dc, L"a=", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    const COLORREF fill = index == kIconIncomplete ? RGB(170, 170, 170) : RGB(0, 160, 0);
    HBRUSH brush = ::CreateSolidBrush(fill);
    ::FillRect(dc, &rc, brush);
    ::DeleteObject(brush);

    // The marks inside the icon must contrast with the panel, not with the icon: on a
    // light background white lines make the icon look washed out.
    const COLORREF marks = luminance(background) > 128 ? RGB(0, 80, 0) : RGB(255, 255, 255);
    HPEN pen = ::CreatePen(PS_SOLID, 1, marks);
    HGDIOBJ old = ::SelectObject(dc, pen);
    ::MoveToEx(dc, 3, 5, nullptr);
    ::LineTo(dc, 12, 5);
    ::MoveToEx(dc, 3, 8, nullptr);
    ::LineTo(dc, 10, 8);
    ::MoveToEx(dc, 3, 11, nullptr);
    ::LineTo(dc, 12, 11);
    ::SelectObject(dc, old);
    ::DeleteObject(pen);
}

HIMAGELIST buildIcons(COLORREF background)
{
    HIMAGELIST list = ::ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 3, 1);
    if (!list) return nullptr;

    HDC screen = ::GetDC(nullptr);
    HDC dc = ::CreateCompatibleDC(screen);
    ::ReleaseDC(nullptr, screen);

    for (int i = 0; i < 3; ++i) {
        HBITMAP bitmap = ::CreateCompatibleBitmap(::GetDC(nullptr), 16, 16);
        HGDIOBJ old = ::SelectObject(dc, bitmap);
        HBRUSH brush = ::CreateSolidBrush(background);
        RECT rc{0, 0, 16, 16};
        ::FillRect(dc, &rc, brush);
        ::DeleteObject(brush);

        drawIcon(dc, i, background);

        ::SelectObject(dc, old);
        ::ImageList_AddMasked(list, bitmap, background);
        ::DeleteObject(bitmap);
    }
    ::DeleteDC(dc);
    return list;
}

} // namespace

bool TreeView::create(HWND parent, int id)
{
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TREEVIEW_CLASSES};
    ::InitCommonControlsEx(&icc);

    tree_ = ::CreateWindowEx(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                                 TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                             0, 0, 100, 100, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             pluginInstance(), nullptr);
    if (!tree_) return false;

    ::SetWindowSubclass(tree_, &TreeView::subclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    icons_ = buildIcons(theme_.background);
    if (icons_) ::SendMessage(tree_, TVM_SETIMAGELIST, TVSIL_NORMAL, reinterpret_cast<LPARAM>(icons_));

    LOGFONTW lf{};
    ::GetObjectW(reinterpret_cast<HGDIOBJ>(::SendMessage(tree_, WM_GETFONT, 0, 0)), sizeof lf, &lf);
    if (lf.lfHeight == 0) {
        NONCLIENTMETRICSW ncm{sizeof ncm};
        ::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        lf = ncm.lfMessageFont;
    }
    lf.lfWeight = FW_BOLD;
    bold_ = ::CreateFontIndirectW(&lf);
    if (bold_) ::SendMessage(tree_, WM_SETFONT, reinterpret_cast<WPARAM>(bold_), TRUE);

    return true;
}

LRESULT CALLBACK TreeView::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR id, DWORD_PTR ref)
{
    auto* self = reinterpret_cast<TreeView*>(ref);
    if (msg == WM_KEYDOWN && self) {
        const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (wParam == VK_DELETE && self->onRemoveKey) {
            self->onRemoveKey(shift);
            return 0;
        }
        if (ctrl && (wParam == 'C' || wParam == 'X') && self->onCopyKey) {
            self->onCopyKey(wParam == 'X');
            return 0;
        }
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

void TreeView::setSource(DocumentView doc, const ElementIndex* idx)
{
    doc_ = doc;
    index_ = idx;
}

void TreeView::clear()
{
    if (tree_) ::SendMessage(tree_, TVM_DELETEITEM, 0, reinterpret_cast<LPARAM>(TVI_ROOT));
}

void TreeView::rebuild()
{
    clear();
    if (!tree_ || !index_ || !doc_.data) return;

    for (size_t i = 0; i < index_->size(); ++i) {
        const ElementPos& e = index_->at(i);
        if (e.depth != 0) continue;
        insert(packNode(e.start, NodeKind::Element), TVI_ROOT, TVI_LAST, elementHasChildren(e));
    }

    HTREEITEM first = reinterpret_cast<HTREEITEM>(
        ::SendMessage(tree_, TVM_GETNEXTITEM, TVGN_ROOT, 0));
    if (first) {
        ::SendMessage(tree_, TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(first));
    }
}

HTREEITEM TreeView::insert(LPARAM param, HTREEITEM parent, HTREEITEM after, bool hasChildren)
{
    TVINSERTSTRUCTW ins{};
    ins.hParent = parent;
    ins.hInsertAfter = after;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    ins.item.pszText = LPSTR_TEXTCALLBACKW;
    ins.item.iImage = I_IMAGECALLBACK;
    ins.item.iSelectedImage = I_IMAGECALLBACK;
    ins.item.lParam = param;
    ins.item.cChildren = hasChildren ? 1 : 0;
    return reinterpret_cast<HTREEITEM>(::SendMessage(tree_, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&ins)));
}

bool TreeView::hasElementChild(const ElementPos& e) const
{
    const size_t self = index_->findStart(e.start);
    if (self == static_cast<size_t>(-1)) return false;
    const uint32_t end = index_->subtreeEnd(self, doc_.length);
    for (size_t i = self + 1; i < index_->size(); ++i) {
        const ElementPos& c = index_->at(i);
        if (c.start >= end) break;
        if (c.depth <= e.depth) break;
        if (c.depth == e.depth + 1) return true;
    }
    return false;
}

bool TreeView::elementHasChildren(const ElementPos& e) const
{
    if (hasElementChild(e)) return true;

    // An element with only attributes still needs an expander.
    const uint32_t tagEnd = e.nameOff + e.nameLen;
    for (uint32_t i = tagEnd; i < e.end && i < doc_.length; ++i) {
        const char c = doc_.data[i];
        if (c == '"' || c == '\'') {
            const char quote = c;
            for (++i; i < e.end && doc_.data[i] != quote; ++i) {
            }
            continue;
        }
        if (c == '>' || c == '/') return false;
        if (c == '=') return true;
    }
    return false;
}

void TreeView::addChildren(HTREEITEM parent, uint32_t parentStart, uint32_t parentEnd, uint16_t depth)
{
    if (!doc_.data) return;

    // An element that never closed still owns the children that follow it, up to the
    // next element that is not nested inside it.
    const size_t selfIndex = index_->findStart(parentStart);
    if (selfIndex != static_cast<size_t>(-1)) {
        parentEnd = index_->subtreeEnd(selfIndex, doc_.length);
    }
    // Attributes first, then elements: same order FOXE shows them in.
    const DocumentView sub{doc_.data + parentStart, parentEnd > parentStart ? parentEnd - parentStart : 0};
    XmlScanner scanner(sub);
    ScanEvent ev;
    int attributes = 0;

    // The sub-view starts at the element's own '<': that first ElementStart is the
    // parent, not a child. Attributes follow it — including for a self-closing tag,
    // where <a b="1"/> is the common case in XAML, SVG and MSBuild files.
    if (scanner.next(ev) && ev.type == ScanEvent::Type::ElementStart) {
        while (scanner.next(ev) && ev.type == ScanEvent::Type::Attribute) {
            if (++attributes > kMaxChildren) break;
            const uint32_t absolute = parentStart + ev.nameOff;
            insert(packNode(absolute, NodeKind::Attribute), parent, TVI_LAST, false);
        }
    }

    const size_t self = index_->findStart(parentStart);
    if (self == static_cast<size_t>(-1)) return;

    int elements = 0;
    for (size_t i = self + 1; i < index_->size(); ++i) {
        const ElementPos& c = index_->at(i);
        if (c.start >= parentEnd) break;
        if (c.depth <= depth) break;
        if (c.depth != depth + 1) continue;
        if (++elements > kMaxChildren) {
            insert(static_cast<LPARAM>(kHintBit), parent, TVI_LAST, false);
            break;
        }
        insert(packNode(c.start, NodeKind::Element), parent, TVI_LAST, elementHasChildren(c));
    }
}

void TreeView::fillDispInfo(NMTVDISPINFO* info) const
{
    if (!(info->item.mask & TVIF_TEXT) || !info->item.pszText || !index_ || !doc_.data) {
        if (info->item.pszText) info->item.pszText[0] = 0;
        return;
    }

    if (isHint(info->item.lParam)) {
        lstrcpynW(info->item.pszText, L"\u2026 filter to see the rest", info->item.cchTextMax);
        return;
    }

    const uint32_t offset = offsetOf(info->item.lParam);
    const NodeKind kind = kindOf(info->item.lParam);
    const NodeRef node = nodeAt(doc_, *index_, offset, kind);

    std::wstring text;
    if (kind == NodeKind::Attribute) {
        text = widen(doc_.data + node.nameOff, node.nameLen);
        text += L" = ";
        text += widen(doc_.data + node.valueOff, node.valueLen);
    } else {
        text = widen(doc_.data + node.nameOff, node.nameLen);
        const size_t self = index_->findStart(node.start);
        const bool leaf = self == static_cast<size_t>(-1) || !hasElementChild(index_->at(self));
        if (leaf && node.valueLen > 0 && node.valueLen <= 40 &&
            !isBlank(doc_.data, node.valueOff, node.valueOff + node.valueLen)) {
            text += L": ";
            text += widen(doc_.data + node.valueOff, node.valueLen);
        }
    }
    lstrcpynW(info->item.pszText, text.c_str(), info->item.cchTextMax);

    if (info->item.mask & TVIF_IMAGE) {
        info->item.iImage = kind == NodeKind::Attribute ? kIconAttribute
                            : node.end == node.start ? kIconIncomplete
                                                     : kIconElement;
        info->item.iSelectedImage = info->item.iImage;
    }
}

void TreeView::destroy()
{
    if (icons_) {
        ::ImageList_Destroy(icons_);
        icons_ = nullptr;
    }
    if (bold_) {
        ::DeleteObject(bold_);
        bold_ = nullptr;
    }
    tree_ = nullptr;
}

void TreeView::setTheme(const Theme& t)
{
    theme_ = t;
    if (!tree_) return;

    ::SendMessage(tree_, TVM_SETBKCOLOR, 0, static_cast<LPARAM>(theme_.background));
    ::SendMessage(tree_, TVM_SETTEXTCOLOR, 0, static_cast<LPARAM>(theme_.text));

    // The icons are bitmaps with the background baked in, so they are rebuilt.
    HIMAGELIST fresh = buildIcons(theme_.background);
    if (fresh) {
        ::SendMessage(tree_, TVM_SETIMAGELIST, TVSIL_NORMAL, reinterpret_cast<LPARAM>(fresh));
        if (icons_) ::ImageList_Destroy(icons_);
        icons_ = fresh;
    }
    ::InvalidateRect(tree_, nullptr, TRUE);
}

void TreeView::drawItem(NMTVCUSTOMDRAW* cd) const
{
    if (cd->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return;

    const LPARAM param = paramOf(reinterpret_cast<HTREEITEM>(cd->nmcd.dwItemSpec));
    const NodeKind kind = kindOf(param);
    cd->clrText = (kind == NodeKind::Attribute) ? theme_.attribute : theme_.tag;
    cd->clrTextBk = theme_.background;
}

bool TreeView::handleNotify(NMHDR* hdr, LRESULT* result)
{
    switch (hdr->code) {
    case TVN_GETDISPINFOW:
        fillDispInfo(reinterpret_cast<NMTVDISPINFO*>(hdr));
        *result = 0;
        return true;

    case NM_CUSTOMDRAW:
        drawItem(reinterpret_cast<NMTVCUSTOMDRAW*>(hdr));
        *result = CDRF_DODEFAULT;
        return true;

    case TVN_ITEMEXPANDINGW: {
        auto* tv = reinterpret_cast<NMTREEVIEWW*>(hdr);
        if (tv->action == TVE_EXPAND && !isHint(tv->itemNew.lParam) &&
            ::SendMessage(tree_, TVM_GETNEXTITEM, TVGN_CHILD, reinterpret_cast<LPARAM>(tv->itemNew.hItem)) == 0) {
            const uint32_t offset = offsetOf(tv->itemNew.lParam);
            const NodeKind kind = kindOf(tv->itemNew.lParam);
            if (kind == NodeKind::Element && offset != kNone) {
                const size_t self = index_ ? index_->findStart(offset) : static_cast<size_t>(-1);
                if (self != static_cast<size_t>(-1)) {
                    const ElementPos& e = index_->at(self);
                    addChildren(tv->itemNew.hItem, e.start, e.end, e.depth);
                }
            }
        }
        *result = 0;
        return true;
    }

    default:
        return false;
    }
}

uint32_t TreeView::selectedOffset() const
{
    if (!tree_) return kNone;
    const HTREEITEM item = reinterpret_cast<HTREEITEM>(::SendMessage(tree_, TVM_GETNEXTITEM, TVGN_CARET, 0));
    if (!item) return kNone;
    return offsetOf(paramOf(item));
}

NodeKind TreeView::selectedKind() const
{
    if (!tree_) return NodeKind::Element;
    const HTREEITEM item = reinterpret_cast<HTREEITEM>(::SendMessage(tree_, TVM_GETNEXTITEM, TVGN_CARET, 0));
    if (!item) return NodeKind::Element;
    return kindOf(paramOf(item));
}

HTREEITEM TreeView::findItemForOffset(HTREEITEM parent, uint32_t offset, uint32_t rangeEnd)
{
    for (HTREEITEM item = reinterpret_cast<HTREEITEM>(::SendMessage(tree_, TVM_GETNEXTITEM,
                                                                   parent ? TVGN_CHILD : TVGN_ROOT,
                                                                   reinterpret_cast<LPARAM>(parent)));
         item; item = reinterpret_cast<HTREEITEM>(::SendMessage(tree_, TVM_GETNEXTITEM, TVGN_NEXT,
                                                                reinterpret_cast<LPARAM>(item)))) {
        const LPARAM itemParam = paramOf(item);
        if (isHint(itemParam)) continue;
        TVITEMW tv{};
        tv.lParam = itemParam;

        const uint32_t start = offsetOf(tv.lParam);
        if (kindOf(tv.lParam) == NodeKind::Element) {
            if (start == offset) return item;
            if (start < offset) {
                HTREEITEM deeper = findItemForOffset(item, offset, rangeEnd);
                if (deeper) return deeper;
            }
        }
    }
    return nullptr;
}

LPARAM TreeView::paramOf(HTREEITEM item) const
{
    if (!tree_ || !item) return 0;
    TVITEMW tv{};
    tv.mask = TVIF_PARAM;
    tv.hItem = item;
    ::SendMessage(tree_, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&tv));
    return tv.lParam;
}

HTREEITEM TreeView::itemFromPoint(POINT screen) const
{
    if (!tree_) return nullptr;
    POINT client = screen;
    ::ScreenToClient(tree_, &client);
    TVHITTESTINFO hit{};
    hit.pt = client;
    return reinterpret_cast<HTREEITEM>(::SendMessage(tree_, TVM_HITTEST, 0, reinterpret_cast<LPARAM>(&hit)));
}

void TreeView::selectItem(HTREEITEM item)
{
    if (!tree_ || !item) return;
    ::SendMessage(tree_, TVM_SELECTITEM, TVGN_CARET, reinterpret_cast<LPARAM>(item));
    ::SendMessage(tree_, TVM_ENSUREVISIBLE, 0, reinterpret_cast<LPARAM>(item));
}

HTREEITEM TreeView::itemFromOffset(uint32_t offset) const
{
    return const_cast<TreeView*>(this)->findItemForOffset(nullptr, offset, doc_.length);
}

bool TreeView::selectElement(uint32_t offset)
{
    if (!tree_ || !index_) return false;

    const size_t target = index_->findStart(offset);
    if (target == static_cast<size_t>(-1)) return false;

    // Expand the whole ancestor chain from the root outwards: the tree only
    // materializes a branch when it is expanded, so the target's parent must exist
    // before the target can be found.
    const uint16_t depth = index_->at(target).depth;
    std::vector<uint32_t> ancestor(depth, 0);
    std::vector<bool> known(depth, false);
    size_t remaining = depth;
    for (size_t i = target; i-- > 0 && remaining > 0;) {
        const ElementPos& a = index_->at(i);
        if (a.depth >= depth) continue;
        if (a.end <= a.start || offset < a.start || offset >= a.end) continue;
        if (known[a.depth]) continue;
        ancestor[a.depth] = a.start;
        known[a.depth] = true;
        --remaining;
    }
    for (uint16_t d = 0; d < depth; ++d) {
        if (!known[d]) break;
        HTREEITEM item = findItemForOffset(nullptr, ancestor[d], doc_.length);
        if (item) ::SendMessage(tree_, TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(item));
    }

    HTREEITEM item = findItemForOffset(nullptr, offset, doc_.length);
    if (!item) return false;
    ::SendMessage(tree_, TVM_SELECTITEM, TVGN_CARET, reinterpret_cast<LPARAM>(item));
    ::SendMessage(tree_, TVM_ENSUREVISIBLE, 0, reinterpret_cast<LPARAM>(item));
    return true;
}
