#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <string>
#include <iomanip>
#include <sstream>
#include <vector>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <tlhelp32.h>
#include <CommCtrl.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "comctl32.lib")
//ȫ�ֱ���
HWND g_hEditKami = NULL;
HWND g_hBtnLogin = NULL;
HWND g_hBtnUnbind = NULL;
const wchar_t* g_appId = L"10003";

// String conversion helpers
std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

// ��ȡC�����к���Ϊ�豸��
std::string GetDiskSerial() {
    DWORD serial = 0;
    if (!GetVolumeInformationA("C:\\", NULL, 0, &serial, NULL, NULL, NULL, 0)) {
        return "default_machine_code";
    }

    std::stringstream ss;
    ss << std::hex << std::setw(8) << std::setfill('0') << serial;
    return ss.str();
}
// ����HTTP GET����
std::string HttpGetRequest(const std::string& url) {
    std::string response;
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;

    // �� std::string תΪ std::wstring
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, NULL, 0);
    std::wstring wurl(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], wlen);

    // ����URL
    URL_COMPONENTS urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = -1;
    urlComp.dwHostNameLength = -1;
    urlComp.dwUrlPathLength = -1;
    urlComp.dwExtraInfoLength = -1;

    if (!WinHttpCrackUrl(wurl.c_str(), wurl.length(), 0, &urlComp)) {
        return "URL����ʧ��";
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);

    // �����Ự
    hSession = WinHttpOpen(L"WinHTTP Client",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return "�Ự����ʧ��";
    }

    // ���ó�ʱ
    int timeout = 8000; // 8�볬ʱ
    WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout);

    // ��������
    hConnect = WinHttpConnect(hSession,
        host.c_str(),
        urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "���ӷ�����ʧ��";
    }

    // ��������
    hRequest = WinHttpOpenRequest(hConnect, L"GET",
        path.c_str(),
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        urlComp.nScheme == INTERNET_SCHEME_HTTPS ?
        WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "��������ʧ��";
    }

    // ����SSL����֤�����
    DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
        SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));

    // ��������
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "��������ʧ��";
    }

    // ������Ӧ
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "������Ӧʧ��";
    }

    // ��ȡ��Ӧ����
    DWORD size = 0;
    DWORD downloaded = 0;
    std::vector<char> buffer;

    do {
        // ������ݿ��ô�С
        if (!WinHttpQueryDataAvailable(hRequest, &size)) {
            response = "��ѯ����ʧ��";
            break;
        }

        if (size == 0) break;

        // ��ȡ����
        buffer.resize(size + 1);
        if (!WinHttpReadData(hRequest, &buffer[0], size, &downloaded)) {
            response = "��ȡ����ʧ��";
            break;
        }

        if (downloaded == 0) break;

        buffer[downloaded] = '\0';
        response.append(&buffer[0]);
    } while (size > 0);

    // ������Դ
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return response;
}

//���� vector<char>������� HTTP ״̬��
std::vector<char> HttpGetRequestBinary(const std::string& url, DWORD* pStatusCode = nullptr) {
    std::vector<char> response;
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;

    // �� std::string תΪ std::wstring
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, NULL, 0);
    std::wstring wurl(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], wlen);

    // ����URL
    URL_COMPONENTS urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = -1;
    urlComp.dwHostNameLength = -1;
    urlComp.dwUrlPathLength = -1;
    urlComp.dwExtraInfoLength = -1;

    if (!WinHttpCrackUrl(wurl.c_str(), wurl.length(), 0, &urlComp)) {
        return {}; // ���ؿ�������ʾʧ��
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);

    // �����Ự
    hSession = WinHttpOpen(L"WinHTTP Client",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return {};
    }

    // ���ó�ʱ
    int timeout = 8000; // 8�볬ʱ
    WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout);

    // ��������
    hConnect = WinHttpConnect(hSession,
        host.c_str(),
        urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return {};
    }

    // ��������
    hRequest = WinHttpOpenRequest(hConnect, L"GET",
        path.c_str(),
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        urlComp.nScheme == INTERNET_SCHEME_HTTPS ?
        WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    // ����SSL����֤�����
    DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
        SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));

    // ���� User-Agent
    LPCWSTR userAgent = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)";
    WinHttpAddRequestHeaders(hRequest, userAgent, -1L, WINHTTP_ADDREQ_FLAG_ADD);

    // ��������
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    // ������Ӧ
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    // ��ȡ HTTP ״̬��
    DWORD statusCode = 0, size = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &statusCode, &size, NULL);
    if (pStatusCode) *pStatusCode = statusCode;

    // ��ȡ��Ӧ����
    DWORD dwSize = 0, dwDownloaded = 0;
    do {
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        std::vector<char> buffer(dwSize);
        if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
        response.insert(response.end(), buffer.begin(), buffer.begin() + dwDownloaded);
    } while (dwSize > 0);

    // ������Դ
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return response;
}

// �����ļ������浽ָ��·��
bool DownloadFile(const std::string& url, const std::string& savePath) {
    DWORD statusCode = 0;
    auto data = HttpGetRequestBinary(url, &statusCode);
    if (statusCode != 200 || data.empty()) {
        return false;
    }
    // ����Ƿ�Ϊ HTML
    if (data.size() > 6 && std::string(data.begin(), data.begin() + 6).find("<html") != std::string::npos) {
        return false;
    }
    std::ofstream ofs(savePath, std::ios::binary);
    if (!ofs) return false;
    ofs.write(data.data(), data.size());
    return ofs.good();
}

// ��װ����������������ʽ���Զ�������
bool InstallAndStartDriver(const std::string& driverPath, const std::string& serviceName) {
    SC_HANDLE hSCM = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        return false;
    }
    SC_HANDLE hService = OpenServiceA(hSCM, serviceName.c_str(), SERVICE_ALL_ACCESS);
    if (!hService) {
        hService = CreateServiceA(
            hSCM,
            serviceName.c_str(),
            serviceName.c_str(),
            SERVICE_ALL_ACCESS,
            SERVICE_KERNEL_DRIVER,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            driverPath.c_str(),
            NULL, NULL, NULL, NULL, NULL);
        if (!hService) {
            CloseServiceHandle(hSCM);
            return false;
        }
    }
    // ��������
    bool result = StartServiceA(hService, 0, NULL) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
    if (!result) {
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return false;
    }
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    return result;
}

std::wstring LoginWithCard(const std::wstring& appId, const std::wstring& cardKey) {
    std::string deviceCode = GetDiskSerial();
    std::string appIdUtf8 = WideToUtf8(appId);
    std::string cardKeyUtf8 = WideToUtf8(cardKey);

    // ��������URL
    std::string url = "" + appIdUtf8 +
        "&kami=" + cardKeyUtf8 +
        "&markcode=" + deviceCode;

    // �������󲢻�ȡ��Ӧ
    std::string response = HttpGetRequest(url);
    if (response.find("\"code\":200") != std::string::npos) {
        // ��鲢����SmileDriver.sys
        std::string sysDir = "C:\\Windows\\System32\\";
        std::string sysPath = sysDir + "SmileDriver.sys";

        if (!std::filesystem::exists(sysPath)) {
            if (!DownloadFile("", sysPath)) {
                return L"Driver download failed";
            }
        }

        // ��װ������SmileDriver.sys
        if (std::filesystem::exists(sysPath)) {
            std::string serviceName = "SmileDriver";
            if (!InstallAndStartDriver(sysPath, serviceName)) {
                return L"Driver installation failed!";
            }
        }

        // ����vip�ֶ�
        std::wstring expiryMsg;
        size_t vipPos = response.find("\"vip\":");
        if (vipPos != std::string::npos) {
            size_t start = response.find('"', vipPos + 6);
            size_t end = response.find('"', start + 1);
            if (start != std::string::npos && end != std::string::npos && end > start) {
                std::string vipValue = response.substr(start + 1, end - start - 1);
                try {
                    std::time_t vipTime = std::stoll(vipValue);
                    std::tm tmBuf;
                    std::tm* tmPtr = nullptr;
                    if (localtime_s(&tmBuf, &vipTime) == 0) {
                        tmPtr = &tmBuf;
                        char buf[32] = { 0 };
                        if (tmPtr && std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmPtr)) {
                            expiryMsg = L"��¼�ɹ���\n����ʱ��: " + Utf8ToWide(buf);
                            return expiryMsg;
                        }
                    }
                }
                catch (...) {}
                expiryMsg = L"Login successful!\nExpiry time: " + Utf8ToWide(vipValue);
                return expiryMsg;
            }
        }
        return L"Login successful!";
    }
    return L"Login failed!";
}