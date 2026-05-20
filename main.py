import asyncio
import ctypes
import struct
import json
import websockets

# --- Windows Çekirdek API Ayarları ---
PROCESS_VM_READ = 0x0010
TH32CS_SNAPPROCESS = 0x00000002
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010

class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [
        ('dwSize', ctypes.c_ulong), ('cntUsage', ctypes.c_ulong),
        ('th32ProcessID', ctypes.c_ulong), ('th32DefaultHeapID', ctypes.c_void_p),
        ('th32ModuleID', ctypes.c_ulong), ('cntThreads', ctypes.c_ulong),
        ('th32ParentProcessID', ctypes.c_ulong), ('pcPriClassBase', ctypes.c_long),
        ('dwFlags', ctypes.c_ulong), ('szExeFile', ctypes.c_char * 260)
    ]

class MODULEENTRY32(ctypes.Structure):
    _fields_ = [
        ('dwSize', ctypes.c_ulong), ('th32ModuleID', ctypes.c_ulong),
        ('th32ProcessID', ctypes.c_ulong), ('GlblcntUsage', ctypes.c_ulong),
        ('ProccntUsage', ctypes.c_ulong), ('modBaseAddr', ctypes.c_void_p),
        ('modBaseSize', ctypes.c_ulong), ('hModule', ctypes.c_void_p),
        ('szModule', ctypes.c_char * 256), ('szExePath', ctypes.c_char * 260)
    ]

class CS2CoreEngine:
    def __init__(self):
        self.process_handle = None
        self.client_base = 0
        self.pid = self._get_pid("cs2.exe")
        
        if self.pid:
            self.process_handle = ctypes.windll.kernel32.OpenProcess(PROCESS_VM_READ, False, self.pid)
            self.client_base = self._get_module("client.dll")

    def _get_pid(self, name):
        snap = ctypes.windll.kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
        entry = PROCESSENTRY32()
        entry.dwSize = ctypes.sizeof(PROCESSENTRY32)
        if ctypes.windll.kernel32.Process32First(snap, ctypes.byref(entry)):
            while ctypes.windll.kernel32.Process32Next(snap, ctypes.byref(entry)):
                if entry.szExeFile.decode('utf-8', errors='ignore').lower() == name.lower():
                    ctypes.windll.kernel32.CloseHandle(snap)
                    return entry.th32ProcessID
        ctypes.windll.kernel32.CloseHandle(snap)
        return 0

    def _get_module(self, name):
        if not self.pid: return 0
        snap = ctypes.windll.kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, self.pid)
        entry = MODULEENTRY32()
        entry.dwSize = ctypes.sizeof(MODULEENTRY32)
        if ctypes.windll.kernel32.Module32First(snap, ctypes.byref(entry)):
            while ctypes.windll.kernel32.Module32Next(snap, ctypes.byref(entry)):
                if entry.szModule.decode('utf-8', errors='ignore').lower() == name.lower():
                    ctypes.windll.kernel32.CloseHandle(snap)
                    return int(entry.modBaseAddr)
        ctypes.windll.kernel32.CloseHandle(snap)
        return 0

    def is_active(self):
        if not self.process_handle: return False
        exit_code = ctypes.c_ulong()
        ctypes.windll.kernel32.GetExitCodeProcess(self.process_handle, ctypes.byref(exit_code))
        return exit_code.value == 259

    def read_raw(self, address, size):
        if not self.process_handle or address <= 0: return b'\x00' * size
        buf = ctypes.create_string_buffer(size)
        read = ctypes.c_size_t()
        if ctypes.windll.kernel32.ReadProcessMemory(self.process_handle, address, buf, size, ctypes.byref(read)):
            return buf.raw
        return b'\x00' * size

    def read_int(self, address):
        return struct.unpack('<i', self.read_raw(address, 4))[0]

    def read_uint64(self, address):
        return struct.unpack('<Q', self.read_raw(address, 8))[0]

    def read_vec3(self, address):
        return struct.unpack('<fff', self.read_raw(address, 12))
        
    def close(self):
        if self.process_handle:
            ctypes.windll.kernel32.CloseHandle(self.process_handle)
            self.process_handle = None

engine = None

async def network_broadcast(websocket):
    global engine
    
    # 2026-05-19 Güncel Statik Adresleri
    dwLocalPlayerPawn = 0x2090880
    dwEntityList = 0x250C5B0
    
    # İç Şema Bilgileri (Offset)
    m_iHealth = 0x334
    m_iTeamNum = 0x3CF
    m_vOldOrigin = 0xCD8
    m_hPlayerPawn = 0x80C

    try:
        while True:
            if not engine or not engine.is_active():
                if engine: engine.close()
                engine = CS2CoreEngine()
                if not engine.process_handle:
                    await websocket.send(json.dumps([]))
                    await asyncio.sleep(1.0)
                    continue

            synchronized_players = []
            
            try:
                entity_list = engine.read_uint64(engine.client_base + dwEntityList)
                local_pawn = engine.read_uint64(engine.client_base + dwLocalPlayerPawn)
                
                # Yerel Oyuncunun Harita ve Canlılık Validasyonu
                if local_pawn > 0:
                    local_hp = engine.read_int(local_pawn + m_iHealth)
                    
                    if local_hp <= 0 or local_hp > 100:
                        await websocket.send(json.dumps([]))
                        await asyncio.sleep(0.1)
                        continue
                        
                    local_team = engine.read_int(local_pawn + m_iTeamNum)
                else:
                    await websocket.send(json.dumps([]))
                    await asyncio.sleep(0.5)
                    continue

                if entity_list:
                    for idx in range(1, 64):
                        # 1. Aşama: Controller Hücresine Bağlanma
                        list_entry = engine.read_uint64(entity_list + ((8 * (idx & 0x7FFF)) >> 9) + 16)
                        if not list_entry: continue
                        
                        controller = engine.read_uint64(list_entry + 120 * (idx & 0x1FF))
                        if not controller: continue
                        
                        pawn_handle = engine.read_int(controller + m_hPlayerPawn)
                        if not pawn_handle: continue
                        
                        # --- Senkronize Edilmiş Çift Katmanlı İndeks Algoritması ---
                        pawn_idx = pawn_handle & 0x7FFF
                        
                        # Genel temel indeks üzerinden katman bloğunu buluyoruz
                        list_entry_pawn = engine.read_uint64(entity_list + ((8 * pawn_idx) >> 9) + 16)
                        if not list_entry_pawn: continue
                        
                        # Aynı indeks köküne alt 9 bit (0x1FF) maskelemesi ile spesifik hücreyi çözüyoruz
                        pawn_ptr = engine.read_uint64(list_entry_pawn + 120 * (pawn_idx & 0x1FF))
                        if not pawn_ptr or pawn_ptr == local_pawn: continue

                        hp = engine.read_int(pawn_ptr + m_iHealth)
                        if hp <= 0 or hp > 100: continue
                        
                        team = engine.read_int(pawn_ptr + m_iTeamNum)
                        x, y, z = engine.read_vec3(pawn_ptr + m_vOldOrigin)

                        synchronized_players.append({
                            "index": idx,
                            "team": "CT" if team == 3 else "T",
                            "is_enemy": team != local_team,
                            "pos_x": round(x, 2),
                            "pos_y": round(y, 2),
                            "pos_z": round(z, 2),
                            "health": hp
                        })
            except Exception:
                pass

            # JSON Serileştirme ile Ağ Yayını
            await websocket.send(json.dumps(synchronized_players))
            await asyncio.sleep(0.016) # 60 FPS Kararlılık Aralığı
            
    except websockets.exceptions.ConnectionClosed:
        pass

async def main():
    async with websockets.serve(network_broadcast, "0.0.0.0", 8080):
        await asyncio.Future()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        if engine:
            engine.close()
      
