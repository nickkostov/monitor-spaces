#include "fullscreen_win32.h"
#include <shellapi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { const wchar_t *name; HANDLE value; } Property;
struct TestWindow {
    DWORD style, exstyle;
    WINDOWPLACEMENT placement;
    RECT rect;
    Property props[32];
    size_t count;
    BOOL owned, hung;
    unsigned mutations;
};
static struct TestWindow windows[4];
static HWND foreground;
static DWORD bar_state, last_error, original_bar, preference;
static BOOL bar_saved, preference_saved, deny_registry, deny_style, deny_placement;
static unsigned property_writes, deny_property;

LSTATUS RegCreateKeyExW(HKEY h, const wchar_t *n, DWORD r, wchar_t *c, DWORD o, DWORD a, void *s, HKEY *key, DWORD *d)
{ (void)h;(void)n;(void)r;(void)c;(void)o;(void)a;(void)s;(void)d; *key=(HKEY)1; return deny_registry ? ERROR_ACCESS_DENIED : ERROR_SUCCESS; }
LSTATUS RegOpenKeyExW(HKEY h, const wchar_t *n, DWORD o, DWORD a, HKEY *key)
{ (void)h;(void)n;(void)o;(void)a; *key=(HKEY)1; return deny_registry ? ERROR_ACCESS_DENIED : ERROR_SUCCESS; }
LSTATUS RegCloseKey(HKEY key) { (void)key; return ERROR_SUCCESS; }
LSTATUS RegSetValueExW(HKEY h, const wchar_t *name, DWORD r, DWORD type, const BYTE *data, DWORD size)
{
    (void)h;(void)r; assert(type==REG_DWORD && size==sizeof(DWORD));
    if (deny_registry) return ERROR_ACCESS_DENIED;
    if (!wcscmp(name,L"FullscreenEnabled")) { memcpy(&preference,data,size); preference_saved=TRUE; }
    else { memcpy(&original_bar,data,size); bar_saved=TRUE; }
    return ERROR_SUCCESS;
}
LSTATUS RegGetValueW(HKEY h, const wchar_t *key, const wchar_t *name, DWORD flags, DWORD *type, void *data, DWORD *size)
{
    (void)h;(void)key;(void)flags;(void)type;
    BOOL pref = !wcscmp(name,L"FullscreenEnabled");
    if (!(pref ? preference_saved : bar_saved)) return ERROR_FILE_NOT_FOUND;
    assert(*size>=sizeof(DWORD)); *(DWORD *)data=pref ? preference : original_bar; *size=sizeof(DWORD);
    return ERROR_SUCCESS;
}
LSTATUS RegDeleteValueW(HKEY key, const wchar_t *name)
{ (void)key;(void)name; if(deny_registry) return ERROR_ACCESS_DENIED; bar_saved=FALSE; return ERROR_SUCCESS; }
HANDLE GetPropW(HWND w, const wchar_t *name)
{ for(size_t i=0;i<w->count;++i) if(!wcscmp(w->props[i].name,name)) return w->props[i].value; return NULL; }
HANDLE RemovePropW(HWND w, const wchar_t *name)
{
    for(size_t i=0;i<w->count;++i) if(!wcscmp(w->props[i].name,name)) {
        HANDLE value=w->props[i].value; w->props[i]=w->props[--w->count]; return value;
    }
    return NULL;
}
BOOL SetPropW(HWND w, const wchar_t *name, HANDLE value)
{
    if (++property_writes==deny_property) return FALSE;
    for(size_t i=0;i<w->count;++i) if(!wcscmp(w->props[i].name,name)) { w->props[i].value=value; return TRUE; }
    assert(w->count<32); w->props[w->count++]=(Property){name,value}; return TRUE;
}
BOOL GetWindowPlacement(HWND w, WINDOWPLACEMENT *p) { *p=w->placement; return TRUE; }
BOOL SetWindowPlacement(HWND w, const WINDOWPLACEMENT *p)
{
    if(deny_placement) return FALSE;
    w->placement=*p; w->rect=p->rcNormalPosition; ++w->mutations;
    w->style &= ~(WS_MINIMIZE|WS_MAXIMIZE);
    if(p->showCmd==SW_SHOWMAXIMIZED) w->style |= WS_MAXIMIZE;
    if(p->showCmd==SW_SHOWMINIMIZED || p->showCmd==SW_SHOWMINNOACTIVE) w->style |= WS_MINIMIZE;
    if(p->showCmd==SW_HIDE) w->style &= ~WS_VISIBLE;
    return TRUE;
}
LONG_PTR GetWindowLongPtrW(HWND w, int index) { return index==GWL_STYLE ? w->style : w->exstyle; }
LONG_PTR SetWindowLongPtrW(HWND w, int index, LONG_PTR value)
{
    assert(index==GWL_STYLE); if(deny_style) { last_error=ERROR_ACCESS_DENIED; return 0; }
    DWORD old=w->style; w->style=(DWORD)value; ++w->mutations; return old;
}
void SetLastError(DWORD error) { last_error=error; }
DWORD GetLastError(void) { return last_error; }
BOOL IsWindow(HWND w) { return w!=NULL; }
BOOL IsHungAppWindow(HWND w) { return w->hung; }
BOOL IsIconic(HWND w) { return (w->style & WS_MINIMIZE)!=0; }
BOOL IsWindowVisible(HWND w) { return (w->style & WS_VISIBLE)!=0; }
BOOL GetWindowRect(HWND w, RECT *r) { *r=w->rect; return TRUE; }
BOOL EqualRect(const RECT *a, const RECT *b) { return a->left==b->left && a->top==b->top && a->right==b->right && a->bottom==b->bottom; }
HMONITOR MonitorFromWindow(HWND w, DWORD flags) { (void)w;(void)flags; return (HMONITOR)1; }
BOOL GetMonitorInfoW(HMONITOR h, MONITORINFO *i)
{ (void)h; i->rcMonitor=(RECT){-1920,-1080,0,0}; i->rcWork=(RECT){-1920,-1040,0,0}; return TRUE; }
HWND GetWindow(HWND w, UINT code) { assert(code==GW_OWNER); return w->owned ? &windows[0] : NULL; }
BOOL SetWindowPos(HWND w, HWND after, int x, int y, int width, int height, UINT flags)
{
    (void)after;
    if(!(flags & SWP_NOMOVE) && !(flags & SWP_NOSIZE)) {
        w->rect=(RECT){x,y,x+width,y+height}; w->placement.rcNormalPosition=w->rect;
    }
    ++w->mutations; return TRUE;
}
HWND GetForegroundWindow(void) { return foreground; }
BOOL SetForegroundWindow(HWND w) { foreground=w; return TRUE; }
BOOL EnumWindows(BOOL (*callback)(HWND, LPARAM), LPARAM p)
{ for(size_t i=0;i<4;++i) if(!callback(&windows[i],p)) return FALSE; return TRUE; }
HWND FindWindowW(const wchar_t *class_name, const wchar_t *title) { (void)class_name;(void)title; return &windows[3]; }
ULONG_PTR SHAppBarMessage(DWORD message, APPBARDATA *data)
{ if(message==ABM_SETSTATE) { assert(data->hWnd); bar_state=(DWORD)data->lParam; return TRUE; } assert(message==ABM_GETSTATE); return bar_state; }

static bool eligible(HWND w) { return IsWindowVisible(w)!=FALSE; }
static void reset(void)
{
    deny_registry=deny_style=deny_placement=FALSE; deny_property=0;
    ms_fullscreen_restore();
    memset(windows,0,sizeof(windows));
    windows[0].style=WS_VISIBLE|WS_CAPTION|WS_THICKFRAME;
    windows[0].placement=(WINDOWPLACEMENT){.length=sizeof(WINDOWPLACEMENT), .showCmd=SW_SHOWNORMAL,
        .ptMinPosition={-1,-1}, .ptMaxPosition={-1,-1}, .rcNormalPosition={-1800,-900,-700,-100}};
    windows[0].rect=windows[0].placement.rcNormalPosition;
    foreground=&windows[0]; bar_state=original_bar=0; bar_saved=preference_saved=FALSE;
    property_writes=0;
    ms_fullscreen_configure(eligible);
}

static void lifecycle(void)
{
    reset(); DWORD style=windows[0].style; RECT original=windows[0].rect;
    assert(ms_fullscreen_preferred());
    assert(ms_fullscreen_enable() && ms_fullscreen_enabled());
    assert(bar_saved && original_bar==0 && (bar_state & ABS_AUTOHIDE));
    assert(!(windows[0].style & (WS_CAPTION|WS_THICKFRAME)));
    assert(windows[0].rect.left==-1920 && windows[0].rect.top==-1080 && windows[0].rect.right==0 && windows[0].rect.bottom==0);
    assert(ms_fullscreen_original_state(&windows[0])==SW_SHOWNORMAL);
    unsigned mutations=windows[0].mutations;
    ms_fullscreen_update();
    assert(windows[0].mutations==mutations); /* Steady polling never resizes again. */
    bar_state=0; ms_fullscreen_update(); /* Explorer restart retains original state. */
    assert(bar_saved && original_bar==0 && (bar_state & ABS_AUTOHIDE));
    ms_fullscreen_suspend(); /* Discard run state; HWND properties/registry survive. */
    assert(!ms_fullscreen_enable()); /* Pending journal cannot be overwritten. */
    assert(ms_fullscreen_restore()==0 && !ms_fullscreen_enabled());
    assert(windows[0].style==style && EqualRect(&windows[0].rect,&original));
    assert(!windows[0].count && !bar_saved && !(bar_state & ABS_AUTOHIDE));
    assert(ms_fullscreen_restore()==0);
    ms_fullscreen_save_preference(false); assert(!ms_fullscreen_preferred());
    ms_fullscreen_save_preference(true); assert(ms_fullscreen_preferred());
    puts("PASS fullscreen lifecycle: negative monitors, original layout, idle polling, interrupted run, Explorer restart, preferences");
}

static void failures(void)
{
    reset(); assert(ms_fullscreen_enable()); unsigned writes=property_writes;
    assert(ms_fullscreen_restore()==0);
    for(unsigned fail=1;fail<=writes;++fail) {
        reset(); DWORD style=windows[0].style; RECT original=windows[0].rect;
        deny_property=fail;
        assert(ms_fullscreen_enable());
        assert(windows[0].style==style && EqualRect(&windows[0].rect,&original));
        assert(!windows[0].mutations && !windows[0].count);
        assert(ms_fullscreen_restore()==0);
    }
    reset(); deny_registry=TRUE;
    assert(!ms_fullscreen_enable() && !ms_fullscreen_enabled());
    assert(!windows[0].mutations && !(bar_state & ABS_AUTOHIDE));
    reset(); deny_style=TRUE; assert(ms_fullscreen_enable());
    assert(!windows[0].mutations && !windows[0].count);
    reset(); assert(ms_fullscreen_enable()); deny_placement=TRUE;
    assert(ms_fullscreen_restore()==1);
    assert(ms_fullscreen_original_state(&windows[0])==SW_SHOWNORMAL);
    assert(!bar_saved && !(bar_state & ABS_AUTOHIDE));
    deny_placement=FALSE; assert(ms_fullscreen_restore()==0 && !windows[0].count);
    puts("PASS fullscreen failures: denied journal writes never mutate windows, registry/style denial, failed restore retains retry data");
}

static void minimized_and_exclusions(void)
{
    reset(); windows[0].style |= WS_MAXIMIZE; windows[0].placement.showCmd=SW_SHOWMAXIMIZED;
    SetPropW(&windows[0],L"NonRudeHWND",(HANDLE)7);
    bar_state=ABS_AUTOHIDE;
    windows[1]=windows[0]; windows[1].count=0; windows[1].owned=TRUE;
    windows[2]=windows[1]; windows[2].owned=FALSE; windows[2].exstyle=WS_EX_TOOLWINDOW;
    assert(ms_fullscreen_enable());
    assert(!windows[1].mutations && !windows[2].mutations);
    assert(ms_fullscreen_original_state(&windows[0])==SW_SHOWMAXIMIZED);
    windows[0].style |= WS_MINIMIZE; /* Inactive workspace or user minimization. */
    assert(ms_fullscreen_restore()==0 && IsIconic(&windows[0]));
    assert(windows[0].placement.flags & WPF_RESTORETOMAXIMIZED);
    assert(GetPropW(&windows[0],L"NonRudeHWND")== (HANDLE)7);
    assert(windows[0].count==1 && (bar_state & ABS_AUTOHIDE));
    reset(); windows[0].style &= ~WS_THICKFRAME;
    assert(ms_fullscreen_enable() && !windows[0].mutations); /* Fixed-size dialogs. */
    assert(ms_fullscreen_restore()==0);
    reset(); windows[0].hung=TRUE;
    assert(ms_fullscreen_enable() && !windows[0].mutations);
    assert(ms_fullscreen_restore()==0);
    puts("PASS fullscreen minimized/maximized state, pre-existing auto-hide/shell properties, owned/tool/fixed/hung exclusions");
}

static void preserve_dragged_size(void)
{
    reset(); DWORD original_style=windows[0].style; RECT original=windows[0].rect;
    assert(ms_fullscreen_enable());
    assert(ms_fullscreen_preserve_size(&windows[0]));
    assert(windows[0].style==original_style && EqualRect(&windows[0].rect,&original));
    assert(ms_fullscreen_original_state(&windows[0])==0);
    unsigned mutations=windows[0].mutations;
    ms_fullscreen_apply(&windows[0]); ms_fullscreen_update();
    assert(windows[0].mutations==mutations);
    assert(ms_fullscreen_enabled() && (bar_state & ABS_AUTOHIDE));
    windows[0].style |= WS_MINIMIZE; /* Moving to an inactive workspace. */
    ms_fullscreen_update();
    windows[0].style &= ~WS_MINIMIZE; /* Workspace return / taskbar activation. */
    ms_fullscreen_apply(&windows[0]);
    assert(windows[0].mutations==mutations && EqualRect(&windows[0].rect,&original));

    windows[1]=windows[0]; windows[1].count=0; windows[1].mutations=0;
    ms_fullscreen_update(); /* Other apps still use the selected full-screen mode. */
    assert(!(windows[1].style & WS_CAPTION));
    assert(windows[0].style==original_style);
    assert(ms_fullscreen_restore()==0 && !windows[0].count);
    assert(ms_fullscreen_enable()); /* An explicit new session resets exceptions. */
    assert(!(windows[0].style & WS_CAPTION));

    reset(); windows[1]=windows[0];
    windows[1].style=0; /* Window opened after the last full-screen scan. */
    assert(ms_fullscreen_enable());
    windows[1].style=original_style;
    mutations=windows[1].mutations;
    assert(ms_fullscreen_preserve_size(&windows[1]));
    ms_fullscreen_apply(&windows[1]); ms_fullscreen_update();
    assert(windows[1].mutations==mutations && windows[1].style==original_style);
    assert(EqualRect(&windows[1].rect,&original));

    reset();
    windows[0].placement.rcNormalPosition=(RECT){100,100,900,700}; /* Saved on a different monitor. */
    windows[0].rect=windows[0].placement.rcNormalPosition;
    assert(ms_fullscreen_enable() && ms_fullscreen_preserve_size(&windows[0]));
    RECT fitted=windows[0].placement.rcNormalPosition;
    assert(fitted.right-fitted.left==800 && fitted.bottom-fitted.top==600);
    assert(fitted.left>=-1920 && fitted.top>=-1080 && fitted.right<=0 && fitted.bottom<=-40);
    reset();
    windows[0].placement.rcNormalPosition=(RECT){-2000,-1200,3000,1800};
    windows[0].rect=windows[0].placement.rcNormalPosition;
    assert(ms_fullscreen_enable() && ms_fullscreen_preserve_size(&windows[0]));
    fitted=windows[0].placement.rcNormalPosition;
    assert(fitted.right-fitted.left==1920 && fitted.bottom-fitted.top==1040);
    assert(windows[0].style==original_style); /* Fit oversized windows, retaining the frame. */

    reset(); windows[0].style |= WS_MAXIMIZE; windows[0].placement.showCmd=SW_SHOWMAXIMIZED;
    assert(ms_fullscreen_enable());
    windows[0].style |= WS_MINIMIZE;
    assert(ms_fullscreen_preserve_size(&windows[0]));
    assert(IsIconic(&windows[0]) && (windows[0].style & WS_CAPTION));
    assert(windows[0].placement.flags & WPF_RESTORETOMAXIMIZED);
    assert(EqualRect(&windows[0].placement.rcNormalPosition,&original));

    reset(); assert(ms_fullscreen_enable()); mutations=windows[0].mutations;
    deny_property=property_writes+1;
    assert(!ms_fullscreen_preserve_size(&windows[0]));
    assert(windows[0].mutations==mutations); /* No resize without a durable exception. */
    deny_property=0; deny_placement=TRUE;
    assert(!ms_fullscreen_preserve_size(&windows[0]));
    assert(ms_fullscreen_original_state(&windows[0])==SW_SHOWNORMAL);
    mutations=windows[0].mutations;
    ms_fullscreen_update(); assert(windows[0].mutations==mutations);
    deny_placement=FALSE;
    assert(ms_fullscreen_preserve_size(&windows[0]));
    assert(EqualRect(&windows[0].rect,&original));
    ms_fullscreen_suspend(); /* Recovery clears surviving drag exceptions. */
    assert(ms_fullscreen_restore()==0 && !windows[0].count);
    reset(); assert(ms_fullscreen_preserve_size(&windows[0]) && !windows[0].count);
    puts("PASS dragged apps: preserve frame/size across polling and workspace return, restore inactive/maximized state, retry failures, reset exceptions");
}

int main(void)
{
    lifecycle(); failures(); minimized_and_exclusions(); preserve_dragged_size();
    return 0;
}
