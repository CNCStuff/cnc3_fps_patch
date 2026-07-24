#include "common.h"
#include "runtime.h"

typedef HRESULT(WINAPI *DirectInput8CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
typedef HRESULT(WINAPI *DllCanUnloadNowFn)(void);
typedef HRESULT(WINAPI *DllGetClassObjectFn)(REFCLSID, REFIID, LPVOID *);
typedef HRESULT(WINAPI *DllRegisterServerFn)(void);

static HMODULE g_real_dinput8;
static INIT_ONCE g_proxy_init_once = INIT_ONCE_STATIC_INIT;
static DirectInput8CreateFn g_real_direct_input_8_create;
static DllCanUnloadNowFn g_real_dll_can_unload_now;
static DllGetClassObjectFn g_real_dll_get_class_object;
static DllRegisterServerFn g_real_dll_register_server;
static DllRegisterServerFn g_real_dll_unregister_server;

static BOOL CALLBACK initialize_real_dinput8(
    PINIT_ONCE init_once, PVOID parameter, PVOID *context) {
    HMODULE module;
    DirectInput8CreateFn direct_input_8_create;
    DllCanUnloadNowFn dll_can_unload_now;
    DllGetClassObjectFn dll_get_class_object;
    DllRegisterServerFn dll_register_server;
    DllRegisterServerFn dll_unregister_server;
    wchar_t path[MAX_PATH];
    (void)init_once;
    (void)parameter;
    (void)context;

    /* An absolute System32 path prevents the proxy from recursively loading itself. */
    if (GetSystemDirectoryW(path, ARRAY_COUNT(path)) == 0 ||
        !wide_append(path, ARRAY_COUNT(path), L"\\dinput8.dll")) {
        return FALSE;
    }
    module = LoadLibraryW(path);
    if (module == NULL) return FALSE;
    direct_input_8_create =
        (DirectInput8CreateFn)GetProcAddress(module, "DirectInput8Create");
    dll_can_unload_now =
        (DllCanUnloadNowFn)GetProcAddress(module, "DllCanUnloadNow");
    dll_get_class_object =
        (DllGetClassObjectFn)GetProcAddress(module, "DllGetClassObject");
    dll_register_server =
        (DllRegisterServerFn)GetProcAddress(module, "DllRegisterServer");
    dll_unregister_server =
        (DllRegisterServerFn)GetProcAddress(module, "DllUnregisterServer");
    if (direct_input_8_create == NULL) {
        FreeLibrary(module);
        return FALSE;
    }

    /* Publish the fully initialized module and export table atomically via INIT_ONCE. */
    g_real_dinput8 = module;
    g_real_direct_input_8_create = direct_input_8_create;
    g_real_dll_can_unload_now = dll_can_unload_now;
    g_real_dll_get_class_object = dll_get_class_object;
    g_real_dll_register_server = dll_register_server;
    g_real_dll_unregister_server = dll_unregister_server;
    return TRUE;
}

static BOOL load_real_dinput8(void) {
    /* A failed callback leaves the INIT_ONCE retryable for a later export call. */
    return InitOnceExecuteOnce(
        &g_proxy_init_once, initialize_real_dinput8, NULL, NULL);
}

HRESULT WINAPI DirectInput8Create(
    HINSTANCE instance, DWORD version, REFIID interface_id, LPVOID *output, LPUNKNOWN outer) {
    /* First normal game call after process startup: safe place for INI/log setup. */
    runtime_proxy_checkpoint();
    if (!load_real_dinput8()) return E_FAIL;
    return g_real_direct_input_8_create(instance, version, interface_id, output, outer);
}

HRESULT WINAPI DllCanUnloadNow(void) {
    if (!load_real_dinput8() || g_real_dll_can_unload_now == NULL) return S_FALSE;
    return g_real_dll_can_unload_now();
}

HRESULT WINAPI DllGetClassObject(
    REFCLSID class_id, REFIID interface_id, LPVOID *output) {
    if (!load_real_dinput8() || g_real_dll_get_class_object == NULL) return CLASS_E_CLASSNOTAVAILABLE;
    return g_real_dll_get_class_object(class_id, interface_id, output);
}

HRESULT WINAPI DllRegisterServer(void) {
    if (!load_real_dinput8() || g_real_dll_register_server == NULL) return E_NOTIMPL;
    return g_real_dll_register_server();
}

HRESULT WINAPI DllUnregisterServer(void) {
    if (!load_real_dinput8() || g_real_dll_unregister_server == NULL) return E_NOTIMPL;
    return g_real_dll_unregister_server();
}
