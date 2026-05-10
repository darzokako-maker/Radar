#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include "httplib.h"

// Senin paylaştığın 2026-05-07 tarihli güncel ofsetler
namespace Offsets {
    const uintptr_t dwEntityList = 0x24D0DC0;        [span_3](start_span)//[span_3](end_span)
    const uintptr_t dwLocalPlayerController = 0x230A4F0; [span_4](start_span)//[span_4](end_span)
    const uintptr_t m_iHealth = 0x32C;               // Standart CS2 netvar
    const uintptr_t m_vOldOrigin = 0x127C;           // Standart CS2 netvar
}

HANDLE hProcess = NULL;
uintptr_t clientBase = 0;

// Modül adresini bulma (client.dll)
uintptr_t GetModuleBase(DWORD pid, const char* name) {
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32 me = {sizeof(me)};
    if (Module32First(h, &me)) {
        do { if (!strcmp(me.szModule, name)) { CloseHandle(h); return (uintptr_t)me.modBaseAddr; } } while (Module32Next(h, &me));
    }
    CloseHandle(h); return 0;
}

// Radar için veri yapısı
struct Enemy { float x, y; int health; };

std::string get_ui() {
    return "<html><head><meta charset='UTF-8'><style>"
           "body{background:#000;color:#0f0;font-family:monospace;overflow:hidden;}"
           "#radar{width:500px;height:500px;border:2px solid #333;position:relative;margin:auto;background:rgba(0,20,0,0.5);}"
           ".point{position:absolute;width:8px;height:8px;background:red;border-radius:50%;transform:translate(-50%,-50%);}"
           ".local{background:blue;z-index:10;}"
           "</style></head><body>"
           "<h2 style='text-align:center'>LUNA V12 - CS2 RADAR</h2>"
           "<div id='radar'></div>"
           "<script>"
           "function update(){ fetch('/api/radar').then(r=>r.json()).then(data=>{"
           "  const r=document.getElementById('radar'); r.innerHTML=''; "
           "  data.forEach(p=>{"
           "    const d=document.createElement('div'); d.className=p.isLocal?'point local':'point';"
           "    d.style.left=(p.x/50 + 250)+'px'; d.style.top=(p.y/50 + 250)+'px';" // Basit koordinat oranlama
           "    r.appendChild(d);"
           "  });"
           "}); } setInterval(update, 100);"
           "</script></body></html>";
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    httplib::Server svr;

    svr.Get("/api/attach", [&](const httplib::Request& req, httplib::Response& res) {
        DWORD pid = std::stoul(req.get_param_value("pid"));
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        clientBase = GetModuleBase(pid, "client.dll");
        res.set_content(clientBase ? "CS2 Baglantisi Tamam!" : "client.dll bulunamadi!", "text/plain");
    });

    svr.Get("/api/radar", [](const httplib::Request&, httplib::Response& res) {
        std::string json = "[";
        [span_5](start_span)// Burada dwEntityList üzerinden döngü kurup oyuncu koordinatlarını okuyoruz[span_5](end_span)
        // (Gerçek uygulamada buraya ReadProcessMemory/SyscallRead döngüsü gelecek)
        json += "{\"x\":100, \"y\":200, \"isLocal\":false, \"hp\":100}"; 
        res.set_content(json + "]", "application/json");
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_content(get_ui(), "text/html"); });
    svr.listen("0.0.0.0", 1337);
    return 0;
}
