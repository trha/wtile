#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <memory>
#include <type_traits>
#include <fmt/format.h>
#include <vector>
#include <shlobj.h>
#include <atlbase.h>
#include <comutil.h>

auto make_unique_hook(HHOOK h) {
  return std::unique_ptr<std::remove_pointer_t<HHOOK>, decltype(&UnhookWindowsHookEx)>{h, &UnhookWindowsHookEx};
}

struct {
  HHOOK kb_hook;
  HHOOK mouse_hook;
} global;

bool is_key_down(int vk) {
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

namespace my_msg {
  enum : UINT {
    divvy_hotkey_pressed = WM_USER + 1,
    esc_pressed,
    shift_focus,
    left_up,
  };
}

enum class Dir {
  h, j, k, l
};

bool move_divvy_to_cursor() {
  auto divvy_wnd = FindWindowW(L"QTool", nullptr);
  if (!divvy_wnd) {
    return false;
  }

  POINT cursor;
  if (!GetCursorPos(&cursor)) {
    return false;
  }

  SetWindowPos(divvy_wnd, nullptr, cursor.x, cursor.y, 0, 0, SWP_NOSIZE | SWP_ASYNCWINDOWPOS);
  return true;
}

std::vector<HWND> top_level_wnds(IVirtualDesktopManager* mgr) {
  struct State{
    IVirtualDesktopManager* mgr;
    std::vector<HWND> r;
  } state{mgr};

  EnumWindows([] (HWND wnd, LPARAM lp) {
    auto& state = *reinterpret_cast<State*>(lp);
    WINDOWPLACEMENT placement;
    placement.length = sizeof(placement);
    wchar_t title[255]{};
    BOOL is_on_current_desktop;

    if (IsWindowVisible(wnd) && !IsIconic(wnd)
      && (GetWindowLongPtr(wnd, GWL_STYLE) & WS_MAXIMIZEBOX)
      && SUCCEEDED(state.mgr->IsWindowOnCurrentVirtualDesktop(wnd, &is_on_current_desktop)) && is_on_current_desktop 
      && GetWindowPlacement(wnd, &placement) && !(placement.showCmd & SW_SHOWMINIMIZED) && ((placement.showCmd & SW_SHOWNORMAL) || (placement.showCmd & SW_SHOWMAXIMIZED)) 
      && GetWindowTextW(wnd, title, std::size(title)) && title[0] && wcscmp(title, L"Program Manager") != 0) {
      state.r.push_back(wnd);
    }
    return TRUE;
  }, reinterpret_cast<LPARAM>(&state));
  return state.r;
}

void shift_focus(std::vector<HWND> wnds, Dir dir) {
  auto b = cbegin(wnds);
  auto e = cend(wnds);
  if (b == e) {
    return;
  }

  auto fg = *b++;
  RECT fg_rc;
  if (!GetWindowRect(fg, &fg_rc)) {
    return;
  }

  struct Best{
    HWND wnd;
    RECT rc;
  } best{};

  for (; b != e; ++b) {
    auto wnd = *b;
    RECT rc;
    if (!GetWindowRect(wnd, &rc)) {
      continue;
    }

    wchar_t buff[255]{};
    wchar_t buff2[255]{};
    GetWindowTextW(wnd, buff, std::size(buff));
    GetClassNameW(wnd, buff2, std::size(buff2));

    switch (dir) {
    case Dir::h: 
    if (rc.left < fg_rc.left && (best.rc.top < 1 || std::abs(rc.top - fg_rc.top) < std::abs(best.rc.top - fg_rc.top))) {
      best = Best{ wnd, rc };
    }
    break;

    case Dir::j: 
    if (rc.top > fg_rc.top && (best.rc.top < 1 || std::abs(rc.left - fg_rc.left) < std::abs(best.rc.left - fg_rc.left))) {
      best = Best{ wnd, rc };
    }
    break;

    case Dir::k: 
    if (rc.top < fg_rc.top && (best.rc.top < 1 || std::abs(rc.left - fg_rc.left) < std::abs(best.rc.left - fg_rc.left))) {
      best = Best{ wnd, rc };
    }
    break;

    case Dir::l: 
    if (rc.left > fg_rc.left && (best.rc.top < 1 || std::abs(rc.top - fg_rc.top) < std::abs(best.rc.top - fg_rc.top))) {
      best = Best{ wnd, rc };
    }
    break;
    }
  }

  if (best.wnd) {
    SetForegroundWindow(best.wnd);
  }
}
const CLSID CLSID_ImmersiveShell = {
  0xC2F03A33, 0x21F5, 0x47FA, 0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39 };

int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
  if (FAILED(CoInitialize(nullptr))) {
    return -1;
  }

  CComPtr<IServiceProvider> shell;
  if (FAILED(shell.CoCreateInstance(CLSID_ImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER))) {
    return -2;
  }

  CComPtr<IVirtualDesktopManager> desktop_mgr;
  if (FAILED(shell->QueryService(__uuidof(IVirtualDesktopManager), &desktop_mgr))) {
    return -3;
  }

  auto kb_hook = make_unique_hook(SetWindowsHookExW(WH_KEYBOARD_LL, [] (int code, WPARAM wp, LPARAM lp) -> LRESULT {
    auto vk = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp)->vkCode;
    if (wp == WM_KEYUP) {
      switch (vk) {
      case 0x5A:
      if (vk == 0x5A && is_key_down(VK_LWIN) && is_key_down(VK_LSHIFT)) {
        PostThreadMessageW(GetCurrentThreadId(), my_msg::divvy_hotkey_pressed, 0, 0);
      }
      break;

      case VK_ESCAPE:
      PostThreadMessageW(GetCurrentThreadId(), my_msg::esc_pressed, 0, 0);
      break;
      }

    }

    if (is_key_down(VK_LWIN) && is_key_down(VK_SHIFT)) {
      switch (vk) {
      case 0x48: if (wp == WM_KEYUP) PostThreadMessageW(GetCurrentThreadId(), my_msg::shift_focus, static_cast<WPARAM>(Dir::h), 0); return 1;
      case 0x4a: if (wp == WM_KEYUP) PostThreadMessageW(GetCurrentThreadId(), my_msg::shift_focus, static_cast<WPARAM>(Dir::j), 0); return 1;
      case 0x4b: if (wp == WM_KEYUP) PostThreadMessageW(GetCurrentThreadId(), my_msg::shift_focus, static_cast<WPARAM>(Dir::k), 0); return 1;
      case 0x4c: if (wp == WM_KEYUP) PostThreadMessageW(GetCurrentThreadId(), my_msg::shift_focus, static_cast<WPARAM>(Dir::l), 0); return 1;
      }
    }
    return CallNextHookEx(global.kb_hook, code, wp, lp);
  }, nullptr, 0));

  auto mouse_hook = make_unique_hook(SetWindowsHookExW(WH_MOUSE_LL, [] (int code, WPARAM wp, LPARAM lp) {
    if (wp == WM_LBUTTONUP) {
      PostThreadMessageW(GetCurrentThreadId(), my_msg::left_up, 0, 0);
    }

    return CallNextHookEx(global.mouse_hook, code, wp, lp);
  }, nullptr, 0));

  global.kb_hook =  kb_hook.get();
  global.mouse_hook = mouse_hook.get();
  
  struct {
    HWND last_fg = nullptr;
    bool activated = false;
  } divvy{};

  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0)) {
    switch (m.message) {
    case my_msg::divvy_hotkey_pressed:
    divvy.activated = move_divvy_to_cursor();
    if (divvy.activated) {
      divvy.last_fg = GetForegroundWindow();
    }
    break;

    case my_msg::left_up: {
      auto fg = GetForegroundWindow();
      if (divvy.activated && fg != divvy.last_fg) {
        divvy.last_fg = fg;
        move_divvy_to_cursor();
      }
      break;
    }

    case my_msg::esc_pressed:
    divvy.activated = false;
    break;
    case my_msg::shift_focus: 
    shift_focus(top_level_wnds(desktop_mgr.p), static_cast<Dir>(m.wParam));
    break;

    }
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
}