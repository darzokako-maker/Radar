#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <thread>
#include <string>

// Proje klasöründeki güncel dumper dosyalarını dahil ediyoruz
#include "offsets.hpp"    // dwEntityList ve dwLocalPlayerController için
#include "client_dll.hpp" // Şemalar (CCSPlayerController, C_BaseEntity) için

HANDLE processHandle = nullptr;

// 64-bit Güvenli Bellek Okuma Şablonu
template <typename T>
T Read(uintptr_t address) {
    T value{};
    ReadProcessMemory(processHandle, reinterpret_cast<LPCVOID>(address), &value, sizeof(T), nullptr);
    return value;
}

// Oyuncu İsmi Okuyucu (UTF-8 ve Bellek Sınırı Güvenli)
std::string ReadPlayerName(uintptr_t address) {
    char buffer[32] = { 0 };
    ReadProcessMemory(processHandle, reinterpret_cast<LPCVOID>(address), &buffer, sizeof(buffer) - 1, nullptr);
    return std::string(buffer);
}

// Ekranı kırpıştırmadan konsol imlecini sol üste sarma fonksiyonu
void ResetCursor() {
    COORD cursorPosition{ 0, 0 };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), cursorPosition);
}

// Konsoldaki yanıp sönen beyaz imleci gizleme fonksiyonu
void HideConsoleCursor() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(out, &cursorInfo);
    cursorInfo.bVisible = FALSE; 
    SetConsoleCursorInfo(out, &cursorInfo);
}

// Sistemden client.dll adresini çeken fonksiyon
uintptr_t GetModuleBaseAddress(DWORD pid, const wchar_t* moduleName) {
    uintptr_t baseAddress = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W modEntry{.dwSize = sizeof(MODULEENTRY32W)};
        if (Module32FirstW(snapshot, &modEntry)) {
            do {
                if (_wcsicmp(modEntry.szModule, moduleName) == 0) {
                    baseAddress = reinterpret_cast<uintptr_t>(modEntry.modBaseAddr);
                    break;
                }
            } while (Module32NextW(snapshot, &modEntry));
        }
        CloseHandle(snapshot);
    }
    return baseAddress;
}

int main() {
    SetConsoleTitleW(L"CS2 Otomatik Ofset Radar v3.1");
    HideConsoleCursor(); // Başlangıçta imleci gizle
    
    std::cout << "[+] CS2 (cs2.exe) Bekleniyor..." << std::endl;

    DWORD pid = 0;
    while (pid == 0) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W procEntry{.dwSize = sizeof(PROCESSENTRY32W)};
            if (Process32FirstW(snapshot, &procEntry)) {
                do {
                    if (_wcsicmp(procEntry.szExeFile, L"cs2.exe") == 0) {
                        pid = procEntry.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(snapshot, &procEntry));
            }
            CloseHandle(snapshot);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!processHandle) {
        std::cout << "[-] Erisim engellendi! Yonetici olarak calistirin." << std::endl;
        return 1;
    }

    uintptr_t clientModule = GetModuleBaseAddress(pid, L"client.dll");
    system("cls"); // İlk bağlantıda konsolu bir kez temizle

    while (true) {
        // offsets.hpp içerisinden ana adresleri okuyoruz
        uintptr_t entityList = Read<uintptr_t>(clientModule + cs2_dumper::offsets::client_dll::dwEntityList);
        uintptr_t localController = Read<uintptr_t>(clientModule + cs2_dumper::offsets::client_dll::dwLocalPlayerController);
        
        if (!entityList || !localController) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // Yerel oyuncunun (bizim) Pawn bilgilerimizi ve Takım ID'mizi alıyoruz
        uint32_t localPawnHandle = Read<uint32_t>(localController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);
        uint32_t localPawnIndexFromHandle = localPawnHandle & 0x1FFF;
        
        uintptr_t localEntryIndex = 16 + (((localPawnIndexFromHandle & 0x7FFF) >> 9) * 8);
        uintptr_t localEntry = Read<uintptr_t>(entityList + localEntryIndex);
        
        int localTeam = 0;
        if (localEntry >= 0x10000 && localEntry <= 0x7FFFFFFEFFFF) {
            uintptr_t localPawn = Read<uintptr_t>(localEntry + (120 * (localPawnIndexFromHandle & 0x1FF)));
            if (localPawn >= 0x10000 && localPawn <= 0x7FFFFFFEFFFF) {
                localTeam = Read<int>(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
            }
        }

        // Titremeyi önleyen akıcı arayüz çizimi
        ResetCursor();
        std::cout << "====================================================" << std::endl;
        std::cout << " ID  | TAKIM | CAN    | OYUNCU ADI                  " << std::endl;
        std::cout << "====================================================" << std::endl;

        int activeCount = 0;

        for (int i = 1; i < 64; i++) {
            // Katman 1: Ana liste girdisinin hesabı
            uintptr_t listEntryIndex = 16 + (((i & 0x7FFF) >> 9) * 8);
            uintptr_t listEntry = Read<uintptr_t>(entityList + listEntryIndex);
            
            if (listEntry < 0x10000 || listEntry > 0x7FFFFFFEFFFF) continue;

            // Katman 2: Controller adresine erişim
            uintptr_t controllerIndex = 120 * (i & 0x1FF);
            uintptr_t playerController = Read<uintptr_t>(listEntry + controllerIndex);
            
            if (playerController < 0x10000 || playerController > 0x7FFFFFFEFFFF) continue;
            if (playerController == localController) continue;

            // Katman 3: Şema üzerinden Pawn Handle okuma
            uint32_t playerPawnHandle = Read<uint32_t>(playerController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);
            if (!playerPawnHandle) continue;

            // Bit maskeleme ile gerçek index tespiti
            uint32_t pawnIndexFromHandle = playerPawnHandle & 0x1FFF;

            // Katman 4: İkinci liste girdisi (Pawn List Entry) hesabı
            uintptr_t listEntry2Index = 16 + (((pawnIndexFromHandle & 0x7FFF) >> 9) * 8);
            uintptr_t listEntry2 = Read<uintptr_t>(entityList + listEntry2Index);
            
            if (listEntry2 < 0x10000 || listEntry2 > 0x7FFFFFFEFFFF) continue;

            // Katman 5: Asıl fiziksel gövde (Pawn) adresine erişim
            uintptr_t pawnIndex = 120 * (pawnIndexFromHandle & 0x1FF);
            uintptr_t playerPawn = Read<uintptr_t>(listEntry2 + pawnIndex);
            
            if (playerPawn < 0x10000 || playerPawn > 0x7FFFFFFEFFFF) continue;

            // --- GÜVENLİ BÖLGE (Şema Veri Okuması) ---
            int health = Read<int>(playerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
            int team = Read<int>(playerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);   
            std::string name = ReadPlayerName(playerController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_sSanitizedPlayerName);

            // KRİTİK GÜNCELLEME: Canı olan, ismi boş olmayan VE sadece aktif olarak T (2) veya CT (3) takımında olanları listele
            if (health > 0 && health <= 100 && (team == 2 || team == 3) && !name.empty()) {
                
                std::string teamStr = "RAKIP";
                
                // Eğer biz izleyici (1) veya menüde (0) değilsek dost/rakip analizini yap
                if (localTeam == 2 || localTeam == 3) {
                    teamStr = (team == localTeam) ? "DOST " : "RAKIP";
                } else {
                    // Biz izleyiciysek, oynayanların gerçek takım isimlerini göster
                    teamStr = (team == 2) ? "T-TAK" : "CT-TAK";
                }

                // Sabit genişlikli düzgün hizalanmış terminal çıktısı
                std::cout << " [" << (i < 10 ? "0" : "") << i << "] | " 
                          << teamStr << " | " 
                          << (health < 100 ? " " : "") << (health < 10 ? " " : "") << health << " HP | " 
                          << name << "                               \n";
                activeCount++;
            }
        }

        // Listeden çıkanlar veya ölenlerin arkasında terminalde hayalet satırlar kalmaması için temizlik
        for (int k = activeCount; k < 32; k++) {
            std::cout << "                                                               \n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(30)); 
    }

    if (processHandle) CloseHandle(processHandle);
    return 0;
}
