#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <iostream>
#include "httplib.h"

typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI* pNtReadVM)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);

[span_2](start_span)[span_3](start_span)// 07.05.2026 Ofsetleri[span_2](end_span)[span_3](end_span)
namespace Offsets {
    const uintptr_t dwEntityList = 0x24D0DC0;        
    const uintptr_t dwLocalPlayerPawn = 0x2056700;   
    const uintptr_t m_vOldOrigin = 0x127C;           
    const uintptr_t m_iHealth = 0x32C;               
}

HANDLE hProcess = NULL;
uintptr_t clientBase = 0;

bool RPM(uintptr_t addr, void* buf, size_t size) {
    static pNtReadVM fn = (pNtReadVM)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtReadVirtualMemory");
    if (!fn || !hProcess) return false;
    return fn(hProcess, (PVOID)addr, buf, size, NULL) == 0;
}

uintptr_t GetModuleBase(DWORD pid, const char* name) {
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32 me = { sizeof(me) };
    if (Module32First(h, &me)) {
        do { if (!strcmp(me.szModule, name)) { CloseHandle(h); return (uintptr_t)me.modBaseAddr; } } while (Module32Next(h, &me));
    }
    CloseHandle(h); return 0;
}

// Arayüz (Mavi nokta merkezi, kırmızı noktalar düşman)
std::string get_ui() {
    return "<html><head><style>"
           "body{background:#000;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;overflow:hidden;}"
           "#radar{width:400px;height:400px;border:1px solid #444;position:relative;background:radial-gradient(circle, #111 0%, #000 100%);border-radius:50%;}"
           ".p{position:absolute;width:6px;height:6px;border-radius:50%;transform:translate(-50%,-50%);}"
           ".e{background:#ff2222;box-shadow:0 0 6px #f00;}"
           ".l{background:#2288ff;width:10px;height:10px;z-index:10;box-shadow:0 0 10px #08f;}"
           "</style></head><body>"
           "<div id='radar'></div>"
           "<script>"
           "function update(){ fetch('/api/radar').then(r=>r.json()).then(data=>{"
           "  const r=document.getElementById('radar'); r.innerHTML=''; "
           "  data.forEach(p=>{"
           "    const d=document.createElement('div'); d.className=p.isLocal?'p l':'p e';"
           "    d.style.left=(p.x/20+200)+'px'; d.style.top=(p.y/-20+200)+'px'; r.appendChild(d);"
           "  });"
           "}).catch(e=>console.log('Hata')); } setInterval(update, 60);"
           "</script></body></html>";
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    httplib::Server svr;

    svr.Get("/api/attach", [&](const httplib::Request& req, httplib::Response& res) {
        DWORD pid = std::stoul(req.get_param_value("pid"));
        if (hProcess) CloseHandle(hProcess);
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        clientBase = GetModuleBase(pid, "client.dll");
        res.set_content(clientBase ? "Baglandi" : "Hata", "text/plain");
    });

    svr.Get("/api/radar", [](const httplib::Request&, httplib::Response& res) {
        if (!hProcess || !clientBase) { res.set_content("[]", "application/json"); return; }
        std::string json = "[";
        float lx=0, ly=0; uintptr_t lpawn;

        // Yerel oyuncuyu oku
        if (RPM(clientBase + Offsets::dwLocalPlayerPawn, &lpawn, sizeof(lpawn))) {
            RPM(lpawn + Offsets::m_vOldOrigin, &lx, sizeof(float));
            RPM(lpawn + Offsets::m_vOldOrigin + 4, &ly, sizeof(float));
            json += "{\"x\":0,\"y\":0,\"isLocal\":true},";
        }

        // Düşman listesini tara (CS2 Entity System Mantığı)
        uintptr_t elist;
        if (RPM(clientBase + Offsets::dwEntityList, &elist, sizeof(elist))) {
            for (int i=1; i<64; i++) {
                uintptr_t listEntry;
                if (!RPM(elist + 0x10, &listEntry, sizeof(listEntry))) continue;
                
                uintptr_t pawn;
                // ListEntry + (Index * 0x78) üzerinden Pawn adresine ulaşım
                if (!RPM(listEntry + (i * 0x78), &pawn, sizeof(pawn))) continue;
                if (pawn == lpawn) continue; // Kendini tekrar çizme

                float ex, ey; int hp;
                if (RPM(pawn + Offsets::m_vOldOrigin, &ex, sizeof(float))) {
                    RPM(pawn + Offsets::m_vOldOrigin + 4, &ey, sizeof(float));
                    RPM(pawn + Offsets::m_iHealth, &hp, sizeof(int));

                    if (hp > 0 && hp <= 100) {
                        json += "{\"x\":" + std::to_string((ex-lx)) + ",\"y\":" + std::to_string((ey-ly)) + ",\"isLocal\":false},";
                    }
                }
            }
        }
        if (json.back() == ',') json.pop_back();
        res.set_content(json + "]", "application/json");
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_content(get_ui(), "text/html"); });
    svr.listen("0.0.0.0", 1337);
    return 0;
}
