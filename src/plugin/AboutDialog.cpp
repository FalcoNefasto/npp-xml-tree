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
#include "AboutDialog.h"
#include "PluginDefinition.h"

#include "Notepad_plus_msgs.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>

#include <string>

namespace {

const wchar_t kAuthorUrl[] = L"https://github.com/FalcoNefasto";

struct AboutFonts {
    HFONT title = nullptr;
    HFONT link = nullptr;
};

// One font derived from the dialog's own, so the About box still follows the system
// font and size; only the weight/size/underline differ.
HFONT deriveFont(HFONT base, int scaleNum, int scaleDen, LONG weight, bool underline)
{
    LOGFONTW info{};
    if (!base || ::GetObjectW(base, sizeof info, &info) == 0) {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof metrics;
        if (!::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0)) {
            return nullptr;
        }
        info = metrics.lfMessageFont;
    }

    info.lfHeight = ::MulDiv(info.lfHeight, scaleNum, scaleDen);
    if (weight != 0) info.lfWeight = weight;
    info.lfUnderline = underline ? TRUE : FALSE;
    return ::CreateFontIndirectW(&info);
}

void centerOn(HWND dialog, HWND owner)
{
    RECT self{};
    if (!::GetWindowRect(dialog, &self)) return;

    RECT frame{};
    if (owner && ::GetWindowRect(owner, &frame)) {
        const int x = frame.left + ((frame.right - frame.left) - (self.right - self.left)) / 2;
        const int y = frame.top + ((frame.bottom - frame.top) - (self.bottom - self.top)) / 2;
        ::SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }

    const int screenW = ::GetSystemMetrics(SM_CXSCREEN);
    const int screenH = ::GetSystemMetrics(SM_CYSCREEN);
    ::SetWindowPos(dialog, nullptr, (screenW - (self.right - self.left)) / 2,
                   (screenH - (self.bottom - self.top)) / 2, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// A static text with SS_NOTIFY still shows the arrow cursor; the underlined name should
// look like something to click.
LRESULT CALLBACK linkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR,
                          DWORD_PTR)
{
    if (msg == WM_SETCURSOR) {
        ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

INT_PTR CALLBACK aboutProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* fonts = reinterpret_cast<AboutFonts*>(::GetWindowLongPtrW(hwnd, DWLP_USER));

    switch (msg) {
    case WM_INITDIALOG: {
        fonts = new AboutFonts();
        ::SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(fonts));

        // What to show is decided here, so the version lives in one place only.
        std::wstring version = L"Version ";
        version += PLUGIN_VERSION;
        ::SetDlgItemTextW(hwnd, IDC_ABOUT_VERSION, version.c_str());
        ::SetDlgItemTextW(hwnd, IDC_ABOUT_AUTHOR, PLUGIN_AUTHOR);
        ::SetDlgItemTextW(hwnd, IDC_ABOUT_TITLE, PLUGIN_MODULE_NAME);

        const HFONT base =
            reinterpret_cast<HFONT>(::SendMessageW(hwnd, WM_GETFONT, 0, 0));
        fonts->title = deriveFont(base, 3, 2, FW_BOLD, false);
        fonts->link = deriveFont(base, 1, 1, 0, true);
        if (fonts->title) {
            ::SendDlgItemMessageW(hwnd, IDC_ABOUT_TITLE, WM_SETFONT,
                                  reinterpret_cast<WPARAM>(fonts->title), TRUE);
        }
        if (fonts->link) {
            ::SendDlgItemMessageW(hwnd, IDC_ABOUT_AUTHOR, WM_SETFONT,
                                  reinterpret_cast<WPARAM>(fonts->link), TRUE);
        }
        ::SetWindowSubclass(::GetDlgItem(hwnd, IDC_ABOUT_AUTHOR), &linkProc, 1, 0);

        const HWND owner = reinterpret_cast<HWND>(lParam);
        ::SendMessageW(owner, NPPM_DARKMODESUBCLASSANDTHEME,
                       static_cast<WPARAM>(NppDarkMode::dmfInit),
                       reinterpret_cast<LPARAM>(hwnd));
        centerOn(hwnd, owner);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_ABOUT_AUTHOR && HIWORD(wParam) == STN_CLICKED) {
            ::ShellExecuteW(nullptr, L"open", kAuthorUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            ::EndDialog(hwnd, IDOK);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        ::EndDialog(hwnd, IDOK);
        return TRUE;

    case WM_DESTROY:
        if (fonts) {
            if (fonts->title) ::DeleteObject(fonts->title);
            if (fonts->link) ::DeleteObject(fonts->link);
            delete fonts;
            ::SetWindowLongPtrW(hwnd, DWLP_USER, 0);
        }
        return TRUE;

    default:
        break;
    }
    return FALSE;
}

} // namespace

void showAboutDialog(HWND nppOwner)
{
    ::DialogBoxParamW(pluginInstance(), MAKEINTRESOURCEW(IDD_ABOUT), nppOwner, aboutProc,
                      reinterpret_cast<LPARAM>(nppOwner));
}
