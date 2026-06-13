#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <iomanip>
#include <algorithm> // std::min için

// Proje klasöründeki güncel dumper dosyaları
#include "offsets.hpp"
#include "client_dll.hpp"

HANDLE processHandle = nullptr;

namespace schema_offsets {
    // Doğrudan fiziksel oyuncu (Pawn) yapısı üzerindeki temel değişkenler
    constexpr std::ptrdiff_t m_iHealth = ::cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth;
    constexpr std::ptrdiff_t m_iTeamNum = ::cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum;
    constexpr std::ptrdiff_t m_hController = ::cs2_dumper::schemas::client_dll::C_BaseEntity::m_hController;
    constexpr std::ptrdiff_t m_sSanitizedPlayerName = ::cs2_dumper::schemas::client_dll::CCSPlayerController::m_sSanitizedPlayerName;
}

struct PlayerInfo {
    int index;
    int health;
    int team;
    std::string name;
};

// Güvenli bellek okuma şablonu
template <typename T>
T Read(uintptr_t address) {
    T value{};
    ReadProcessMemory(processHandle, reinterpret_cast<LPCVOID>(address), &value, sizeof(T), nullptr);
    return value;
}

// String okuma (Bellek sınırı güvenli)
std::string ReadString(uintptr_t address, size_t maxSize = 32) {
    char buffer[128] = { 0 };
    ReadProcessMemory(processHandle, reinterpret_cast<LPCVOID>(address), buffer, (std::min)(maxSize, sizeof(buffer) - 1), nullptr);
    return std::string(buffer);
}

// Process ID bulma fonksiyonu
DWORD GetProcessId(const wchar_t* processName) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, processName) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    return pid;
}

// Modül base adresini bulma fonksiyonu
uintptr_t GetModuleBaseAddress(DWORD pid, const wchar_t* moduleName) {
    uintptr_t baseAddress = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me = { sizeof(MODULEENTRY32W) };
        if (Module32FirstW(snapshot, &me)) {
            do {
                if (_wcsicmp(me.szModule, moduleName) == 0) {
                    baseAddress = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                    break;
                }
            } while (Module32NextW(snapshot, &me));
        }
        CloseHandle(snapshot);
    }
    return baseAddress;
}

// Konsol başlangıç ayarları
void SetupConsole() {
    SetConsoleTitleW(L"CS2 Radar v1.3 Dynamic");
    
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(consoleHandle, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(consoleHandle, &cursorInfo);
    
    system("mode con: cols=65 lines=30");
}

void DisplayPlayers(const std::vector<PlayerInfo>& players, int localTeam) {
    COORD cursorPos = { 0, 0 };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), cursorPos);
    
    std::cout << "===============================================================\n";
    std::cout << " ID  | TAKIM  | CAN    | OYUNCU ADI\n";
    std::cout << "===============================================================\n";
    
    int printedLines = 0;
    for (const auto& p : players) {
        std::string teamStr;
        if (localTeam == 2 || localTeam == 3) {
            teamStr = (p.team == localTeam) ? "DOST " : "RAKIP";
        } else {
            teamStr = (p.team == 2) ? "T-TAK" : "CT-TAK";
        }
        
        std::cout << " [" << std::right << std::setw(2) << std::setfill('0') << p.index << "] | "
                  << std::setfill(' ') << std::setw(5) << teamStr << "  | "
                  << std::right << std::setw(3) << p.health << " HP | "
                  << std::left << std::setw(25) << p.name << "\n";
        printedLines++;
    }
    
    for (int k = printedLines; k < 20; k++) {
        std::cout << "                                                               \n";
    }
    
    std::cout << "===============================================================\n";
    std::cout << "Aktif Oyuncu Sayisi: " << std::right << std::setw(2) << players.size() << "                                \n";
}

int main() {
    SetupConsole();
    
    std::cout << "[*] CS2 (cs2.exe) bekleniyor..." << std::endl;
    
    DWORD pid = 0;
    while (pid == 0) {
        pid = GetProcessId(L"cs2.exe");
        if (pid == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    std::cout << "[+] CS2 Bulundu! PID: " << pid << std::endl;
    
    processHandle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!processHandle) {
        std::cout << "[-] Erisim engellendi! Programi Yonetici olarak calistirin." << std::endl;
        system("pause");
        return 1;
    }
    
    uintptr_t clientModule = GetModuleBaseAddress(pid, L"client.dll");
    if (!clientModule) {
        std::cout << "[-] client.dll bulunamadi!" << std::endl;
        CloseHandle(processHandle);
        system("pause");
        return 1;
    }
    
    std::cout << "[+] client.dll adresi: 0x" << std::hex << clientModule << std::dec << std::endl;
    std::cout << "[*] Radar baslatiliyor..." << std::endl;
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    system("cls");
    
    while (true) {
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            break;
        }

        uintptr_t entityList = Read<uintptr_t>(clientModule + cs2_dumper::offsets::client_dll::dwEntityList);
        uintptr_t localPlayerPawn = Read<uintptr_t>(clientModule + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
        
        if (entityList && localPlayerPawn) {
            std::vector<PlayerInfo> currentPlayers;
            int localTeam = Read<int>(localPlayerPawn + schema_offsets::m_iTeamNum);

            // Maksimum varlık listesi aralığında doğrudan Pawn (fiziksel nesne) taraması yapılıyor
            for (int i = 1; i < 512; i++) {
                uintptr_t listEntry = Read<uintptr_t>(entityList + (8 * (i >> 9) + 16));
                if (!listEntry) continue;
                
                uintptr_t currentPawn = Read<uintptr_t>(listEntry + 120 * (i & 0x1FF));
                if (!currentPawn || currentPawn == localPlayerPawn) continue;
                
                int health = Read<int>(currentPawn + schema_offsets::m_iHealth);
                int team = Read<int>(currentPawn + schema_offsets::m_iTeamNum);
                
                // Sadece geçerli takımlardaki canlı oyuncuları filtrele
                if (health > 0 && health <= 100 && (team == 2 || team == 3)) {
                    
                    // İsmi çekebilmek için Pawn üzerinden Controller'a ters bağlantı kuruyoruz
                    std::string playerName = "Unknown Player";
                    uint32_t controllerHandle = Read<uint32_t>(currentPawn + schema_offsets::m_hController);
                    if (controllerHandle != 0xFFFFFFFF) {
                        uint32_t controllerIndex = controllerHandle & 0x1FFF;
                        uintptr_t ctrlListEntry = Read<uintptr_t>(entityList + (8 * (controllerIndex >> 9) + 16));
                        if (ctrlListEntry) {
                            uintptr_t playerController = Read<uintptr_t>(ctrlListEntry + 120 * (controllerIndex & 0x1FF));
                            if (playerController) {
                                std::string fetchedName = ReadString(playerController + schema_offsets::m_sSanitizedPlayerName);
                                if (!fetchedName.empty()) {
                                    playerName = fetchedName;
                                }
                            }
                        }
                    }
                    
                    currentPlayers.push_back({ i, health, team, playerName });
                }
            }
            
            DisplayPlayers(currentPlayers, localTeam);
        } else {
            COORD cursorPos = { 0, 0 };
            SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), cursorPos);
            std::cout << "Oyun verileri bekleniyor (Maca girilmesi gerek)...          " << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
    }
    
    if (processHandle) {
        CloseHandle(processHandle);
    }
    return 0;
}
