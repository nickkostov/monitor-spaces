#ifndef MS_TEST_SHELLAPI_H
#define MS_TEST_SHELLAPI_H
#include "windows.h"
typedef struct { DWORD cbSize; HWND hWnd; UINT uCallbackMessage, uEdge; RECT rc; LPARAM lParam; } APPBARDATA;
#define ABM_GETSTATE 4
#define ABM_SETSTATE 10
#define ABS_AUTOHIDE 1
#define ABS_ALWAYSONTOP 2
ULONG_PTR SHAppBarMessage(DWORD, APPBARDATA *);
#endif
