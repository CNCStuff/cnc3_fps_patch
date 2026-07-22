#include "kw_common.h"
#include "runtime.h"

typedef HRESULT (WINAPI *DirectInput8CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
typedef HRESULT (WINAPI *DllCanUnloadNowFn)(void);
typedef HRESULT (WINAPI *DllGetClassObjectFn)(REFCLSID, REFIID, LPVOID *);
typedef HRESULT (WINAPI *DllRegisterServerFn)(void);

static HMODULE g_real_dinput8;
static volatile LONG g_proxy_load_state;
static DirectInput8CreateFn g_real_direct_input_8_create;
static DllCanUnloadNowFn g_real_dll_can_unload_now;
static DllGetClassObjectFn g_real_dll_get_class_object;
static DllRegisterServerFn g_real_dll_register_server;
static DllRegisterServerFn g_real_dll_unregister_server;

static BOOL kw_load_real_dinput8(void) {
    wchar_t path[MAX_PATH];
    LONG state = InterlockedCompareExchange(&g_proxy_load_state, 1, 0);
    if (state == 2) return TRUE;
    if (state == 1) {
        while (InterlockedCompareExchange(&g_proxy_load_state, 1, 1) == 1) Sleep(0);
        return g_proxy_load_state == 2;
    }
    if (GetSystemDirectoryW(path, KW_ARRAY_COUNT(path)) == 0 ||
        !kw_wide_append(path, KW_ARRAY_COUNT(path), L"\\dinput8.dll")) {
        InterlockedExchange(&g_proxy_load_state, -1);
        return FALSE;
    }
    g_real_dinput8 = LoadLibraryW(path);
    if (g_real_dinput8 == NULL) {
        InterlockedExchange(&g_proxy_load_state, -1);
        return FALSE;
    }
    g_real_direct_input_8_create =
        (DirectInput8CreateFn)GetProcAddress(g_real_dinput8, "DirectInput8Create");
    g_real_dll_can_unload_now =
        (DllCanUnloadNowFn)GetProcAddress(g_real_dinput8, "DllCanUnloadNow");
    g_real_dll_get_class_object =
        (DllGetClassObjectFn)GetProcAddress(g_real_dinput8, "DllGetClassObject");
    g_real_dll_register_server =
        (DllRegisterServerFn)GetProcAddress(g_real_dinput8, "DllRegisterServer");
    g_real_dll_unregister_server =
        (DllRegisterServerFn)GetProcAddress(g_real_dinput8, "DllUnregisterServer");
    if (g_real_direct_input_8_create == NULL) {
        InterlockedExchange(&g_proxy_load_state, -1);
        return FALSE;
    }
    InterlockedExchange(&g_proxy_load_state, 2);
    return TRUE;
}

HRESULT WINAPI DirectInput8Create(
    HINSTANCE instance, DWORD version, REFIID interface_id, LPVOID *output, LPUNKNOWN outer) {
    kw_runtime_proxy_checkpoint();
    if (!kw_load_real_dinput8()) return E_FAIL;
    return g_real_direct_input_8_create(instance, version, interface_id, output, outer);
}

HRESULT WINAPI DllCanUnloadNow(void) {
    if (!kw_load_real_dinput8() || g_real_dll_can_unload_now == NULL) return S_FALSE;
    return g_real_dll_can_unload_now();
}

HRESULT WINAPI DllGetClassObject(
    REFCLSID class_id, REFIID interface_id, LPVOID *output) {
    if (!kw_load_real_dinput8() || g_real_dll_get_class_object == NULL) return CLASS_E_CLASSNOTAVAILABLE;
    return g_real_dll_get_class_object(class_id, interface_id, output);
}

HRESULT WINAPI DllRegisterServer(void) {
    if (!kw_load_real_dinput8() || g_real_dll_register_server == NULL) return E_NOTIMPL;
    return g_real_dll_register_server();
}

HRESULT WINAPI DllUnregisterServer(void) {
    if (!kw_load_real_dinput8() || g_real_dll_unregister_server == NULL) return E_NOTIMPL;
    return g_real_dll_unregister_server();
}
