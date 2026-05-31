#include <windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <sstream>
#include <mutex> // Race Condition çözümü için eklendi

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
        constexpr std::ptrdiff_t m_sSanitizedPlayerName = 0x770; // CUtlString / Pointer korumalı okuma yapılacak
    }
}

struct Vector3 { float x, y, z; };
struct PlayerData {
    std::string name;
    int health;
    int team;
    float x, y;
};

// Global Tanı ve Senkronizasyon Değişkenleri
std::string g_Status = "Baslatiliyor...";
DWORD g_PID = 0;
uintptr_t g_ClientModule = 0;
HANDLE g_hProcess = NULL;

std::vector<PlayerData> g_Players;
std::mutex g_PlayersMutex; // Veri yarışını önleyen kritik mutex kilidi

// Güvenli WinSock HTTP Web Sunucusu
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
    serverAddr.sin_port = htons(8080);

    bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, SOMAXCONN);

    // HTTP isteklerini karşılamak için daha geniş tampon bellek (Büyük Header'lar için)
    std::vector<char> requestBuffer(4096);

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket != INVALID_SOCKET) {
            recv(clientSocket, requestBuffer.data(), static_cast<int>(requestBuffer.size()) - 1, 0);

            std::ostringstream json;
            json << "[\n";
            
            // CRITICAL: Okuma yaparken veriyi kilitliyoruz, ana thread bu esnada yazamaz!
            {
                std::lock_guard<std::mutex> lock(g_PlayersMutex);
                for (size_t i = 0; i < g_Players.size(); ++i) {
                    json << "  {\"name\": \"";
                    for (char c : g_Players[i].name) {
                        if (c == '"' || c == '\\') json << ' ';
                        else if (c >= 32 && c <= 126) json << c;
                    }
                    json << "\", \"health\": " << g_Players[i].health 
                         << ", \"team\": " << g_Players[i].team 
                         << ", \"x\": " << g_Players[i].x 
                         << ", \"y\": " << g_Players[i].y << "}";
                    if (i + 1 < g_Players.size()) json << ",";
                    json << "\n";
                }
            } // Kilit burada otomatik açılır (Scope sonu)

            json << "]";

            std::string body = json.str();
            std::ostringstream response;
            
            // Standarda uygun HTTP yanıtı (Sonunda çift \r\n\r\n ile kilitlenme önlendi)
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Access-Control-Allow-Origin: *\r\n"
                     << "Access-Control-Allow-Headers: *\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n" 
                     << body << "\r\n\r\n"; 

            send(clientSocket, response.str().c_str(), static_cast<int>(response.str().length()), 0);
            closesocket(clientSocket);
        }
        Sleep(10);
    }
    WSACleanup();
    return 0;
}

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
        g_Status = "HATA: Yonetici izni eksik!";
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
        g_Status = "HATA: client.dll baglanamadi!";
        return false;
    }

    g_Status = "SISTEM AKTIF: http://localhost:8080 hazir.";
    return true;
}

int main() {
    SetConsoleTitleA("CS2 Fixed external Web Radar Engine");
    CreateThread(nullptr, 0, WebServerThread, nullptr, 0, nullptr);

    std::cout << "[+] Tani ve Filtreleme Motoru Baslatildi.\n";

    while (true) {
        if (!g_hProcess || !g_ClientModule) {
            if (!InitializeSystem()) {
                std::cout << "\r[DURUM] " << g_Status << "        " << std::flush;
                Sleep(1000);
                continue;
            }
            std::cout << "\n[+] Oyuna basariyla baglanildi.\n";
        }

        uintptr_t entityList = 0;
        if (!ReadProcessMemory(g_hProcess, (LPCVOID)(g_ClientModule + cs2_dumper::offsets::client_dll::dwEntityList), &entityList, sizeof(entityList), nullptr) || !entityList) {
            std::cout << "\r[HATA] EntityList okunamiyor, haritaya girilmesi bekleniyor..." << std::flush;
            Sleep(1000);
            continue;
        }

        std::vector<PlayerData> tempPlayers;

        // Source 2 Max Entity sınırlarında tarama
        for (int i = 1; i <= 64; i++) {
            // Source 2 Doğru Pointer Çarpanı Mimarisi: Node basına 8 byte (0x8) dizilimi
            uintptr_t listEntry = 0;
            if (!ReadProcessMemory(g_hProcess, (LPCVOID)(entityList + ((8 * (i & 0x7FFF) >> 9) + 16)), &listEntry, sizeof(listEntry), nullptr) || !listEntry) continue;

            uintptr_t playerController = 0;
            if (!ReadProcessMemory(g_hProcess, (LPCVOID)(listEntry + 120 * (i & 0x1FF)), &playerController, sizeof(playerController), nullptr) || !playerController) continue;

            uint32_t playerPawnHandle = 0;
            if (!ReadProcessMemory(g_hProcess, (LPCVOID)(playerController + schemas::client_dll::m_hPlayerPawn), &playerPawnHandle, sizeof(playerPawnHandle), nullptr) || !playerPawnHandle) continue;

            // İkinci katman adres doğrulaması (Pointer boyutu 8 byte olarak düzeltildi)
            uintptr_t listEntry2 = 0;
            if (!ReadProcessMemory(g_hProcess, (LPCVOID)(entityList + (8 * ((playerPawnHandle & 0x1FFF) >> 9) + 16)), &listEntry2, sizeof(listEntry2), nullptr) || !listEntry2) continue;

            uintptr_t playerPawn = 0;
            if (!ReadProcessMemory(g_hProcess, (LPCVOID)(listEntry2 + 120 * (playerPawnHandle & 0x1FF)), &playerPawn, sizeof(playerPawn), nullptr) || !playerPawn) continue;

            int health = 0;
            int team = 0;
            Vector3 pos{ 0.0f, 0.0f, 0.0f };
            char nameBuf[64]{ 0 };

            ReadProcessMemory(g_hProcess, (LPCVOID)(playerPawn + schemas::client_dll::m_iHealth), &health, sizeof(health), nullptr);
            ReadProcessMemory(g_hProcess, (LPCVOID)(playerPawn + schemas::client_dll::m_iTeamNum), &team, sizeof(team), nullptr);
            ReadProcessMemory(g_hProcess, (LPCVOID)(playerPawn + schemas::client_dll::m_vOldOrigin), &pos, sizeof(pos), nullptr);
            
            // CUtlString Çözümleme Koruması: İsmin tutulduğu asıl ham pointer adresini çekme
            uintptr_t namePointer = 0;
            ReadProcessMemory(g_hProcess, (LPCVOID)(playerController + schemas::client_dll::m_sSanitizedPlayerName), &namePointer, sizeof(namePointer), nullptr);
            
            if (namePointer != 0) {
                ReadProcessMemory(g_hProcess, (LPCVOID)namePointer, &nameBuf, sizeof(nameBuf) - 1, nullptr);
            } else {
                // Eğer doğrudan adres üzerinden okunması gerekiyorsa fallback koruması
                ReadProcessMemory(g_hProcess, (LPCVOID)(playerController + schemas::client_dll::m_sSanitizedPlayerName), &nameBuf, sizeof(nameBuf) - 1, nullptr);
            }

            if (health > 0 && health <= 100 && (team == 2 || team == 3)) {
                PlayerData p;
                p.name = (strlen(nameBuf) > 0) ? nameBuf : "Oyuncu";
                p.health = health;
                p.team = team;
                p.x = pos.x;
                p.y = pos.y;
                tempPlayers.push_back(p);
            }
        }

        // CRITICAL: Yeni verileri global listeye yazarken kilitleme yapıyoruz. Race Condition tamamen engellendi.
        {
            std::lock_guard<std::mutex> lock(g_PlayersMutex);
            g_Players = std::move(tempPlayers);
        }

        std::cout << "\r[RADAR MOTORU] Aktif Tarama Canli. Tespit Edilen Oyuncu: " << g_Players.size() << "     " << std::flush;
        Sleep(50); 
    }
    return 0;
}
