#pragma once

#include <string>
#include <expected>
#include <chrono>
#include <cstdint>
#include <Windows.h>

namespace hse {

    enum class ProcessError : std::uint8_t {
        GameNotFound,
        GameStartFailed,
        ProcessOpenFailed,
        MemoryAllocationFailed,
        DllPathWriteFailed,
        ThreadCreationFailed,
        InjectionTimeout,
        InvalidDllPath,
        DllLoadFailed
    };

    class ProcessHandle {
    public:
        explicit ProcessHandle(HANDLE handle = nullptr) noexcept
            : handle_(handle) {
        }

        ~ProcessHandle() noexcept {
            if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(handle_);
            }
        }

        ProcessHandle(const ProcessHandle&) = delete;
        ProcessHandle& operator=(const ProcessHandle&) = delete;

        ProcessHandle(ProcessHandle&& other) noexcept
            : handle_(std::exchange(other.handle_, nullptr)) {
        }

        ProcessHandle& operator=(ProcessHandle&& other) noexcept {
            if (this != &other) {
                if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
                    CloseHandle(handle_);
                }

                handle_ = std::exchange(other.handle_, nullptr);
            }

            return *this;
        }

        [[nodiscard]] HANDLE get() const noexcept {
            return handle_;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return handle_ && handle_ != INVALID_HANDLE_VALUE;
        }

    private:
        HANDLE handle_{};
    };

    class ProcessManager {
    public:
        static ProcessManager& Instance() noexcept {
            static ProcessManager instance;
            return instance;
        }

        [[nodiscard]]
        std::expected<DWORD, ProcessError>
        LocateOrStartGame() noexcept;

        [[nodiscard]]
        std::expected<void, ProcessError>
        InjectDLL(
            DWORD processId,
            const std::wstring& dllPath
        ) noexcept;

    private:
        static constexpr wchar_t GAME_EXE_NAME[] = L"HalfSword-Win64-Shipping.exe";
        static constexpr wchar_t STEAM_GAME_URL[] = L"steam://rungameid/2642680";

        static constexpr auto INJECTION_TIMEOUT =
            std::chrono::seconds(10);

        static constexpr auto MAX_GAME_WAIT_TIME =
            std::chrono::seconds(60);

        ProcessManager() = default;

        [[nodiscard]]
        std::expected<DWORD, ProcessError>
        FindGameProcess() const noexcept;

        [[nodiscard]]
        std::expected<void, ProcessError>
        StartGameViaSteam() const noexcept;

        [[nodiscard]]
        std::expected<ProcessHandle, ProcessError>
        OpenGameProcess(DWORD pid) const noexcept;
    };

}
