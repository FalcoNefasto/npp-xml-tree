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
#include "ProblemsDialog.h"
#include "PluginDefinition.h"

#include "Notepad_plus_msgs.h"
#include "resource.h"

namespace {

constexpr int kMargin = 10;
constexpr int kGap = 6;
constexpr int kButtonWidth = 92;
constexpr int kButtonHeight = 26;
constexpr int kMinWidth = 420;
constexpr int kMinHeight = 240;

} // namespace

bool ProblemsDialog::create(HWND nppOwner)
{
    if (hwnd_) return true;

    hwnd_ = ::CreateDialogParamW(pluginInstance(), MAKEINTRESOURCEW(IDD_PROBLEMS), nppOwner,
                                 &ProblemsDialog::dlgProc, reinterpret_cast<LPARAM>(this));
    if (!hwnd_) return false;

    list_ = ::GetDlgItem(hwnd_, IDC_PROBLEM_LIST);
    previous_ = ::GetDlgItem(hwnd_, IDC_PROBLEM_PREVIOUS);
    next_ = ::GetDlgItem(hwnd_, IDC_PROBLEM_NEXT);
    close_ = ::GetDlgItem(hwnd_, IDC_PROBLEM_CLOSE);

    // Let Notepad++ treat it as one of its own dialogs (Tab, Enter, Escape) and theme
    // its controls the way it themes the Find dialog.
    ::SendMessageW(nppOwner, NPPM_MODELESSDIALOG, MODELESSDIALOGADD, reinterpret_cast<LPARAM>(hwnd_));
    ::SendMessageW(nppOwner, NPPM_DARKMODESUBCLASSANDTHEME, static_cast<WPARAM>(NppDarkMode::dmfInit),
                   reinterpret_cast<LPARAM>(hwnd_));
    layout();
    return true;
}

// Left column: the list, all the way down. Right column: the three buttons, in the order
// the eye expects (Previous, Next, Close).
void ProblemsDialog::layout() const
{
    if (!hwnd_ || !list_) return;

    RECT rc{};
    ::GetClientRect(hwnd_, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;

    const int column = width - kMargin - kButtonWidth;
    const int listWidth = column - kGap - kMargin;

    ::MoveWindow(list_, kMargin, kMargin, listWidth > 0 ? listWidth : 0,
                 height - 2 * kMargin > 0 ? height - 2 * kMargin : 0, TRUE);

    ::MoveWindow(previous_, column, kMargin, kButtonWidth, kButtonHeight, TRUE);
    ::MoveWindow(next_, column, kMargin + kButtonHeight + kGap, kButtonWidth, kButtonHeight, TRUE);
    ::MoveWindow(close_, column, kMargin + 2 * (kButtonHeight + kGap), kButtonWidth, kButtonHeight,
                 TRUE);
}

void ProblemsDialog::updateHorizontalExtent() const
{
    if (!list_) return;

    HDC dc = ::GetDC(list_);
    HGDIOBJ previousFont = ::SelectObject(dc, reinterpret_cast<HFONT>(::SendMessageW(
                                                   list_, WM_GETFONT, 0, 0)));

    int widest = 0;
    for (const std::wstring& item : items_) {
        SIZE size{};
        if (::GetTextExtentPoint32W(dc, item.c_str(), static_cast<int>(item.size()), &size) &&
            size.cx > widest) {
            widest = size.cx;
        }
    }

    ::SelectObject(dc, previousFont);
    ::ReleaseDC(list_, dc);

    ::SendMessageW(list_, LB_SETHORIZONTALEXTENT,
                   static_cast<WPARAM>(widest + 16 > 0 ? widest + 16 : 0), 0);
}

INT_PTR CALLBACK ProblemsDialog::dlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<ProblemsDialog*>(::GetWindowLongPtrW(hwnd, DWLP_USER));

    switch (msg) {
    case WM_INITDIALOG:
        ::SetWindowLongPtrW(hwnd, DWLP_USER, lParam);
        return TRUE;

    case WM_SIZE:
        if (self) self->layout();
        return TRUE;

    case WM_GETMINMAXINFO:
        if (auto* info = reinterpret_cast<MINMAXINFO*>(lParam)) {
            info->ptMinTrackSize.x = kMinWidth;
            info->ptMinTrackSize.y = kMinHeight;
        }
        return TRUE;

    case WM_COMMAND:
        if (!self) break;
        switch (LOWORD(wParam)) {
        case IDC_PROBLEM_LIST:
            if (HIWORD(wParam) == LBN_SELCHANGE && self->onSelect) {
                const auto index =
                    static_cast<int>(::SendMessageW(self->list_, LB_GETCURSEL, 0, 0));
                self->onSelect(index);
            }
            return TRUE;
        case IDC_PROBLEM_PREVIOUS:
            if (self->onPrevious) self->onPrevious();
            return TRUE;
        case IDC_PROBLEM_NEXT:
            if (self->onNext) self->onNext();
            return TRUE;
        case IDC_PROBLEM_CLOSE:
        case IDCANCEL:
            self->hide();
            if (self->onClose) self->onClose();
            return TRUE;
        default:
            break;
        }
        break;

    case WM_CLOSE:
        if (self) {
            self->hide();
            if (self->onClose) self->onClose();
        }
        return TRUE;

    default:
        break;
    }
    return FALSE;
}

void ProblemsDialog::show()
{
    if (!hwnd_) return;
    ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    ::SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ProblemsDialog::hide()
{
    if (hwnd_) ::ShowWindow(hwnd_, SW_HIDE);
}

bool ProblemsDialog::visible() const
{
    return hwnd_ && ::IsWindowVisible(hwnd_);
}

void ProblemsDialog::setProblems(const std::vector<std::wstring>& items, bool navigable)
{
    if (!list_) return;

    // Rebuilding the list sends no LBN_SELCHANGE, so the panel's idea of which problem is
    // current is never overwritten behind its back.
    items_ = items;
    ::SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    for (const std::wstring& item : items_) {
        ::SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    ::EnableWindow(previous_, navigable ? TRUE : FALSE);
    ::EnableWindow(next_, navigable ? TRUE : FALSE);
    updateHorizontalExtent();
}

void ProblemsDialog::select(int index)
{
    if (!list_) return;
    ::SendMessageW(list_, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
}
