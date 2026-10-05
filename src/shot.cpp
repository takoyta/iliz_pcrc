#define WIN32_LEAN_AND_MEAN
#include <string.h>
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include "shot.h"

HGLOBAL ShotCaptureJpeg(DWORD *outSize)
{
    *outSize = 0;
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (w < 1 || h < 1 || w > 16000 || h > 16000)
        return 0;

    HDC screen = GetDC(0);
    if (!screen)
        return 0;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *pixels = 0;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &pixels, 0, 0);
    HDC mem = CreateCompatibleDC(screen);
    BOOL blt = FALSE;
    if (bmp && mem && pixels) {
        HGDIOBJ old = SelectObject(mem, bmp);
        blt = BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT);
        SelectObject(mem, old);
    }
    if (mem)
        DeleteDC(mem);
    ReleaseDC(0, screen);
    if (!blt) {
        if (bmp)
            DeleteObject(bmp);
        return 0;
    }

    static const GUID clsidFac = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
    static const GUID iidFac = {0xec5ec8a9, 0xc395, 0x4314, {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}};
    static const GUID jpeg = {0x19e4a5aa, 0x5662, 0x4fc5, {0xa0, 0xc0, 0x17, 0x58, 0x02, 0x8e, 0x10, 0x57}};
    static const GUID fmt24 = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0c}};

    HRESULT co = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    BOOL uninit = (co == S_OK || co == S_FALSE);
    IWICImagingFactory *fac = 0;
    IWICBitmap *wb = 0;
    IWICBitmapEncoder *enc = 0;
    IWICBitmapFrameEncode *frame = 0;
    IStream *stream = 0;
    HGLOBAL owned = 0;
    HRESULT hr = CoCreateInstance(clsidFac, 0, CLSCTX_INPROC_SERVER, iidFac, (void **)&fac);
    if (SUCCEEDED(hr) && fac)
        hr = fac->CreateBitmapFromHBITMAP(bmp, 0, WICBitmapIgnoreAlpha, &wb);
    if (SUCCEEDED(hr))
        hr = CreateStreamOnHGlobal(0, FALSE, &stream);
    if (SUCCEEDED(hr) && fac)
        hr = fac->CreateEncoder(jpeg, 0, &enc);
    if (SUCCEEDED(hr) && enc)
        hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr) && enc)
        hr = enc->CreateNewFrame(&frame, 0);
    if (SUCCEEDED(hr) && frame)
        hr = frame->Initialize(0);
    if (SUCCEEDED(hr) && frame)
        hr = frame->SetSize((UINT)w, (UINT)h);
    if (SUCCEEDED(hr) && frame) {
        GUID fmt = fmt24;
        hr = frame->SetPixelFormat(&fmt);
    }
    if (SUCCEEDED(hr) && frame)
        hr = frame->WriteSource(wb, 0);
    if (SUCCEEDED(hr) && frame)
        hr = frame->Commit();
    if (SUCCEEDED(hr) && enc)
        hr = enc->Commit();
    if (SUCCEEDED(hr) && stream) {
        STATSTG st;
        memset(&st, 0, sizeof(st));
        HGLOBAL raw = 0;
        if (SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME)) &&
            SUCCEEDED(GetHGlobalFromStream(stream, &raw)) &&
            raw && st.cbSize.QuadPart > 0 && st.cbSize.QuadPart < 9000000) {
            owned = raw;
            *outSize = (DWORD)st.cbSize.QuadPart;
        }
    }

    HGLOBAL leftover = 0;
    if (stream && !owned)
        GetHGlobalFromStream(stream, &leftover);
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (wb) wb->Release();
    if (fac) fac->Release();
    if (stream) stream->Release();
    if (leftover)
        GlobalFree(leftover);
    DeleteObject(bmp);
    if (uninit)
        CoUninitialize();
    return owned;
}
