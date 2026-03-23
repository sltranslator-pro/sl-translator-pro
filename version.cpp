// SL Translator Pro - Proxy version.dll
// Firestorm'un send_chat_from_viewer fonksiyonunu hooklar
// Giden mesajları otomatik çevirir

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>
#include <vector>
#include <atomic>
#include <shlobj.h>
#include "minhook/include/MinHook.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

#include <wincrypt.h>

// ==========================================
// Orijinal DINPUT8.dll fonksiyonları - runtime forwarding
// ==========================================
typedef HRESULT(WINAPI* pDirectInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
typedef HRESULT(WINAPI* pDllCanUnloadNow)();
typedef HRESULT(WINAPI* pDllGetClassObject)(REFCLSID, REFIID, LPVOID*);
typedef HRESULT(WINAPI* pDllRegisterServer)();
typedef HRESULT(WINAPI* pDllUnregisterServer)();
typedef void*(WINAPI* pGetdfDIJoystick)();

static pDirectInput8Create orig_DirectInput8Create = NULL;
static pDllCanUnloadNow orig_DllCanUnloadNow = NULL;
static pDllGetClassObject orig_DllGetClassObject = NULL;
static pDllRegisterServer orig_DllRegisterServer = NULL;
static pDllUnregisterServer orig_DllUnregisterServer = NULL;
static pGetdfDIJoystick orig_GetdfDIJoystick = NULL;

extern "C" {
    __declspec(dllexport) HRESULT WINAPI proxy_DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
        if (orig_DirectInput8Create) return orig_DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
        return E_FAIL;
    }
    __declspec(dllexport) HRESULT WINAPI proxy_DllCanUnloadNow() {
        if (orig_DllCanUnloadNow) return orig_DllCanUnloadNow();
        return S_FALSE;
    }
    __declspec(dllexport) HRESULT WINAPI proxy_DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
        if (orig_DllGetClassObject) return orig_DllGetClassObject(rclsid, riid, ppv);
        return E_FAIL;
    }
    __declspec(dllexport) HRESULT WINAPI proxy_DllRegisterServer() {
        if (orig_DllRegisterServer) return orig_DllRegisterServer();
        return E_FAIL;
    }
    __declspec(dllexport) HRESULT WINAPI proxy_DllUnregisterServer() {
        if (orig_DllUnregisterServer) return orig_DllUnregisterServer();
        return E_FAIL;
    }
    __declspec(dllexport) void* WINAPI proxy_GetdfDIJoystick() {
        if (orig_GetdfDIJoystick) return orig_GetdfDIJoystick();
        return NULL;
    }
}

// Linker export directives - orijinal isimlerle export et
#pragma comment(linker, "/export:DirectInput8Create=proxy_DirectInput8Create")
#pragma comment(linker, "/export:DllCanUnloadNow=proxy_DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/export:DllGetClassObject=proxy_DllGetClassObject,PRIVATE")
#pragma comment(linker, "/export:DllRegisterServer=proxy_DllRegisterServer,PRIVATE")
#pragma comment(linker, "/export:DllUnregisterServer=proxy_DllUnregisterServer,PRIVATE")
#pragma comment(linker, "/export:GetdfDIJoystick=proxy_GetdfDIJoystick")

// ==========================================
// Globals
// ==========================================
static HMODULE g_hOrigVersion = NULL;
static std::string g_licenseKey;
static std::string g_sourceLang = "tr";
static std::string g_targetLang = "en";
static std::string g_hwid;
static long long g_expires = 0;
static std::atomic<bool> g_active(false);

// Hook targets - we hook BOTH send_chat_from_viewer and really_send_chat_from_viewer
typedef void (*send_chat_fn)(void* utf8_text, int type, int channel);
static send_chat_fn g_origSendChat = NULL;      // really_send_chat (via string ref)
static send_chat_fn g_origSendChat2 = NULL;     // send_chat (via pattern)

// Pattern for send_chat_from_viewer (broad prologue)
static const BYTE SEND_CHAT_PATTERN[] = {
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8D, 0xAC, 0x24
};

// ==========================================
// Config
// ==========================================
std::string GetConfigPath() {
    char path[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path);
    std::string dir = std::string(path) + "\\SLTranslator";
    CreateDirectoryA(dir.c_str(), NULL);
    return dir;
}

std::string JsonGet(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && json[pos] == ' ') pos++;
    if (json[pos] == '"') {
        size_t end = json.find('"', pos + 1);
        return json.substr(pos + 1, end - pos - 1);
    }
    size_t end = json.find_first_of(",}", pos);
    return json.substr(pos, end - pos);
}

// Debug log (disabled by default, enable with /tr debug)
static std::atomic<bool> g_debugLog(false);

void LogMsg(const char* msg) {
    if (!g_debugLog.load()) return;
    std::string path = GetConfigPath() + "\\debug.log";
    std::ofstream f(path, std::ios::app);
    f << msg << std::endl;
}

bool LoadConfig() {
    std::ifstream f(GetConfigPath() + "\\license.json");
    if (!f) return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    g_licenseKey = JsonGet(content, "key");
    g_sourceLang = JsonGet(content, "source_lang");
    g_targetLang = JsonGet(content, "target_lang");
    if (g_sourceLang.empty()) g_sourceLang = "tr";
    if (g_targetLang.empty()) g_targetLang = "en";
    return !g_licenseKey.empty();
}

void SaveConfig() {
    CreateDirectoryA(GetConfigPath().c_str(), NULL);
    std::ofstream f(GetConfigPath() + "\\license.json");
    f << "{\"key\":\"" << g_licenseKey << "\",\"source_lang\":\"" << g_sourceLang << "\",\"target_lang\":\"" << g_targetLang << "\"}";
}

// ==========================================
// HWID
// ==========================================
std::string GetHWID() {
    // wmic csproduct get uuid → SHA256 → first 32 hex chars
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    CreatePipe(&hRead, &hWrite, &sa, 0);
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    char cmd[] = "wmic csproduct get uuid";
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead); CloseHandle(hWrite);
        return "unknown";
    }
    CloseHandle(hWrite);
    WaitForSingleObject(pi.hProcess, 5000);

    char buf[512] = {};
    DWORD bytesRead = 0;
    ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, NULL);
    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Parse UUID from output (skip header line)
    std::string output(buf);
    std::string uuid;
    size_t pos = output.find('\n');
    if (pos != std::string::npos) {
        std::string line = output.substr(pos + 1);
        // Trim
        size_t start = line.find_first_not_of(" \r\n\t");
        size_t end = line.find_last_not_of(" \r\n\t");
        if (start != std::string::npos && end != std::string::npos)
            uuid = line.substr(start, end - start + 1);
    }
    if (uuid.empty()) uuid = "unknown";

    // Simple hash (not crypto-grade, but matches Tauri's approach concept)
    // Using Windows CryptoAPI for SHA256
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return uuid;

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return uuid;
    }

    CryptHashData(hHash, (BYTE*)uuid.c_str(), (DWORD)uuid.length(), 0);

    BYTE hash[32];
    DWORD hashLen = 32;
    CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    // First 16 bytes → 32 hex chars
    char hex[65];
    for (int i = 0; i < 16; i++)
        sprintf_s(hex + i * 2, 3, "%02x", hash[i]);
    hex[32] = 0;

    return std::string(hex);
}

// ==========================================
// License verify (server API)
// ==========================================
// Returns: 0=fail, 1=active, -1=expired
int VerifyLicense(const std::string& key, const std::string& hwid) {
    HINTERNET hSession = WinHttpOpen(L"SLT/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return 0;
    HINTERNET hConnect = WinHttpConnect(hSession, L"api.sltranslate.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return 0; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/verify", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return 0; }

    DWORD timeout = 10000;
    WinHttpSetTimeouts(hRequest, timeout, timeout, timeout, timeout);

    std::string body = "{\"license_key\":\"" + key + "\",\"hwid\":\"" + hwid + "\"}";

    WinHttpSendRequest(hRequest, L"Content-Type: application/json\r\n", -1,
        (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
    WinHttpReceiveResponse(hRequest, NULL);

    std::string result;
    DWORD sz = 0;
    do {
        WinHttpQueryDataAvailable(hRequest, &sz);
        if (sz == 0) break;
        std::vector<char> buf(sz + 1);
        WinHttpReadData(hRequest, buf.data(), sz, &sz);
        buf[sz] = 0;
        result += buf.data();
    } while (sz > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    LogMsg(("Verify response: " + result).c_str());

    // Parse response
    std::string status = JsonGet(result, "status");
    if (status != "active") return 0;

    // Parse expires
    std::string expiresStr = JsonGet(result, "expires");
    if (!expiresStr.empty()) {
        g_expires = std::stoll(expiresStr);
    }

    // Parse languages array: "languages":["tr","en"]
    size_t langPos = result.find("\"languages\"");
    if (langPos != std::string::npos) {
        size_t arrStart = result.find('[', langPos);
        size_t arrEnd = result.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::string arr = result.substr(arrStart + 1, arrEnd - arrStart - 1);
            // Extract first two quoted strings
            size_t q1 = arr.find('"');
            if (q1 != std::string::npos) {
                size_t q2 = arr.find('"', q1 + 1);
                if (q2 != std::string::npos)
                    g_sourceLang = arr.substr(q1 + 1, q2 - q1 - 1);
            }
            size_t q3 = arr.find('"', arr.find(','));
            if (q3 != std::string::npos) {
                size_t q4 = arr.find('"', q3 + 1);
                if (q4 != std::string::npos)
                    g_targetLang = arr.substr(q3 + 1, q4 - q3 - 1);
            }
        }
    }

    // Check if expired
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    long long now = ((long long)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10000 - 11644473600000LL;
    if (g_expires > 0 && now > g_expires) return -1;

    return 1;
}

// ==========================================
// HTTP translate
// ==========================================
std::string HttpTranslate(const std::string& text) {
    HINTERNET hSession = WinHttpOpen(L"SLT/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return "";
    HINTERNET hConnect = WinHttpConnect(hSession, L"api.sltranslate.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/translate", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

    DWORD timeout = 10000;
    WinHttpSetTimeouts(hRequest, timeout, timeout, timeout, timeout);

    std::string escaped;
    for (char c : text) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else escaped += c;
    }
    std::string body = "{\"license_key\":\"" + g_licenseKey + "\",\"text\":\"" + escaped +
                       "\",\"source\":\"" + g_sourceLang + "\",\"target\":\"" + g_targetLang + "\"}";

    WinHttpSendRequest(hRequest, L"Content-Type: application/json\r\n", -1, (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
    WinHttpReceiveResponse(hRequest, NULL);

    std::string result;
    DWORD sz = 0;
    do {
        WinHttpQueryDataAvailable(hRequest, &sz);
        if (sz == 0) break;
        std::vector<char> buf(sz + 1);
        DWORD read = 0;
        WinHttpReadData(hRequest, buf.data(), sz, &read);
        result.append(buf.data(), read);
    } while (sz > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return JsonGet(result, "translated");
}

// ==========================================
// MSVC std::string layout (x64)
// ==========================================
// MSVC std::string (basic_string) x64 layout:
// offset 0:  union { char buf[16]; char* ptr; } - SSO buffer or heap pointer
// offset 16: size_t _Mysize (string length)
// offset 24: size_t _Myres  (buffer capacity)
// If _Myres >= 16, ptr points to heap allocated buffer
// If _Myres < 16, buf contains the string (SSO)

const char* ReadStdString(void* str) {
    size_t* fields = (size_t*)str;
    size_t mysize = fields[2]; // offset 16
    size_t myres = fields[3];  // offset 24
    if (myres >= 16) {
        // Heap allocated
        return *(const char**)str;
    } else {
        // SSO - inline buffer
        return (const char*)str;
    }
}

size_t GetStdStringSize(void* str) {
    size_t* fields = (size_t*)str;
    return fields[2]; // _Mysize at offset 16
}

void WriteStdString(void* str, const std::string& newText) {
    // Tehlikeli ama çalışır: mevcut string'in içeriğini değiştir
    // Sadece SSO veya heap buffer'ı yeterli büyükse
    size_t* fields = (size_t*)str;
    size_t myres = fields[3]; // capacity

    if (newText.size() <= myres) {
        // Mevcut buffer'a sığıyor
        char* dest;
        if (myres >= 16) {
            dest = *(char**)str; // heap pointer
        } else {
            dest = (char*)str; // SSO buffer
        }
        memcpy(dest, newText.c_str(), newText.size() + 1);
        fields[2] = newText.size(); // _Mysize güncelle
    }
    // Sığmıyorsa çevirme - orijinal metni gönder
}

// ==========================================
// Hook handler
// ==========================================
void DoTranslation(void* utf8_text) {
    char dbg[512];
    const char* text = ReadStdString(utf8_text);
    size_t len = GetStdStringSize(utf8_text);
    sprintf_s(dbg, "text='%.100s' len=%zu", text ? text : "(null)", len);
    LogMsg(dbg);

    if (text && len > 0 && text[0] != '/') {
        std::string original(text, len);
        std::string translated = HttpTranslate(original);
        if (!translated.empty()) {
            LogMsg(("Translated: " + translated).c_str());
        }
        if (!translated.empty() && translated != original) {
            WriteStdString(utf8_text, translated);
            LogMsg("Text replaced in string buffer");
        }
    }
}

// SEH wrapper - no C++ objects allowed here
static int SafeTranslate(void* utf8_text) {
    __try {
        DoTranslation(utf8_text);
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

void Hooked_really_send_chat(void* utf8_text, int type, int channel) {
    LogMsg("=== HOOK1 (really_send) TRIGGERED ===");
    char dbg[512];
    sprintf_s(dbg, "type=%d channel=%d active=%d utf8_text=%p", type, channel, g_active.load() ? 1 : 0, utf8_text);
    LogMsg(dbg);

    if (g_active.load() && channel == 0 && utf8_text) {
        if (SafeTranslate(utf8_text) != 0) {
            LogMsg("EXCEPTION in hook1!");
        }
    }

    g_origSendChat(utf8_text, type, channel);
    LogMsg("=== HOOK1 DONE ===");
}

void Hooked_send_chat_wrapper(void* utf8_text, int type, int channel) {
    LogMsg("=== HOOK2 (send_chat) TRIGGERED ===");
    char dbg[512];
    sprintf_s(dbg, "type=%d channel=%d active=%d utf8_text=%p", type, channel, g_active.load() ? 1 : 0, utf8_text);
    LogMsg(dbg);

    if (g_active.load() && channel == 0 && utf8_text) {
        if (SafeTranslate(utf8_text) != 0) {
            LogMsg("EXCEPTION in hook2!");
        }
    }

    g_origSendChat2(utf8_text, type, channel);
    LogMsg("=== HOOK2 DONE ===");
}

// ==========================================
// String reference scanner
// ==========================================
struct SectionInfo {
    BYTE* start;
    DWORD size;
};

SectionInfo FindSection(BYTE* base, const char* name) {
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sec[i].Name, name, strlen(name)) == 0) {
            return { base + sec[i].VirtualAddress, sec[i].Misc.VirtualSize };
        }
    }
    return { NULL, 0 };
}

// Find function by string reference (with _PREHASH indirection support)
// SL Viewer uses _PREHASH_XXX global pointers to message name strings.
// So: .rdata has "ChatFromViewer" string, .rdata has a qword pointer to it,
// and code does MOV reg, [rip + offset_to_pointer]
void* FindFunctionByStringRef(const char* searchStr) {
    HMODULE hModule = GetModuleHandle(NULL);
    if (!hModule) return NULL;
    BYTE* base = (BYTE*)hModule;

    SectionInfo rdata = FindSection(base, ".rdata");
    SectionInfo data = FindSection(base, ".data");
    SectionInfo text = FindSection(base, ".text");
    if (!text.start) { LogMsg("ERROR: .text not found"); return NULL; }

    char dbg[256];
    sprintf_s(dbg, ".text: %p+%u .rdata: %p+%u .data: %p+%u",
        text.start, text.size,
        rdata.start ? rdata.start : 0, rdata.size,
        data.start ? data.start : 0, data.size);
    LogMsg(dbg);

    // Step 1: Find ALL occurrences of the string in all sections
    size_t strLen = strlen(searchStr);
    std::vector<BYTE*> strAddrs;

    auto scanStr = [&](SectionInfo& sec) {
        if (!sec.start) return;
        for (DWORD i = 0; i + strLen < sec.size; i++) {
            if (memcmp(sec.start + i, searchStr, strLen + 1) == 0) {
                strAddrs.push_back(sec.start + i);
                sprintf_s(dbg, "String \"%s\" copy at %p (offset 0x%llX)", searchStr, sec.start + i, (ULONGLONG)(sec.start + i - base));
                LogMsg(dbg);
            }
        }
    };
    scanStr(rdata);
    scanStr(data);

    if (strAddrs.empty()) {
        LogMsg("ERROR: String not found in any section");
        return NULL;
    }
    sprintf_s(dbg, "Found %d string copies", (int)strAddrs.size());
    LogMsg(dbg);

    // Step 2: For each string, try direct LEA/MOV references from .text
    BYTE* firstRef = NULL;
    for (BYTE* strAddr : strAddrs) {
        if (firstRef) break;
        for (DWORD i = 0; i + 7 < text.size; i++) {
            BYTE* code = text.start + i;
            // LEA reg, [rip+disp32]
            if ((code[0] == 0x48 || code[0] == 0x4C) && code[1] == 0x8D) {
                if ((code[2] & 0xC7) == 0x05) {
                    int32_t disp = *(int32_t*)(code + 3);
                    if (code + 7 + disp == strAddr) {
                        sprintf_s(dbg, "Direct LEA ref at %p -> string %p", code, strAddr);
                        LogMsg(dbg);
                        firstRef = code;
                        break;
                    }
                }
            }
            // MOV reg, [rip+disp32] (might load pointer directly)
            if ((code[0] == 0x48 || code[0] == 0x4C) && code[1] == 0x8B) {
                if ((code[2] & 0xC7) == 0x05) {
                    int32_t disp = *(int32_t*)(code + 3);
                    if (code + 7 + disp == strAddr) {
                        sprintf_s(dbg, "Direct MOV ref at %p -> string %p", code, strAddr);
                        LogMsg(dbg);
                        firstRef = code;
                        break;
                    }
                }
            }
        }
    }

    // Step 3: If no direct ref, try _PREHASH pointer indirection
    if (!firstRef) {
        LogMsg("No direct code ref, trying _PREHASH pointer indirection...");

        for (BYTE* strAddr : strAddrs) {
            if (firstRef) break;
            ULONGLONG strAddrVal = (ULONGLONG)strAddr;

            // Scan ALL data sections for qword pointers (unaligned scan)
            auto scanForPtr = [&](SectionInfo& sec) -> BYTE* {
                if (!sec.start) return nullptr;
                for (DWORD i = 0; i + 8 <= sec.size; i++) {
                    ULONGLONG val = *(ULONGLONG*)(sec.start + i);
                    if (val == strAddrVal) {
                        return sec.start + i;
                    }
                }
                return nullptr;
            };

            BYTE* ptrAddr = scanForPtr(rdata);
            if (!ptrAddr) ptrAddr = scanForPtr(data);

            if (ptrAddr) {
                sprintf_s(dbg, "_PREHASH ptr at %p -> string %p", ptrAddr, strAddr);
                LogMsg(dbg);

                // Find code referencing this pointer
                for (DWORD i = 0; i + 7 < text.size; i++) {
                    BYTE* code = text.start + i;
                    // MOV reg, [rip+disp32]
                    if ((code[0] == 0x48 || code[0] == 0x4C) && code[1] == 0x8B) {
                        if ((code[2] & 0xC7) == 0x05) {
                            int32_t disp = *(int32_t*)(code + 3);
                            if (code + 7 + disp == ptrAddr) {
                                sprintf_s(dbg, "MOV ref to _PREHASH at %p", code);
                                LogMsg(dbg);
                                firstRef = code;
                                break;
                            }
                        }
                    }
                    // LEA reg, [rip+disp32]
                    if ((code[0] == 0x48 || code[0] == 0x4C) && code[1] == 0x8D) {
                        if ((code[2] & 0xC7) == 0x05) {
                            int32_t disp = *(int32_t*)(code + 3);
                            if (code + 7 + disp == ptrAddr) {
                                sprintf_s(dbg, "LEA ref to _PREHASH at %p", code);
                                LogMsg(dbg);
                                firstRef = code;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (!firstRef) {
        LogMsg("ERROR: No code references found (direct or indirect)");
        return NULL;
    }

    // Step 4: Walk backwards to find function start
    // Look for CC padding between functions
    for (int back = 1; back < 512; back++) {
        BYTE* scan = firstRef - back;
        if (scan < text.start) break;
        if (*scan == 0xCC) {
            BYTE* fnStart = scan;
            while (*fnStart == 0xCC) fnStart++;
            sprintf_s(dbg, "Function start (CC pad): %p (offset 0x%llX)", fnStart, (ULONGLONG)(fnStart - base));
            LogMsg(dbg);
            return fnStart;
        }
    }

    // Fallback: look for prologue patterns
    for (int back = 1; back < 256; back++) {
        BYTE* scan = firstRef - back;
        if (scan < text.start) break;
        if (scan[0] == 0x48 && scan[1] == 0x83 && scan[2] == 0xEC) {
            sprintf_s(dbg, "Function start (sub rsp): %p (offset 0x%llX)", scan, (ULONGLONG)(scan - base));
            LogMsg(dbg);
            return scan;
        }
        if (scan[0] == 0x48 && scan[1] == 0x89 && back > 1 && *(scan - 1) == 0xCC) {
            sprintf_s(dbg, "Function start (mov+CC): %p (offset 0x%llX)", scan, (ULONGLONG)(scan - base));
            LogMsg(dbg);
            return scan;
        }
    }

    LogMsg("ERROR: Could not determine function start");
    return NULL;
}

// ==========================================
// Setup dialog (dark theme matching Tauri UI)
// ==========================================
static HWND g_hSetupWnd = NULL;
static HWND g_hKeyEdit = NULL;
static HWND g_hLangSetupCombo = NULL;
static HWND g_hMsgLabel = NULL;
static HWND g_hActivateBtn = NULL;
static HBRUSH g_hDarkBrush = NULL;
static HBRUSH g_hEditBrush = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontTitle = NULL;
static HFONT g_hFontSub = NULL;
static HFONT g_hFontSmall = NULL;
static bool g_setupResult = false;

static const char* g_setupLangNames[] = {
    "-- Select your language --",
    "Turkish", "English", "Portuguese", "Japanese", "Korean", "German",
    "French", "Spanish", "Russian", "Chinese", "Arabic", "Italian",
    "Dutch", "Polish", "Ukrainian", "Danish", "Hungarian"
};
static const char* g_setupLangCodes[] = {
    "", "tr", "en", "pt", "ja", "ko", "de",
    "fr", "es", "ru", "zh", "ar", "it",
    "nl", "pl", "uk", "da", "hu"
};
static const int g_setupNumLangs = 18;

LRESULT CALLBACK SetupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hFontNormal = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        g_hFontTitle = CreateFontA(30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        g_hFontSub = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        g_hFontSmall = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        g_hDarkBrush = CreateSolidBrush(RGB(26, 26, 46));  // #1a1a2e
        g_hEditBrush = CreateSolidBrush(RGB(18, 18, 42));   // #12122a

        HWND h;

        // Title: "SL Translator Pro"
        h = CreateWindowA("STATIC", "SL Translator Pro",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 35, 480, 36, hwnd, (HMENU)300, NULL, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        // Subtitle
        h = CreateWindowA("STATIC", "AI-Powered Chat Translation",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 72, 480, 20, hwnd, NULL, NULL, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)g_hFontSub, TRUE);

        // License Key input
        g_hKeyEdit = CreateWindowExA(0, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_CENTER,
            60, 115, 360, 36, hwnd, (HMENU)100, NULL, NULL);
        SendMessage(g_hKeyEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        SendMessageA(g_hKeyEdit, EM_SETCUEBANNER, 0, (LPARAM)L"License Key");

        // Language select label
        h = CreateWindowA("STATIC", "Select your language",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 170, 480, 20, hwnd, (HMENU)201, NULL, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)g_hFontSub, TRUE);

        // Language combo
        g_hLangSetupCombo = CreateWindowA("COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            60, 196, 360, 400, hwnd, (HMENU)101, NULL, NULL);
        SendMessage(g_hLangSetupCombo, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        for (int i = 0; i < g_setupNumLangs; i++)
            SendMessageA(g_hLangSetupCombo, CB_ADDSTRING, 0, (LPARAM)g_setupLangNames[i]);
        SendMessage(g_hLangSetupCombo, CB_SETCURSEL, 0, 0);

        // Activate button
        g_hActivateBtn = CreateWindowA("BUTTON", "Activate",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW,
            140, 260, 200, 44, hwnd, (HMENU)IDOK, NULL, NULL);
        SendMessage(g_hActivateBtn, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // Status message
        g_hMsgLabel = CreateWindowA("STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 318, 480, 22, hwnd, (HMENU)202, NULL, NULL);
        SendMessage(g_hMsgLabel, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // Version + credit
        h = CreateWindowA("STATIC", "v1.0.0",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 355, 480, 16, hwnd, NULL, NULL, NULL);
        SendMessage(h, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == IDOK) {
            // Gradient-style activate button (#00d4ff → #0077aa)
            HBRUSH hBtn;
            if (dis->itemState & ODS_SELECTED)
                hBtn = CreateSolidBrush(RGB(0, 100, 140));
            else
                hBtn = CreateSolidBrush(RGB(0, 170, 210));
            FillRect(dis->hDC, &dis->rcItem, hBtn);
            DeleteObject(hBtn);

            // Round corners simulation with border
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 212, 255));
            SelectObject(dis->hDC, hPen);
            SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                dis->rcItem.right, dis->rcItem.bottom, 16, 16);
            DeleteObject(hPen);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(0, 0, 0));
            SelectObject(dis->hDC, g_hFontNormal);
            DrawTextA(dis->hDC, "Activate", -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDOK) {
            char key[256] = {};
            GetWindowTextA(g_hKeyEdit, key, 256);
            if (strlen(key) == 0) {
                SetWindowTextA(g_hMsgLabel, "Please enter your license key");
                SetFocus(g_hKeyEdit);
                return 0;
            }

            int sel = (int)SendMessage(g_hLangSetupCombo, CB_GETCURSEL, 0, 0);
            if (sel <= 0) {
                SetWindowTextA(g_hMsgLabel, "Please select your language!");
                SetFocus(g_hLangSetupCombo);
                return 0;
            }

            SetWindowTextA(g_hMsgLabel, "Verifying...");
            EnableWindow(g_hActivateBtn, FALSE);
            UpdateWindow(hwnd);

            g_licenseKey = key;
            g_sourceLang = g_setupLangCodes[sel];
            SaveConfig();
            g_setupResult = true;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND ctrl = (HWND)lParam;
        int id = GetDlgCtrlID(ctrl);
        if (id == 300) {
            // Title - white
            SetTextColor(hdc, RGB(255, 255, 255));
        } else if (id == 201) {
            // Language label - cyan accent
            SetTextColor(hdc, RGB(0, 212, 255));
        } else if (id == 202) {
            // Message - check content for color
            char txt[64];
            GetWindowTextA(ctrl, txt, 64);
            if (strstr(txt, "!") || strstr(txt, "Please") || strstr(txt, "Invalid") || strstr(txt, "expired"))
                SetTextColor(hdc, RGB(244, 67, 54));  // Red for errors
            else if (strstr(txt, "Activated") || strstr(txt, "OK"))
                SetTextColor(hdc, RGB(76, 175, 80));   // Green for success
            else
                SetTextColor(hdc, RGB(0, 212, 255));   // Cyan for info
        } else {
            SetTextColor(hdc, RGB(160, 160, 160));
        }
        SetBkColor(hdc, RGB(26, 26, 46));
        return (LRESULT)g_hDarkBrush;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkColor(hdc, RGB(18, 18, 42));
        return (LRESULT)g_hEditBrush;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_hDarkBrush);
        return 1;
    }
    case WM_DESTROY:
        g_hSetupWnd = NULL;
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

bool ShowSetupDialog(HMODULE hModule) {
    g_setupResult = false;

    static bool regSetup = false;
    if (!regSetup) {
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SetupWndProc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = "SLTranslatorSetup";
        RegisterClassExA(&wc);
        regSetup = true;
    }

    int w = 480, h = 420;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    g_hSetupWnd = CreateWindowExA(
        WS_EX_TOPMOST,
        "SLTranslatorSetup", "SL Translator Pro",
        WS_POPUP | WS_BORDER,
        x, y, w, h,
        NULL, NULL, GetModuleHandleA(NULL), NULL);

    ShowWindow(g_hSetupWnd, SW_SHOW);
    UpdateWindow(g_hSetupWnd);
    SetForegroundWindow(g_hSetupWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsWindow(g_hSetupWnd)) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return g_setupResult;
}

// ==========================================
// Pattern scanner (find ALL matches)
// ==========================================
std::vector<void*> FindAllPatterns(const BYTE* pattern, int patternSize) {
    std::vector<void*> results;
    HMODULE hModule = GetModuleHandle(NULL);
    if (!hModule) return results;
    BYTE* base = (BYTE*)hModule;
    SectionInfo text = FindSection(base, ".text");
    if (!text.start) return results;

    for (DWORD j = 0; j + patternSize < text.size; j++) {
        if (memcmp(text.start + j, pattern, patternSize) == 0) {
            results.push_back(text.start + j);
        }
    }
    return results;
}

// Dump first N bytes of memory as hex string
void DumpBytes(const char* label, void* addr, int count) {
    char buf[1024];
    int pos = sprintf_s(buf, "%s %p: ", label, addr);
    BYTE* p = (BYTE*)addr;
    for (int i = 0; i < count && pos < 900; i++) {
        pos += sprintf_s(buf + pos, sizeof(buf) - pos, "%02X ", p[i]);
    }
    LogMsg(buf);
}

// ==========================================
// Winsock sendto hook - intercept ALL outgoing UDP
// ==========================================
typedef int (WSAAPI* pSendTo)(SOCKET s, const char* buf, int len, int flags, const struct sockaddr* to, int tolen);
static pSendTo g_origSendTo = NULL;

// SL ChatFromViewer UDP packet layout:
// [0-5]   Header (6 bytes): flags, sequence, etc.
// [6-9]   Message template ID (4 bytes): FF FF 00 50 = ChatFromViewer
// [10-25] AgentID (16 bytes UUID)
// [26-41] SessionID (16 bytes UUID)
// [42-43] Message length (2 bytes, little-endian, includes null terminator)
// [44...] Message text (null-terminated UTF-8)
// [44+len] Chat type (1 byte)
// [45+len] Channel (4 bytes, little-endian)

static const unsigned char CHAT_MSG_ID[] = { 0xFF, 0xFF, 0x00, 0x50 };
static const unsigned char IM_MSG_ID[]   = { 0xFF, 0xFF, 0x00, 0x01 };

int WSAAPI Hooked_sendto(SOCKET s, const char* buf, int len, int flags, const struct sockaddr* to, int tolen) {
    if (!g_active.load() || !buf || len < 50)
        return g_origSendTo(s, buf, len, flags, to, tolen);

    // === ChatFromViewer: template ID FF FF 00 50 at offset 6 ===
    if (memcmp(buf + 6, CHAT_MSG_ID, 4) == 0) {
        unsigned short msgLen = *(unsigned short*)(buf + 42);
        if (msgLen > 1 && 44 + msgLen <= len) {
            const char* chatText = buf + 44;
            int textLen = msgLen - 1;

            int channel = 0;
            if (44 + msgLen + 1 + 4 <= len)
                channel = *(int*)(buf + 44 + msgLen + 1);

            char dbg[512];
            sprintf_s(dbg, "CHAT DETECTED: len=%d channel=%d text='%.100s'", textLen, channel, chatText);
            LogMsg(dbg);

            // Handle /tr commands (language change, on/off, status)
            if (channel == 0 && textLen >= 3 && strncmp(chatText, "/tr", 3) == 0) {
                std::string cmd(chatText, textLen);
                if (cmd == "/tr") {
                    // Show current status
                    char status[256];
                    sprintf_s(status, "SL Translator: %s | Source: %s | Target: %s",
                        g_active.load() ? "ON" : "OFF", g_sourceLang.c_str(), g_targetLang.c_str());
                    LogMsg(status);
                    MessageBoxA(NULL, status, "SL Translator", MB_OK | MB_TOPMOST);
                } else if (cmd == "/tr off") {
                    g_active.store(false);
                    LogMsg("Translation DISABLED by command");
                    MessageBoxA(NULL, "Translation OFF", "SL Translator", MB_OK | MB_TOPMOST);
                } else if (cmd == "/tr on") {
                    g_active.store(true);
                    LogMsg("Translation ENABLED by command");
                    MessageBoxA(NULL, "Translation ON", "SL Translator", MB_OK | MB_TOPMOST);
                } else if (cmd == "/tr debug") {
                    bool newDbg = !g_debugLog.load();
                    g_debugLog.store(newDbg);
                    if (newDbg) {
                        LogMsg("=== Debug logging ENABLED ===");
                        MessageBoxA(NULL, "Debug logging ON\nLog: %APPDATA%\\SLTranslator\\debug.log", "SL Translator", MB_OK | MB_TOPMOST);
                    } else {
                        MessageBoxA(NULL, "Debug logging OFF", "SL Translator", MB_OK | MB_TOPMOST);
                    }
                } else if (cmd.length() >= 6 && cmd.substr(0, 4) == "/tr ") {
                    std::string newLang = cmd.substr(4);
                    // Trim whitespace
                    while (!newLang.empty() && newLang.back() == ' ') newLang.pop_back();
                    if (newLang.length() == 2) {
                        g_targetLang = newLang;
                        SaveConfig();
                        char msg[128];
                        sprintf_s(msg, "Target language changed to: %s", newLang.c_str());
                        LogMsg(msg);
                        MessageBoxA(NULL, msg, "SL Translator", MB_OK | MB_TOPMOST);
                    }
                }
                // Don't send /tr commands to server - swallow the packet
                return len;
            }

            if (channel == 0 && textLen > 0 && chatText[0] != '/') {
                std::string original(chatText, textLen);
                std::string translated = HttpTranslate(original);

                if (!translated.empty() && translated != original) {
                    LogMsg(("Translated: " + translated).c_str());
                    int newMsgLen = (int)translated.length() + 1;
                    int newPktLen = 44 + newMsgLen + (len - 44 - msgLen);
                    std::vector<char> newPkt(newPktLen);
                    memcpy(newPkt.data(), buf, 42);
                    *(unsigned short*)(newPkt.data() + 42) = (unsigned short)newMsgLen;
                    memcpy(newPkt.data() + 44, translated.c_str(), translated.length());
                    newPkt[44 + translated.length()] = 0;
                    int remainStart = 44 + msgLen;
                    int remainLen = len - remainStart;
                    if (remainLen > 0)
                        memcpy(newPkt.data() + 44 + newMsgLen, buf + remainStart, remainLen);
                    return g_origSendTo(s, newPkt.data(), newPktLen, flags, to, tolen);
                }
            }
        }
    }

    // === ImprovedInstantMessage: template ID FF FF 00 01 at offset 6, sub-byte FE at offset 10 ===
    if (memcmp(buf + 6, IM_MSG_ID, 4) == 0 && len > 62 && (unsigned char)buf[10] == 0xFE) {
        // Byte 62 = IM dialog type: 0x22 = message, 0x21 = typing indicator
        unsigned char imType = (unsigned char)buf[62];

        // Dump IM packet for debugging
        {
            char dbg[2048];
            int pos = sprintf_s(dbg, "IM PKT len=%d type=0x%02X: ", len, imType);
            int dumpLen = (len > 120) ? 120 : len;
            for (int i = 0; i < dumpLen && pos < 1900; i++)
                pos += sprintf_s(dbg + pos, sizeof(dbg) - pos, "%02X ", (unsigned char)buf[i]);
            LogMsg(dbg);
            pos = sprintf_s(dbg, "IM ASCII: ");
            for (int i = 0; i < dumpLen && pos < 1900; i++) {
                unsigned char c = (unsigned char)buf[i];
                dbg[pos++] = (c >= 32 && c < 127) ? (char)c : '.';
            }
            dbg[pos] = 0;
            LogMsg(dbg);
        }

        if (imType == 0x22) {
            // Packet structure after fixed header:
            //   [80] 00 04 + name_len_byte + avatar_name\0 + 00 01 + msg_len_byte + 00 01 + message\0 + trailing
            // Strategy: scan forward from offset 82 to find avatar name, then message after it

            // Find the avatar name (null-terminated string starting around offset 82+)
            int nameStart = -1;
            int nameEnd = -1;
            for (int i = 82; i < len - 1; i++) {
                unsigned char c = (unsigned char)buf[i];
                if (c >= 0x20 && c < 0x7F) {
                    if (nameStart < 0) nameStart = i;
                } else if (c == 0x00 && nameStart > 0) {
                    nameEnd = i;
                    break;
                } else {
                    nameStart = -1;
                }
            }

            if (nameEnd > 0 && nameEnd < len - 6) {
                // SL Variable 2 field: 2-byte LE length prefix
                // Zero-coded: 00 bytes become 00 01
                // After name null (00 01 in zero-coded):
                //   nameEnd+2: LEN_LO
                //   nameEnd+3: LEN_HI (if 0x00 → zero-coded as 00 01, if != 0x00 → raw byte)

                bool zeroCoded = ((unsigned char)buf[0] & 0x80) != 0;

                int msgLenPos, msgStart;
                int msgLen; // decoded message length including null terminator

                if (zeroCoded) {
                    msgLenPos = nameEnd + 2;
                    unsigned char lenLo = (unsigned char)buf[msgLenPos];
                    unsigned char lenHiByte = (unsigned char)buf[msgLenPos + 1];

                    if (lenHiByte == 0x00) {
                        // LEN_HI is zero, zero-coded as 00 01
                        msgLen = lenLo;
                        msgStart = nameEnd + 5; // skip: 01 + LEN_LO + 00 + 01
                    } else {
                        // LEN_HI is non-zero, NOT zero-coded
                        msgLen = lenLo | (lenHiByte << 8);
                        msgStart = nameEnd + 4; // skip: 01 + LEN_LO + LEN_HI
                    }
                } else {
                    msgLenPos = nameEnd + 1;
                    msgStart = nameEnd + 3;
                    msgLen = *(unsigned short*)(buf + msgLenPos);
                }

                int textLen = msgLen - 1; // exclude null terminator

                if (textLen > 0 && msgStart + textLen < len) {
                    std::string original(buf + msgStart, textLen);

                    char dbg[512];
                    sprintf_s(dbg, "IM DETECTED: name='%.50s' msgLen=%d text='%.100s'",
                        buf + nameStart, msgLen, original.c_str());
                    LogMsg(dbg);

                    if (original[0] != '/') {
                        std::string translated = HttpTranslate(original);

                        if (!translated.empty() && translated != original) {
                            LogMsg(("IM Translated: " + translated).c_str());

                            int nullSize = zeroCoded ? 2 : 1; // 00 01 vs just 00

                            // Original raw message region: msgStart + textLen + nullSize
                            int origTrailStart = msgStart + textLen + nullSize;
                            int trailLen = len - origTrailStart;
                            if (trailLen < 0) trailLen = 0;

                            int newTextLen = (int)translated.length();
                            int newMsgLen = newTextLen + 1; // +1 for null

                            // Calculate new length field size for zero-coded packets
                            // Original: if msgLen < 256 → LEN_LO + 00 01 (3 bytes), else LEN_LO + LEN_HI (2 bytes)
                            // New: if newMsgLen < 256 → LEN_LO + 00 01 (3 bytes), else LEN_LO + LEN_HI (2 bytes)
                            int origLenFieldSize = 0, newLenFieldSize = 0;
                            if (zeroCoded) {
                                origLenFieldSize = (msgLen < 256) ? 3 : 2; // LEN_LO + (00 01 or LEN_HI)
                                newLenFieldSize = (newMsgLen < 256) ? 3 : 2;
                            } else {
                                origLenFieldSize = newLenFieldSize = 2; // always 2-byte LE
                            }

                            // Rebuild: header_before_len + new_len_field + new_text + null + trailing
                            int headerLen = msgLenPos; // everything before length field
                            int newMsgStart = headerLen + newLenFieldSize;
                            int newPktLen = newMsgStart + newTextLen + nullSize + trailLen;
                            std::vector<char> newPkt(newPktLen);

                            // Copy header up to length field
                            memcpy(newPkt.data(), buf, headerLen);

                            // Write new length field
                            if (zeroCoded) {
                                newPkt[headerLen] = (char)(newMsgLen & 0xFF);
                                if (newMsgLen < 256) {
                                    // LEN_HI = 0, zero-coded as 00 01
                                    newPkt[headerLen + 1] = 0;
                                    newPkt[headerLen + 2] = 1;
                                } else {
                                    // LEN_HI != 0, raw byte
                                    newPkt[headerLen + 1] = (char)((newMsgLen >> 8) & 0xFF);
                                }
                            } else {
                                *(unsigned short*)(newPkt.data() + headerLen) = (unsigned short)newMsgLen;
                            }

                            // Write translated text
                            memcpy(newPkt.data() + newMsgStart, translated.c_str(), newTextLen);

                            // Write null terminator (zero-coded: 00 01, normal: 00)
                            int nullPos = newMsgStart + newTextLen;
                            newPkt[nullPos] = 0;
                            if (zeroCoded) newPkt[nullPos + 1] = 1;

                            // Copy trailing bytes (BinaryBucket etc.)
                            if (trailLen > 0)
                                memcpy(newPkt.data() + nullPos + nullSize, buf + origTrailStart, trailLen);

                            char dbg2[256];
                            sprintf_s(dbg2, "IM SEND: orig=%d new=%d msgLen=%d->%d", len, newPktLen, msgLen, newMsgLen);
                            LogMsg(dbg2);

                            return g_origSendTo(s, newPkt.data(), newPktLen, flags, to, tolen);
                        }
                    }
                }
            }
        }
    }

    return g_origSendTo(s, buf, len, flags, to, tolen);
}

// ==========================================
// Firestorm settings.xml updater
// ==========================================
std::string FindFirestormSettings() {
    char appdata[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata);
    std::string base(appdata);

    // Try known folder names first
    const char* names[] = { "Firestorm_x64", "Firestorm" };
    for (auto name : names) {
        std::string path = base + "\\" + name + "\\user_settings\\settings.xml";
        std::ifstream f(path);
        if (f.good()) return path;
    }

    // Search for any Firestorm* folder
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((base + "\\Firestorm*").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::string path = base + "\\" + fd.cFileName + "\\user_settings\\settings.xml";
                std::ifstream f(path);
                if (f.good()) { FindClose(hFind); return path; }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    return "";
}

std::string SetXmlValue(const std::string& content, const std::string& key, const std::string& type, const std::string& value) {
    std::string keyTag = "<key>" + key + "</key>";
    size_t pos = content.find(keyTag);
    if (pos == std::string::npos) return content;

    // Find <key>Value</key> after the main key
    size_t vpos = content.find("<key>Value</key>", pos);
    if (vpos == std::string::npos) return content;

    std::string openTag = (type == "Boolean") ? "<boolean>" : "<string>";
    std::string closeTag = (type == "Boolean") ? "</boolean>" : "</string>";

    size_t tagStart = content.find(openTag, vpos);
    if (tagStart == std::string::npos) return content;
    size_t valStart = tagStart + openTag.length();

    size_t valEnd = content.find(closeTag, valStart);
    if (valEnd == std::string::npos) return content;

    return content.substr(0, valStart) + value + content.substr(valEnd);
}

std::string SetAzureKeyId(const std::string& content, const std::string& licenseKey) {
    size_t azurePos = content.find("<key>AzureTranslateAPIKey</key>");
    if (azurePos == std::string::npos) return content;

    size_t idPos = content.find("<key>id</key>", azurePos);
    if (idPos == std::string::npos) return content;

    size_t sStart = content.find("<string>", idPos);
    if (sStart == std::string::npos) return content;
    sStart += 8;

    size_t sEnd = content.find("</string>", sStart);
    if (sEnd == std::string::npos) return content;

    return content.substr(0, sStart) + licenseKey + content.substr(sEnd);
}

void UpdateFirestormSettings() {
    std::string path = FindFirestormSettings();
    if (path.empty()) {
        LogMsg("Firestorm settings.xml not found");
        return;
    }

    std::ifstream fin(path);
    if (!fin) return;
    std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
    fin.close();

    content = SetXmlValue(content, "TranslateChat", "Boolean", "1");
    content = SetXmlValue(content, "TranslateLanguage", "String", g_sourceLang);
    content = SetXmlValue(content, "TranslationService", "String", "azure");
    content = SetAzureKeyId(content, g_licenseKey);

    std::ofstream fout(path);
    if (!fout) {
        LogMsg(("ERROR: Cannot write to " + path).c_str());
        return;
    }
    fout << content;
    fout.close();

    // Verify write
    std::ifstream verify(path);
    std::string check((std::istreambuf_iterator<char>(verify)), std::istreambuf_iterator<char>());
    if (check.find(g_licenseKey) != std::string::npos)
        LogMsg(("Firestorm settings updated OK: key=" + g_licenseKey).c_str());
    else
        LogMsg("WARNING: Firestorm settings write may have failed - key not found after write");
}

// ==========================================
// Settings Popup (Ctrl+Shift+T)
// ==========================================
static HWND g_hSettingsWnd = NULL;
static HWND g_hLangCombo = NULL;
static HWND g_hStatusLabel = NULL;
static HWND g_hToggleBtn = NULL;

static const char* g_langNames[] = {
    "English", "German", "French", "Spanish", "Italian", "Portuguese",
    "Russian", "Japanese", "Chinese", "Korean", "Arabic", "Dutch",
    "Polish", "Swedish", "Turkish", "Czech", "Romanian"
};
static const char* g_langCodes[] = {
    "en", "de", "fr", "es", "it", "pt",
    "ru", "ja", "zh", "ko", "ar", "nl",
    "pl", "sv", "tr", "cs", "ro"
};
static const int g_numLangs = 17;

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Set dark background
        HFONT hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        HFONT hTitleFont = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

        // Title
        HWND hTitle = CreateWindowA("STATIC", "SL Translator Pro", WS_CHILD | WS_VISIBLE | SS_CENTER,
            10, 12, 330, 28, hwnd, NULL, NULL, NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

        // License
        char licText[128];
        sprintf_s(licText, "License: %s", g_licenseKey.c_str());
        HWND hLic = CreateWindowA("STATIC", licText, WS_CHILD | WS_VISIBLE,
            20, 55, 310, 20, hwnd, NULL, NULL, NULL);
        SendMessage(hLic, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Source language label
        HWND hSrcLabel = CreateWindowA("STATIC", "Your Language:", WS_CHILD | WS_VISIBLE,
            20, 90, 120, 20, hwnd, NULL, NULL, NULL);
        SendMessage(hSrcLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Source language display
        const char* srcName = "Turkish";
        for (int i = 0; i < g_numLangs; i++) {
            if (g_sourceLang == g_langCodes[i]) { srcName = g_langNames[i]; break; }
        }
        HWND hSrcVal = CreateWindowA("STATIC", srcName, WS_CHILD | WS_VISIBLE,
            150, 90, 180, 20, hwnd, NULL, NULL, NULL);
        SendMessage(hSrcVal, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Target language label
        HWND hTgtLabel = CreateWindowA("STATIC", "Translate To:", WS_CHILD | WS_VISIBLE,
            20, 122, 120, 20, hwnd, NULL, NULL, NULL);
        SendMessage(hTgtLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Target language combo
        g_hLangCombo = CreateWindowA("COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            150, 118, 180, 300, hwnd, (HMENU)101, NULL, NULL);
        SendMessage(g_hLangCombo, WM_SETFONT, (WPARAM)hFont, TRUE);

        int selIdx = 0;
        for (int i = 0; i < g_numLangs; i++) {
            SendMessageA(g_hLangCombo, CB_ADDSTRING, 0, (LPARAM)g_langNames[i]);
            if (g_targetLang == g_langCodes[i]) selIdx = i;
        }
        SendMessage(g_hLangCombo, CB_SETCURSEL, selIdx, 0);

        // ON/OFF toggle button
        g_hToggleBtn = CreateWindowA("BUTTON",
            g_active.load() ? "Translation: ON" : "Translation: OFF",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            20, 160, 310, 32, hwnd, (HMENU)102, NULL, NULL);
        SendMessage(g_hToggleBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Status
        char statusText[128];
        sprintf_s(statusText, "Status: %s | Chat: /tr <lang>",
            g_active.load() ? "Active" : "Inactive");
        g_hStatusLabel = CreateWindowA("STATIC", statusText, WS_CHILD | WS_VISIBLE | SS_CENTER,
            10, 205, 330, 20, hwnd, NULL, NULL, NULL);
        SendMessage(g_hStatusLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Save & Close button
        HWND hSaveBtn = CreateWindowA("BUTTON", "Save && Close",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            100, 238, 150, 35, hwnd, (HMENU)103, NULL, NULL);
        SendMessage(hSaveBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int notif = HIWORD(wParam);

        if (id == 102) { // Toggle ON/OFF
            bool newState = !g_active.load();
            g_active.store(newState);
            SetWindowTextA(g_hToggleBtn, newState ? "Translation: ON" : "Translation: OFF");
            char statusText[128];
            sprintf_s(statusText, "Status: %s | Chat: /tr <lang>", newState ? "Active" : "Inactive");
            SetWindowTextA(g_hStatusLabel, statusText);
        }
        if (id == 103) { // Save & Close
            int sel = (int)SendMessage(g_hLangCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < g_numLangs) {
                g_targetLang = g_langCodes[sel];
            }
            SaveConfig();
            LogMsg(("Settings saved: target=" + g_targetLang).c_str());
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(220, 220, 220));
        SetBkColor(hdc, RGB(45, 45, 48));
        static HBRUSH hBrush = CreateSolidBrush(RGB(45, 45, 48));
        return (LRESULT)hBrush;
    }
    case WM_CTLCOLORBTN: {
        static HBRUSH hBtnBrush = CreateSolidBrush(RGB(60, 60, 65));
        return (LRESULT)hBtnBrush;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH hBrush = CreateSolidBrush(RGB(45, 45, 48));
        FillRect(hdc, &rc, hBrush);
        DeleteObject(hBrush);
        // Draw red accent line at top
        RECT topLine = { 0, 0, rc.right, 3 };
        HBRUSH hRed = CreateSolidBrush(RGB(200, 50, 50));
        FillRect(hdc, &topLine, hRed);
        DeleteObject(hRed);
        return 1;
    }
    case WM_DESTROY:
        g_hSettingsWnd = NULL;
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void ShowSettingsPopup() {
    if (g_hSettingsWnd) {
        // Already open, bring to front
        SetForegroundWindow(g_hSettingsWnd);
        return;
    }

    // Register window class (once)
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = "SLTranslatorSettings";
        RegisterClassExA(&wc);
        registered = true;
    }

    // Create popup window (centered on screen)
    int w = 360, h = 310;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    g_hSettingsWnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "SLTranslatorSettings", "SL Translator Pro",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h,
        NULL, NULL, GetModuleHandleA(NULL), NULL);

    ShowWindow(g_hSettingsWnd, SW_SHOW);
    UpdateWindow(g_hSettingsWnd);
    SetForegroundWindow(g_hSettingsWnd);
}

// ==========================================
// Init thread
// ==========================================
void InitThread(HMODULE hModule) {
    Sleep(3000);
    LogMsg("=== SL Translator DLL Init ===");

    // Get HWID
    g_hwid = GetHWID();
    LogMsg(("HWID: " + g_hwid).c_str());

    // Load saved config or show setup dialog
    if (!LoadConfig()) {
        LogMsg("No config, showing setup dialog");
        if (!ShowSetupDialog(hModule)) { LogMsg("Setup cancelled"); return; }
    }
    LogMsg(("Config loaded: key=" + g_licenseKey + " lang=" + g_sourceLang).c_str());

    // Verify license with server
    LogMsg("Verifying license...");
    int verifyResult = VerifyLicense(g_licenseKey, g_hwid);

    if (verifyResult == 0) {
        LogMsg("License verification FAILED");
        MessageBoxA(NULL, "License key is invalid or could not be verified.\nPlease check your key and try again.",
            "SL Translator - License Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
        // Show setup dialog again
        if (!ShowSetupDialog(hModule)) { LogMsg("Setup cancelled"); return; }
        // Re-verify with new key
        verifyResult = VerifyLicense(g_licenseKey, g_hwid);
        if (verifyResult == 0) {
            LogMsg("License verification FAILED again");
            MessageBoxA(NULL, "License verification failed. Translation will not work.",
                "SL Translator", MB_OK | MB_ICONERROR | MB_TOPMOST);
            return;
        }
    }

    if (verifyResult == -1) {
        LogMsg("License EXPIRED");
        MessageBoxA(NULL, "Your license has expired.\nPlease renew to continue using SL Translator.",
            "SL Translator - License Expired", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        return;
    }

    // License valid - save updated config (server may have changed languages)
    SaveConfig();
    LogMsg(("License verified OK. Source: " + g_sourceLang + " Target: " + g_targetLang).c_str());

    // Update Firestorm settings.xml (incoming translation)
    UpdateFirestormSettings();

    char buf[512];

    MH_STATUS st = MH_Initialize();
    sprintf_s(buf, "MH_Initialize: %d", st);
    LogMsg(buf);
    if (st != MH_OK) return;

    // Hook Winsock sendto - guaranteed to be called for ALL outgoing network
    HMODULE hWs2 = GetModuleHandleA("ws2_32.dll");
    if (!hWs2) hWs2 = LoadLibraryA("ws2_32.dll");

    if (hWs2) {
        void* pSend = GetProcAddress(hWs2, "sendto");
        sprintf_s(buf, "ws2_32!sendto at %p", pSend);
        LogMsg(buf);

        if (pSend) {
            st = MH_CreateHook(pSend, (void*)&Hooked_sendto, (void**)&g_origSendTo);
            sprintf_s(buf, "MH_CreateHook(sendto): %d", st);
            LogMsg(buf);
            if (st == MH_OK) {
                st = MH_EnableHook(pSend);
                sprintf_s(buf, "MH_EnableHook(sendto): %d", st);
                LogMsg(buf);
            }
        }
    } else {
        LogMsg("ERROR: ws2_32.dll not found");
    }

    g_active.store(true);
    LogMsg("=== Hook active, monitoring network ===");

    // Start hotkey listener thread (Ctrl+Shift+T)
    std::thread([]() {
        // Register hotkey on this thread's message queue
        if (!RegisterHotKey(NULL, 1, MOD_CONTROL | MOD_SHIFT, 'T')) {
            LogMsg("WARNING: Failed to register hotkey Ctrl+Shift+T");
            return;
        }
        LogMsg("Hotkey Ctrl+Shift+T registered");

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            if (msg.message == WM_HOTKEY && msg.wParam == 1) {
                ShowSettingsPopup();
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }).detach();
}

// ==========================================
// DllMain
// ==========================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Orijinal dinput8.dll yükle
        char sysDir[MAX_PATH];
        GetSystemDirectoryA(sysDir, MAX_PATH);
        strcat_s(sysDir, "\\dinput8.dll");
        g_hOrigVersion = LoadLibraryA(sysDir);
        if (g_hOrigVersion) {
            orig_DirectInput8Create = (pDirectInput8Create)GetProcAddress(g_hOrigVersion, "DirectInput8Create");
            orig_DllCanUnloadNow = (pDllCanUnloadNow)GetProcAddress(g_hOrigVersion, "DllCanUnloadNow");
            orig_DllGetClassObject = (pDllGetClassObject)GetProcAddress(g_hOrigVersion, "DllGetClassObject");
            orig_DllRegisterServer = (pDllRegisterServer)GetProcAddress(g_hOrigVersion, "DllRegisterServer");
            orig_DllUnregisterServer = (pDllUnregisterServer)GetProcAddress(g_hOrigVersion, "DllUnregisterServer");
            orig_GetdfDIJoystick = (pGetdfDIJoystick)GetProcAddress(g_hOrigVersion, "GetdfDIJoystick");
        }

        std::thread(InitThread, hModule).detach();
    }
    else if (reason == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        if (g_hOrigVersion) FreeLibrary(g_hOrigVersion);
    }
    return TRUE;
}
