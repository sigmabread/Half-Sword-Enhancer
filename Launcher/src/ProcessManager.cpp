#include "../include/ProcessManager.h"

#include <TlHelp32.h>
#include <thread>
#include <filesystem>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace hse {

    std::expected<DWORD, ProcessError>
    ProcessManager::LocateOrStartGame() noexcept {

        if (auto pid = FindGameProcess()) {
            return *pid;
        }

        if (auto start = StartGameViaSteam(); !start) {
            return std::unexpected(start.error());
        }

        const auto timeout =
            std::chrono::steady_clock::now() + MAX_GAME_WAIT_TIME;

        while (std::chrono::steady_clock::now() < timeout) {

            if (auto pid = FindGameProcess()) {
                return *pid;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return std::unexpected(ProcessError::GameNotFound);
    }

    std::expected<DWORD, ProcessError>
    ProcessManager::FindGameProcess() const noexcept {

        HANDLE snapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPPROCESS,
            0
        );

        if (snapshot == INVALID_HANDLE_VALUE) {
            return std::unexpected(ProcessError::GameNotFound);
        }

        ProcessHandle snapshotGuard(snapshot);

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        if (!Process32FirstW(snapshot, &entry)) {
            return std::unexpected(ProcessError::GameNotFound);
        }

        do {

            if (_wcsicmp(entry.szExeFile, GAME_EXE_NAME) == 0) {
                return entry.th32ProcessID;
            }

        } while (Process32NextW(snapshot, &entry));

        return std::unexpected(ProcessError::GameNotFound);
    }

    std::expected<void, ProcessError>
    ProcessManager::StartGameViaSteam() const noexcept {

        HINSTANCE result = ShellExecuteW(
            nullptr,
            L"open",
            STEAM_GAME_URL,
            nullptr,
            nullptr,
            SW_SHOWDEFAULT
        );

        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            return std::unexpected(ProcessError::GameStartFailed);
        }

        return {};
    }

    std::expected<ProcessHandle, ProcessError>
    ProcessManager::OpenGameProcess(DWORD pid) const noexcept {

        HANDLE handle = OpenProcess(
            PROCESS_CREATE_THREAD |
            PROCESS_VM_OPERATION |
            PROCESS_VM_WRITE |
            PROCESS_VM_READ |
            PROCESS_QUERY_INFORMATION,
            FALSE,
            pid
        );

        if (!handle || handle == INVALID_HANDLE_VALUE) {
            return std::unexpected(ProcessError::ProcessOpenFailed);
        }

        return ProcessHandle(handle);
    }

    std::expected<void, ProcessError>
    ProcessManager::InjectDLL(
        DWORD processId,
        const std::wstring& dllPath
    ) noexcept {

        if (dllPath.empty() || !fs::exists(dllPath)) {
            return std::unexpected(ProcessError::InvalidDllPath);
        }

        auto process = OpenGameProcess(processId);

        if (!process) {
            return std::unexpected(process.error());
        }

        const SIZE_T allocSize =
            (dllPath.size() + 1) * sizeof(wchar_t);

        LPVOID remoteMemory = VirtualAllocEx(
            process->get(),
            nullptr,
            allocSize,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE
        );

        if (!remoteMemory) {
            return std::unexpected(ProcessError::MemoryAllocationFailed);
        }

        struct RemoteMemoryGuard {
            HANDLE process{};
            LPVOID memory{};

            ~RemoteMemoryGuard() {
                if (process && memory) {
                    VirtualFreeEx(
                        process,
                        memory,
                        0,
                        MEM_RELEASE
                    );
                }
            }
        };

        RemoteMemoryGuard guard{
            process->get(),
            remoteMemory
        };

        SIZE_T written{};

        if (!WriteProcessMemory(
            process->get(),
            remoteMemory,
            dllPath.c_str(),
            allocSize,
            &written
        ) || written != allocSize) {

            return std::unexpected(ProcessError::DllPathWriteFailed);
        }

        HMODULE kernel32 =
            GetModuleHandleW(L"kernel32.dll");

        if (!kernel32) {
            return std::unexpected(ProcessError::ThreadCreationFailed);
        }

        auto loadLibrary =
            reinterpret_cast<LPTHREAD_START_ROUTINE>(
                GetProcAddress(kernel32, "LoadLibraryW")
            );

        if (!loadLibrary) {
            return std::unexpected(ProcessError::ThreadCreationFailed);
        }

        HANDLE thread = CreateRemoteThread(
            process->get(),
            nullptr,
            0,
            loadLibrary,
            remoteMemory,
            0,
            nullptr
        );

        if (!thread) {
            return std::unexpected(ProcessError::ThreadCreationFailed);
        }

        ProcessHandle threadGuard(thread);

        DWORD waitResult = WaitForSingleObject(
            thread,
            static_cast<DWORD>(
                INJECTION_TIMEOUT.count() * 1000
            )
        );

        if (waitResult != WAIT_OBJECT_0) {
            return std::unexpected(ProcessError::InjectionTimeout);
        }

        DWORD remoteModule{};

        if (!GetExitCodeThread(thread, &remoteModule)) {
            return std::unexpected(ProcessError::ThreadCreationFailed);
        }

        if (remoteModule == 0) {
            return std::unexpected(ProcessError::DllLoadFailed);
        }

        return {};
    }

}
