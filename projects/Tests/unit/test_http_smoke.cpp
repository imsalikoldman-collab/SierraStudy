/**
 * @brief Дымовой тест HTTP-запроса к Gexbot API (или другому URL) для проверки, что мы получаем ответ.
 * @note Тест запускается только если установлена переменная окружения `GEXBOT_TEST_URL`.
 * @warning Требуется сетевое подключение; при отсутствии переменной окружения тест пропускается.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), size);
  if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
  return wide;
}

}  // namespace

TEST(HttpSmokeTest, FetchesUrlWhenEnvProvided) {
  const char* url_env = std::getenv("GEXBOT_TEST_URL");
  if (url_env == nullptr || std::string(url_env).empty()) {
    GTEST_SKIP() << "GEXBOT_TEST_URL is not set; skipping network smoke test.";
  }

  const std::wstring url_wide = Utf8ToWide(url_env);
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  uc.dwSchemeLength = -1;
  uc.dwHostNameLength = -1;
  uc.dwUrlPathLength = -1;
  uc.dwExtraInfoLength = -1;

  ASSERT_TRUE(WinHttpCrackUrl(url_wide.c_str(), 0, 0, &uc));

  const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
  const std::wstring path_base(uc.lpszUrlPath, uc.dwUrlPathLength);
  const std::wstring extra = (uc.lpszExtraInfo && uc.dwExtraInfoLength != static_cast<DWORD>(-1))
                                 ? std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength)
                                 : std::wstring();
  const std::wstring path = path_base + extra;

  HINTERNET hSession = WinHttpOpen(L"GexbotSmokeTest/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
  ASSERT_NE(hSession, nullptr);

  HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
  ASSERT_NE(hConnect, nullptr);

  HINTERNET hRequest =
      WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
  ASSERT_NE(hRequest, nullptr);

  BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  ASSERT_TRUE(sent);
  ASSERT_TRUE(WinHttpReceiveResponse(hRequest, nullptr));

  DWORD status_code = 0;
  DWORD status_size = sizeof(status_code);
  ASSERT_TRUE(WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                  &status_code, &status_size, WINHTTP_NO_HEADER_INDEX));
  EXPECT_EQ(status_code, 200u);

  std::vector<char> buffer;
  DWORD bytes_read = 0;
  do {
    DWORD avail = 0;
    ASSERT_TRUE(WinHttpQueryDataAvailable(hRequest, &avail));
    if (avail == 0) break;
    std::size_t offset = buffer.size();
    buffer.resize(offset + avail);
    ASSERT_TRUE(WinHttpReadData(hRequest, buffer.data() + offset, avail, &bytes_read));
    buffer.resize(offset + bytes_read);
  } while (bytes_read > 0);

  std::string body(buffer.begin(), buffer.end());
  EXPECT_FALSE(body.empty());

  const std::string prefix = body.substr(0, 50);
  std::cout << "[http-smoke] received " << body.size() << " bytes, prefix='" << prefix << "'" << std::endl;

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
}
