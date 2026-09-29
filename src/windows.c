#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <powrprof.h>
#include <stdio.h>
#include <stdlib.h>
#include "core.h"

#define ACTION_DONE (WM_APP + 1)
static Options options;
static Action visible[ACTION_COUNT];
static int count, selected;
static BOOL busy;
static HWND window, buttons[ACTION_COUNT], footer;
static HFONT font, heading_font, small_font;
static HBRUSH background;
static const COLORREF bg = RGB(29,32,32), fg = RGB(170,161,146), green = RGB(122,184,122);
static int scale = 96;
static int px(int value) { return MulDiv(value, scale, 96); }

static DWORD shutdown_privilege(void) {
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return GetLastError();
    TOKEN_PRIVILEGES privileges = {0};
    privileges.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid)) {
        DWORD error = GetLastError(); CloseHandle(token); return error;
    }
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    BOOL success = AdjustTokenPrivileges(token, FALSE, &privileges, 0, NULL, NULL);
    DWORD error = GetLastError();
    CloseHandle(token);
    return success ? error : (error ? error : ERROR_ACCESS_DENIED);
}
static DWORD WINAPI run_action(LPVOID data) {
    Action action = (Action)(INT_PTR)data;
    char mock[512] = {0};
    wchar_t *message = calloc(2048, sizeof(wchar_t));
    if (!message) { PostMessageW(window, ACTION_DONE, ERROR_OUTOFMEMORY, 0); return 0; }
    DWORD error = ERROR_SUCCESS;
    if (mock_action(options, action, mock, sizeof(mock))) {
        if (mock[0]) MultiByteToWideChar(CP_UTF8, 0, mock, -1, message, 2048);
    } else {
        if (action == SLEEP || action == HIBERNATE || action == RESTART || action == SHUTDOWN)
            error = shutdown_privilege();
        if (!error) {
            BOOL ok = FALSE;
            switch (action) {
                case LOCK: ok = LockWorkStation(); break;
                case LOGOUT: ok = ExitWindowsEx(EWX_LOGOFF, 0); break;
                case SLEEP: ok = SetSuspendState(FALSE, FALSE, FALSE); break;
                case HIBERNATE: ok = SetSuspendState(TRUE, FALSE, FALSE); break;
                case RESTART: ok = ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED); break;
                case SHUTDOWN: ok = ExitWindowsEx(EWX_POWEROFF, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED); break;
                default: SetLastError(ERROR_NOT_SUPPORTED); break;
            }
            if (!ok) { error = GetLastError(); if (!error) error = ERROR_GEN_FAILURE; }
        }
        if (error) {
            wchar_t detail[1536] = {0};
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0, detail, 1536, NULL);
            swprintf(message, 2048, L"Windows could not perform the action (error %lu).\n%ls", error, detail);
        }
    }
    if (!PostMessageW(window, ACTION_DONE, 0, (LPARAM)message)) free(message);
    return 0;
}
static void restore_footer(void) {
    SetWindowTextW(footer, options.simulate_error ? L"TEST MODE · selections show a simulated error" :
        options.dry_run ? L"DRY RUN · selections close without taking action" : L"Arrows / Tab navigate · Enter select · Esc / q quit");
}
static void select_button(int index) {
    selected = index;
    for (int i = 0; i < count; i++) InvalidateRect(buttons[i], NULL, TRUE);
    SetFocus(buttons[selected]);
}
static void activate(int index) {
    if (busy || index < 0 || index >= count) return;
    select_button(index);
    busy = TRUE;
    for (int i = 0; i < count; i++) EnableWindow(buttons[i], FALSE);
    SetWindowTextW(footer, L"Waiting for Windows…");
    HANDLE thread = CreateThread(NULL, 0, run_action, (LPVOID)(INT_PTR)visible[index], 0, NULL);
    if (thread) CloseHandle(thread);
    else SendMessageW(window, ACTION_DONE, GetLastError(), 0);
}
static BOOL handle_key(MSG *message) {
    if (message->message != WM_KEYDOWN || busy) return FALSE;
    if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0 || GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) return FALSE;
    if (message->lParam & (1L << 30)) return TRUE;
    UINT key = (UINT)message->wParam;
    if (key == VK_ESCAPE || key == 'Q') { DestroyWindow(window); return TRUE; }
    if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN || key == VK_TAB) {
        int delta = key == VK_LEFT || key == VK_UP || (key == VK_TAB && GetKeyState(VK_SHIFT) < 0) ? -1 : 1;
        select_button(move_selection(selected, count, delta)); return TRUE;
    }
    if (key == VK_RETURN || key == VK_SPACE) { activate(selected); return TRUE; }
    if (key >= VK_NUMPAD1 && key <= VK_NUMPAD9) key = '1' + key - VK_NUMPAD1;
    int index = action_for_key(visible, count, key);
    if (index >= 0) { activate(index); return TRUE; }
    return FALSE;
}
static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_COMMAND:
            if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) >= 100 && LOWORD(wp) < 100 + count) activate(LOWORD(wp)-100);
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, GetDlgCtrlID((HWND)lp) == 10 ? RGB(77,163,168) : fg);
            SetBkColor(dc, bg); return (LRESULT)background;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT *item = (DRAWITEMSTRUCT *)lp;
            int index = (int)item->CtlID-100;
            if (index < 0 || index >= count) break;
            COLORREF color = busy ? RGB(100,100,100) : index == selected ? green : fg;
            FillRect(item->hDC, &item->rcItem, background);
            HPEN pen = CreatePen(PS_SOLID, px(index == selected ? 2 : 1), color);
            HGDIOBJ old_pen = SelectObject(item->hDC, pen);
            HGDIOBJ old_brush = SelectObject(item->hDC, background);
            RoundRect(item->hDC, 1, 1, item->rcItem.right-1, item->rcItem.bottom-1, px(12), px(12));
            SelectObject(item->hDC, old_pen); SelectObject(item->hDC, old_brush); DeleteObject(pen);
            wchar_t label[128]; GetWindowTextW(item->hwndItem, label, 128);
            SetBkMode(item->hDC, TRANSPARENT); SetTextColor(item->hDC, color);
            HGDIOBJ old_font = SelectObject(item->hDC, font);
            DrawTextW(item->hDC, label, -1, &item->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(item->hDC, old_font);
            return TRUE;
        }
        case ACTION_DONE: {
            wchar_t *error = (wchar_t *)lp;
            if (!wp && error && !error[0]) { free(error); DestroyWindow(hwnd); return 0; }
            MessageBoxW(hwnd, error ? error : L"The system request could not be started.", L"Action failed", MB_OK | MB_ICONERROR);
            free(error);
            busy = FALSE;
            for (int i = 0; i < count; i++) EnableWindow(buttons[i], TRUE);
            restore_footer(); select_button(selected); return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    (void)previous; (void)command; (void)show;
    int argc;
    LPWSTR *wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide) return 1;
    char **argv = calloc((size_t)argc, sizeof(char *));
    if (!argv) { LocalFree(wide); return 1; }
    for (int i = 0; i < argc; i++) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, NULL, 0, NULL, NULL);
        argv[i] = calloc((size_t)size, 1);
        if (!argv[i]) return 1;
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, argv[i], size, NULL, NULL);
    }
    BOOL valid = parse_options(argc, argv, &options);
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv); LocalFree(wide);
    if (!valid || options.help || options.list) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) { FILE *stream; freopen_s(&stream, "CONOUT$", "w", stdout); }
    }
    if (!valid || options.help) { print_help(); return valid ? 0 : 2; }
    visible[count++] = LOCK; visible[count++] = LOGOUT;
    if (IsPwrSuspendAllowed()) visible[count++] = SLEEP;
    visible[count++] = SHUTDOWN; visible[count++] = RESTART;
    if (IsPwrHibernateAllowed()) visible[count++] = HIBERNATE;
    if (options.list) {
        for (int i = 0; i < count; i++) printf("%d  %s [%c]\n", i+1, actions[visible[i]].label, actions[visible[i]].key);
        return 0;
    }
    SetProcessDPIAware();
    HDC screen = GetDC(NULL); scale = GetDeviceCaps(screen, LOGPIXELSX); ReleaseDC(NULL, screen);
    background = CreateSolidBrush(bg);
    font = CreateFontW(-px(19), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    heading_font = CreateFontW(-px(24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    small_font = CreateFontW(-px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = window_proc; wc.hInstance = instance; wc.lpszClassName = L"Byebye";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.hbrBackground = background;
    if (!RegisterClassW(&wc)) return 1;
    int height = 180 + ((count+1)/2)*78;
    RECT rect = {0,0,px(580),px(height)};
    DWORD style = WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&rect, style, FALSE, WS_EX_TOPMOST);
    POINT cursor; GetCursorPos(&cursor);
    MONITORINFO monitor = {0}; monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
    RECT area = monitor.rcWork;
    int width = rect.right-rect.left, outer_height = rect.bottom-rect.top;
    window = CreateWindowExW(WS_EX_TOPMOST, L"Byebye", L"Byebye", style,
        area.left+(area.right-area.left-width)/2, area.top+(area.bottom-area.top-outer_height)/2,
        width, outer_height, NULL, NULL, instance, NULL);
    if (!window) return 1;
    HWND heading = CreateWindowW(L"STATIC", L"Where do you want to GO?", WS_CHILD | WS_VISIBLE | SS_CENTER,
        px(20), px(36), px(540), px(36), window, (HMENU)10, instance, NULL);
    SendMessageW(heading, WM_SETFONT, (WPARAM)heading_font, TRUE);
    for (int i = 0; i < count; i++) {
        wchar_t label[128], name[32];
        MultiByteToWideChar(CP_UTF8, 0, actions[visible[i]].label, -1, name, 32);
        swprintf(label, 128, L"%d  %ls   [%c]", i+1, name, actions[visible[i]].key);
        int x = i == count-1 && count%2 ? 170 : 40+(i%2)*260;
        buttons[i] = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            px(x), px(110+(i/2)*78), px(240), px(58), window, (HMENU)(INT_PTR)(100+i), instance, NULL);
    }
    footer = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
        px(10), px(height-36), px(560), px(24), window, (HMENU)11, instance, NULL);
    SendMessageW(footer, WM_SETFONT, (WPARAM)small_font, TRUE);
    restore_footer();
    ShowWindow(window, SW_SHOW); SetForegroundWindow(window); select_button(0);
    MSG message;
    int result;
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        if (!handle_key(&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    DeleteObject(font); DeleteObject(heading_font); DeleteObject(small_font); DeleteObject(background);
    return result < 0 ? 1 : 0;
}
