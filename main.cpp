#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include "httplib.h"

// Derleme hatalarını önlemek için manuel tanımlamalar
typedef LONG NTSTATUS;
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

typedef NTSTATUS(NTAPI* pNtReadVM)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
typedef NTSTATUS(NTAPI* pNtWriteVM)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);

// 2026-05-07 Tarihli Güncel Ofsetler
namespace Offsets {
    const uintptr_t dwEntityList = 0x24D0DC0;        //
    const uintptr_t dwLocalPlayerController = 0x230A4F0; //
    const uintptr_t m_iHealth = 0x32C;               
    const uintptr_t m_vOldOrigin = 0x127C;           
}

HANDLE hProcess = NULL;
uintptr_t clientBase = 0;

// Syscall Fonksiyonları
NTSTATUS SyscallRead(PVOID base, PVOID buf, SIZE_T size) {
    static pNtReadVM fn = (pNtReadVM)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtReadVirtualMemory");
    return (fn) ? fn(hProcess, base, buf, size, NULL) : (NTSTATUS)0xC0000001L;
}

uintptr_t GetModuleBase(DWORD pid, const char* name) {
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (h == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32 me = { sizeof(me) };
    if (Module32First(h, &me)) {
        do { if (!strcmp(me.szModule, name)) { CloseHandle(h); return (uintptr_t)me.modBaseAddr; } } while (Module32Next(h, &me));
    }
    CloseHandle(h); return 0;
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    httplib::Server svr;

    svr.Get("/api/attach", [&](const httplib::Request& req, httplib::Response& res) {
        DWORD pid = std::stoul(req.get_param_value("pid"));
        if (hProcess) CloseHandle(hProcess);
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        clientBase = GetModuleBase(pid, "client.dll");
        res.set_content(clientBase ? "Baglandi" : "Modul Bulunamadi", "text/plain");
    });

    svr.Get("/api/radar", [](const httplib::Request&, httplib::Response& res) {
        // Radar mantığı burada çalışacak
        res.set_content("[]", "application/json");
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("<html><body><h2>Luna V12 Radar Aktif</h2></body></html>", "text/html");
    });

    svr.listen("0.0.0.0", 1337);
    return 0;
}
