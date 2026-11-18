#include "platform/droptarget.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>

static const IID nw_droptarget_unknown = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const IID nw_droptarget_iid = { 0x00000122, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };

static struct {
    IDropTarget target;
    HWND hwnd;
    bool ole;
    bool accepted;
    NwDropAccept accept;
    Uint32 event;
    Sint32 code;
} nw_droptarget;

static char *nw_droptarget_list(HDROP drop, NwDropAccept accept)
{
    char *found = NULL;
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    for (UINT i = 0; i < count && !found; i++) {
        UINT len = DragQueryFileW(drop, i, NULL, 0);
        WCHAR *wide = SDL_malloc((len + 1) * sizeof(WCHAR));
        if (wide && DragQueryFileW(drop, i, wide, len + 1)) {
            char *path = SDL_iconv_string("UTF-8", "UTF-16LE", (const char *)wide, (len + 1) * sizeof(WCHAR));
            if (path && accept(path)) {
                found = path;
            } else {
                SDL_free(path);
            }
        }
        SDL_free(wide);
    }
    return found;
}

static char *nw_droptarget_file(IDataObject *data)
{
    FORMATETC format = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium;
    if (FAILED(data->lpVtbl->GetData(data, &format, &medium))) {
        return NULL;
    }
    char *found = nw_droptarget_list((HDROP)medium.hGlobal, nw_droptarget.accept);
    ReleaseStgMedium(&medium);
    return found;
}

char *nw_droptarget_clipboard(SDL_Window *window, NwDropAccept accept)
{
    HWND hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    char *found = NULL;
    if (!hwnd || !IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(hwnd)) {
        return NULL;
    }
    HANDLE drop = GetClipboardData(CF_HDROP);
    if (drop) {
        found = nw_droptarget_list((HDROP)drop, accept);
    }
    CloseClipboard();
    return found;
}

static DWORD nw_droptarget_effect(DWORD allowed)
{
    return nw_droptarget.accepted && (allowed & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

static HRESULT STDMETHODCALLTYPE nw_droptarget_query(IDropTarget *self, REFIID riid, void **out)
{
    if (SDL_memcmp(riid, &nw_droptarget_unknown, sizeof(IID)) == 0 ||
        SDL_memcmp(riid, &nw_droptarget_iid, sizeof(IID)) == 0) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE nw_droptarget_ref(IDropTarget *self)
{
    (void)self;
    return 1;
}

static HRESULT STDMETHODCALLTYPE nw_droptarget_enter(IDropTarget *self, IDataObject *data, DWORD keys, POINTL pt,
                                                     DWORD *effect)
{
    (void)self;
    (void)keys;
    (void)pt;
    char *path = nw_droptarget_file(data);
    nw_droptarget.accepted = path != NULL;
    SDL_free(path);
    *effect = nw_droptarget_effect(*effect);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE nw_droptarget_over(IDropTarget *self, DWORD keys, POINTL pt, DWORD *effect)
{
    (void)self;
    (void)keys;
    (void)pt;
    *effect = nw_droptarget_effect(*effect);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE nw_droptarget_leave(IDropTarget *self)
{
    (void)self;
    nw_droptarget.accepted = false;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE nw_droptarget_drop(IDropTarget *self, IDataObject *data, DWORD keys, POINTL pt,
                                                    DWORD *effect)
{
    (void)self;
    (void)keys;
    (void)pt;
    char *path = nw_droptarget_file(data);
    nw_droptarget.accepted = path != NULL;
    *effect = nw_droptarget_effect(*effect);
    nw_droptarget.accepted = false;
    if (path) {
        SDL_Event e;
        SDL_zero(e);
        e.type = nw_droptarget.event;
        e.user.code = nw_droptarget.code;
        e.user.data1 = path;
        if (!SDL_PushEvent(&e)) {
            SDL_free(path);
        }
    }
    return S_OK;
}

static IDropTargetVtbl nw_droptarget_vtbl = {
    .QueryInterface = nw_droptarget_query,
    .AddRef = nw_droptarget_ref,
    .Release = nw_droptarget_ref,
    .DragEnter = nw_droptarget_enter,
    .DragOver = nw_droptarget_over,
    .DragLeave = nw_droptarget_leave,
    .Drop = nw_droptarget_drop,
};

bool nw_droptarget_start(SDL_Window *window, NwDropAccept accept, Uint32 event, Sint32 code)
{
    nw_droptarget_stop();
    HWND hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!hwnd || !accept || !event) {
        return false;
    }
    nw_droptarget.target.lpVtbl = &nw_droptarget_vtbl;
    nw_droptarget.accept = accept;
    nw_droptarget.event = event;
    nw_droptarget.code = code;
    nw_droptarget.ole = SUCCEEDED(OleInitialize(NULL));
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, false);
    SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, false);
    RevokeDragDrop(hwnd);
    if (FAILED(RegisterDragDrop(hwnd, &nw_droptarget.target))) {
        if (nw_droptarget.ole) {
            OleUninitialize();
        }
        SDL_zero(nw_droptarget);
        SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
        SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, true);
        return false;
    }
    nw_droptarget.hwnd = hwnd;
    return true;
}

void nw_droptarget_stop(void)
{
    if (!nw_droptarget.hwnd) {
        return;
    }
    RevokeDragDrop(nw_droptarget.hwnd);
    if (nw_droptarget.ole) {
        OleUninitialize();
    }
    SDL_zero(nw_droptarget);
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
    SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, true);
}

#else

bool nw_droptarget_start(SDL_Window *window, NwDropAccept accept, Uint32 event, Sint32 code)
{
    (void)window;
    (void)accept;
    (void)event;
    (void)code;
    return false;
}

void nw_droptarget_stop(void)
{
}

char *nw_droptarget_clipboard(SDL_Window *window, NwDropAccept accept)
{
    (void)window;
    (void)accept;
    return NULL;
}

#endif
