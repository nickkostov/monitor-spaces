#ifndef UNICODE
#define UNICODE
#endif
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shlobj.h>
#include <wincodec.h>
#include <stdlib.h>

#include "app_icons_win32.h"

void ms_icon_loader_open(MSIconLoader *loader)
{
    *loader = (MSIconLoader){0};
    HRESULT result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    loader->apartment = SUCCEEDED(result);
    if (SUCCEEDED(result) || result == RPC_E_CHANGED_MODE)
        CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                         &IID_IWICImagingFactory, &loader->factory);
}

void ms_icon_loader_close(MSIconLoader *loader)
{
    if (loader->factory)
        IWICImagingFactory_Release((IWICImagingFactory *)loader->factory);
    if (loader->apartment)
        CoUninitialize();
    *loader = (MSIconLoader){0};
}

static int icon_size(HICON icon)
{
    ICONINFO info = {0};
    if (!icon || !GetIconInfo(icon, &info))
        return 0;
    BITMAP bitmap = {0};
    int size = 0;
    if (GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bitmap), &bitmap)) {
        int height = info.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
        size = bitmap.bmWidth < height ? bitmap.bmWidth : height;
    }
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return size;
}

/* Borrowed window/class handles must never be destroyed or resized in place. */
static void consider(HICON candidate, HICON *best, int *size)
{
    int candidate_size = icon_size(candidate);
    if (candidate_size <= *size)
        return;
    HICON copy = CopyIcon(candidate);
    if (!copy)
        return;
    if (*best) DestroyIcon(*best);
    *best = copy;
    *size = candidate_size;
}

static HICON resize_icon(MSIconLoader *loader, HICON icon, int size)
{
    if (!icon)
        return NULL;
    int original_size = icon_size(icon);
    if (!loader->factory || original_size == size)
        return CopyIcon(icon);
    IWICImagingFactory *factory = loader->factory;
    IWICBitmap *source = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICBitmapSource *pixels = NULL;
    HBITMAP bitmap = NULL, mask = NULL;
    HICON result = NULL;
    if (FAILED(IWICImagingFactory_CreateBitmapFromHICON(factory, icon, &source)) ||
        FAILED(IWICImagingFactory_CreateBitmapScaler(factory, &scaler)) ||
        FAILED(IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)source, size, size,
            original_size > size ? WICBitmapInterpolationModeFant : WICBitmapInterpolationModeCubic)) ||
        FAILED(WICConvertBitmapSource(&GUID_WICPixelFormat32bppPBGRA, (IWICBitmapSource *)scaler, &pixels)))
        goto done;
    BITMAPINFO format = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER), .biWidth = size,
        .biHeight = -size, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB}};
    void *bits = NULL;
    bitmap = CreateDIBSection(NULL, &format, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bitmap || FAILED(IWICBitmapSource_CopyPixels(pixels, NULL, (UINT)size * 4,
                                                      (UINT)size * (UINT)size * 4, bits)))
        goto done;
    size_t mask_bytes = (size_t)((size + 15) / 16) * 2 * (size_t)size;
    void *mask_bits = calloc(1, mask_bytes);
    if (mask_bits) {
        mask = CreateBitmap(size, size, 1, 1, mask_bits);
        free(mask_bits);
    }
    if (mask) {
        ICONINFO info = {.fIcon = TRUE, .hbmMask = mask, .hbmColor = bitmap};
        result = CreateIconIndirect(&info);
    }
done:
    if (mask) DeleteObject(mask);
    if (bitmap) DeleteObject(bitmap);
    if (pixels) IWICBitmapSource_Release(pixels);
    if (scaler) IWICBitmapScaler_Release(scaler);
    if (source) IWICBitmap_Release(source);
    return result ? result : CopyIcon(icon);
}

MSAppIcons ms_app_icons_load(MSIconLoader *loader, HWND window, DWORD process,
                            unsigned dpi, ULONGLONG deadline)
{
    int large = MulDiv(48, (int)dpi, 96), caption = MulDiv(20, (int)dpi, 96);
    int miniature = MulDiv(16, (int)dpi, 96);
    if (large < 1 || large > 512 || caption < 1 || miniature < 1)
        return (MSAppIcons){0};
    HICON best = NULL;
    int best_size = 0;
    if (GetTickCount64() < deadline) {
        DWORD_PTR icon = 0;
        /* ICON_BIG is nominally 32 logical pixels. Ask for the DPI that supplies
         * the actual 48-logical-pixel preview, rather than enlarging ICON_SMALL2. */
        SendMessageTimeoutW(window, WM_GETICON, ICON_BIG, (LPARAM)MulDiv((int)dpi, 48, 32),
                            SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 30, &icon);
        consider((HICON)icon, &best, &best_size);
    }
    consider((HICON)GetClassLongPtrW(window, GCLP_HICON), &best, &best_size);
    if (best_size < large && GetTickCount64() < deadline) {
        /* Executables commonly contain 48/64/128/256-pixel resources even when
         * their windows expose only a legacy 16/32-pixel HICON. */
        DWORD current_process = 0;
        if (GetWindowThreadProcessId(window, &current_process) && current_process == process) {
            HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
            if (handle) {
                wchar_t path[32768];
                DWORD length = sizeof(path) / sizeof(path[0]);
                if (QueryFullProcessImageNameW(handle, 0, path, &length) && GetTickCount64() < deadline) {
                    HICON extracted = NULL;
                    SHDefExtractIconW(path, 0, 0, &extracted, NULL, (UINT)large);
                    consider(extracted, &best, &best_size);
                    if (extracted) DestroyIcon(extracted);
                }
                CloseHandle(handle);
            }
        }
    }
    if (!best && GetTickCount64() < deadline) {
        DWORD_PTR icon = 0;
        SendMessageTimeoutW(window, WM_GETICON, ICON_SMALL2, dpi,
                            SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 30, &icon);
        consider((HICON)icon, &best, &best_size);
    }
    if (!best)
        consider((HICON)GetClassLongPtrW(window, GCLP_HICONSM), &best, &best_size);
    if (!best)
        consider((HICON)LoadImageW(NULL, IDI_APPLICATION, IMAGE_ICON, large, large, LR_SHARED), &best, &best_size);
    MSAppIcons icons = {resize_icon(loader, best, large), resize_icon(loader, best, caption),
                        resize_icon(loader, best, miniature)};
    if (best) DestroyIcon(best);
    return icons;
}
