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
#include "PluginDefinition.h"

#include "ProblemsDialog.h"
#include "ResultDialog.h"
#include "TreeView.h"
#include "document.h"
#include "index.h"
#include "ops.h"
#include "wellformed.h"
#include "scanjob.h"

#include <memory>
#include <string>
#include <vector>

#define IDC_XMLTREE 2001
#define IDC_BANNER 2002
#define IDC_FILTER 2003

// The commands a plugin menu item (and therefore a user-assigned shortcut) can invoke
// on the row selected in the tree.
enum class PanelAction {
    Locate,
    Delete,
    Cut,
    Copy,
    Paste,
    PasteAfter,
    Duplicate,
    Path,
    Sum,
    Unique,
};

#define IDT_SCAN 1
#define IDT_SYNC 2
#define IDT_FILTER 3
#define IDT_REPARSE 4

// A plain Win32 child window registered with Notepad++'s docking manager.
// Deliberately not DockingDlgInterface/StaticDialog: those bring a dialog resource
// template and their own .cpp, and this panel is a single window with two children.
class Panel {
public:
    bool create(HWND nppHandle);
    void registerDocked();
    void show(bool visible) const;

    // Re-reads the colours from the active document so the tree follows the theme.
    void applyTheme();

    // Runs one of the tree commands on the selected row. Used by the plugin menu, so
    // the same operations are reachable from Settings > Shortcut Mapper.
    void runSelected(PanelAction action);

    // Tools: well-formedness and character references.
    void checkForm();
    void nextProblem();
    void previousProblem();
    void updateProblemsPanel();
    // Selects the current row of the list and takes the caret there.
    void selectProblem();
    void highlightProblems();
    void clearProblemHighlights();
    // Drops a problem list that no longer describes the text it was computed from.
    // Callers must not touch Scintilla: this also runs inside SCN_MODIFIED.
    void markProblemsStale(const wchar_t* reason);
    // One row of the list: line number and what is wrong there.
    std::wstring describeProblem(const XmlProblem& problem) const;
    void convertCharRefs(bool toChars);

    // Tree editing: the four operations that change the text from a node.
    void editRename();
    void editAddChild();
    void editAddAttribute();
    void editAttributeValue();

    // Text -> tree selection. Off by default: a click in the editor should not move
    // the tree's selection, the way FOXE behaves.
    void setFollowCaret(bool follow) { followCaret_ = follow; }
    bool followCaret() const { return followCaret_; }
    HWND handle() const { return hwnd_; }

    // Decides what the panel shows for the active buffer: the tree, or a message
    // explaining why there is no tree.
    void loadDocument(DocumentView doc, const wchar_t* path, bool nppSaysXml = false);

    // Called on SCN_UPDATEUI: debounced, then the tree follows the caret.
    void onCaretMoved();

    // SCN_MODIFIED: refresh the buffer pointer and, after a pause, rebuild the index.
    void onDocumentModified(int modificationType);

    // Reparses the active document (menu command).
    void reparse();

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    static bool registerClass();

    void layout() const;
    void showBanner(const wchar_t* text);
    void showTree();
    void startScan();
    void cancelScan();
    void onScanTick();

    // One parsed document, kept per buffer so switching tabs does not re-parse.
    struct DocSlot {
        UINT_PTR id = 0;
        DocumentView doc{};
        ElementIndex index;
        std::vector<XmlProblem> problems;
        uint32_t length = 0;
        uint64_t lastUsed = 0;
    };
    DocSlot& slotFor(UINT_PTR id);
    void storeActive(); // moves the active document into its slot
    void syncFromCaret();
    void onTreeSelectionChanged();
    void onTreeDoubleClick();
    void updateProgressBanner();
    void refreshDuringScan();
    void onTreeRightClick();
    void showContextMenuAt(POINT screen);
    void runCommand(int command, uint32_t offset, NodeKind kind);
    void applyPlanned(const PlannedEdit& planned);
    void runFilter(bool next);
    bool setClipboard(const std::string& utf8);
    std::string getClipboard();
    void setStatus(const wchar_t* text) const;
    void reportSum(const NodeRef& node);
    void reportUnique(const NodeRef& node);
    bool selectedNode(NodeRef& out) const;
    ResultTheme dialogTheme() const;
    void afterStructuralEdit();

    HWND hwnd_ = nullptr;
    HWND banner_ = nullptr;
    HWND filter_ = nullptr;
    std::vector<size_t> matches_;
    ProblemsDialog problemsDialog_;
    std::vector<XmlProblem> problems_;
    // True while `problems_` describes the text as it is now: a typed edit invalidates it,
    // and a list of offsets into the old text would point at the wrong lines.
    bool problemsValid_ = false;
    size_t problemCursor_ = 0;
    size_t matchCursor_ = 0;
    static LRESULT CALLBACK filterProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    TreeView tree_;

    DocumentView doc_{};
    ElementIndex index_;
    std::unique_ptr<ScanJob> job_;
    bool scanning_ = false;

    // Guards the two directions of the caret/tree sync from feeding each other.
    bool syncing_ = false;
    bool selfEditing_ = false;
    bool modifiedSinceScan_ = false;
    bool activeDirty_ = false;
    static constexpr int kMaxProgressiveRefreshes = 30;
    static constexpr DWORD kProgressiveIntervalMs = 120;
    int indicatorNumber_ = -1;
    int progressiveRefreshes_ = 0;
    DWORD lastProgressiveRefresh_ = 0;
    UINT_PTR activeId_ = 0;
    uint64_t useClock_ = 0;
    std::vector<DocSlot> slots_;
    bool followCaret_ = false;
    uint32_t lastSyncedOffset_ = 0xFFFFFFFFu;
};
