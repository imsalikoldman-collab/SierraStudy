#include "..\include\sierra\bridge\api.hpp"

#include <windows.h>

namespace {

HANDLE g_pipe = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_lock;
bool g_lock_initialized = false;

void EnsureLock() {
  if (!g_lock_initialized) {
    InitializeCriticalSection(&g_lock);
    g_lock_initialized = true;
  }
}

void ClosePipeInternal() {
  if (g_pipe != INVALID_HANDLE_VALUE) {
    CloseHandle(g_pipe);
    g_pipe = INVALID_HANDLE_VALUE;
  }
}

}  // namespace

SIERRA_ADVISOR_API int __stdcall SierraPipeConnect(const char* pipe_name) {
  EnsureLock();
  EnterCriticalSection(&g_lock);
  ClosePipeInternal();
  g_pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                       OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  if (g_pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) {
    // Ждём максимум 3 секунды.
    if (!WaitNamedPipeA(pipe_name, 3000)) {
      LeaveCriticalSection(&g_lock);
      return 0;
    }
    g_pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                         OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  }
  LeaveCriticalSection(&g_lock);
  return g_pipe != INVALID_HANDLE_VALUE ? 1 : 0;
}

SIERRA_ADVISOR_API void __stdcall SierraPipeClose() {
  EnsureLock();
  EnterCriticalSection(&g_lock);
  ClosePipeInternal();
  LeaveCriticalSection(&g_lock);
}

SIERRA_ADVISOR_API int __stdcall SierraPipeWrite(const uint8_t* data, int size) {
  if (!data || size <= 0) {
    return -1;
  }
  EnsureLock();
  EnterCriticalSection(&g_lock);
  if (g_pipe == INVALID_HANDLE_VALUE) {
    LeaveCriticalSection(&g_lock);
    return -1;
  }
  DWORD written = 0;
  const BOOL ok = WriteFile(g_pipe, data, static_cast<DWORD>(size), &written, nullptr);
  LeaveCriticalSection(&g_lock);
  return ok ? static_cast<int>(written) : -1;
}

SIERRA_ADVISOR_API int __stdcall SierraPipeRead(uint8_t* buffer, int size, int timeout_ms) {
  if (!buffer || size <= 0) {
    return -1;
  }
  EnsureLock();
  EnterCriticalSection(&g_lock);
  if (g_pipe == INVALID_HANDLE_VALUE) {
    LeaveCriticalSection(&g_lock);
    return -1;
  }

  OVERLAPPED overlapped{};
  HANDLE event_handle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  overlapped.hEvent = event_handle;
  DWORD read = 0;
  BOOL status = ReadFile(g_pipe, buffer, static_cast<DWORD>(size), &read, &overlapped);
  if (!status && GetLastError() == ERROR_IO_PENDING) {
    const DWORD wait = WaitForSingleObject(event_handle,
                                           timeout_ms > 0 ? timeout_ms : INFINITE);
    if (wait == WAIT_OBJECT_0) {
      status = GetOverlappedResult(g_pipe, &overlapped, &read, FALSE);
    } else {
      CancelIo(g_pipe);
      status = FALSE;
    }
  }
  CloseHandle(event_handle);
  if (!status) {
    LeaveCriticalSection(&g_lock);
    return -1;
  }
  LeaveCriticalSection(&g_lock);
  return static_cast<int>(read);
}
