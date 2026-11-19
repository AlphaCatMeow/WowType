#include "updater.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <vector>

#include "../common/utf.h"

namespace wowtype::win {

namespace {

std::wstring widen(const std::string &s) {
    const std::u16string u = utf8To16(s);
    return std::wstring(u.begin(), u.end());
}

/// 一个 WinHTTP 的请求，析构时关掉。
struct Request {
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    ~Request() {
        if (request) {
            WinHttpCloseHandle(request);
        }
        if (connection) {
            WinHttpCloseHandle(connection);
        }
        if (session) {
            WinHttpCloseHandle(session);
        }
    }

    /// 打开、发出去、等到响应头；返回 HTTP 状态码，失败是 0。
    DWORD open(const std::wstring &url, std::string *error) {
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof parts;
        wchar_t host[256] = {};
        wchar_t path[2048] = {};
        parts.lpszHostName = host;
        parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
        wchar_t extra[1024] = {};
        parts.lpszExtraInfo = extra;
        parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
            *error = "网址不对";
            return 0;
        }
        // 用户代理只写程序名（GitHub 的接口要有），不带别的信息
        session = WinHttpOpen(L"WowType", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            *error = "联网失败";
            return 0;
        }
        WinHttpSetTimeouts(session, 10000, 15000, 30000, 60000);
        connection = WinHttpConnect(session, host, parts.nPort, 0);
        const std::wstring object = std::wstring(path) + extra;
        request = connection ? WinHttpOpenRequest(connection, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                  parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                             : nullptr;
        if (!request) {
            *error = "连不上服务器";
            return 0;
        }
        const wchar_t *accept = wcsstr(host, L"api.github.com") ? L"Accept: application/vnd.github+json" : L"Accept: */*";
        if (!WinHttpSendRequest(request, accept, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request, nullptr)) {
            *error = "连不上服务器（看看网络和代理）";
            return 0;
        }
        DWORD status = 0;
        DWORD size = sizeof status;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) {
            *error = "服务器回了 " + std::to_string(status);
        }
        return status;
    }

    unsigned long long length() {
        wchar_t buffer[32] = {};
        DWORD size = sizeof buffer;
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, buffer, &size,
                                 WINHTTP_NO_HEADER_INDEX)) {
            return 0;
        }
        return _wcstoui64(buffer, nullptr, 10);
    }

    /// 读一块；0 是读完了，-1 是出错。
    long read(char *buffer, DWORD capacity) {
        DWORD got = 0;
        if (!WinHttpReadData(request, buffer, capacity, &got)) {
            return -1;
        }
        return static_cast<long>(got);
    }
};

std::wstring statePath(const std::wstring &dataDir) { return dataDir + L"\\update-state"; }

} // namespace

const char *arch() {
#if defined(_M_ARM64)
    return "arm64";
#else
    return "x64";
#endif
}

std::optional<std::string> httpGet(const std::wstring &url, std::string *error) {
    Request r;
    if (r.open(url, error) != 200) {
        return std::nullopt;
    }
    std::string body;
    char buffer[16384];
    while (true) {
        const long n = r.read(buffer, sizeof buffer);
        if (n < 0) {
            *error = "下载到一半断了";
            return std::nullopt;
        }
        if (n == 0) {
            return body;
        }
        body.append(buffer, static_cast<std::size_t>(n));
        if (body.size() > (8u << 20)) {
            *error = "回的东西太大";
            return std::nullopt;
        }
    }
}

bool httpDownload(const std::wstring &url, const std::wstring &path,
                  const std::function<void(unsigned long long, unsigned long long)> &progress, std::string *error) {
    Request r;
    if (r.open(url, error) != 200) {
        return false;
    }
    const unsigned long long total = r.length();
    const std::wstring part = path + L".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) {
            *error = "写不了下载的文件";
            return false;
        }
        std::vector<char> buffer(1 << 16);
        unsigned long long done = 0;
        while (true) {
            const long n = r.read(buffer.data(), static_cast<DWORD>(buffer.size()));
            if (n < 0) {
                *error = "下载到一半断了，再试一次";
                return false;
            }
            if (n == 0) {
                break;
            }
            out.write(buffer.data(), n);
            done += static_cast<unsigned long long>(n);
            if (progress) {
                progress(done, total);
            }
        }
        if (!out) {
            *error = "写不了下载的文件";
            return false;
        }
    }
    if (!MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        *error = "写不了下载的文件";
        return false;
    }
    return true;
}

update::State readState(const std::wstring &dataDir) {
    std::ifstream in(statePath(dataDir), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return update::State::parse(text);
}

void writeState(const std::wstring &dataDir, const update::State &state) {
    CreateDirectoryW(dataDir.c_str(), nullptr);
    const std::wstring tmp = statePath(dataDir) + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << state.text();
    }
    MoveFileExW(tmp.c_str(), statePath(dataDir).c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::optional<update::Release> fetchLatest(const std::wstring &dataDir, bool beta, std::string *error) {
    std::optional<update::Release> release;
    if (beta) {
        const auto body = httpGet(widen(update::kListApi), error);
        if (!body) {
            return std::nullopt;
        }
        const auto list = update::parseReleaseList(*body);
        if (const auto i = update::pickNewest(list)) {
            release = list[*i];
        }
    } else if (const auto body = httpGet(widen(update::kLatestApi), error)) {
        release = update::parseRelease(*body);
    } else {
        return std::nullopt;
    }
    if (!release || !update::version(release->tag)) {
        *error = "发布页上的版本号看不懂";
        return std::nullopt;
    }
    writeState(dataDir, update::State{static_cast<int64_t>(std::time(nullptr)), release->tag});
    return release;
}

std::string sha256File(const std::wstring &path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string out;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return out;
    }
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> buffer(1 << 20);
        bool ok = static_cast<bool>(in);
        while (ok && in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto n = static_cast<ULONG>(in.gcount());
            if (n > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), n, 0) != 0) {
                ok = false;
            }
        }
        unsigned char digest[32] = {};
        if (ok && BCryptFinishHash(hash, digest, sizeof digest, 0) == 0) {
            for (const unsigned char b : digest) {
                char hex[3];
                snprintf(hex, sizeof hex, "%02x", b);
                out += hex;
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return out;
}

} // namespace wowtype::win
