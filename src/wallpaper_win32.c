#ifndef UNICODE
#define UNICODE
#endif
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <stdint.h>
#include <limits.h>

#include "wallpaper_win32.h"
#include "overview_layout.h"

/* WIC handles the user's JPEG/PNG/BMP wallpaper without shipping an image codec.
 * Decode through a scaler to bound allocation even for very large source files. */
static HBITMAP decode(const wchar_t *path, SIZE *original)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICBitmapSource *pixels = NULL;
    HBITMAP bitmap = NULL;
    UINT width = 0, height = 0;
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                               &IID_IWICImagingFactory, (void **)&factory)) ||
        FAILED(IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ,
                WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)) ||
        FAILED(IWICBitmapFrameDecode_GetSize(frame, &width, &height)) || !width || !height ||
        width > INT_MAX || height > INT_MAX)
        goto done;
    *original = (SIZE){(LONG)width, (LONG)height};
    UINT largest = width > height ? width : height;
    UINT target_width = largest > 4096 ? (UINT)((uint64_t)width * 4096 / largest) : width;
    UINT target_height = largest > 4096 ? (UINT)((uint64_t)height * 4096 / largest) : height;
    if (!target_width) target_width = 1;
    if (!target_height) target_height = 1;
    if (FAILED(IWICImagingFactory_CreateBitmapScaler(factory, &scaler)) ||
        FAILED(IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)frame,
                target_width, target_height, WICBitmapInterpolationModeFant)) ||
        FAILED(WICConvertBitmapSource(&GUID_WICPixelFormat32bppBGR, (IWICBitmapSource *)scaler, &pixels)))
        goto done;
    BITMAPINFO info = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER),
        .biWidth = (LONG)target_width, .biHeight = -(LONG)target_height,
        .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
    void *bits = NULL;
    bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bitmap && FAILED(IWICBitmapSource_CopyPixels(pixels, NULL, target_width * 4,
            target_width * target_height * 4, bits))) {
        DeleteObject(bitmap);
        bitmap = NULL;
    }
done:
    if (pixels) IWICBitmapSource_Release(pixels);
    if (scaler) IWICBitmapScaler_Release(scaler);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    return bitmap;
}

HBITMAP ms_wallpaper_load(HMONITOR monitor)
{
    MONITORINFO info = {.cbSize = sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info))
        return NULL;
    int width = info.rcMonitor.right - info.rcMonitor.left;
    int height = info.rcMonitor.bottom - info.rcMonitor.top;
    /* Two monitor-sized DIBs are used by the overview; reject unreasonable sizes. */
    if (width <= 0 || height <= 0 || (uint64_t)width * height > 64000000)
        return NULL;
    HRESULT apartment = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE)
        return NULL;
    IDesktopWallpaper *desktop = NULL;
    LPWSTR path = NULL;
    DESKTOP_WALLPAPER_POSITION position = DWPOS_FILL;
    COLORREF color = GetSysColor(COLOR_BACKGROUND);
    if (SUCCEEDED(CoCreateInstance(&CLSID_DesktopWallpaper, NULL, CLSCTX_ALL,
                                  &IID_IDesktopWallpaper, (void **)&desktop))) {
        IDesktopWallpaper_GetBackgroundColor(desktop, &color);
        IDesktopWallpaper_GetPosition(desktop, &position);
        UINT count = 0;
        IDesktopWallpaper_GetMonitorDevicePathCount(desktop, &count);
        for (UINT i = 0; i < count; ++i) {
            LPWSTR id = NULL;
            RECT rect;
            if (SUCCEEDED(IDesktopWallpaper_GetMonitorDevicePathAt(desktop, i, &id))) {
                if (SUCCEEDED(IDesktopWallpaper_GetMonitorRECT(desktop, id, &rect)) &&
                    EqualRect(&rect, &info.rcMonitor))
                    IDesktopWallpaper_GetWallpaper(desktop, id, &path);
                CoTaskMemFree(id);
            }
            if (path) break;
        }
    }
    wchar_t fallback[32768] = {0};
    if (!path && !desktop)
        SystemParametersInfoW(SPI_GETDESKWALLPAPER, 32768, fallback, 0);
    SIZE original = {0};
    const wchar_t *filename = path ? path : fallback;
    HBITMAP image = *filename ? decode(filename, &original) : NULL;
    BITMAPINFO format = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER),
        .biWidth = width, .biHeight = -height, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
    void *bits = NULL;
    HBITMAP result = CreateDIBSection(NULL, &format, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC dc = CreateCompatibleDC(NULL), source = CreateCompatibleDC(NULL);
    HGDIOBJ old = NULL, old_source = NULL;
    if (result && dc && source) {
        old = SelectObject(dc, result);
        RECT client = {0, 0, width, height};
        HBRUSH brush = CreateSolidBrush(color);
        if (brush) { FillRect(dc, &client, brush); DeleteObject(brush); }
        if (image) {
            old_source = SelectObject(source, image);
            BITMAP bitmap;
            GetObjectW(image, sizeof(bitmap), &bitmap);
            RECT destination = client;
            MSOverviewRect area = {0, 0, width, height};
            if (position == DWPOS_SPAN)
                area = (MSOverviewRect){GetSystemMetrics(SM_XVIRTUALSCREEN) - info.rcMonitor.left,
                    GetSystemMetrics(SM_YVIRTUALSCREEN) - info.rcMonitor.top,
                    GetSystemMetrics(SM_XVIRTUALSCREEN) - info.rcMonitor.left + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                    GetSystemMetrics(SM_YVIRTUALSCREEN) - info.rcMonitor.top + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
            MSOverviewRect fitted = position == DWPOS_FILL || position == DWPOS_SPAN ?
                ms_overview_cover(area, original.cx, original.cy) : ms_overview_fit(area, original.cx, original.cy);
            if (position == DWPOS_FILL || position == DWPOS_SPAN || position == DWPOS_FIT)
                destination = (RECT){fitted.left, fitted.top, fitted.right, fitted.bottom};
            else if (position == DWPOS_CENTER)
                destination = (RECT){width / 2 - original.cx / 2, height / 2 - original.cy / 2,
                    width / 2 - original.cx / 2 + original.cx, height / 2 - original.cy / 2 + original.cy};
            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, NULL);
            if (position == DWPOS_TILE) {
                for (int y = 0; y < height; y += original.cy)
                    for (int x = 0; x < width; x += original.cx)
                        StretchBlt(dc, x, y, original.cx, original.cy, source, 0, 0,
                                   bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
            } else {
                StretchBlt(dc, destination.left, destination.top, destination.right - destination.left,
                    destination.bottom - destination.top, source, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
            }
        }
    } else {
        if (result) DeleteObject(result);
        result = NULL;
    }
    if (old_source) SelectObject(source, old_source);
    if (old) SelectObject(dc, old);
    if (source) DeleteDC(source);
    if (dc) DeleteDC(dc);
    if (image) DeleteObject(image);
    CoTaskMemFree(path);
    if (desktop) IDesktopWallpaper_Release(desktop);
    if (SUCCEEDED(apartment)) CoUninitialize();
    return result;
}
