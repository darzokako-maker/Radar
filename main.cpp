#include <windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <sstream>

// =========================================================================
// YÜKLEDİĞİNİZ OFSETLER & ŞEMALAR
// =========================================================================
namespace cs2_dumper {
    namespace offsets {
        namespace client_dll {
            constexpr std::ptrdiff_t dwEntityList = 0x24E5590;
            constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x233F698;
        }
    }
}
namespace schemas {
    namespace client_dll {
        constexpr std::ptrdiff_t m_iHealth = 0x334;
        constexpr std::ptrdiff_t m_iTeamNum = 0x3E3;
        constexpr std::ptrdiff_t m_vOldOrigin = 0x1324;
        constexpr std::ptrdiff_t m_hPlayerPawn = 0x80C;
        constexpr std::ptrdiff_t m_sSanitizedPlayerName = 0x770;
    }
}

struct Vector3 { float x, y, z; };
struct PlayerData {
    std::string name;
    int health;
    int team;
    float x, y;
};

// Global Tanı ve Durum Değişkenleri (Hata Ayıklayıcı İçin)
std::string g_Status = "Baslatiliyor...";
DWORD g_PID = 0;
uintptr_t g_ClientModule = 0;
HANDLE g_hProcess = NULL;
std::vector<PlayerData> g_Players;

// Basit WinSock HTTP Web Sunucusu Yapısı
DWORD WINAPI WebServerThread(LPVOID lpParam) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        g_Status = "HATA: WinSock baslatilamadi!";
        return 0;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(8080); // http://localhost:8080

    bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, SOMAXCONN);

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket != INVALID_SOCKET) {
            char buffer[1024];
            recv(clientSocket, buffer, sizeof(buffer), 0);

            // JSON formatında oyuncu koordinatlarını dönüyoruz
            std::ostringstream json;
            json << "[\n";
            for (size_t i = 0; i < g_Players.size(); ++i) {
                json << "  {\"name\": \"" << g_Players[i].name 
                     << "\", \"health\": " << g_Players[i].health 
                     << ", \"team\": " << g_Players[i].team 
                     << ", \"x\": " << g_Players[i].x 
                     << ", \"y\": " << g_Players[i].y << "}";
                if (i + 1 < g_Players.size()) json << ",";
                json << "\n";
            }
            json << "]";

            std::string body = json.str();
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;

            send(clientSocket, response.str().c_str(), response.str().length(), 0);
            closesocket(clientSocket);
        }
        Sleep(10);
    }
    WSACleanup();
    return 0;
}

// Süreç ve Modül Bulucu Tanı Fonksiyonları
bool InitializeSystem() {
    g_Status = "cs2.exe bekleniyor...";
    
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe32{ sizeof(PROCESSENTRY32) };
    if (Process32First(hSnapshot, &pe32)) {
        do {
            if (std::string(pe32.szExeFile) == "cs2.exe") {
                g_PID = pe32.th32ProcessID;
                break;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);

    if (!g_PID) return false;

    g_hProcess = OpenProcess(PROCESS_VM_READ, FALSE, g_PID);
    if (!g_hProcess) {
        g_Status = "HATA: Oyun bulundu ama bellek okuma yetkisi alinamadi (Yonetici calistirin)!";
        return false;
    }

    HANDLE hModSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, g_PID);
    MODULEENTRY32 me32{ sizeof(MODULEENTRY32) };
    if (Module32First(hModSnapshot, &me32)) {
        do {
            if (std::string(me32.szModule) == "client.dll") {
                g_ClientModule = (uintptr_t)me32.modBaseAddr;
                break;
            }
        } while (Module32Next(hModSnapshot, &me32));
    }
    CloseHandle(hModSnapshot);

    if (!g_ClientModule) {
        g_Status = "HATA: client.dll bulunamadi! Oyun tam yuklenmemis olabilir.";
        return false;
    }

    g_Status = "SISTEM AKTIF: Tarayicidan http://localhost:8080 adresini dinleyin.";
    return true;
}

int main() {
    SetConsoleTitleA("CS2 Web Radar & Live Debugger Engine");
    CreateThread(nullptr, 0, WebServerThread, nullptr, 0, nullptr);

    std::cout << "[+] Tani ve Hata Ayiklama Sistemi Baslatildi.\n";

    while (true) {
        if (!g_hProcess || !g_ClientModule) {
            if (!InitializeSystem()) {
                std::cout << "\r[DURUM] " << g_Status << "        " << std::flush;
                Sleep(1000);
                continue;
            }
            std::cout << "\n[+] Oyuna baglanildi! PID: " << g_PID << " | Modul: 0x" << std::hex << g_ClientModule << std::dec << "\n";
        }

        uintptr_t entityList = 0;
        if (!ReadProcessMemory(g_hProcess, (LPCVOID)(g_ClientModule + cs2_dumper::offsets::client_dll::dwEntityList), &entityList, sizeof(entityList), nullptr) || !entityList) {
            g_Status = "HATA: EntityList okunamadi! Ofsetler eski olabilir.";
            std::cout << "\r[HATA] " << g_Status << std::flush;
            g_hProcess = NULL; 
            Sleep(1000);
            continue;
        }

        std::vector<PlayerData> tempPlayers;

        for (int i = 1; i <= 64; i++) {
            uintptr_t listEntry = 0;
            ReadProcessMemory(g_hProcess, (LPCVOID)(entityList + ((8 * (i & 0x7FFF) >> 9) + 16)), &listEntry, sizeof(listEntry), nullptr);
            if (!listEntry) continue;

            uintptr_t playerController = 0;
            ReadProcessMemory(g_hProcess, (LPCVOID)(listEntry + 120 * (i & 0x1FF)), &playerController, sizeof(playerController), nullptr);
            if (!playerController) continue;

            uint32_t playerPawnHandle = 0;
            ReadProcessMemory(g_hProcess, (LPCVOID)(playerController + schemas::client_dll::m_hPlayerPawn), &playerPawnHandle, sizeof(playerPawnHandle), nullptr);
            if (!playerPawnHandle) continue;

            uintptr_t listEntry2 = 0;
            ReadProcessMemory(g_hProcess, (LPCVOID)(entityList + (8 * ((playerPawnHandle & 0x1FFF) >> 9) + 16)), &listEntry2, sizeof(listEntry2), nullptr);
            if (!listEntry2) continue;

            uintptr_t playerPawn = 0;
            ReadProcessMemory(g_hProcess, (LPCVOID)(listEntry2 + 120 * (playerPawnHandle & 0x1FF)), &playerPawn, sizeof(playerPawn), nullptr);
            if (!playerPawn) continue;

            int health = 0;
            int team = 0;
            Vector3 pos{};
            char nameBuf[128]{};

            ReadProcessMemory(g_hProcess, (LPCVOID)(playerPawn + schemas::client_dll::m_iHealth), &health, sizeof(health), nullptr);
            ReadProcessMemory(g_hProcess, (LPCVOID)(playerPawn + schemas::client_dll::m_iTeamNum), &team, sizeof(team), nullptr);
            ReadProcessMemory(g_hProcess, (LPCVOID)(playerPawn + schemas::client_dll::m_vOldOrigin), &pos, sizeof(pos), nullptr);
            ReadProcessMemory(g_hProcess, (LPCVOID)(playerController + schemas::client_dll::m_sSanitizedPlayerName), &nameBuf, sizeof(nameBuf), nullptr);

            if (health > 0 && health <= 100 && (team == 2 || team == 3)) {
                PlayerData p;
                p.name = nameBuf;
                p.health = health;
                p.team = team;
                p.x = pos.x;
                p.y = pos.y;
                tempPlayers.push_back(p);
            }
        }

        g_Players = tempPlayers;
        std::cout << "\r[IZLEME] Aktif Tarama Yapiliyor. Izlenen Oyuncu Sayisi: " << g_Players.size() << "    " << std::flush;
        Sleep(100);
    }
    return 0;
}
