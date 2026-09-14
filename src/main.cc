#include <windows.h>
#include <commctrl.h>

#include "main_window.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
HANDLE g_single_instance_mutex = nullptr;
}  // namespace

// Releases the single-instance lock before this process relaunches itself
// (self-restart / restart-as-admin). Without this, the freshly spawned
// process can start and probe the mutex before this process has fully torn
// down, see ERROR_ALREADY_EXISTS, and quit thinking another instance is
// already running - i.e. the "restart" silently does nothing.
void ReleaseSingleInstanceLock() {
  if (g_single_instance_mutex != nullptr) {
    ReleaseMutex(g_single_instance_mutex);
    CloseHandle(g_single_instance_mutex);
    g_single_instance_mutex = nullptr;
  }
}

// Re-establishes the single-instance lock after a restart attempt was
// aborted (e.g. the UAC prompt was cancelled), so this still-running
// process stays protected against a duplicate launch.
void RestoreSingleInstanceLock(HANDLE mutex) {
  g_single_instance_mutex = mutex;
}

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

  g_single_instance_mutex = mutex;

  HRESULT com_result = CoInitializeEx(
      nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

  int exit_code = 0;
  {
    lite_proc_manager::MainWindow main_window;
    if (main_window.Create(hInstance, nCmdShow)) {
      exit_code = main_window.RunMessageLoop();
    } else {
      exit_code = 1;
    }
  }

  // A restart flow may have already released and cleared this via
  // ReleaseSingleInstanceLock().
  ReleaseSingleInstanceLock();
  if (SUCCEEDED(com_result)) {
    CoUninitialize();
  }
  return exit_code;
}
