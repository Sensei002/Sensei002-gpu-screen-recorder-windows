#include "../../include/RegionSelector/RegionSelectorWin32.hpp"

#include <stdio.h>

namespace gsr {
    static void paint_region_selector(HWND hwnd, uint32_t border_color, bool selecting, int start_x, int start_y, int cur_x, int cur_y) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if(!dc)
            return;

        /* Fill the overlay with a transparent-ish black backdrop. */
        HBRUSH backdrop = CreateSolidBrush(RGB(0, 0, 0));
        RECT client;
        GetClientRect(hwnd, &client);
        FillRect(dc, &client, backdrop);
        DeleteObject(backdrop);

        if(selecting) {
            /* Draw the selection border. */
            HBRUSH border = CreateSolidBrush(RGB((border_color >> 16) & 0xFF, (border_color >> 8) & 0xFF, border_color & 0xFF));
            HGDIOBJ prev_brush = SelectObject(dc, border);
            HPEN pen = CreatePen(PS_SOLID, 2, RGB((border_color >> 16) & 0xFF, (border_color >> 8) & 0xFF, border_color & 0xFF));
            HGDIOBJ prev_pen = SelectObject(dc, pen);

            int left = start_x < cur_x ? start_x : cur_x;
            int top = start_y < cur_y ? start_y : cur_y;
            int right = start_x > cur_x ? start_x : cur_x;
            int bottom = start_y > cur_y ? start_y : cur_y;
            Rectangle(dc, left, top, right, bottom);

            SelectObject(dc, prev_pen);
            DeleteObject(pen);
            SelectObject(dc, prev_brush);
            DeleteObject(border);
        }

        EndPaint(hwnd, &ps);
    }

    LRESULT CALLBACK region_selector_wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        RegionSelectorWin32 *self = (RegionSelectorWin32*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
        switch(message) {
            case WM_PAINT: {
                if(self) {
                    paint_region_selector(hwnd, self->border_color, self->mouse_down,
                        self->drag_start_x, self->drag_start_y,
                        self->drag_cur_x, self->drag_cur_y);
                } else {
                    PAINTSTRUCT ps;
                    BeginPaint(hwnd, &ps);
                    EndPaint(hwnd, &ps);
                }
                return 0;
            }
        }
        return DefWindowProcA(hwnd, message, wparam, lparam);
    }

    RegionSelectorWin32::RegionSelectorWin32() {

    }

    RegionSelectorWin32::~RegionSelectorWin32() {
        stop();
    }

    bool RegionSelectorWin32::start(SelectionType selection_type, mgl::Color border_color) {
        if(started)
            return false;

        if(!hwnd) {
            WNDCLASSA wc = { 0 };
            wc.lpfnWndProc = region_selector_wnd_proc;
            wc.hInstance = GetModuleHandleA(NULL);
            wc.lpszClassName = "gsr_ui_region_selector";
            RegisterClassA(&wc);

            /* Cover the entire virtual screen (all monitors). */
            virtual_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
            virtual_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
            const int virtual_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            const int virtual_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

            hwnd = CreateWindowExA(
                WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
                wc.lpszClassName, "gsr-ui-region-selector",
                WS_POPUP,
                virtual_x, virtual_y, virtual_width, virtual_height,
                NULL, NULL, wc.hInstance, NULL);
            if(!hwnd) {
                fprintf(stderr, "RegionSelectorWin32 error: failed to create overlay window\n");
                failed_ = true;
                return false;
            }
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)this);
            SetLayeredWindowAttributes(hwnd, 0, 200, LWA_ALPHA);
        }

        this->selection_type = selection_type;
        this->border_color = ((uint32_t)border_color.r << 16) | ((uint32_t)border_color.g << 8) | (uint32_t)border_color.b;

        selected = false;
        canceled = false;
        selecting = false;
        mouse_down = false;

        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetCapture(hwnd);
        SetCursor(LoadCursor(NULL, IDC_CROSS));

        started = true;
        return true;
    }

    void RegionSelectorWin32::stop() {
        if(hwnd) {
            if(GetCapture() == hwnd)
                ReleaseCapture();
            ShowWindow(hwnd, SW_HIDE);
        }
        started = false;
        selecting = false;
        mouse_down = false;
    }

    void RegionSelectorWin32::cancel() {
        canceled = true;
        stop();
    }

    bool RegionSelectorWin32::is_started() const {
        return started;
    }

    bool RegionSelectorWin32::failed() const {
        return failed_;
    }

    void RegionSelectorWin32::pump_messages() {
        if(!hwnd)
            return;

        MSG msg;
        while(PeekMessageA(&msg, hwnd, 0, 0, PM_REMOVE)) {
            if(msg.message == WM_MOUSEMOVE) {
                const int x = (short)LOWORD(msg.lParam);
                const int y = (short)HIWORD(msg.lParam);
                if(mouse_down) {
                    drag_cur_x = x;
                    drag_cur_y = y;
                    /* Repaint with the current drag position. */
                    paint_region_selector(hwnd, border_color, true, drag_start_x, drag_start_y, x, y);
                } else {
                    /* Track hovered window for WINDOW selection. */
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else if(msg.message == WM_LBUTTONDOWN) {
                mouse_down = true;
                drag_start_x = (short)LOWORD(msg.lParam);
                drag_start_y = (short)HIWORD(msg.lParam);
                drag_cur_x = drag_start_x;
                drag_cur_y = drag_start_y;
            } else if(msg.message == WM_LBUTTONUP) {
                const int x = (short)LOWORD(msg.lParam);
                const int y = (short)HIWORD(msg.lParam);
                if(mouse_down) {
                    mouse_down = false;
                    selected = true;
                    region.pos.x = virtual_x + (drag_start_x < x ? drag_start_x : x);
                    region.pos.y = virtual_y + (drag_start_y < y ? drag_start_y : y);
                    region.size.x = drag_start_x < x ? x - drag_start_x : drag_start_x - x;
                    region.size.y = drag_start_y < y ? y - drag_start_y : drag_start_y - y;
                    if(region.size.x < 2 || region.size.y < 2) {
                        /* Treat a click as canceled. */
                        canceled = true;
                        selected = false;
                    }
                }
                stop();
            } else if(msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                canceled = true;
                stop();
            }
            DispatchMessageA(&msg);
        }
    }

    void RegionSelectorWin32::handle_event(void *native_event) {
        (void)native_event;
        pump_messages();
    }

    void RegionSelectorWin32::on_left_button_down() {}
    void RegionSelectorWin32::on_mouse_move() {}
    void RegionSelectorWin32::on_left_button_up() {}
    void RegionSelectorWin32::on_key_escape() {}
    void RegionSelectorWin32::invalidate() {}

    bool RegionSelectorWin32::take_selection() {
        const bool was_selected = selected;
        selected = false;
        return was_selected;
    }

    bool RegionSelectorWin32::take_canceled() {
        const bool was_canceled = canceled;
        canceled = false;
        return was_canceled;
    }

    Region RegionSelectorWin32::get_region_selection(Display *x11_dpy, struct wl_display *wayland_dpy) const {
        (void)x11_dpy;
        (void)wayland_dpy;
        return region;
    }

    Window RegionSelectorWin32::get_window_selection() const {
        return 0; /* WINDOW selection is not supported on Windows in this milestone */
    }

    RegionSelector::SelectionType RegionSelectorWin32::get_selection_type() const {
        return selection_type;
    }
}
