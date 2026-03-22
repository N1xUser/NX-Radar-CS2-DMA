#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <shlobj.h>
#include <string>
#include <tlhelp32.h>
#include <vector>
#include <windows.h>

#include "client_dll.hpp"
#include "offsets.hpp"

#define ENTITY_STRIDE 0x70
#define HANDSHAKE_MSG "RADAR_INIT\n"
#define HANDSHAKE_RESPONSE "RADAR_ACK\n"
#define HANDSHAKE_TIMEOUT_MS 2000
#define DISCONNECT_MSG "RADAR_DISCONNECT\n"

struct NameBuffer {
  char buffer[128];
};
struct Vec3 {
  float x, y, z;
};

std::ofstream log_file;
bool logging_enabled = false;

void init_logging() {
  char desktop_path[MAX_PATH];
  if (SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop_path) ==
      S_OK) {
    std::string log_path = std::string(desktop_path) + "\\out.txt";

    // Check if file exists
    std::ifstream test(log_path);
    if (test.good()) {
      test.close();
      log_file.open(log_path, std::ios::app);
      if (log_file.is_open()) {
        logging_enabled = true;
        log_file << "\n=== New Session Started ===\n";
        log_file.flush();
      }
    }
  }
}

void log_message(const std::string &msg) {
  if (logging_enabled && log_file.is_open()) {
    log_file << msg << std::endl;
    log_file.flush();
  }
}

namespace driver {
namespace codes {
constexpr ULONG attach =
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
constexpr ULONG read =
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
} // namespace codes
struct Request {
  HANDLE process_id;
  PVOID target;
  PVOID buffer;
  SIZE_T size;
  SIZE_T return_size;
};
bool attach_to_process(HANDLE h, DWORD pid) {
  Request r;
  r.process_id = (HANDLE)pid;
  return DeviceIoControl(h, codes::attach, &r, sizeof(r), &r, sizeof(r),
                         nullptr, nullptr);
}
template <class T> T read_memory(HANDLE h, std::uintptr_t addr) {
  T temp = {};
  Request r;
  r.target = (PVOID)addr;
  r.buffer = &temp;
  r.size = sizeof(T);
  DeviceIoControl(h, codes::read, &r, sizeof(r), &r, sizeof(r), nullptr,
                  nullptr);
  return temp;
}
} // namespace driver

static DWORD get_process_id(const wchar_t *process_name) {
  DWORD pid = 0;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
  if (snap == INVALID_HANDLE_VALUE)
    return 0;
  PROCESSENTRY32W entry = {sizeof(entry)};
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

static std::uintptr_t get_module_base(DWORD pid, const wchar_t *module_name) {
  std::uintptr_t base = 0;
  HANDLE snap =
      CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
  if (snap == INVALID_HANDLE_VALUE)
    return 0;
  MODULEENTRY32W entry = {sizeof(entry)};
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

static HANDLE try_open_com_port(int port_num) {
  std::wstring port_name = L"\\\\.\\COM" + std::to_wstring(port_num);

  HANDLE h = CreateFile(port_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  if (h == INVALID_HANDLE_VALUE)
    return INVALID_HANDLE_VALUE;

  DCB dcb = {0};
  dcb.DCBlength = sizeof(dcb);
  GetCommState(h, &dcb);
  dcb.BaudRate = CBR_115200;
  dcb.ByteSize = 8;
  dcb.StopBits = ONESTOPBIT;
  dcb.Parity = NOPARITY;
  SetCommState(h, &dcb);

  COMMTIMEOUTS to = {0};
  to.ReadIntervalTimeout = 50;
  to.ReadTotalTimeoutConstant = 100;
  to.ReadTotalTimeoutMultiplier = 10;
  to.WriteTotalTimeoutConstant = 100;
  SetCommTimeouts(h, &to);

  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
  return h;
}

static bool perform_handshake(HANDLE hSerial) {
  DWORD written;
  if (!WriteFile(hSerial, HANDSHAKE_MSG, strlen(HANDSHAKE_MSG), &written,
                 NULL)) {
    return false;
  }

  char response[64] = {0};
  DWORD start_time = GetTickCount();
  DWORD bytes_read = 0;
  int total_read = 0;

  while (GetTickCount() - start_time < HANDSHAKE_TIMEOUT_MS) {
    if (ReadFile(hSerial, response + total_read,
                 sizeof(response) - total_read - 1, &bytes_read, NULL)) {
      if (bytes_read > 0) {
        total_read += bytes_read;
        response[total_read] = '\0';

        if (strstr(response, "RADAR_ACK") != NULL) {
          return true;
        }
      }
    }
    Sleep(50);
  }

  return false;
}

static HANDLE auto_detect_com_port() {
  log_message("Auto-detecting COM port...");

  for (int port = 1; port <= 20; port++) {
    std::string msg = "Trying COM" + std::to_string(port) + "...";
    log_message(msg);

    HANDLE h = try_open_com_port(port);
    if (h == INVALID_HANDLE_VALUE) {
      log_message("  Not available.");
      continue;
    }

    log_message("  Open. Testing handshake...");

    if (perform_handshake(h)) {
      msg = "SUCCESS! Radar display detected on COM" + std::to_string(port);
      log_message(msg);
      return h;
    }

    log_message("  No response.");
    CloseHandle(h);
    Sleep(100);
  }

  return INVALID_HANDLE_VALUE;
}

bool is_device_connected(HANDLE hSerial) {
  DWORD errors;
  COMSTAT status;
  if (!ClearCommError(hSerial, &errors, &status)) {
    return false;
  }
  return true;
}

void send_disconnect_signal(HANDLE hSerial) {
  DWORD written;
  WriteFile(hSerial, DISCONNECT_MSG, strlen(DISCONNECT_MSG), &written, NULL);
  log_message("Disconnect signal sent to display.");
}

std::uintptr_t GetPawnFromHandle(HANDLE driver, std::uintptr_t entity_list,
                                 std::uint32_t handle) {
  if (handle == 0xFFFFFFFF)
    return 0;
  std::uintptr_t list_entry = driver::read_memory<std::uintptr_t>(
      driver, entity_list + 0x8 * ((handle & 0x7FFF) >> 9) + 16);
  if (!list_entry)
    return 0;
  return driver::read_memory<std::uintptr_t>(
      driver, list_entry + ENTITY_STRIDE * (handle & 0x1FF));
}

int main() {
  // Hide console window
  HWND console = GetConsoleWindow();
  ShowWindow(console, SW_HIDE);

  init_logging();
  log_message("=== CS2 External Radar Sender ===");

  HANDLE hSerial = auto_detect_com_port();
  if (hSerial == INVALID_HANDLE_VALUE) {
    log_message("ERROR: Failed to detect radar display.");
    if (logging_enabled)
      log_file.close();
    return 1;
  }

  DWORD pid = get_process_id(L"cs2.exe");
  if (!pid) {
    log_message("ERROR: CS2 not found.");
    send_disconnect_signal(hSerial);
    CloseHandle(hSerial);
    if (logging_enabled)
      log_file.close();
    return 1;
  }
  log_message("CS2 Found PID: " + std::to_string(pid));

  HANDLE driver = CreateFile(L"\\\\.\\NXWire", GENERIC_READ | GENERIC_WRITE, 0,
                             0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  if (driver == INVALID_HANDLE_VALUE) {
    log_message("ERROR: Driver not loaded.");
    send_disconnect_signal(hSerial);
    CloseHandle(hSerial);
    if (logging_enabled)
      log_file.close();
    return 1;
  }

  if (!driver::attach_to_process(driver, pid)) {
    log_message("ERROR: Driver attach failed.");
    CloseHandle(driver);
    send_disconnect_signal(hSerial);
    CloseHandle(hSerial);
    if (logging_enabled)
      log_file.close();
    return 1;
  }

  std::uintptr_t client = get_module_base(pid, L"client.dll");
  if (!client) {
    log_message("ERROR: client.dll not found.");
    CloseHandle(driver);
    send_disconnect_signal(hSerial);
    CloseHandle(hSerial);
    if (logging_enabled)
      log_file.close();
    return 1;
  }

  char hex_str[32];
  sprintf_s(hex_str, sizeof(hex_str), "0x%llx", (unsigned long long)client);
  log_message("client.dll: " + std::string(hex_str));
  log_message("LOOP RUNNING - Press END key to stop");

  const auto offset_entity_list =
      client + cs2_dumper::offsets::client_dll::dwEntityList;
  const auto offset_local_controller =
      client + cs2_dumper::offsets::client_dll::dwLocalPlayerController;
  const auto offset_view_angles =
      client + cs2_dumper::offsets::client_dll::dwViewAngles;
  const auto offset_planted_c4 =
      client + cs2_dumper::offsets::client_dll::dwPlantedC4;
  const auto offset_game_rules =
      client + cs2_dumper::offsets::client_dll::dwGameRules;

  using namespace cs2_dumper::schemas::client_dll;
  const auto m_hPlayerPawn = CCSPlayerController::m_hPlayerPawn;
  const auto m_iTeamNum = C_BaseEntity::m_iTeamNum;
  const auto m_iHealth = C_BaseEntity::m_iHealth;
  const auto m_lifeState = C_BaseEntity::m_lifeState;
  const auto m_vOldOrigin = C_BasePlayerPawn::m_vOldOrigin;
  const auto m_sSanitizedPlayerName =
      CCSPlayerController::m_sSanitizedPlayerName;
  const auto m_bBombPlanted = C_CSGameRules::m_bBombPlanted;
  const auto m_nBombSite = C_PlantedC4::m_nBombSite;

  const auto m_hObserverPawn = CCSPlayerController::m_hObserverPawn;
  const auto m_pObserverServices = C_BasePlayerPawn::m_pObserverServices;
  const auto m_hObserverTarget = CPlayer_ObserverServices::m_hObserverTarget;

  char send_buffer[1024];
  int disconnect_check_counter = 0;

  while (true) {
    if (GetAsyncKeyState(VK_END) & 0x8000) {
      log_message("END key pressed - Shutting down.");
      break;
    }

    // Check device connection every 60 frames (~1 second)
    disconnect_check_counter++;
    if (disconnect_check_counter >= 60) {
      disconnect_check_counter = 0;
      if (!is_device_connected(hSerial)) {
        log_message("ERROR: ESP32 disconnected! Exiting...");
        break;
      }
    }

    uintptr_t entity_list =
        driver::read_memory<uintptr_t>(driver, offset_entity_list);
    if (!entity_list) {
      Sleep(50);
      continue;
    }

    uintptr_t local_ctrl =
        driver::read_memory<uintptr_t>(driver, offset_local_controller);
    if (!local_ctrl) {
      Sleep(50);
      continue;
    }

    uint32_t local_h =
        driver::read_memory<uint32_t>(driver, local_ctrl + m_hPlayerPawn);
    uintptr_t local_pawn = GetPawnFromHandle(driver, entity_list, local_h);
    if (!local_pawn) {
      Sleep(50);
      continue;
    }

    int local_hp = driver::read_memory<int>(driver, local_pawn + m_iHealth);

    uintptr_t render_pawn = local_pawn;

    if (local_hp <= 0) {
      uint32_t obs_pawn_h =
          driver::read_memory<uint32_t>(driver, local_ctrl + m_hObserverPawn);
      uintptr_t obs_pawn = GetPawnFromHandle(driver, entity_list, obs_pawn_h);
      if (obs_pawn) {
        uintptr_t obs_services = driver::read_memory<uintptr_t>(
            driver, obs_pawn + m_pObserverServices);
        if (obs_services) {
          uint32_t target_h = driver::read_memory<uint32_t>(
              driver, obs_services + m_hObserverTarget);
          uintptr_t target = GetPawnFromHandle(driver, entity_list, target_h);
          if (target) {
            render_pawn = target;
          }
        }
      }
    }

    int local_team =
        (int)driver::read_memory<uint8_t>(driver, render_pawn + m_iTeamNum);
    Vec3 local_pos =
        driver::read_memory<Vec3>(driver, render_pawn + m_vOldOrigin);
    Vec3 angles = driver::read_memory<Vec3>(driver, offset_view_angles);

    int pos = 0;
    pos += snprintf(send_buffer + pos, sizeof(send_buffer) - pos, "p,%d,%d,%d",
                    (int)local_pos.x, (int)local_pos.y, (int)angles.y);

    int enemy_count = 0;
    for (int i = 1; i <= 64 && enemy_count < 10; i++) {
      uintptr_t list_entry = driver::read_memory<uintptr_t>(
          driver, entity_list + 0x8 * ((i & 0x7FFF) >> 9) + 16);
      if (!list_entry)
        continue;

      uintptr_t ctrl = driver::read_memory<uintptr_t>(
          driver, list_entry + ENTITY_STRIDE * (i & 0x1FF));
      if (!ctrl)
        continue;

      uint32_t p_h =
          driver::read_memory<uint32_t>(driver, ctrl + m_hPlayerPawn);
      if (p_h == 0xFFFFFFFF)
        continue;

      uintptr_t pawn = GetPawnFromHandle(driver, entity_list, p_h);
      if (!pawn)
        continue;

      if (pawn == render_pawn)
        continue;

      int team = (int)driver::read_memory<uint8_t>(driver, pawn + m_iTeamNum);
      if (team == local_team)
        continue;

      int hp = driver::read_memory<int>(driver, pawn + m_iHealth);
      int life = (int)driver::read_memory<uint8_t>(driver, pawn + m_lifeState);
      if (life != 0 || hp <= 0)
        continue;

      uintptr_t name_ptr =
          driver::read_memory<uintptr_t>(driver, ctrl + m_sSanitizedPlayerName);
      NameBuffer nb = driver::read_memory<NameBuffer>(driver, name_ptr);

      std::string name = "";
      for (int k = 0; k < 32 && nb.buffer[k] != '\0'; k++) {
        char c = nb.buffer[k];
        if (isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' ||
            c == '-')
          name += c;
      }
      if (name.empty())
        name = "Unknown";
      if (name.length() > 7)
        name = name.substr(0, 7);

      Vec3 e_pos = driver::read_memory<Vec3>(driver, pawn + m_vOldOrigin);

      pos += snprintf(send_buffer + pos, sizeof(send_buffer) - pos,
                      ";e,%d,%d,%d,%s", (int)e_pos.x, (int)e_pos.y, hp,
                      name.c_str());
      enemy_count++;
    }

    uintptr_t game_rules =
        driver::read_memory<uintptr_t>(driver, offset_game_rules);
    bool is_planted =
        driver::read_memory<bool>(driver, game_rules + m_bBombPlanted);

    if (is_planted) {
      uintptr_t c4_ptr =
          driver::read_memory<uintptr_t>(driver, offset_planted_c4);
      if (c4_ptr) {
        int site = driver::read_memory<int>(driver, c4_ptr + m_nBombSite);
        // CS2 standard: 0 = A, 1 = B
        // Send raw value, let display handle any map-specific logic if needed
        pos += snprintf(send_buffer + pos, sizeof(send_buffer) - pos, ";b,%d",
                        site);
      }
    }

    send_buffer[pos++] = '\n';
    send_buffer[pos] = '\0';

    DWORD written;
    WriteFile(hSerial, send_buffer, pos, &written, NULL);

    Sleep(16);
  }

  send_disconnect_signal(hSerial);
  CloseHandle(driver);
  CloseHandle(hSerial);

  log_message("=== Session Ended ===");
  if (logging_enabled)
    log_file.close();

  return 0;
}