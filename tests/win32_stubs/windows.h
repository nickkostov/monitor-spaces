#ifndef MS_TEST_WINDOWS_H
#define MS_TEST_WINDOWS_H
/* Minimal host-side Win32 boundary for exercising the real fullscreen module.
 * The Windows build never includes this directory. No shell/GUI behavior is emulated. */
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
typedef int BOOL;
typedef uint32_t DWORD, UINT;
typedef int32_t LONG, LSTATUS;
typedef intptr_t LONG_PTR, LPARAM;
typedef uintptr_t ULONG_PTR;
typedef unsigned char BYTE;
typedef void *HANDLE, *HKEY, *HMONITOR;
typedef struct TestWindow *HWND;
typedef struct { LONG x, y; } POINT;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { UINT length, flags, showCmd; POINT ptMinPosition, ptMaxPosition; RECT rcNormalPosition; } WINDOWPLACEMENT;
typedef struct { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; } MONITORINFO;
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define HKEY_CURRENT_USER ((HKEY)1)
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define KEY_SET_VALUE 2
#define REG_DWORD 4
#define RRF_RT_REG_DWORD 16
#define WS_CAPTION 0x00c00000u
#define WS_THICKFRAME 0x00040000u
#define WS_VISIBLE 0x10000000u
#define WS_DISABLED 0x08000000u
#define WS_MINIMIZE 0x20000000u
#define WS_MAXIMIZE 0x01000000u
#define WS_EX_TOOLWINDOW 0x80u
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_SHOWMINNOACTIVE 7
#define WPF_RESTORETOMAXIMIZED 2
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GW_OWNER 4
#define MONITOR_DEFAULTTONEAREST 2
#define SWP_NOMOVE 2
#define SWP_NOSIZE 1
#define SWP_NOZORDER 4
#define SWP_NOOWNERZORDER 512
#define SWP_NOACTIVATE 16
#define SWP_FRAMECHANGED 32
LSTATUS RegCreateKeyExW(HKEY, const wchar_t *, DWORD, wchar_t *, DWORD, DWORD, void *, HKEY *, DWORD *);
LSTATUS RegSetValueExW(HKEY, const wchar_t *, DWORD, DWORD, const BYTE *, DWORD);
LSTATUS RegGetValueW(HKEY, const wchar_t *, const wchar_t *, DWORD, DWORD *, void *, DWORD *);
LSTATUS RegOpenKeyExW(HKEY, const wchar_t *, DWORD, DWORD, HKEY *);
LSTATUS RegDeleteValueW(HKEY, const wchar_t *);
LSTATUS RegCloseKey(HKEY);
BOOL SetPropW(HWND, const wchar_t *, HANDLE);
HANDLE GetPropW(HWND, const wchar_t *);
HANDLE RemovePropW(HWND, const wchar_t *);
BOOL GetWindowPlacement(HWND, WINDOWPLACEMENT *);
BOOL SetWindowPlacement(HWND, const WINDOWPLACEMENT *);
LONG_PTR GetWindowLongPtrW(HWND, int);
LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR);
BOOL IsWindow(HWND);
BOOL IsHungAppWindow(HWND);
BOOL IsIconic(HWND);
BOOL IsWindowVisible(HWND);
BOOL GetWindowRect(HWND, RECT *);
BOOL EqualRect(const RECT *, const RECT *);
HMONITOR MonitorFromWindow(HWND, DWORD);
BOOL GetMonitorInfoW(HMONITOR, MONITORINFO *);
HWND GetWindow(HWND, UINT);
BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT);
HWND GetForegroundWindow(void);
BOOL SetForegroundWindow(HWND);
BOOL EnumWindows(BOOL (*)(HWND, LPARAM), LPARAM);
HWND FindWindowW(const wchar_t *, const wchar_t *);
void SetLastError(DWORD);
DWORD GetLastError(void);
#endif
