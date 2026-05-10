#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>
#include "httplib.h"

// Derleme hatalarını önlemek için manuel tip tanımları
typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI* pNtReadVM)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);

// 07.05.2026 Tarihli Güncel Ofsetler
namespace Offsets {
    const uintptr_t dwEntityList = 0x24D0DC0;        
    const uintptr_t dwLocalPlayerPawn = 0x2056700;   
    const uintptr_t m_vOldOrigin = 0x127C;           
    const uintptr_t m_iTeamNum = 0x3C3;              
    const uintptr_t m_iHealth = 0x32C;               
}

HANDLE hProcess = NULL;
uintptr_t clientBase = 0;

// Düşük seviyeli bellek okuma fonksiyonu
bool RPM(uintptr_t addr, void* buffer, size_t size) {
    static pNtReadVM fn = (pNtReadVM)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtReadVirtualMemory");
    if (!fn || !hProcess) return false;
    return fn(hProcess, (PVOID)addr, buffer, size, NULL) == 0;
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

std::string get_ui() {
    return "<html><head><meta charset='UTF-8'><style>"
           "body{background:#000;color:#0f0;font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
           "#radar{width:400px;height:400px;border:2px solid #333;position:relative;background:rgba(0,30,0,0.3);border-radius:50%;overflow:hidden;}"
           ".point{position:absolute;width:8px;height:8px;border-radius:50%;transform:translate(-50%,-50%);}"
           ".enemy{background:#ff4d4d;box-shadow:0 0 5px #f00;}"
           ".local{background:#4d79ff;width:10px;height:10px;z-index:10;box-shadow:0 0 8px #00f;}"
           ".cross{position:absolute;top:50%;left:50%;width:100%;height:1px;background:#222;} .v{width:1px;height:100%;}"
           "</style></head><body>"
           "<div id='radar'><div class='cross'></div><div class='cross v'></div></div>"
           "<script>"
           "function update(){ fetch('/api/radar').then(r=>r.json()).then(data=>{"
           "  const r=document.getElementById('radar'); "
           "  r.querySelectorAll('.point').forEach(e=>e.remove()); "
           "  data.forEach(p=>{"
           "    const d=document.createElement('div'); d.className=p.isLocal?'point local':'point enemy';"
           "    d.style.left=(p.x / 15 + 200)+'px'; d.style.top=(p.y / -15 + 200)+'px';" 
           "    r.appendChild(d);"
           "  });"
           "}).catch(e=>console.log('Hata')); } setInterval(update, 50);"
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
        
        std::stringstream json; json << "[";
        
        // 1. Local Player Konumu
        uintptr_t localPawn;
        float localX = 0, localY = 0;
        if (RPM(clientBase + Offsets::dwLocalPlayerPawn, &localPawn, sizeof(localPawn))) {
            RPM(localPawn + Offsets::m_vOldOrigin, &localX, sizeof(float));
            RPM(localPawn + Offsets::m_vOldOrigin + 4, &localY, sizeof(float));
            json << "{\"x\":0,\"y\":0,\"isLocal\":true},"; // Kendini merkeze al
        }

        // 2. Düşmanları Tara (Basitleştirilmiş döngü)
        uintptr_t entityList;
        if (RPM(clientBase + Offsets::dwEntityList, &entityList, sizeof(entityList))) {
            for (int i = 1; i < 32; i++) {
                uintptr_t listEntry;
                if (!RPM(entityList + ((i & 0x7FFF) >> 9) * 8 + 16, &listEntry, sizeof(listEntry))) continue;
                uintptr_t playerPawn;
                if (!RPM(listEntry + 120 * (i & 0x1FF), &playerPawn, sizeof(playerPawn))) continue;

                float ex, ey;
                int hp;
                RPM(playerPawn + Offsets::m_vOldOrigin, &ex, sizeof(float));
                RPM(playerPawn + Offsets::m_vOldOrigin + 4, &ey, sizeof(float));
                RPM(playerPawn + Offsets::m_iHealth, &hp, sizeof(int));

                if (hp > 0 && hp <= 100) {
                    // Kendi konumuna göre olan farkı hesapla
                    json << "{\"x\":" << (ex - localX) << ",\"y\":" << (ey - localY) << ",\"isLocal\":false},";
                }
            }
        }

        std::string out = json.str();
        if (out.back() == ',') out.pop_back();
        res.set_content(out + "]", "application/json");
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_content(get_ui(), "text/html"); });
    svr.listen("0.0.0.0", 1337);
    return 0;
}
