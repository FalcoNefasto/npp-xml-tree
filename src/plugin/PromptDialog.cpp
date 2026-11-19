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
#include "PromptDialog.h"
#include "PluginDefinition.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <cstring>

namespace {

const wchar_t* kClass = L"NppXmlTreePrompt";
constexpr int kOkButton = 1;
constexpr int kCancelButton = 2;
constexpr int kEdit = 100;
constexpr int kCaption = 101;

struct PromptState {
    std::wstring title;
    std::wstring caption;
    std::wstring value;
    ResultTheme theme{};
    HBRUSH background = nullptr;
    HBRUSH editBackground = nullptr;
    HWND edit = nullptr;
    HWND owner = nullptr;
    bool accepted = false;
    bool closed = false;
};

COLORREF shaded(COLORREF colour, bool dark, int delta)
{
    const int sign = dark ? 1 : -1;
    const auto adjust = [&](int v) {
        const int out = v + sign * delta;
        return out < 0 ? 0 : (out > 255 ? 255 : out);
    };
    return RGB(adjust(GetRValue(colour)), adjust(GetGValue(colour)), adjust(GetBValue(colour)));
}

void paintButton(const DRAWITEMSTRUCT* item, const ResultTheme& theme)
{
    HDC dc = item->hDC;
    RECT rc = item->rcItem;

    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    COLORREF fill = theme.background;
    if (pressed) {
        const int bg = (GetRValue(theme.background) + GetGValue(theme.background) +
                        GetBValue(theme.background)) / 3;
        const int v = theme.dark ? (bg + 40 > 255 ? 255 : bg + 40) : (bg - 30 < 0 ? 0 : bg - 30);
        fill = RGB(v, v, v);
    }

    HBRUSH brush = ::CreateSolidBrush(fill);
    ::FillRect(dc, &rc, brush);
    ::DeleteObject(brush);

    const int bg = (GetRValue(theme.background) + GetGValue(theme.background) +
                    GetBValue(theme.background)) / 3;
    const int borderValue = theme.dark ? (bg + 60 > 255 ? 255 : bg + 60) : (bg - 60 < 0 ? 0 : bg - 60);
    HBRUSH frame = ::CreateSolidBrush(RGB(borderValue, borderValue, borderValue));
    ::FrameRect(dc, &rc, frame);
    ::DeleteObject(frame);

    wchar_t label[128] = {0};
    ::GetWindowTextW(item->hwndItem, label, 128);
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, theme.text);
    RECT text = rc;
    if (pressed) ::OffsetRect(&text, 1, 1);
    ::DrawTextW(dc, label, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void layout(HWND hwnd, PromptState* state)
{
    RECT rc;
    ::GetClientRect(hwnd, &rc);
    const int pad = 12;
    const int buttonW = 96;
    const int buttonH = 26;
    const int buttonsY = rc.bottom - pad - buttonH;

    ::MoveWindow(::GetDlgItem(hwnd, kCaption), pad, pad, rc.right - 2 * pad, 20, TRUE);
    ::MoveWindow(state->edit, pad, pad + 24, rc.right - 2 * pad, 24, TRUE);

    ::MoveWindow(::GetDlgItem(hwnd, kCancelButton), rc.right - pad - buttonW, buttonsY, buttonW,
                 buttonH, TRUE);
    ::MoveWindow(::GetDlgItem(hwnd, kOkButton), rc.right - pad - 2 * buttonW - 8, buttonsY, buttonW,
                 buttonH, TRUE);
}

void accept(HWND hwnd, PromptState* state)
{
    const int len = ::GetWindowTextLengthW(state->edit);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    ::GetWindowTextW(state->edit, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    state->value = text;
    state->accepted = true;
    ::DestroyWindow(hwnd);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<PromptState*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_ERASEBKGND:
        if (state && state->background) {
            RECT rc;
            ::GetClientRect(hwnd, &rc);
            ::FillRect(reinterpret_cast<HDC>(wParam), &rc, state->background);
            return 1;
        }
        break;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
        if (state) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            const bool isEdit = state->edit && reinterpret_cast<HWND>(lParam) == state->edit;
            ::SetTextColor(dc, state->theme.text);
            ::SetBkColor(dc, isEdit ? shaded(state->theme.background, state->theme.dark, 10)
                                    : state->theme.background);
            return reinterpret_cast<LRESULT>(isEdit ? state->editBackground : state->background);
        }
        break;

    case WM_DRAWITEM:
        if (state) {
            auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (item->CtlID == kOkButton || item->CtlID == kCancelButton) {
                paintButton(item, state->theme);
                return TRUE;
            }
        }
        break;

    case WM_SIZE:
        if (state) layout(hwnd, state);
        return 0;

    case WM_COMMAND:
        if (!state) break;
        if (LOWORD(wParam) == kOkButton) {
            accept(hwnd, state);
            return 0;
        }
        if (LOWORD(wParam) == kCancelButton) {
            ::DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        ::DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (state) {
            state->closed = true;
            if (state->owner) ::EnableWindow(state->owner, TRUE);
        }
        return 0;

    default:
        break;
    }
    return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

bool registerClass()
{
    static bool done = false;
    if (done) return true;

    WNDCLASSEX wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &wndProc;
    wc.hInstance = pluginInstance();
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClass;
    done = ::RegisterClassEx(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return done;
}

} // namespace

bool showPromptDialog(HWND owner, const wchar_t* title, const wchar_t* caption, std::wstring& value,
                      const ResultTheme& theme)
{
    if (!registerClass()) return false;

    PromptState state;
    state.title = title ? title : L"XML Tree";
    state.caption = caption ? caption : L"";
    state.value = value;
    state.theme = theme;
    state.owner = owner;
    state.background = ::CreateSolidBrush(theme.background);
    state.editBackground = ::CreateSolidBrush(shaded(theme.background, theme.dark, 10));

    RECT ownerRect{0, 0, 900, 600};
    if (owner) ::GetWindowRect(owner, &ownerRect);
    const int width = 520;
    const int height = 150;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    HWND hwnd = ::CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW, kClass, state.title.c_str(),
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, x, y, width,
                                  height, owner, nullptr, pluginInstance(), nullptr);
    if (!hwnd) {
        if (state.background) ::DeleteObject(state.background);
        if (state.editBackground) ::DeleteObject(state.editBackground);
        return false;
    }
    ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

    if (theme.dark) {
        const BOOL dark = TRUE;
        ::DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
        ::SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
    }

    ::CreateWindowExW(0, L"STATIC", state.caption.c_str(),
                      WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 100, 20, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCaption)),
                      pluginInstance(), nullptr);

    state.edit = ::CreateWindowExW(0, L"EDIT", state.value.c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 24,
                                   hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEdit)),
                                   pluginInstance(), nullptr);
    if (state.edit) {
        ::SendMessageW(state.edit, WM_SETFONT, ::SendMessage(hwnd, WM_GETFONT, 0, 0), TRUE);
        if (theme.dark) ::SetWindowTheme(state.edit, L"DarkMode_Explorer", nullptr);
    }

    ::CreateWindowExW(0, L"BUTTON", L"OK",
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON, 0, 0, 96, 26,
                      hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOkButton)),
                      pluginInstance(), nullptr);
    ::CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0,
                      96, 26, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancelButton)),
                      pluginInstance(), nullptr);

    layout(hwnd, &state);

    if (owner) ::EnableWindow(owner, FALSE);
    ::ShowWindow(hwnd, SW_SHOW);
    ::SetFocus(state.edit);
    if (state.edit) {
        ::SendMessageW(state.edit, EM_SETSEL, 0, static_cast<LPARAM>(-1));
    }

    MSG msg;
    while (!state.closed && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!::IsDialogMessageW(hwnd, &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    if (state.background) ::DeleteObject(state.background);
    if (state.editBackground) ::DeleteObject(state.editBackground);

    if (state.accepted) value = state.value;
    return state.accepted;
}
