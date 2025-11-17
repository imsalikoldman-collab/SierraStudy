#pragma once

#include <stdint.h>

#ifdef SIERRA_ADVISOR_EXPORTS
#define SIERRA_ADVISOR_API extern "C" __declspec(dllexport)
#else
#define SIERRA_ADVISOR_API extern "C" __declspec(dllimport)
#endif

/**
 * @brief Устанавливает соединение с именованным каналом.
 * @param pipe_name Путь вида \\\\.\\pipe\\MyPipe.
 * @return 1 при успехе, 0 при ошибке (подробности через GetLastError).
 */
SIERRA_ADVISOR_API int __stdcall SierraPipeConnect(const char* pipe_name);

/**
 * @brief Закрывает текущее соединение, если открыто.
 */
SIERRA_ADVISOR_API void __stdcall SierraPipeClose();

/**
 * @brief Отправляет сотруки bytes в pipe.
 * @param data Указатель на буфер.
 * @param size Размер буфера.
 * @return Количество записанных байт или -1 при ошибке.
 */
SIERRA_ADVISOR_API int __stdcall SierraPipeWrite(const uint8_t* data, int size);

/**
 * @brief Читает данные из pipe.
 * @param buffer Буфер для приёма.
 * @param size Размер буфера.
 * @param timeout_ms Таймаут ожидания (мс).
 * @return Количество прочитанных байт или -1 при ошибке/таймауте.
 */
SIERRA_ADVISOR_API int __stdcall SierraPipeRead(uint8_t* buffer, int size, int timeout_ms);
