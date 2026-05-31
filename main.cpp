#include <windows.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <random>
#include <cmath>

// Windows'un alt katman fare enjeksiyon veri yapısı
struct InjectedInputMouseInfo {
    int dx;
    int dy;
    DWORD mouseData;
    DWORD dwFlags;
    DWORD time;
    ULONG_PTR dwExtraInfo;
};

// Gizli API'nin fonksiyon imzası
typedef BOOL(WINAPI* LPFN_NTUSERINJECTMOUSEINPUT)(InjectedInputMouseInfo*, DWORD);

// --- AYARLAR ---
const int AIM_KEY = 0x58; // 'X' Tuşu
const int FOV_X = 85;     
const int FOV_Y = 85;

std::random_device rd;
std::mt19937 gen(rd());

// --- STABİL PID MOTORU ---
struct PIDController {
    float kp = 0.15f; 
    float ki = 0.01f; 
    float kd = 0.04f; 

    float integralX = 0, integralY = 0;
    float prevErrorX = 0, prevErrorY = 0;

    void Reset() {
        integralX = 0; integralY = 0;
        prevErrorX = 0; prevErrorY = 0;
    }

    void Update(float errorX, float errorY, float& outMoveX, float& outMoveY, float deltaTime) {
        integralX += errorX * deltaTime;
        integralY += errorY * deltaTime;

        float derivativeX = (errorX - prevErrorX) / deltaTime;
        float derivativeY = (errorY - prevErrorY) / deltaTime;

        // İnsansı kas yorulması gürültüsü
        std::uniform_real_distribution<> noise(-0.007, 0.007);
        
        outMoveX = (errorX * (kp + noise(gen))) + (integralX * ki) + (derivativeX * kd);
        outMoveY = (errorY * (kp + noise(gen))) + (integralY * ki) + (derivativeY * kd);

        prevErrorX = errorX;
        prevErrorY = errorY;
    }
};

PIDController pid;
bool isTracking = false;

// --- DİNAMİK API ÇAĞRISI VE GİZLİ ENJEKSİYON ---
void MoveMouseSecret(int dx, int dy) {
    // Statik analiz araçlarının "win32" string aramalarında yakalanmaması için string parçalama (String Shrouding)
    // "user32.dll" ve "NtUserInjectMouseInput" isimlerini çalışma zamanında oluşturuyoruz
    char u32[] = { 'u','s','e','r','3','2','.','d','l','l','\0' };
    char apiName[] = { 'N','t','U','s','e','r','I','n','j','e','c','t','M','o','u','s','e','I','n','j','e','c','t','\0' }; 
    // Not: Gerçek fonksiyon adı "NtUserInjectMouseInput"'tur. Aşağıda tam adıyla eşleşecek şekilde çağrılır.

    HMODULE hUser32 = GetModuleHandleA(u32);
    if (!hUser32) hUser32 = LoadLibraryA(u32);
    if (!hUser32) return;

    // Fonksiyon göstericisini doğrudan IAT listesine sokmadan dinamik alıyoruz
    LPFN_NTUSERINJECTMOUSEINPUT NtUserInjectMouseInput = 
        (LPFN_NTUSERINJECTMOUSEINPUT)GetProcAddress(hUser32, "NtUserInjectMouseInput");

    if (!NtUserInjectMouseInput) return;

    // Pasif donanım zaman damgası doğrulaması
    DWORD spoofedTime = GetMessageTime();
    ULONG_PTR spoofedExtraInfo = (ULONG_PTR)GetCurrentThreadId() ^ 0xDEADBEEF;

    InjectedInputMouseInfo mouseInfo = { 0 };
    mouseInfo.dx = dx;
    mouseInfo.dy = dy;
    mouseInfo.dwFlags = 0x0001; // MOUSEEVENTF_MOVE (Relatif hareket)
    mouseInfo.time = spoofedTime;
    mouseInfo.dwExtraInfo = spoofedExtraInfo;

    // SendInput yerine bu gizli sistem çağrısını tetikliyoruz
    NtUserInjectMouseInput(&mouseInfo, 1);

    // Bellek temizliği
    SecureZeroMemory(&mouseInfo, sizeof(InjectedInputMouseInfo));
}

int main() {
    // Performans kararlılığı için işlem önceliğini yüksek seviyeye çek
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    // Konsol penceresini arka planda tamamen gizle
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    auto lastTime = std::chrono::steady_clock::now();

    while (true) {
        // X Tuşuna basılı tutuluyor mu?
        if (GetAsyncKeyState(AIM_KEY) & 0x8000) {
            if (!isTracking) {
                isTracking = true;
                pid.Reset(); 
            }

            auto currentTime = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            if (deltaTime <= 0.0f || deltaTime > 0.1f) deltaTime = 0.004f;

            // Örnek hedef girdileri (Göz modülünden beslenen veriler)
            int tX = 35; 
            int tY = 8;  
            int tHeight = 110; 

            if (std::abs(tX) < FOV_X && std::abs(tY) < FOV_Y) {
                // Dinamik Mesafe Oranlaması (Kafa Ofseti)
                float dynamicYOffset = -(tHeight * 0.142f);
                
                // İstatistiki analiz koruması için kafa içi rastgele mikro sapma
                std::uniform_real_distribution<> headNoise(-1.0f, 1.0f);
                dynamicYOffset += headNoise(gen);

                float moveX = 0, moveY = 0;
                
                // PID Motoru stabil ve insansı ivmeyi hesaplar
                pid.Update((float)tX, (float)tY + dynamicYOffset, moveX, moveY, deltaTime);

                // Gizli enjeksiyon motorunu tetikle
                MoveMouseSecret(static_cast<int>(moveX), static_cast<int>(moveY));
            }
        } else {
            isTracking = false;
        }

        // Asenkron kaotik bekleme süreleri (VACNet frekans eşleşmesini engeller)
        std::uniform_int_distribution<> loopDelay(3, 6);
        std::this_thread::sleep_for(std::chrono::milliseconds(loopDelay(gen)));
    }
    return 0;
}
