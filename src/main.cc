#include <windows.h>
#include <commctrl.h>

#include "main_window.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*pCmdLine*/, int nCmdShow) {
  // Single instance control per user session
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\LiteProcManager_SingleInstance_Mutex");
  if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
    if (mutex) {
      CloseHandle(mutex);
    }
    // Activate and bring existing instance to foreground
    HWND existing_hwnd = FindWindowW(L"LiteProcManagerMainWindow", nullptr);
    if (existing_hwnd) {
      if (!IsWindowVisible(existing_hwnd) || IsIconic(existing_hwnd)) {
        ShowWindow(existing_hwnd, SW_SHOW);
        ShowWindow(existing_hwnd, SW_RESTORE);
      } else {
        ShowWindow(existing_hwnd, SW_SHOW);
      }
      SetForegroundWindow(existing_hwnd);
    }
    return 0;
  }

  int exit_code = 0;
  {
    lite_proc_manager::MainWindow main_window;
    if (main_window.Create(hInstance, nCmdShow)) {
      exit_code = main_window.RunMessageLoop();
    } else {
      exit_code = 1;
    }
  }

  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return exit_code;
}
