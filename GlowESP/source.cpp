#include "../GlowESP/memory.hpp"
#include "../GlowESP/arrange.hpp"
#include <conio.h>
#include <stdio.h>
#include <iostream>
#include <Windows.h>

using namespace std;

DWORD GetProcessIdByName(const wchar_t* processName) {
    DWORD processId = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W processEntry = { sizeof(processEntry) };
        if (Process32FirstW(snapshot, &processEntry)) {
            do {
                if (_wcsicmp(processEntry.szExeFile, processName) == 0) {
                    processId = processEntry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &processEntry));
        }
        CloseHandle(snapshot);
    }
    return processId;
}

// 检查驱动服务是否已安装并运行
bool IsDriverInstalledAndRunning(const std::string& serviceName) {
    SC_HANDLE hSCM = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return false;

    SC_HANDLE hService = OpenServiceA(hSCM, serviceName.c_str(), SERVICE_QUERY_STATUS);
    if (!hService) {
        CloseServiceHandle(hSCM);
        return false;
    }

    SERVICE_STATUS_PROCESS status;
    DWORD bytesNeeded;
    bool running = false;
    if (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(status), &bytesNeeded)) {
        running = (status.dwCurrentState == SERVICE_RUNNING);
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    return running;
}


struct Offsets {
    //offsets.cs
    const uintptr_t dwEntityList = 0x1D15578;
    const uintptr_t dwLocalPlayerPawn = 0x1BF14A0;

    //client_dll.cs
    const uintptr_t m_Glow = 0xCC0;
    const uintptr_t m_iGlowType = 0x30;
    const uintptr_t m_glowColorOverride = 0x40;
    const uintptr_t m_bGlowing = 0x51;
    const uintptr_t m_iHealth = 0x34C;
    const uintptr_t m_iTeamNum = 0x3EB;
    const uintptr_t m_hPlayerPawn = 0x8FC;
}Offsets;


int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

    std::string driverPath = "C:\\Windows\\System32\\SmileDriver.sys";
    std::string driverUrl = "";
    std::string serviceName = "SmileDriver";

    // 检查驱动文件是否已存在
    if (!std::filesystem::exists(driverPath)) {
        if (!DownloadFile(driverUrl, driverPath)) {
            std::wcout << L"云驱动下载失败！\n";
            return 1;
        }
    }

    // 检查驱动是否已安装并运行
    if (!IsDriverInstalledAndRunning(serviceName)) {
        // 未安装或未运行则安装并启动
        if (!InstallAndStartDriver(driverPath, serviceName)) {
            std::wcout << L"驱动安装或启动失败！\n";
            return 1;
        }
        std::wcout << L"驱动安装成功！\n";
    }
    else {
        std::wcout << L"驱动已安装并运行，无需重复安装。\n";
    }

    // 获取cs2进程PID
    DWORD pid = GetProcessIdByName(L"cs2.exe");
    while (pid) {
        Memory memory(L"cs2.exe");
        uintptr_t client = memory.GetModuleAddress(L"client.dll");
        if (!memory.IsValid() || !client) { _getch(); return 0; }
        pid = GetProcessIdByName(L"cs2.exe");
        while (true) {
            uintptr_t dwLocalPlayerPawn = memory.Read<uintptr_t>(client + Offsets.dwLocalPlayerPawn);
            if (!dwLocalPlayerPawn) continue;
            uint32_t localTeam = memory.Read<uint32_t>(dwLocalPlayerPawn + Offsets.m_iTeamNum);
            if (!localTeam) continue;

            uintptr_t dwEntityList = memory.Read<uintptr_t>(client + Offsets.dwEntityList);
            if (!dwEntityList) continue;

            for (int i = 0; i < 65; i++) {
                uintptr_t dwEntityPtr = memory.Read<uintptr_t>(dwEntityList + (8 * (i & 0x7FFF) >> 9) + 16);
                if (!dwEntityPtr) continue;
                uintptr_t dwControllerPtr = memory.Read<uintptr_t>(dwEntityPtr + 120 * (i & 0x1FF));
                if (!dwControllerPtr) continue;
                uintptr_t m_hPlayerPawn = memory.Read<uintptr_t>(dwControllerPtr + Offsets.m_hPlayerPawn);
                if (!m_hPlayerPawn) continue;
                uintptr_t dwListEntityPtr = memory.Read<uintptr_t>(dwEntityList + 0x8 * ((m_hPlayerPawn & 0x7FFF) >> 9) + 16);
                if (!dwListEntityPtr) continue;
                uintptr_t dwEntityPawn = memory.Read<uintptr_t>(dwListEntityPtr + 120 * (m_hPlayerPawn & 0x1FF));
                if (!dwEntityPawn || dwEntityPawn == dwLocalPlayerPawn) continue;
                uint32_t m_iHealth = memory.Read<uint32_t>(dwEntityPawn + Offsets.m_iHealth);
                if (!(m_iHealth > 0)) continue;
                uint32_t m_iTeamNum = memory.Read<uint32_t>(dwEntityPawn + Offsets.m_iTeamNum);

                // 如果是队友就跳过
                if (m_iTeamNum == localTeam) continue;

                uintptr_t glowColor = ((uintptr_t)(0) << 24) | ((uintptr_t)(0) << 16) | ((uintptr_t)(0) << 8) | ((uintptr_t)(0));
                uint32_t glowType = 1;
                uint32_t glowEnabled = 1;

                // 根据敌人队伍设置发光颜色
                if (m_iTeamNum == 2) { glowColor = ((uintptr_t)(255) << 24) | ((uintptr_t)(0) << 16) | ((uintptr_t)(0) << 8) | ((uintptr_t)(255)); }
                if (m_iTeamNum == 3) { glowColor = ((uintptr_t)(255) << 24) | ((uintptr_t)(255) << 16) | ((uintptr_t)(0) << 8) | ((uintptr_t)(0)); }

                memory.Write<uint32_t>(dwEntityPawn + Offsets.m_Glow + Offsets.m_iGlowType, glowType);
                memory.Write<uintptr_t>(dwEntityPawn + Offsets.m_Glow + Offsets.m_glowColorOverride, glowColor);
                memory.Write<uint32_t>(dwEntityPawn + Offsets.m_Glow + Offsets.m_bGlowing, glowEnabled);
            }
            Sleep(1);
        }
        return 0;
    }
    return 0;
}
