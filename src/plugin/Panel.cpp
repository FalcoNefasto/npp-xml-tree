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
#include "Panel.h"

#include "Docking.h"
#include "menuCmdID.h"
#include "PromptDialog.h"
#include "PluginDefinition.h"
#include "ResultDialog.h"
#include "ScintillaBridge.h"
#include "charref.h"
#include "docdetect.h"
#include "encoding.h"
#include "filter.h"
#include "ops.h"

#include <string>

namespace {

// Shown when the panel has no list that matches the text in front of the user.
const wchar_t kNotChecked[] =
    L"This document has not been checked yet.\r\n\r\n"
    L"Run Tools > Check well-formedness.";

std::wstring widen(const std::string& utf8)
{
    if (utf8.empty()) return std::wstring();
    const int chars = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                         nullptr, 0);
    std::wstring out(static_cast<size_t>(chars), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), chars);
    return out;
}

std::string narrow(const std::wstring& wide)
{
    if (wide.empty()) return std::string();
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), bytes,
                          nullptr, nullptr);
    return out;
}

const wchar_t* kPanelClass = L"NppXmlTreePanel";
const wchar_t* kPanelTitle = L"XML Tree";

// Per tick: about four megabytes of document, well under the 100 ms budget.
constexpr uint32_t kScanBudget = 4u * 1024u * 1024u;
constexpr UINT kScanTickMs = 30;
constexpr UINT kSyncDelayMs = 150;
constexpr UINT kFilterDelayMs = 200;
constexpr UINT kReparseDelayMs = 500;

// Same order and names as the tree menu in FOXE, then the extras that only make sense
// here. There is no "Remove line": Remove already takes the line when the element is
// alone on it.
enum MenuCommand {
    cmdLocate = 1,
    cmdDelete,
    cmdCut,
    cmdCopy,
    cmdPaste,
    cmdPasteAfter,
    cmdDuplicate,
    cmdPath,
    cmdCopyData,
    cmdCopyAttributeValue,
    cmdSumValues,
    cmdUniqueValues,
    cmdRename,
    cmdAddChild,
    cmdAddAttribute,
    cmdEditValue,
};

} // namespace

bool Panel::registerClass()
{
    static bool done = false;
    if (done) return true;

    WNDCLASSEX wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Panel::wndProc;
    wc.hInstance = pluginInstance();
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kPanelClass;

    done = RegisterClassEx(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return done;
}

LRESULT CALLBACK Panel::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Panel* self = reinterpret_cast<Panel*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));

    if (self && msg == WM_NOTIFY) {
        auto* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr->idFrom == IDC_XMLTREE) {
            if (hdr->code == TVN_SELCHANGEDW) self->onTreeSelectionChanged();
            if (hdr->code == NM_DBLCLK) {
                self->onTreeDoubleClick();
                return 0;
            }
            if (hdr->code == NM_RCLICK) {
                self->onTreeRightClick();
                return 0;
            }
            LRESULT result = 0;
            if (self->tree_.handleNotify(hdr, &result)) return result;
        }
    }

    switch (msg) {
    case WM_SIZE:
        if (self) self->layout();
        return 0;
    case WM_TIMER:
        if (self && wParam == IDT_SCAN) {
            self->onScanTick();
            return 0;
        }
        if (self && wParam == IDT_SYNC) {
            ::KillTimer(hwnd, IDT_SYNC);
            self->syncFromCaret();
            return 0;
        }
        if (self && wParam == IDT_FILTER) {
            ::KillTimer(hwnd, IDT_FILTER);
            self->runFilter(false);
            return 0;
        }
        if (self && wParam == IDT_REPARSE) {
            ::KillTimer(hwnd, IDT_REPARSE);
            self->reparse();
            return 0;
        }
        return 0;
    case WM_NCDESTROY:
        if (self) self->tree_.destroy();
        break;
    case WM_COMMAND:
        if (self && LOWORD(wParam) == IDC_FILTER && HIWORD(wParam) == EN_CHANGE) {
            ::SetTimer(hwnd, IDT_FILTER, kFilterDelayMs, nullptr);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool Panel::create(HWND nppHandle)
{
    if (hwnd_) return true;
    if (!registerClass()) {
        return false;
    }

    hwnd_ = CreateWindowEx(0, kPanelClass, kPanelTitle, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                           0, 0, 240, 400, nppHandle, nullptr, pluginInstance(), nullptr);
    if (!hwnd_) {
        return false;
    }

    ::SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    banner_ = CreateWindowEx(0, L"STATIC", L"", WS_CHILD | SS_LEFT | SS_NOPREFIX, 8, 8, 100, 60, hwnd_,
                             reinterpret_cast<HMENU>(IDC_BANNER), pluginInstance(), nullptr);
    if (!banner_) return false;

    filter_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 24, hwnd_,
                              reinterpret_cast<HMENU>(IDC_FILTER), pluginInstance(), nullptr);
    if (!filter_) return false;
    ::SendMessageW(filter_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"find element\u2026"));
    ::SetWindowSubclass(filter_, &Panel::filterProc, 1, reinterpret_cast<DWORD_PTR>(this));

    if (!tree_.create(hwnd_, IDC_XMLTREE)) return false;
    tree_.onRemoveKey = [this](bool /*removeLine*/) {
        // Delete already takes the line when the element is alone on it.
        runCommand(cmdDelete, tree_.selectedOffset(), tree_.selectedKind());
    };
    tree_.onCopyKey = [this](bool cut) {
        runCommand(cut ? cmdCut : cmdCopy, tree_.selectedOffset(), tree_.selectedKind());
    };
    return true;
}

void Panel::layout() const
{
    if (!hwnd_) return;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    const int width = rc.right > 0 ? rc.right : 0;
    const int height = rc.bottom > 0 ? rc.bottom : 0;

    if (banner_ && ::IsWindowVisible(banner_)) {
        ::MoveWindow(banner_, 8, 8, width > 16 ? width - 16 : 0, height > 16 ? height - 16 : 0, TRUE);
        return;
    }

    constexpr int kFilterHeight = 24;
    if (filter_) {
        ::MoveWindow(filter_, 0, 0, width, kFilterHeight, TRUE);
    }
    if (tree_.handle()) {
        const int top = kFilterHeight + 2;
        ::MoveWindow(tree_.handle(), 0, top, width, height > top ? height - top : 0, TRUE);
    }
}

void Panel::registerDocked()
{
    if (!hwnd_) return;

    DockedWidgetData data{};
    data.hClient = hwnd_;
    data.pszName = kPanelTitle;
    data.dlgID = 0;
    data.uMask = DWS_DF_CONT_LEFT;
    data.pszModuleName = PLUGIN_DLL_NAME;
    ::SendMessage(nppData._nppHandle, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&data));
}

void Panel::show(bool visible) const
{
    if (!hwnd_) return;
    ::SendMessage(nppData._nppHandle, visible ? NPPM_DMMSHOW : NPPM_DMMHIDE, 0,
                  reinterpret_cast<LPARAM>(hwnd_));
}

void Panel::showBanner(const wchar_t* text)
{
    if (tree_.handle()) ::ShowWindow(tree_.handle(), SW_HIDE);
    if (!banner_) return;
    ::SetWindowText(banner_, text ? text : L"");
    ::ShowWindow(banner_, SW_SHOW);
    layout();
}

void Panel::showTree()
{
    if (banner_) {
        ::SetWindowText(banner_, L"");
        ::ShowWindow(banner_, SW_HIDE);
    }
    if (tree_.handle()) ::ShowWindow(tree_.handle(), SW_SHOW);
    layout();
}

void Panel::cancelScan()
{
    if (hwnd_) ::KillTimer(hwnd_, IDT_SCAN);
    scanning_ = false;
    job_.reset();
}

void Panel::refreshDuringScan()
{
    // Show what has been indexed so far instead of an empty panel for the whole scan.
    // Rebuilding is not free: it re-creates the visible rows, so it runs at most every
    // 120 ms and never more than 30 times for one document.
    if (index_.size() == 0) return;
    if (progressiveRefreshes_ >= kMaxProgressiveRefreshes) return;

    const DWORD now = ::GetTickCount();
    if (now - lastProgressiveRefresh_ < kProgressiveIntervalMs) return;
    lastProgressiveRefresh_ = now;
    ++progressiveRefreshes_;

    tree_.setSource(doc_, &index_);
    tree_.rebuild();
    layout();
}

void Panel::highlightProblems()
{
    if (indicatorNumber_ < 0) return;
    HWND sci = currentScintilla();
    if (!sci) return;

    const auto length = static_cast<WPARAM>(::SendMessage(sci, SCI_GETLENGTH, 0, 0));
    ::SendMessage(sci, SCI_SETINDICATORCURRENT, static_cast<WPARAM>(indicatorNumber_), 0);
    ::SendMessage(sci, SCI_INDICATORCLEARRANGE, 0, static_cast<LPARAM>(length));

    for (const XmlProblem& problem : problems_) {
        if (problem.length == 0) continue;
        ::SendMessage(sci, SCI_INDICATORFILLRANGE, static_cast<WPARAM>(problem.offset),
                      static_cast<LPARAM>(problem.length));
    }
}

void Panel::clearProblemHighlights()
{
    if (indicatorNumber_ < 0) return;
    HWND sci = currentScintilla();
    if (!sci) return;

    ::SendMessage(sci, SCI_SETINDICATORCURRENT, static_cast<WPARAM>(indicatorNumber_), 0);
    const auto length = static_cast<WPARAM>(::SendMessage(sci, SCI_GETLENGTH, 0, 0));
    ::SendMessage(sci, SCI_INDICATORCLEARRANGE, 0, static_cast<LPARAM>(length));
}

void Panel::markProblemsStale(const wchar_t* reason)
{
    if (problems_.empty() && !problemsValid_) return;
    problems_.clear();
    problemCursor_ = 0;
    problemsValid_ = false;
    if (problemsDialog_.visible()) {
        problemsDialog_.setProblems({reason ? reason : kNotChecked}, /*navigable=*/false);
    }
}

// One line per problem, exactly the list to work through by hand.
void Panel::updateProblemsPanel()
{
    if (!problemsDialog_.visible()) return;

    std::vector<std::wstring> items;
    items.reserve(problems_.empty() ? 1 : problems_.size());
    for (const XmlProblem& problem : problems_) {
        items.push_back(describeProblem(problem));
    }
    if (items.empty()) {
        items.push_back(problemsValid_ ? std::wstring(L"No problems found. The document is well-formed.")
                                       : std::wstring(kNotChecked));
    }

    if (problemCursor_ >= problems_.size()) problemCursor_ = 0;
    problemsDialog_.setProblems(items, /*navigable=*/problems_.size() > 1);
    if (problems_.empty()) return;

    highlightProblems();
    selectProblem();
}

// Selects the current row and takes the caret there. Separate from the rebuild so that
// Previous/Next do not reset the list (and its scroll) on every click.
void Panel::selectProblem()
{
    if (!problemsDialog_.visible() || problems_.empty()) return;
    if (problemCursor_ >= problems_.size()) problemCursor_ = 0;
    problemsDialog_.select(static_cast<int>(problemCursor_));
    bridge::selectRange(problems_[problemCursor_].offset, problems_[problemCursor_].offset,
                        /*center=*/true);
}

std::wstring Panel::describeProblem(const XmlProblem& problem) const
{
    HWND sci = currentScintilla();
    const long line = sci ? static_cast<long>(::SendMessage(
                                    sci, SCI_LINEFROMPOSITION,
                                    static_cast<WPARAM>(problem.offset), 0))
                          : -1;

    std::wstring text = L"Line ";
    text += std::to_wstring(line + 1);
    text += L": ";
    text += widen(problem.message);
    return text;
}

void Panel::checkForm()
{
    // The pointer is dropped the moment the text changes, on purpose. The command must
    // not turn into a no-op because of a notification handled a moment ago: it reads the
    // buffer again, and the pending realign rebuilds the tree separately.
    DocumentView live = doc_;
    if (!live.data) live = bridge::view();
    if (!live.data) {
        setStatus(L"XML Tree: no document loaded");
        return;
    }

    problems_ = checkWellFormed(live, 200);
    problemCursor_ = 0;
    problemsValid_ = true;

    if (!problemsDialog_.create(nppData._nppHandle)) {
        setStatus(L"XML Tree: cannot open the problems panel");
        return;
    }
    ::SendMessage(nppData._nppHandle, NPPM_MODELESSDIALOG, MODELESSDIALOGADD,
                  reinterpret_cast<LPARAM>(problemsDialog_.handle()));

    problemsDialog_.onSelect = [this](int index) {
        if (index < 0 || static_cast<size_t>(index) >= problems_.size()) return;
        problemCursor_ = static_cast<size_t>(index);
        selectProblem();
    };
    problemsDialog_.onPrevious = [this] { previousProblem(); };
    problemsDialog_.onNext = [this] { nextProblem(); };
    problemsDialog_.onClose = [this] {
        clearProblemHighlights();
        setStatus(L"XML Tree: problems panel closed");
    };
    problemsDialog_.show();

    if (problems_.empty()) {
        setStatus(L"XML Tree: the document is well-formed");
    } else {
        wchar_t note[128];
        std::swprintf(note, 128, L"XML Tree: %zu problem%s", problems_.size(),
                      problems_.size() == 1 ? L"" : L"s");
        setStatus(note);
    }
    updateProblemsPanel();
}

void Panel::nextProblem()
{
    if (problems_.empty()) {
        checkForm();
        return;
    }
    if (problemCursor_ + 1 >= problems_.size()) {
        setStatus(L"XML Tree: that was the last problem");
    }
    problemCursor_ = (problemCursor_ + 1) % problems_.size();
    selectProblem();
}

void Panel::previousProblem()
{
    if (problems_.empty()) {
        checkForm();
        return;
    }
    problemCursor_ = (problemCursor_ == 0) ? problems_.size() - 1 : problemCursor_ - 1;
    selectProblem();
}

void Panel::convertCharRefs(bool toChars)
{
    uint32_t start = 0;
    uint32_t end = 0;
    if (!bridge::selection(start, end)) {
        // Nothing selected in the editor: use the element selected in the tree, which
        // is the natural subject when you are working from the panel.
        const uint32_t offset = tree_.selectedOffset();
        if (offset == TreeView::kNone || !doc_.data) {
            setStatus(L"XML Tree: select some text, or a node in the tree");
            return;
        }
        const NodeRef node = nodeAt(doc_, index_, offset, tree_.selectedKind());
        if (!node.found()) {
            setStatus(L"XML Tree: select some text, or a node in the tree");
            return;
        }
        start = node.start;
        end = node.end;
    }

    const std::string before = bridge::textRange(start, end);
    size_t converted = 0;
    const std::string after =
        toChars ? charRefsToChars(before, &converted) : charsToCharRefs(before, &converted);
    if (converted == 0) {
        setStatus(toChars ? L"XML Tree: no character references in the selection"
                          : L"XML Tree: no characters to convert in the selection");
        return;
    }

    bridge::replaceRange(start, end, after);

    wchar_t text[128];
    std::swprintf(text, 128, L"XML Tree: %zu conversion%s", converted, converted == 1 ? L"" : L"s");
    setStatus(text);
}

ResultTheme Panel::dialogTheme() const
{
    const Theme current = bridge::theme();
    ResultTheme theme;
    theme.background = current.background;
    theme.text = current.text;
    theme.dark = current.dark;
    return theme;
}

bool Panel::selectedNode(NodeRef& out) const
{
    const uint32_t offset = tree_.selectedOffset();
    if (offset == TreeView::kNone || !doc_.data) return false;
    out = nodeAt(doc_, index_, offset, tree_.selectedKind());
    return out.found();
}

void Panel::afterStructuralEdit()
{
    // Any of these can change the shape: one rescan is simpler than tracking which
    // offsets moved, and it keeps the tree and the index in agreement by construction.
    doc_ = bridge::view();
    lastSyncedOffset_ = 0xFFFFFFFFu;
    startScan();
}

void Panel::editRename()
{
    NodeRef node;
    if (!selectedNode(node) || node.kind != NodeKind::Element) {
        setStatus(L"XML Tree: select an element to rename");
        return;
    }

    const std::string current(doc_.data + node.nameOff, node.nameLen);
    std::wstring value = widen(current);
    if (!showPromptDialog(nppData._nppHandle, L"XML Tree: rename element", L"New name:", value,
                          dialogTheme())) {
        return;
    }

    const RenamePlan plan = planRename(doc_, index_, node, narrow(value));
    if (plan.status == OpStatus::NotApplicable) {
        setStatus(L"XML Tree: that is not a valid XML name");
        return;
    }
    if (plan.status != OpStatus::Ok) {
        setStatus(L"XML Tree: the document changed, realigning");
        afterStructuralEdit();
        return;
    }

    std::vector<Edit> edits{plan.open};
    if (plan.hasClose) edits.push_back(plan.close);

    selfEditing_ = true;
    bridge::applyEdits(edits);
    selfEditing_ = false;
    setStatus(L"XML Tree: element renamed");
    afterStructuralEdit();
}

void Panel::editAddChild()
{
    NodeRef node;
    if (!selectedNode(node) || node.kind != NodeKind::Element) {
        setStatus(L"XML Tree: select an element first");
        return;
    }

    std::wstring value = L"item";
    if (!showPromptDialog(nppData._nppHandle, L"XML Tree: add child",
                          L"Name of the new element:", value, dialogTheme())) {
        return;
    }

    const PlannedEdit plan = planAddChild(doc_, index_, node, narrow(value));
    if (plan.status == OpStatus::NotApplicable) {
        setStatus(L"XML Tree: that is not a valid XML name");
        return;
    }
    if (plan.status != OpStatus::Ok) {
        setStatus(L"XML Tree: the document changed, realigning");
        afterStructuralEdit();
        return;
    }

    selfEditing_ = true;
    bridge::applyEdit(plan.edit);
    selfEditing_ = false;
    setStatus(L"XML Tree: child added");
    afterStructuralEdit();
}

void Panel::editAddAttribute()
{
    NodeRef node;
    if (!selectedNode(node) || node.kind != NodeKind::Element) {
        setStatus(L"XML Tree: select an element first");
        return;
    }

    std::wstring value = L"name=\"value\"";
    if (!showPromptDialog(nppData._nppHandle, L"XML Tree: add attribute",
                          L"Write name and value, as name=\"value\":", value, dialogTheme())) {
        return;
    }

    const std::string text = narrow(value);
    const size_t equals = text.find('=');
    if (equals == std::string::npos) {
        setStatus(L"XML Tree: write it as name=\"value\"");
        return;
    }
    std::string name = text.substr(0, equals);
    std::string attrValue = text.substr(equals + 1);
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
    while (!attrValue.empty() && (attrValue.front() == ' ' || attrValue.front() == '\t')) {
        attrValue.erase(0, 1);
    }
    if (attrValue.size() >= 2 && (attrValue.front() == '"' || attrValue.front() == '\'') &&
        attrValue.back() == attrValue.front()) {
        attrValue = attrValue.substr(1, attrValue.size() - 2);
    }

    const PlannedEdit plan = planSetAttribute(doc_, index_, node, name, attrValue);
    if (plan.status == OpStatus::NotApplicable) {
        setStatus(L"XML Tree: that is not a valid attribute name");
        return;
    }
    if (plan.status != OpStatus::Ok) {
        setStatus(L"XML Tree: the document changed, realigning");
        afterStructuralEdit();
        return;
    }

    selfEditing_ = true;
    bridge::applyEdit(plan.edit);
    selfEditing_ = false;
    setStatus(L"XML Tree: attribute written");
    afterStructuralEdit();
}

void Panel::editAttributeValue()
{
    NodeRef node;
    if (!selectedNode(node) || node.kind != NodeKind::Attribute) {
        setStatus(L"XML Tree: select an attribute to edit");
        return;
    }

    const std::string current(doc_.data + node.valueOff, node.valueLen);
    std::wstring value = widen(current);
    if (!showPromptDialog(nppData._nppHandle, L"XML Tree: edit value", L"New value:", value,
                          dialogTheme())) {
        return;
    }

    const PlannedEdit plan = planSetAttributeValue(doc_, node, narrow(value));
    if (plan.status != OpStatus::Ok) {
        setStatus(L"XML Tree: the document changed, realigning");
        afterStructuralEdit();
        return;
    }

    selfEditing_ = true;
    bridge::applyEdit(plan.edit);
    selfEditing_ = false;
    setStatus(L"XML Tree: value changed");
    afterStructuralEdit();
}

void Panel::applyTheme()
{
    tree_.setTheme(bridge::theme());
}

void Panel::runSelected(PanelAction action)
{
    const uint32_t offset = tree_.selectedOffset();
    if (offset == TreeView::kNone) {
        setStatus(L"XML Tree: select a node in the tree first");
        return;
    }

    int command = cmdLocate;
    switch (action) {
    case PanelAction::Locate: command = cmdLocate; break;
    case PanelAction::Delete: command = cmdDelete; break;
    case PanelAction::Cut: command = cmdCut; break;
    case PanelAction::Copy: command = cmdCopy; break;
    case PanelAction::Paste: command = cmdPaste; break;
    case PanelAction::PasteAfter: command = cmdPasteAfter; break;
    case PanelAction::Duplicate: command = cmdDuplicate; break;
    case PanelAction::Path: command = cmdPath; break;
    case PanelAction::Sum: command = cmdSumValues; break;
    case PanelAction::Unique: command = cmdUniqueValues; break;
    }

    runCommand(command, offset, tree_.selectedKind());
}

Panel::DocSlot& Panel::slotFor(UINT_PTR id)
{
    for (DocSlot& slot : slots_) {
        if (slot.id == id) {
            slot.lastUsed = ++useClock_;
            return slot;
        }
    }

    // Three open documents is more than anyone edits at once; the oldest goes.
    constexpr size_t kSlots = 3;
    if (slots_.size() >= kSlots) {
        size_t oldest = 0;
        for (size_t i = 1; i < slots_.size(); ++i) {
            if (slots_[i].lastUsed < slots_[oldest].lastUsed) oldest = i;
        }
        slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(oldest));
    }

    slots_.push_back(DocSlot{});
    slots_.back().id = id;
    slots_.back().lastUsed = ++useClock_;
    return slots_.back();
}

void Panel::storeActive()
{
    if (activeId_ == 0) return;

    if (activeDirty_) {
        // It changed while it was on screen: the index describes an older text, so it
        // is dropped instead of kept.
        doc_ = DocumentView{};
        index_.clear();
        problems_.clear();
        problemsValid_ = false;
        activeDirty_ = false;
        return;
    }
    if (!doc_.data || index_.size() == 0) return;

    DocSlot& slot = slotFor(activeId_);
    slot.doc = doc_;
    slot.length = doc_.length;
    slot.index = std::move(index_);
    slot.problems = std::move(problems_);
}

void Panel::loadDocument(DocumentView doc, const wchar_t* path, bool nppSaysXml)
{
    cancelScan();

    if (!nppSaysXml && !looksLikeXml(doc, path)) {
        // The panel is for XML and nothing else: on any other buffer it goes away
        // rather than sitting there with a message.
        index_.clear();
        matches_.clear();
        tree_.clear();
        show(false);
        return;
    }

    show(true);

    const Encoding enc = detectEncoding(doc.data, doc.length);
    if (enc == Encoding::Utf16Le || enc == Encoding::Utf16Be) {
        index_.clear();
        matches_.clear();
        tree_.clear();
        showBanner(L"UTF-16 is not supported.\n\nConvert the file to UTF-8 "
                   L"(Encoding > Convert to UTF-8) and reload it.");
        return;
    }

    applyTheme();

    // Switching tabs should not re-parse a document we already have.
    const UINT_PTR id = currentBufferId();
    if (id != 0) {
        storeActive();
        DocSlot& target = slotFor(id);
        activeId_ = id;
        activeDirty_ = false;

        if (target.index.size() > 0 && target.doc.data && target.length == doc.length) {
            doc_ = doc;
            index_ = std::move(target.index);
            problems_ = std::move(target.problems);
            // The slot kept the list computed for this very text, so it still applies.
            problemsValid_ = true;
            target.index.clear();
            target.problems.clear();
            target.doc = DocumentView{};
            lastSyncedOffset_ = 0xFFFFFFFFu;
            tree_.setSource(doc_, &index_);
            tree_.rebuild();
            showTree();
            if (!problems_.empty()) {
                wchar_t note[128];
                std::swprintf(note, 128, L"XML Tree: %zu structural problem%s", problems_.size(),
                              problems_.size() == 1 ? L"" : L"s");
                setStatus(note);
            }
            syncFromCaret();
            return;
        }
    }

    doc_ = doc;
    lastSyncedOffset_ = 0xFFFFFFFFu;
    // Another document: the old list is about text that is no longer on screen.
    markProblemsStale(nullptr);
    startScan();
}

void Panel::startScan()
{
    index_.clear();
    progressiveRefreshes_ = 0;
    lastProgressiveRefresh_ = 0;
    matches_.clear();
    matchCursor_ = 0;
    modifiedSinceScan_ = false;
    lastSyncedOffset_ = 0xFFFFFFFFu;
    tree_.clear();
    job_ = std::make_unique<ScanJob>(doc_, index_);
    scanning_ = true;
    showTree();
    updateProgressBanner();
    if (hwnd_) ::SetTimer(hwnd_, IDT_SCAN, kScanTickMs, nullptr);
}

void Panel::updateProgressBanner()
{
    if (!job_ || !scanning_) return;
    // A small document is scanned in a single slice: a banner that lives for 20 ms is
    // just a flash, so it is shown only when there is something to wait for.
    if (doc_.length < 2u * 1024u * 1024u) return;
    const int percent = static_cast<int>(job_->progress() * 100.0f);
    wchar_t text[128];
    std::swprintf(text, 128, L"Parsing\u2026 %d%%", percent);
    ::SetWindowText(banner_, text);
    ::ShowWindow(banner_, SW_SHOW);
    layout();
}

void Panel::onScanTick()
{
    if (!job_ || !hwnd_) {
        cancelScan();
        return;
    }

    if (!job_->step(kScanBudget)) {
        updateProgressBanner();
        refreshDuringScan();
        return;
    }

    ::KillTimer(hwnd_, IDT_SCAN);
    scanning_ = false;
    progressiveRefreshes_ = 0;

    // A scan that ran while the text changed describes a document that is no longer in
    // the editor. Publishing it would leave a tree whose offsets cut the wrong bytes, and
    // the pointer is already dropped: the panel would answer "no document loaded" for the
    // rest of the session. Throw it away and go again.
    if (!doc_.data || modifiedSinceScan_) {
        modifiedSinceScan_ = false;
        doc_ = DocumentView{};
        index_.clear();
        tree_.clear();
        showBanner(L"The document changed.\n\nRe-aligning the tree\u2026");
        ::SetTimer(hwnd_, IDT_REPARSE, kReparseDelayMs, nullptr);
        return;
    }

    if (index_.size() == 0) {
        tree_.clear();
        showBanner(L"No XML elements found.\n\nThe panel needs at least one element; "
                   L"this file has none.");
        return;
    }

    activeDirty_ = false;
    if (activeId_ != 0) {
        DocSlot& slot = slotFor(activeId_);
        slot.length = doc_.length;
    }

    tree_.setSource(doc_, &index_);
    tree_.rebuild();
    showTree();

    // The problem list belongs to the panel, not to the scan: it is refreshed only when it
    // no longer describes this text (a check, or the text changed under it).
    //
    // A full extra pass over a 100 MB document would double the load time for advice
    // nobody asked for yet; past this size the check waits for the menu command.
    constexpr uint32_t kAutoCheckLimit = 8u * 1024u * 1024u;
    if (!problemsValid_) {
        problemCursor_ = 0;
        if (doc_.length <= kAutoCheckLimit) {
            problems_ = checkWellFormed(doc_, 200);
            problemsValid_ = true; // checked, even when the answer is "none"
        } else {
            // Not checked: leave the list empty and invalid so the panel asks for a
            // check instead of claiming the document is well-formed.
            problems_.clear();
        }
        if (!problems_.empty() && !problemsDialog_.visible()) {
            wchar_t note[128];
            std::swprintf(note, 128, L"XML Tree: %zu structural problem%s (Tools > Check well-formedness)",
                          problems_.size(), problems_.size() == 1 ? L"" : L"s");
            setStatus(note);
        }
    }
    if (problemsDialog_.visible()) updateProblemsPanel();

    // Put the tree on the element the caret is already in.
    syncFromCaret();
}

void Panel::reparse()
{
    DocumentView doc = bridge::view();
    doc_ = doc;
    if (doc.data) startScan();
}

void Panel::onCaretMoved()
{
    if (!followCaret_ || !hwnd_ || scanning_ || !doc_.data) return;
    ::SetTimer(hwnd_, IDT_SYNC, kSyncDelayMs, nullptr);
}

void Panel::onDocumentModified(int modificationType)
{
    if (!hwnd_) return;

    // Our own edits already refreshed the pointer and handled the index.
    if (selfEditing_) return;

    const bool content =
        (modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) != 0;
    if (!content) return;

    // Scintilla has invalidated the buffer and we are inside its notification: no
    // SCI_* call here (asking for the character pointer now is what crashes it).
    // Drop the pointer so nothing reads it, blank the rows that came from it, and
    // let the timer refetch and rescan outside the notification.
    activeDirty_ = true;
    doc_ = DocumentView{};
    modifiedSinceScan_ = true;
    tree_.setSource(doc_, &index_);
    // The problem list holds offsets into the text that just changed; from here on a Fix
    // would edit the wrong bytes. Only window text and enabled state here: no SCI_*.
    markProblemsStale(L"The document changed.\r\n\r\n"
                      L"Run Tools > Check well-formedness again.");
    // Scheduled even when a scan is running: that scan is now about the old text, so it
    // is discarded when it ends and this timer re-runs it on the new one.
    showBanner(L"The document changed.\n\nRe-aligning the tree\u2026");
    ::SetTimer(hwnd_, IDT_REPARSE, kReparseDelayMs, nullptr);
}

void Panel::syncFromCaret()
{
    if (!doc_.data || index_.size() == 0 || !tree_.handle()) return;

    // A selection ends *after* its last byte, so a caret sitting at node.end is
    // outside that node. Looking one byte back keeps a clicked row selected instead
    // of bouncing the tree to its parent.
    uint32_t caret = bridge::caret();
    if (caret > 0) --caret;

    const size_t containing = index_.findContaining(caret);
    if (containing == static_cast<size_t>(-1)) return;

    const uint32_t start = index_.at(containing).start;
    if (start == lastSyncedOffset_) return;
    lastSyncedOffset_ = start;

    syncing_ = true;
    tree_.selectElement(start);
    syncing_ = false;
}

void Panel::onTreeSelectionChanged()
{
    // A single click only selects the row. Jumping to the text is the double click's
    // job, the way FOXE behaves: the tree is a navigator, not a scrollbar for the
    // editor's caret.
    if (!doc_.data) return;
    const uint32_t offset = tree_.selectedOffset();
    if (offset == TreeView::kNone) return;
    lastSyncedOffset_ = offset;
}

void Panel::onTreeDoubleClick()
{
    if (!doc_.data) return;

    const uint32_t offset = tree_.selectedOffset();
    if (offset == TreeView::kNone) return;

    const NodeRef node = nodeAt(doc_, index_, offset, tree_.selectedKind());
    if (!node.found()) return;

    syncing_ = true;
    bridge::selectRange(node.start, node.end, /*center=*/true);
    syncing_ = false;

    // Keep the row selected after the editor takes the focus.
    tree_.selectItem(tree_.itemFromOffset(offset));
}

// --- filtering -------------------------------------------------------------

LRESULT CALLBACK Panel::filterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                   DWORD_PTR ref)
{
    auto* self = reinterpret_cast<Panel*>(ref);
    if (msg == WM_KEYDOWN && wParam == VK_RETURN && self) {
        self->runFilter(true);
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && self && self->filter_) {
        ::SetWindowTextW(self->filter_, L"");
        self->runFilter(false);
        return 0;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

void Panel::runFilter(bool next)
{
    if (!doc_.data || !filter_ || index_.size() == 0) return;

    if (next && !matches_.empty()) {
        matchCursor_ = (matchCursor_ + 1) % matches_.size();
    } else {
        wchar_t text[256] = {0};
        ::GetWindowTextW(filter_, text, 256);
        char utf8[1024] = {0};
        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof utf8, nullptr, nullptr);
        const std::string needle = bytes > 1 ? std::string(utf8, static_cast<size_t>(bytes - 1))
                                             : std::string();
        matches_ = filterByName(index_, doc_, needle);
        matchCursor_ = 0;
    }

    wchar_t status[128];
    if (matches_.empty()) {
        std::swprintf(status, 128, L"XML Tree: no match");
    } else {
        std::swprintf(status, 128, L"XML Tree: %zu match%s (%zu/%zu)", matches_.size(),
                      matches_.size() == 1 ? L"" : L"es", matchCursor_ + 1, matches_.size());
    }
    ::SendMessage(nppData._nppHandle, NPPM_SETSTATUSBAR, 1, reinterpret_cast<LPARAM>(status));

    if (matches_.empty() || matches_[matchCursor_] >= index_.size()) return;
    const ElementPos& e = index_.at(matches_[matchCursor_]);
    tree_.selectElement(e.start);
}

// --- editing ---------------------------------------------------------------

void Panel::onTreeRightClick()
{
    POINT screen{};
    ::GetCursorPos(&screen);
    showContextMenuAt(screen);
}

void Panel::showContextMenuAt(POINT screen)
{
    const HTREEITEM item = tree_.itemFromPoint(screen);
    if (!item) return;
    tree_.selectItem(item);

    const LPARAM param = tree_.paramOfItem(item);
    const uint32_t offset = TreeView::offsetOf(param);
    if (offset == TreeView::kNone) return;
    const NodeKind kind = TreeView::kindOf(param);
    const bool isAttribute = kind == NodeKind::Attribute;
    const NodeRef node = nodeAt(doc_, index_, offset, kind);

    // A command that cannot do anything is greyed out, never a silent no-op: an
    // empty "Copy data" would wipe the clipboard, and a paste with no text would
    // insert a blank line for no reason.
    const bool emptyContent = !isAttribute &&
                              (node.end <= node.start || copyData(doc_, node).empty());
    const bool hasClipboardText = ::IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;

    HMENU menu = ::CreatePopupMenu();
    ::AppendMenuW(menu, MF_STRING, cmdLocate, L"Locate\tEnter");
    ::AppendMenuW(menu, MF_STRING, cmdDelete, L"Delete\tDel");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, cmdCut, L"Cut");
    ::AppendMenuW(menu, MF_STRING, cmdCopy, L"Copy");
    ::AppendMenuW(menu, MF_STRING | (isAttribute || !hasClipboardText ? MF_GRAYED : 0), cmdPaste,
                  L"Paste (replace)");
    ::AppendMenuW(menu, MF_STRING | (isAttribute || !hasClipboardText ? MF_GRAYED : 0),
                  cmdPasteAfter, L"Paste After");
    ::AppendMenuW(menu, MF_STRING | (isAttribute ? MF_GRAYED : 0), cmdDuplicate, L"Duplicate");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING | (isAttribute ? MF_GRAYED : 0), cmdRename, L"Rename...");
    ::AppendMenuW(menu, MF_STRING | (isAttribute ? MF_GRAYED : 0), cmdAddChild, L"Add child...");
    ::AppendMenuW(menu, MF_STRING | (isAttribute ? MF_GRAYED : 0), cmdAddAttribute,
                  L"Add attribute...");
    ::AppendMenuW(menu, MF_STRING | (isAttribute ? 0 : MF_GRAYED), cmdEditValue, L"Edit value...");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING | (isAttribute ? MF_GRAYED : 0), cmdPath, L"Path");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING | (emptyContent ? MF_GRAYED : 0), cmdCopyData, L"Copy data");
    ::AppendMenuW(menu, MF_STRING | (isAttribute ? 0 : MF_GRAYED), cmdCopyAttributeValue,
                  L"Copy attribute value");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, cmdSumValues, L"Sum values");
    ::AppendMenuW(menu, MF_STRING, cmdUniqueValues, L"Unique values");

    const UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON;
    ::SetForegroundWindow(hwnd_);
    const int chosen = static_cast<int>(
        ::TrackPopupMenu(menu, flags, screen.x, screen.y, 0, hwnd_, nullptr));
    ::DestroyMenu(menu);
    ::PostMessage(hwnd_, WM_NULL, 0, 0);

    if (chosen > 0) runCommand(chosen, offset, kind);
}

void Panel::runCommand(int command, uint32_t offset, NodeKind kind)
{
    if (!doc_.data) return;

    const NodeRef node = nodeAt(doc_, index_, offset, kind);
    if (!node.found()) return;

    const std::string clipboard = getClipboard();

    if (command == cmdLocate) {
        syncing_ = true;
        bridge::selectRange(node.start, node.end, /*center=*/true);
        syncing_ = false;
        tree_.selectItem(tree_.itemFromOffset(offset));
        return;
    }
    if (command == cmdCopyAttributeValue) {
        if (setClipboard(copyAttributeValue(doc_, node))) setStatus(L"XML Tree: attribute value copied");
        else setStatus(L"XML Tree: the clipboard is busy, nothing copied");
        return;
    }
    if (command == cmdPath) {
        if (setClipboard(getPath(doc_, index_, node, true))) setStatus(L"XML Tree: path copied");
        else setStatus(L"XML Tree: the clipboard is busy, nothing copied");
        return;
    }
    if (command == cmdSumValues) {
        reportSum(node);
        return;
    }
    if (command == cmdUniqueValues) {
        reportUnique(node);
        return;
    }

    if (kind == NodeKind::Attribute) {
        if (command == cmdCopy) {
            setClipboard(std::string(doc_.data + node.start, node.end - node.start));
            return;
        }
        if (command == cmdEditValue) {
            editAttributeValue();
            return;
        }
        if (command == cmdCut && !setClipboard(std::string(doc_.data + node.start, node.end - node.start))) return;
        if (command == cmdDelete || command == cmdCut) {
            applyPlanned(planRemoveAttribute(doc_, node));
        }
        return;
    }

    switch (command) {
    case cmdCopy:
        if (setClipboard(copyElement(doc_, node))) setStatus(L"XML Tree: block copied");
        else setStatus(L"XML Tree: the clipboard is busy, nothing copied");
        break;
    case cmdCopyData:
        setClipboard(copyData(doc_, node));
        break;
    case cmdCut:
        // Copy first; if the clipboard is busy, keep the text rather than losing it.
        if (!setClipboard(copyElement(doc_, node))) return;
        applyPlanned(planRemove(doc_, index_, node, true));
        break;
    case cmdDelete:
        // Removing a block that sits alone on its line takes the line with it, so no
        // indentation-only line is left behind.
        applyPlanned(planRemove(doc_, index_, node, true));
        break;
    case cmdDuplicate:
        applyPlanned(planDuplicate(doc_, index_, node));
        break;
    case cmdPaste:
        applyPlanned(planPaste(doc_, index_, node, clipboard));
        break;
    case cmdRename:
        editRename();
        break;
    case cmdAddChild:
        editAddChild();
        break;
    case cmdAddAttribute:
        editAddAttribute();
        break;
    case cmdEditValue:
        editAttributeValue();
        break;
    case cmdPasteAfter:
        applyPlanned(planPasteAfter(doc_, index_, node, clipboard));
        break;
    default:
        break;
    }
}

void Panel::applyPlanned(const PlannedEdit& planned)
{
    if (planned.status == OpStatus::Stale) {
        doc_ = bridge::view(); // never rescan a pointer Scintilla already dropped
        showBanner(L"The document changed since the tree was built.\n\nRealigning\u2026");
        startScan();
        return;
    }
    if (planned.status != OpStatus::Ok) return;

    selfEditing_ = true;
    bridge::applyEdit(planned.edit);
    selfEditing_ = false;

    // The buffer moved: refetch the pointer before anything reads it again.
    doc_ = bridge::view();


    if (!planned.edit.insertText.empty()) {
        // Inserted markup is not an offset shift: the index cannot learn about new
        // elements by moving numbers, so the document is rescanned in slices.
        startScan();
        return;
    }

    index_.applyEdit(planned.edit.start, planned.edit.removeLen, 0);
    tree_.setSource(doc_, &index_);
    tree_.rebuild();
    showTree();
    lastSyncedOffset_ = 0;
    syncFromCaret();
}

void Panel::setStatus(const wchar_t* text) const
{
    ::SendMessage(nppData._nppHandle, NPPM_SETSTATUSBAR, 1, reinterpret_cast<LPARAM>(text));
}

void Panel::reportSum(const NodeRef& node)
{
    const auto values = collectValues(doc_, index_, node);
    const SumResult sum = sumValues(values);

    wchar_t headline[256];
    if (sum.values == 0) {
        std::swprintf(headline, 256, L"No values here to add up.");
    } else if (sum.numbers == 0) {
        std::swprintf(headline, 256, L"%zu values, none of them numeric.", sum.values);
    } else if (sum.numbers < sum.values) {
        std::swprintf(headline, 256, L"Sum %g over %zu values (%zu not numeric).", sum.sum,
                      sum.numbers, sum.values - sum.numbers);
    } else {
        std::swprintf(headline, 256, L"Sum %g over %zu values.", sum.sum, sum.numbers);
    }

    setStatus(headline);

    std::wstring body = headline;
    body += L"\r\n\r\nValues read, in document order:\r\n";
    const size_t shown = values.size() < 200 ? values.size() : 200;
    for (size_t i = 0; i < shown; ++i) {
        body += L"  ";
        body += widen(values[i]);
        body += L"\r\n";
    }
    if (values.size() > shown) {
        body += L"  ... and ";
        body += std::to_wstring(values.size() - shown);
        body += L" more\r\n";
    }

    const Theme current = bridge::theme();
    ResultTheme dialogTheme;
    dialogTheme.background = current.background;
    dialogTheme.text = current.text;
    dialogTheme.dark = current.dark;
    showResultDialog(nppData._nppHandle, L"XML Tree: sum values", headline, body, dialogTheme);
}

void Panel::reportUnique(const NodeRef& node)
{
    const auto values = collectValues(doc_, index_, node);
    const auto unique = uniqueValues(values);
    if (unique.empty()) {
        setStatus(L"XML Tree: no values here to list");
        return;
    }

    std::string out;
    out.reserve(unique.size() * 16);
    for (const std::string& v : unique) {
        out += v;
        out += '\n';
    }

    // A list needs to be scrollable and copyable: it goes into a new document.
    ::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW);
    HWND sci = currentScintilla();
    if (!sci) return;
    ::SendMessage(sci, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(out.c_str()));
    ::SendMessage(sci, SCI_SETSAVEPOINT, 0, 0);
    ::SendMessage(sci, SCI_GOTOPOS, 0, 0);
}

bool Panel::setClipboard(const std::string& utf8)
{
    // Another process can hold the clipboard for a moment (the editor itself does
    // after a copy). Retrying briefly is the documented way to win it.
    bool opened = false;
    for (int attempt = 0; attempt < 20 && !opened; ++attempt) {
        opened = ::OpenClipboard(hwnd_) != FALSE;
        if (!opened) ::Sleep(10);
    }
    if (!opened) return false;

    ::EmptyClipboard();

    bool ok = false;
    const int chars = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                            nullptr, 0);
    HGLOBAL handle = ::GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(chars + 1) * sizeof(wchar_t));
    if (handle) {
        auto* text = static_cast<wchar_t*>(::GlobalLock(handle));
        if (text) {
            ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), text, chars);
            text[chars] = L'\0';
            ::GlobalUnlock(handle);
            ok = ::SetClipboardData(CF_UNICODETEXT, handle) != nullptr;
        }
        if (!ok) ::GlobalFree(handle);
    }
    ::CloseClipboard();
    return ok;
}

std::string Panel::getClipboard()
{
    std::string out;
    if (!::IsClipboardFormatAvailable(CF_UNICODETEXT)) return out;
    if (!::OpenClipboard(hwnd_)) return out;

    HANDLE handle = ::GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        const auto* text = static_cast<const wchar_t*>(::GlobalLock(handle));
        if (text) {
            const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
            if (bytes > 1) {
                // The second call writes `bytes` including the terminator: size the
                // buffer for it, then drop the terminator.
                out.resize(static_cast<size_t>(bytes));
                ::WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
                out.pop_back();
            }
            ::GlobalUnlock(handle);
        }
    }
    ::CloseClipboard();
    return out;
}
