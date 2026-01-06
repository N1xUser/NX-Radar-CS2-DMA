#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>

#include "offsets.hpp"
#include "client_dll.hpp"

#define ENTITY_STRIDE 0x70 

struct NameBuffer { char buffer[128]; };
struct Vec3 { float x, y, z; };

namespace driver {
    namespace codes {
        constexpr ULONG attach = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
    }
    struct Request {
        HANDLE process_id;
        PVOID target;
        PVOID buffer;
        SIZE_T size;
        SIZE_T return_size;
    };
    bool attach_to_process(HANDLE h, DWORD pid) {
        Request r; r.process_id = (HANDLE)pid;
        return DeviceIoControl(h, codes::attach, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);
    }
    template <class T>
    T read_memory(HANDLE h, std::uintptr_t addr) {
        T temp = {};
        Request r; r.target = (PVOID)addr; r.buffer = &temp; r.size = sizeof(T);
        DeviceIoControl(h, codes::read, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);
        return temp;
    }
}

static DWORD get_process_id(const wchar_t* process_name) {
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry = { sizeof(entry) };
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(process_name, entry.szExeFile) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return pid;
}

static std::uintptr_t get_module_base(DWORD pid, const wchar_t* module_name) {
    std::uintptr_t base = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W entry = { sizeof(entry) };
    if (Module32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(module_name, entry.szModule) == 0) {
                base = (std::uintptr_t)entry.modBaseAddr;
                break;
            }
        } while (Module32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return base;
}

static HANDLE open_com_port() {
    int port_num;
    std::cout << "[?] Enter COM port number (e.g., 3): ";
    std::cin >> port_num;
    if (std::cin.fail()) {
        std::cin.clear();
        std::cin.ignore(32767, '\n');
        return INVALID_HANDLE_VALUE;
    }

    std::wstring port_name = L"\\\\.\\COM" + std::to_wstring(port_num);
    std::cout << "[...] Opening COM" << port_num << "...\n";

    HANDLE h = CreateFile(port_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    DCB dcb = { 0 }; dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    SetCommState(h, &dcb);

    COMMTIMEOUTS to = { 0 };
    to.ReadIntervalTimeout = 1;
    to.ReadTotalTimeoutConstant = 1;
    to.WriteTotalTimeoutConstant = 10;
    SetCommTimeouts(h, &to);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return h;
}

void exit_with_pause(const char* msg) {
    std::cout << "\n[!] ERROR: " << msg << "\n";
    std::cout << "Press any key to close...\n";
    system("pause > nul");
    exit(1);
}

std::uintptr_t GetPawnFromHandle(HANDLE driver, std::uintptr_t entity_list, std::uint32_t handle) {
    if (handle == 0xFFFFFFFF) return 0;
    std::uintptr_t list_entry = driver::read_memory<std::uintptr_t>(driver, entity_list + 0x8 * ((handle & 0x7FFF) >> 9) + 16);
    if (!list_entry) return 0;
    return driver::read_memory<std::uintptr_t>(driver, list_entry + ENTITY_STRIDE * (handle & 0x1FF));
}

int main() {
    std::cout << "--- CS2 External Radar Sender ---\n";

    HANDLE hSerial = open_com_port();
    if (hSerial == INVALID_HANDLE_VALUE) exit_with_pause("Failed to open COM port.");
    std::cout << "[+] COM Port Opened.\n";

    DWORD pid = get_process_id(L"cs2.exe");
    if (!pid) exit_with_pause("CS2 not found.");
    std::cout << "[+] CS2 Found PID: " << pid << "\n";

    HANDLE driver = CreateFile(L"\\\\.\\NXWire", GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (driver == INVALID_HANDLE_VALUE) exit_with_pause("Driver not loaded.");

    if (!driver::attach_to_process(driver, pid)) {
        CloseHandle(driver);
        exit_with_pause("Driver attach failed.");
    }

    std::uintptr_t client = get_module_base(pid, L"client.dll");
    if (!client) {
        CloseHandle(driver);
        exit_with_pause("client.dll not found.");
    }
    std::cout << "[+] client.dll: 0x" << std::hex << client << std::dec << "\n";
    std::cout << "[***] LOOP RUNNING [***]\n";

    const auto offset_entity_list = client + cs2_dumper::offsets::client_dll::dwEntityList;
    const auto offset_local_controller = client + cs2_dumper::offsets::client_dll::dwLocalPlayerController;
    const auto offset_view_angles = client + cs2_dumper::offsets::client_dll::dwViewAngles;
    const auto offset_planted_c4 = client + cs2_dumper::offsets::client_dll::dwPlantedC4;
    const auto offset_game_rules = client + cs2_dumper::offsets::client_dll::dwGameRules;

    using namespace cs2_dumper::schemas::client_dll;
    const auto m_hPlayerPawn = CCSPlayerController::m_hPlayerPawn;
    const auto m_iTeamNum = C_BaseEntity::m_iTeamNum;
    const auto m_iHealth = C_BaseEntity::m_iHealth;
    const auto m_lifeState = C_BaseEntity::m_lifeState;
    const auto m_vOldOrigin = C_BasePlayerPawn::m_vOldOrigin;
    const auto m_sSanitizedPlayerName = CCSPlayerController::m_sSanitizedPlayerName;
    const auto m_bBombPlanted = C_CSGameRules::m_bBombPlanted;
    const auto m_nBombSite = C_PlantedC4::m_nBombSite;

    const auto m_hObserverPawn = CCSPlayerController::m_hObserverPawn;
    const auto m_pObserverServices = C_BasePlayerPawn::m_pObserverServices;
    const auto m_hObserverTarget = CPlayer_ObserverServices::m_hObserverTarget;

    char send_buffer[1024];

    while (true) {
        if (GetAsyncKeyState(VK_END) & 0x8000) break;

        uintptr_t entity_list = driver::read_memory<uintptr_t>(driver, offset_entity_list);
        if (!entity_list) { Sleep(50); continue; }

        uintptr_t local_ctrl = driver::read_memory<uintptr_t>(driver, offset_local_controller);
        if (!local_ctrl) { Sleep(50); continue; }

        uint32_t local_h = driver::read_memory<uint32_t>(driver, local_ctrl + m_hPlayerPawn);
        uintptr_t local_pawn = GetPawnFromHandle(driver, entity_list, local_h);
        if (!local_pawn) { Sleep(50); continue; }

        int local_hp = driver::read_memory<int>(driver, local_pawn + m_iHealth);

        uintptr_t render_pawn = local_pawn;

        if (local_hp <= 0) {
            uint32_t obs_pawn_h = driver::read_memory<uint32_t>(driver, local_ctrl + m_hObserverPawn);
            uintptr_t obs_pawn = GetPawnFromHandle(driver, entity_list, obs_pawn_h);
            if (obs_pawn) {
                uintptr_t obs_services = driver::read_memory<uintptr_t>(driver, obs_pawn + m_pObserverServices);
                if (obs_services) {
                    uint32_t target_h = driver::read_memory<uint32_t>(driver, obs_services + m_hObserverTarget);
                    uintptr_t target = GetPawnFromHandle(driver, entity_list, target_h);
                    if (target) {
                        render_pawn = target;
                    }
                }
            }
        }

        int local_team = (int)driver::read_memory<uint8_t>(driver, render_pawn + m_iTeamNum);
        Vec3 local_pos = driver::read_memory<Vec3>(driver, render_pawn + m_vOldOrigin);
        Vec3 angles = driver::read_memory<Vec3>(driver, offset_view_angles);

        int pos = 0;
        pos += snprintf(send_buffer + pos, sizeof(send_buffer) - pos, "p,%d,%d,%d",
            (int)local_pos.x, (int)local_pos.y, (int)angles.y);

        int enemy_count = 0;
        for (int i = 0; i < 64 && enemy_count < 10; i++) {
            uintptr_t entry = driver::read_memory<uintptr_t>(driver, entity_list + (8 * (i & 0x7FFF) >> 9) + 16);
            if (!entry) continue;

            uintptr_t ctrl = driver::read_memory<uintptr_t>(driver, entry + ENTITY_STRIDE * (i & 0x1FF));
            if (!ctrl) continue;

            uint32_t p_h = driver::read_memory<uint32_t>(driver, ctrl + m_hPlayerPawn);
            if (p_h == 0xFFFFFFFF) continue;

            uintptr_t pawn = GetPawnFromHandle(driver, entity_list, p_h);
            if (!pawn) continue;

            if (pawn == render_pawn) continue;

            int team = (int)driver::read_memory<uint8_t>(driver, ctrl + m_iTeamNum);
            if (team == local_team) continue;

            int hp = driver::read_memory<int>(driver, pawn + m_iHealth);
            int life = (int)driver::read_memory<uint8_t>(driver, pawn + m_lifeState);
            if (life != 0 || hp <= 0) continue;

            uintptr_t name_ptr = driver::read_memory<uintptr_t>(driver, ctrl + m_sSanitizedPlayerName);
            NameBuffer nb = driver::read_memory<NameBuffer>(driver, name_ptr);

            std::string name = "";
            for (int k = 0; k < 32 && nb.buffer[k] != '\0'; k++) {
                char c = nb.buffer[k];
                if (isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '-') name += c;
            }
            if (name.empty()) name = "Unknown";
            if (name.length() > 7) name = name.substr(0, 7);

            Vec3 e_pos = driver::read_memory<Vec3>(driver, pawn + m_vOldOrigin);

            pos += snprintf(send_buffer + pos, sizeof(send_buffer) - pos, ";e,%d,%d,%d,%s",
                (int)e_pos.x, (int)e_pos.y, hp, name.c_str());
            enemy_count++;
        }

        uintptr_t game_rules = driver::read_memory<uintptr_t>(driver, offset_game_rules);
        bool is_planted = driver::read_memory<bool>(driver, game_rules + m_bBombPlanted);

        if (is_planted) {
            uintptr_t c4_ptr = driver::read_memory<uintptr_t>(driver, offset_planted_c4);
            if (c4_ptr) {
                int site = driver::read_memory<int>(driver, c4_ptr + m_nBombSite);
                pos += snprintf(send_buffer + pos, sizeof(send_buffer) - pos, ";b,%d", site);
            }
        }

        send_buffer[pos++] = '\n';
        send_buffer[pos] = '\0';

        DWORD written;
        WriteFile(hSerial, send_buffer, pos, &written, NULL);

        Sleep(16);
    }

    CloseHandle(driver);
    CloseHandle(hSerial);
    return 0;
}