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
#include "PluginDefinition.h"

#include "AboutDialog.h"
#include "Panel.h"
#include "ResultDialog.h"
#include "ScintillaBridge.h"
#include "docdetect.h"
#include "encoding.h"

#include <string>

NppData nppData{};

namespace {

Panel gPanel;

enum PluginCommand {
    cmdShowPanel = 0,
    cmdReParse,
    cmdFollowCaret,
    sepPanel,
    cmdLocate,
    cmdDelete,
    cmdCopyBlock,
    cmdCutBlock,
    cmdPasteOver,
    cmdPasteAfter,
    sepEdits,
    cmdSumValues,
    cmdUniqueValues,
    sepReports,
    cmdCheckForm,
    cmdRefsToChars,
    cmdCharsToRefs,
    sepAbout,
    cmdAbout,
    cmdCount
};

FuncItem gFuncs[cmdCount];

void cmdShowPanelClicked()
{
    gPanel.show(true);
}

void cmdReParseClicked()
{
    gPanel.show(true);
    gPanel.reparse();
}

void cmdFollowCaretClicked()
{
    const bool follow = !gPanel.followCaret();
    gPanel.setFollowCaret(follow);
    ::SendMessage(nppData._nppHandle, NPPM_SETMENUITEMCHECK,
                  static_cast<WPARAM>(gFuncs[cmdFollowCaret]._cmdID), follow ? TRUE : FALSE);
}

void cmdAboutClicked()
{
    showAboutDialog(nppData._nppHandle);
}

// The same operations as the tree's context menu, so they can be given a shortcut in
// Settings > Shortcut Mapper > Plugin commands.
void eachAction(FuncItem& item, PFUNCPLUGINCMD fn, const wchar_t* name)
{
    item._pFunc = fn;
    wcscpy_s(item._itemName, name);
    item._init2Check = false;
    item._pShKey = nullptr;
}

// Notepad++ draws a separator wherever _pFunc is null: that is the whole protocol.
void separator(FuncItem& item)
{
    item._pFunc = nullptr;
    item._itemName[0] = L'\0';
    item._init2Check = false;
    item._pShKey = nullptr;
}

void fillFuncs()
{
    // Panel and behaviour.
    eachAction(gFuncs[cmdShowPanel], cmdShowPanelClicked, L"Show XML Tree");
    eachAction(gFuncs[cmdReParse], cmdReParseClicked, L"Re-parse document");
    eachAction(gFuncs[cmdFollowCaret], cmdFollowCaretClicked, L"Follow caret");
    separator(gFuncs[sepPanel]);

    // The tree's context menu, so these can be given a shortcut in the Shortcut Mapper.
    eachAction(gFuncs[cmdLocate], [] { gPanel.runSelected(PanelAction::Locate); }, L"Locate element");
    eachAction(gFuncs[cmdDelete], [] { gPanel.runSelected(PanelAction::Delete); }, L"Delete element");
    eachAction(gFuncs[cmdCopyBlock], [] { gPanel.runSelected(PanelAction::Copy); }, L"Copy block");
    eachAction(gFuncs[cmdCutBlock], [] { gPanel.runSelected(PanelAction::Cut); }, L"Cut block");
    eachAction(gFuncs[cmdPasteOver], [] { gPanel.runSelected(PanelAction::Paste); },
               L"Paste over element");
    eachAction(gFuncs[cmdPasteAfter], [] { gPanel.runSelected(PanelAction::PasteAfter); },
               L"Paste after element");
    separator(gFuncs[sepEdits]);

    // Reports on the selected node.
    eachAction(gFuncs[cmdSumValues], [] { gPanel.runSelected(PanelAction::Sum); }, L"Sum values");
    eachAction(gFuncs[cmdUniqueValues], [] { gPanel.runSelected(PanelAction::Unique); },
               L"Unique values");
    separator(gFuncs[sepReports]);

    // Tools.
    eachAction(gFuncs[cmdCheckForm], [] { gPanel.checkForm(); }, L"Check well-formedness");
    eachAction(gFuncs[cmdRefsToChars], [] { gPanel.convertCharRefs(true); },
               L"Char refs to characters");
    eachAction(gFuncs[cmdCharsToRefs], [] { gPanel.convertCharRefs(false); },
               L"Characters to char refs");
    separator(gFuncs[sepAbout]);
    eachAction(gFuncs[cmdAbout], cmdAboutClicked, L"About XML Tree");
}

DocumentView currentView()
{
    HWND sci = currentScintilla();
    if (!sci) return DocumentView{};

    const auto length = static_cast<uint32_t>(::SendMessage(sci, SCI_GETLENGTH, 0, 0));
    const char* text = reinterpret_cast<const char*>(::SendMessage(sci, SCI_GETCHARACTERPOINTER, 0, 0));
    return DocumentView{text, length};
}

void onBufferActivated()
{
    DocumentView doc = currentView();

    // Let Notepad++ decide what is XML: it already knows every extension the user has
    // associated, so .csproj, .resx, .xaml and friends work without a settings screen.
    int langType = 0;
    ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTLANGTYPE, 0,
                  reinterpret_cast<LPARAM>(&langType));
    const bool nppSaysXml = langType == L_XML;
    wchar_t path[MAX_PATH * 4] = {0};
    currentFullPath(path, static_cast<int>(std::size(path)));

    // loadDocument decides whether the panel belongs on screen at all.
    gPanel.loadDocument(doc, path, nppSaysXml);
}

} // namespace

// --- Notepad++ API ---------------------------------------------------------

HINSTANCE pluginInstance()
{
    static HINSTANCE instance = nullptr;
    if (!instance) {
        ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(&pluginInstance), &instance);
    }
    return instance;
}

HWND currentScintilla()
{
    int which = 0;
    ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
    return which == 0 ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
}

UINT_PTR currentBufferId()
{
    return static_cast<UINT_PTR>(::SendMessage(nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
}

void currentFullPath(wchar_t* out, int outChars)
{
    if (!out || outChars <= 0) return;
    out[0] = 0;
    ::SendMessage(nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, static_cast<WPARAM>(currentBufferId()),
                  reinterpret_cast<LPARAM>(out));
}

extern "C" __declspec(dllexport) void setInfo(NppData data)
{
    nppData = data;
    fillFuncs();
}

extern "C" __declspec(dllexport) const wchar_t* getName()
{
    return PLUGIN_MODULE_NAME;
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* nbF)
{
    *nbF = cmdCount;
    return gFuncs;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notifyCode)
{
    switch (notifyCode->nmhdr.code) {
    case NPPN_READY:
        if (gPanel.create(nppData._nppHandle)) {
            gPanel.registerDocked();
            // Let Notepad++ theme the panel chrome (banner, filter box) whenever the
            // user switches between light and dark.
            ::SendMessage(nppData._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME,
                          static_cast<WPARAM>(NppDarkMode::dmfInit),
                          reinterpret_cast<LPARAM>(gPanel.handle()));
            // A file opened on the command line raised BUFFERACTIVATED before the panel
            // existed, so the decision has to be taken again here rather than defaulting
            // to hidden.
            onBufferActivated();
        }
        break;
    case NPPN_BUFFERACTIVATED:
    case NPPN_FILEOPENED:
    case NPPN_LANGCHANGED:
        onBufferActivated();
        break;
    case NPPN_DARKMODECHANGED:
        ::SendMessage(nppData._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME,
                      static_cast<WPARAM>(NppDarkMode::dmfHandleChange),
                      reinterpret_cast<LPARAM>(gPanel.handle()));
        gPanel.applyTheme();
        break;
    case NPPN_WORDSTYLESUPDATED:
        // The user changed the theme's colours: the tree reads them from Scintilla.
        gPanel.applyTheme();
        break;
    case SCN_UPDATEUI:
        if (notifyCode->updated & SC_UPDATE_SELECTION) gPanel.onCaretMoved();
        break;
    case SCN_MODIFIED:
        gPanel.onDocumentModified(notifyCode->modificationType);
        break;
    default:
        break;
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT /*Message*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode()
{
    return TRUE;
}
