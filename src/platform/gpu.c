#include "platform/gpu.h"

#include <SDL3/SDL.h>

#include "platform/log.h"

#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <dxgi1_6.h>

__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

bool nw_gpu_flags(const char *flags, int preference, char *out, size_t size)
{
    static const char key[] = "GpuPreference=";
    size_t n = 0;
    if (size == 0) {
        return false;
    }
    while (*flags) {
        const char *end = SDL_strchr(flags, ';');
        size_t len = end ? (size_t)(end - flags) : SDL_strlen(flags);
        if (len && SDL_strncmp(flags, key, sizeof(key) - 1) != 0) {
            if (n + len + 2 > size) {
                return false;
            }
            SDL_memcpy(out + n, flags, len);
            n += len;
            out[n++] = ';';
        }
        flags += len + (end != NULL);
    }
    out[n] = '\0';
    if (preference == 0) {
        return true;
    }
    int w = SDL_snprintf(out + n, size - n, "%s%d;", key, preference);
    return w > 0 && (size_t)w < size - n;
}

#ifdef _WIN32
static const wchar_t nw_gpu_key[] = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";

static bool nw_gpu_write(int preference, bool *changed)
{
    wchar_t exe[1024], value[512];
    char old[512] = "", flags[512];
    DWORD bytes = sizeof(value), n = GetModuleFileNameW(NULL, exe, SDL_arraysize(exe));
    if (n == 0 || n >= SDL_arraysize(exe)) {
        return SDL_SetError("the exe's path is too long");
    }
    LSTATUS r = RegGetValueW(HKEY_CURRENT_USER, nw_gpu_key, exe, RRF_RT_REG_SZ, NULL, value, &bytes);
    if (r == ERROR_SUCCESS && !WideCharToMultiByte(CP_UTF8, 0, value, -1, old, sizeof(old), NULL, NULL)) {
        r = ERROR_INVALID_DATA;
    }
    if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
        return SDL_SetError("cannot read Windows' graphics settings (error %ld)", (long)r);
    }
    if (!nw_gpu_flags(old, preference, flags, sizeof(flags))) {
        return SDL_SetError("Windows' graphics settings for NuWorld are too long");
    }
    *changed = SDL_strcmp(old, flags) != 0;
    if (!*changed) {
        return true;
    }
    HKEY key;
    r = RegCreateKeyExW(HKEY_CURRENT_USER, nw_gpu_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (r == ERROR_SUCCESS) {
        int len = MultiByteToWideChar(CP_UTF8, 0, flags, -1, value, SDL_arraysize(value));
        r = !flags[0] ? RegDeleteValueW(key, exe)
            : len > 0 ? RegSetValueExW(key, exe, 0, REG_SZ, (const BYTE *)value, (DWORD)len * sizeof(wchar_t))
                      : ERROR_INVALID_DATA;
        RegCloseKey(key);
    }
    return r == ERROR_SUCCESS || SDL_SetError("cannot write Windows' graphics settings (error %ld)", (long)r);
}

static bool nw_gpu_card(IDXGIFactory6 *f, DXGI_GPU_PREFERENCE preference, char *name, LUID *id)
{
    IDXGIAdapter1 *a = NULL;
    DXGI_ADAPTER_DESC1 d;
    bool ok = SUCCEEDED(IDXGIFactory6_EnumAdapterByGpuPreference(f, 0, preference, &IID_IDXGIAdapter1, (void **)&a)) &&
              SUCCEEDED(IDXGIAdapter1_GetDesc1(a, &d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
              WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, NW_GPU_NAME, NULL, NULL) > 0;
    if (a) {
        IDXGIAdapter1_Release(a);
    }
    if (ok) {
        *id = d.AdapterLuid;
    }
    return ok;
}
#endif

int nw_gpu_list(char names[NW_GPU_CARDS][NW_GPU_NAME])
{
    int n = 0;
#ifdef _WIN32
    typedef HRESULT(WINAPI * NwCreateFactory)(REFIID riid, void **factory);
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    NwCreateFactory create = dxgi ? (NwCreateFactory)(void *)GetProcAddress(dxgi, "CreateDXGIFactory1") : NULL;
    IDXGIFactory1 *f1 = NULL;
    IDXGIFactory6 *f6 = NULL;
    LUID fast, saving;
    if (create && SUCCEEDED(create(&IID_IDXGIFactory1, (void **)&f1)) &&
        SUCCEEDED(IDXGIFactory1_QueryInterface(f1, &IID_IDXGIFactory6, (void **)&f6))) {
        if (nw_gpu_card(f6, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, names[0], &fast)) {
            n = 1 + (nw_gpu_card(f6, DXGI_GPU_PREFERENCE_MINIMUM_POWER, names[1], &saving) &&
                     SDL_memcmp(&fast, &saving, sizeof(LUID)) != 0);
        }
        IDXGIFactory6_Release(f6);
    }
    if (f1) {
        IDXGIFactory1_Release(f1);
    }
    if (dxgi) {
        FreeLibrary(dxgi);
    }
#else
    (void)names;
#endif
    return n;
}

void nw_gpu_prefer(const char *name)
{
#ifdef _WIN32
    char names[NW_GPU_CARDS][NW_GPU_NAME];
    bool automatic = SDL_strcmp(name, "auto") == 0, changed = false;
    int n = automatic ? 0 : nw_gpu_list(names), preference = automatic ? 0 : -1;
    for (int i = 0; i < n && preference < 0; i++) {
        if (SDL_strcmp(name, names[i]) == 0) {
            preference = n > 1 ? 2 - i : 0;
        }
    }
    if (preference < 0) {
        nw_log_warn(NW_LOG_GL, "graphics card \"%s\" is not in this PC, Windows chooses", name);
    } else if (!nw_gpu_write(preference, &changed)) {
        nw_log_warn(NW_LOG_GL, "graphics card: %s", SDL_GetError());
    } else if (changed) {
        nw_log_info(NW_LOG_GL, "graphics card: %s from the next start", name);
    }
#else
    (void)name;
#endif
}
