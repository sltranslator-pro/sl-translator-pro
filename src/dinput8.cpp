// SL Translator Pro - Proxy dinput8.dll
// Sendto hook ile giden chat/IM mesajlarını çevirir
// Dil seçimi ve lisans yönetimi Firestorm floater'dan yapılır

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <vector>
#include <array>
#include <deque>
#include <set>
#include <unordered_map>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <shlobj.h>
#include "MinHook.h"
#include "sl_packet.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

#include <objbase.h>
#include <wincrypt.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

// ==========================================
// Orijinal DINPUT8.dll - runtime forwarding
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
    __declspec(dllexport) HRESULT WINAPI proxy_DllCanUnloadNow() { if (orig_DllCanUnloadNow) return orig_DllCanUnloadNow(); return S_FALSE; }
    __declspec(dllexport) HRESULT WINAPI proxy_DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) { if (orig_DllGetClassObject) return orig_DllGetClassObject(rclsid, riid, ppv); return E_FAIL; }
    __declspec(dllexport) HRESULT WINAPI proxy_DllRegisterServer() { if (orig_DllRegisterServer) return orig_DllRegisterServer(); return E_FAIL; }
    __declspec(dllexport) HRESULT WINAPI proxy_DllUnregisterServer() { if (orig_DllUnregisterServer) return orig_DllUnregisterServer(); return E_FAIL; }
    __declspec(dllexport) void* WINAPI proxy_GetdfDIJoystick() { if (orig_GetdfDIJoystick) return orig_GetdfDIJoystick(); return NULL; }
}

#pragma comment(linker, "/export:DirectInput8Create=proxy_DirectInput8Create")
#pragma comment(linker, "/export:DllCanUnloadNow=proxy_DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/export:DllGetClassObject=proxy_DllGetClassObject,PRIVATE")
#pragma comment(linker, "/export:DllRegisterServer=proxy_DllRegisterServer,PRIVATE")
#pragma comment(linker, "/export:DllUnregisterServer=proxy_DllUnregisterServer,PRIVATE")
#pragma comment(linker, "/export:GetdfDIJoystick=proxy_GetdfDIJoystick")

// ==========================================
// Version
// ==========================================
#define SLT_VERSION "1.5.1"

// ==========================================
// Globals
// ==========================================
static HMODULE g_hOrigVersion = NULL;
static std::string g_licenseKey;
static std::string g_hwid;
static std::string g_sessionId;
static std::atomic<long long> g_expires(0);
// Three independent gates control translation:
//   g_active       = Firestorm's TranslateChat checkbox (the main fuse)
//   g_userEnabled  = the HUD/chat /tr on|off switch
//   g_licenseValid = the most recent authoritative licence result
// Keeping them separate prevents /tr on from bypassing a disabled checkbox and
// prevents a transient licence heartbeat from permanently overwriting the UI
// setting. Always use IsTranslationEnabled() for the effective state.
static std::atomic<bool> g_active(false);
static std::atomic<bool> g_licenseValid(false);
static std::atomic<bool> g_shutdown(false); // set on DLL_PROCESS_DETACH so long-lived background threads exit promptly
static std::atomic<bool> g_userEnabled(true);
static std::atomic<bool> g_includeOriginal(false); // /tr src on|off — ask server to prefix the outgoing text with the untranslated original (space permitting)
static std::atomic<unsigned long long> g_statusGeneration(0);
static std::string g_targetLang; // outgoing translation target ("Send Messages In"), parsed from settings.xml's AzureTranslateAPIKey.endpoint URL
static std::string g_myLang;      // "My Language" (what I read), from settings.xml's TranslateLanguage. With target_lang this forms the language pair the model translates on.
// A HUD language-pair verification can override Firestorm's in-memory
// translation pair for the rest of the session. This is deliberately separate
// from settings.xml: Firestorm only commits the Translation floater controls
// when its OK button is pressed, while the HUD must take effect immediately.
// `/tr lang auto` clears this override and returns to settings.xml.
static std::atomic<bool> g_runtimeLanguageOverride(false);
static std::atomic<unsigned long long> g_languageOverrideGeneration(0);
static std::atomic<unsigned long long> g_loggedMyLanguageGeneration(0);
static std::atomic<unsigned long long> g_loggedAzureLanguageGeneration(0);
static const wchar_t* API_HOST = L"api.sltranslate.com";
static std::mutex g_configMutex;

static bool IsTranslationEnabled() {
    return g_active.load() && g_userEnabled.load() && g_licenseValid.load();
}

static bool SleepUntilShutdown(DWORD totalMs) {
    const DWORD sliceMs = 200;
    DWORD waited = 0;
    while (waited < totalMs && !g_shutdown.load()) {
        DWORD remaining = totalMs - waited;
        DWORD step = remaining < sliceMs ? remaining : sliceMs;
        Sleep(step);
        waited += step;
    }
    return !g_shutdown.load();
}

// Defined in the debug-log section below. The Firestorm patch is initialized
// before that section in this file, so it needs a forward declaration here.
void LogMsg(const char* msg);
static std::string GetOwnAgentUuidString();

static bool NormalizeTwoLetterLanguageCode(const std::string& raw, std::string& normalized) {
    if (raw.size() != 2) return false;
    unsigned char a = (unsigned char)raw[0];
    unsigned char b = (unsigned char)raw[1];
    if (!std::isalpha(a) || !std::isalpha(b)) return false;
    normalized.clear();
    normalized.push_back((char)std::tolower(a));
    normalized.push_back((char)std::tolower(b));
    return true;
}

// ==========================================
// Firestorm local-chat source filter
// ==========================================
// Firestorm 7.2.4 normally sends every non-system local-chat line through
// LLTranslate. That includes object/HUD/game messages (CHAT_SOURCE_OBJECT),
// even though only real avatar chat is useful to translate. The existing
// WinHTTP interception below cannot reliably stop those requests because this
// Firestorm build uses its embedded libcurl/OpenSSL stack for LLTranslate.
//
// For the local test build we make the smallest possible in-process change:
// change Firestorm's condition from
//
//     chat.mSourceType != CHAT_SOURCE_SYSTEM
//
// to
//
//     chat.mSourceType == CHAT_SOURCE_AGENT
//
// Firestorm then follows its own normal non-translation branch for object and
// system messages, so the original text is still displayed and no synthetic
// callback/network response is needed. The patch is guarded by a unique byte
// signature from Firestorm-Releasex64 7.2.4.80712. Unknown viewer builds fail
// closed: if the exact instruction sequence is absent or ambiguous, nothing is
// modified and Firestorm keeps its stock behaviour.
static BYTE* g_avatarOnlyPatchAddress = NULL;
static BYTE g_avatarOnlyOriginalBytes[3] = {};
static std::atomic<bool> g_avatarOnlyPatchInstalled(false);

static bool GetMainModuleSection(const char* wantedName, BYTE*& sectionBase, size_t& sectionSize) {
    sectionBase = NULL;
    sectionSize = 0;
    if (!wantedName || !*wantedName) return false;

    HMODULE module = GetModuleHandleW(NULL);
    if (!module) return false;

    BYTE* imageBase = reinterpret_cast<BYTE*>(module);
    IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(imageBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    IMAGE_NT_HEADERS64* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(imageBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;

    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        char name[9] = {};
        memcpy(name, section->Name, IMAGE_SIZEOF_SHORT_NAME);
        if (strcmp(name, wantedName) == 0) {
            sectionBase = imageBase + section->VirtualAddress;
            sectionSize = (size_t)section->Misc.VirtualSize;
            return sectionBase != NULL && sectionSize > 0;
        }
    }
    return false;
}

static bool GetMainModuleTextSection(BYTE*& textBase, size_t& textSize) {
    return GetMainModuleSection(".text", textBase, textSize);
}

// ==========================================
// Firestorm live translation-language hooks
// ==========================================
// REA/source analysis of Firestorm 7.2.5.81788 established two narrow hook
// points:
//
//   LLTranslate::getTranslateLanguage()       RVA 0x1163BD0
//   LLAzureTranslationHandler::getTranslateURL() RVA 0x115E8C0
//
// The first reads Global.TranslateLanguage for every incoming translation.
// The second builds the Azure endpoint from AzureTranslateAPIKey.endpoint.
// Hooking both lets a HUD-selected pair take effect immediately without
// pressing OK in Preferences: `my` replaces the query target and `target`
// replaces the two-letter /azure/xx/ endpoint segment.
//
// We never construct or destroy a Firestorm std::string in this DLL. Firestorm
// uses the dynamic MSVC runtime while this proxy is /MT; crossing allocators
// would be unsafe. Instead, each original function constructs its own result,
// then the hook overwrites only same-size bytes in that already-valid object.
// For getTranslateLanguage the returned value is guaranteed by Firestorm's
// source to be at most two bytes (`substr(0,2)`), so updating its size and SSO
// buffer cannot allocate. The URL hook replaces exactly two existing bytes.
// Unknown viewer layouts fail closed: both functions must be found exactly
// once by their code-to-literal relationship before either hook is installed.

using FirestormGetTranslateLanguageFn = void* (__fastcall*)(void* resultString);
using FirestormAzureGetTranslateUrlFn = void* (__fastcall*)(
    void* self,
    void* resultString,
    const void* fromLanguage,
    const void* toLanguage,
    const void* text);

static FirestormGetTranslateLanguageFn g_origFirestormGetTranslateLanguage = NULL;
static FirestormAzureGetTranslateUrlFn g_origFirestormAzureGetTranslateUrl = NULL;
static std::atomic<bool> g_liveLanguageHooksInstalled(false);

static bool GetFirestormStringView(void* stringObject, char*& data, size_t& length, size_t& capacity) {
    data = NULL;
    length = 0;
    capacity = 0;
    if (!stringObject) return false;

    BYTE* objectBytes = reinterpret_cast<BYTE*>(stringObject);
    memcpy(&length, objectBytes + 16, sizeof(length));
    memcpy(&capacity, objectBytes + 24, sizeof(capacity));

    // MSVC's release x64 basic_string layout observed in the target binary:
    // 16-byte SSO union, size at +0x10, capacity at +0x18.
    if (capacity > (1024 * 1024) || length > capacity) return false;
    if (capacity <= 15) {
        data = reinterpret_cast<char*>(objectBytes);
    } else {
        memcpy(&data, objectBytes, sizeof(data));
    }
    return data != NULL;
}

static bool ReplaceFirestormStringWithTwoLetterCode(void* stringObject, const std::string& code) {
    if (code.size() != 2) return false;

    char* data = NULL;
    size_t length = 0;
    size_t capacity = 0;
    if (!GetFirestormStringView(stringObject, data, length, capacity) || capacity < 2) return false;

    data[0] = code[0];
    data[1] = code[1];
    data[2] = '\0';
    size_t newLength = 2;
    memcpy(reinterpret_cast<BYTE*>(stringObject) + 16, &newLength, sizeof(newLength));
    return true;
}

static bool ReplaceAzureEndpointLanguageInFirestormUrl(void* stringObject, const std::string& code) {
    if (code.size() != 2) return false;

    char* data = NULL;
    size_t length = 0;
    size_t capacity = 0;
    if (!GetFirestormStringView(stringObject, data, length, capacity) || length < 12) return false;

    static const char marker[] = "/azure/";
    const size_t markerLength = sizeof(marker) - 1;
    for (size_t i = 0; i + markerLength + 3 <= length; ++i) {
        if (memcmp(data + i, marker, markerLength) != 0) continue;
        size_t languagePos = i + markerLength;
        // Our endpoint contract is /azure/<two-letter-code>/.... Do not alter
        // an unrelated path that merely contains the word "azure".
        if (data[languagePos + 2] != '/') return false;
        data[languagePos] = code[0];
        data[languagePos + 1] = code[1];
        return true;
    }
    return false;
}

static BYTE* FindUniqueAsciiLiteral(BYTE* sectionBase, size_t sectionSize, const char* literal) {
    if (!sectionBase || !literal) return NULL;
    size_t literalLength = strlen(literal);
    if (literalLength == 0 || sectionSize <= literalLength) return NULL;

    BYTE* hit = NULL;
    size_t hitCount = 0;
    for (size_t i = 0; i + literalLength < sectionSize; ++i) {
        if (memcmp(sectionBase + i, literal, literalLength) == 0 &&
            sectionBase[i + literalLength] == 0) {
            hit = sectionBase + i;
            ++hitCount;
            if (hitCount > 1) return NULL;
        }
    }
    return hitCount == 1 ? hit : NULL;
}

static BYTE* ResolveRipRelativeTarget(BYTE* instruction, size_t displacementOffset, size_t instructionLength) {
    int32_t displacement = 0;
    memcpy(&displacement, instruction + displacementOffset, sizeof(displacement));
    return instruction + instructionLength + displacement;
}

static BYTE* FindFirestormGetTranslateLanguageFunction() {
    BYTE* textBase = NULL;
    size_t textSize = 0;
    BYTE* rdataBase = NULL;
    size_t rdataSize = 0;
    if (!GetMainModuleSection(".text", textBase, textSize) ||
        !GetMainModuleSection(".rdata", rdataBase, rdataSize)) return NULL;

    BYTE* literal = FindUniqueAsciiLiteral(rdataBase, rdataSize, "TranslateLanguage");
    if (!literal) return NULL;

    static const BYTE prologue[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x70 };
    static const BYTE afterLiteral[] = {
        0x48, 0x89, 0x44, 0x24, 0x40,
        0x48, 0xC7, 0x44, 0x24, 0x48, 0x11, 0x00, 0x00, 0x00,
        0x4C, 0x8D, 0x44, 0x24, 0x40,
        0x48, 0x8B, 0xD1,
        0x48, 0x8D, 0x0D
    };

    BYTE* match = NULL;
    size_t matchCount = 0;
    for (size_t i = 0x25; i + 64 < textSize; ++i) {
        BYTE* p = textBase + i;
        if (p[0] != 0x48 || p[1] != 0x8D || p[2] != 0x05) continue;
        if (ResolveRipRelativeTarget(p, 3, 7) != literal) continue;
        if (memcmp(p + 7, afterLiteral, sizeof(afterLiteral)) != 0) continue;

        BYTE* candidate = p - 0x25;
        if (memcmp(candidate, prologue, sizeof(prologue)) != 0) continue;
        match = candidate;
        ++matchCount;
    }
    return matchCount == 1 ? match : NULL;
}

static BYTE* FindFirestormAzureGetTranslateUrlFunction() {
    BYTE* textBase = NULL;
    size_t textSize = 0;
    BYTE* rdataBase = NULL;
    size_t rdataSize = 0;
    if (!GetMainModuleSection(".text", textBase, textSize) ||
        !GetMainModuleSection(".rdata", rdataBase, rdataSize)) return NULL;

    BYTE* literal = FindUniqueAsciiLiteral(rdataBase, rdataSize, "translate?api-version=3.0&to=");
    if (!literal) return NULL;

    static const BYTE prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x48, 0x89, 0x74, 0x24, 0x18,
        0x55, 0x57, 0x41, 0x56,
        0x48, 0x8D, 0x6C, 0x24, 0xE0,
        0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00
    };
    static const BYTE argumentMoves[] = { 0x49, 0x8B, 0xD9, 0x48, 0x8B, 0xFA };

    BYTE* match = NULL;
    size_t matchCount = 0;
    for (size_t i = 0; i + 8 < textSize; ++i) {
        BYTE* p = textBase + i;
        // vmovups xmm1, xmmword ptr [rip+disp32]
        if (p[0] != 0xC5 || p[1] != 0xF8 || p[2] != 0x10 || p[3] != 0x0D) continue;
        if (ResolveRipRelativeTarget(p, 4, 8) != literal) continue;
        if (i < 0x27E) continue;

        BYTE* candidate = p - 0x27E;
        if (memcmp(candidate, prologue, sizeof(prologue)) != 0) continue;
        if (memcmp(candidate + 0x28, argumentMoves, sizeof(argumentMoves)) != 0) continue;
        match = candidate;
        ++matchCount;
    }
    return matchCount == 1 ? match : NULL;
}

static bool SnapshotRuntimeLanguageOverride(std::string& myLanguage, std::string& targetLanguage) {
    if (!g_runtimeLanguageOverride.load(std::memory_order_acquire)) return false;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        myLanguage = g_myLang;
        targetLanguage = g_targetLang;
    }
    return myLanguage.size() == 2 && targetLanguage.size() == 2;
}

static void* __fastcall HookedFirestormGetTranslateLanguage(void* resultString) {
    void* result = g_origFirestormGetTranslateLanguage
        ? g_origFirestormGetTranslateLanguage(resultString)
        : resultString;

    std::string myLanguage;
    std::string targetLanguage;
    if (SnapshotRuntimeLanguageOverride(myLanguage, targetLanguage)) {
        if (ReplaceFirestormStringWithTwoLetterCode(resultString, myLanguage)) {
            unsigned long long generation = g_languageOverrideGeneration.load();
            if (generation != 0 && g_loggedMyLanguageGeneration.exchange(generation) != generation) {
                LogMsg(("Firestorm live my-language applied: " + myLanguage).c_str());
            }
        }
    }
    return result ? result : resultString;
}

static void* __fastcall HookedFirestormAzureGetTranslateUrl(
    void* self,
    void* resultString,
    const void* fromLanguage,
    const void* toLanguage,
    const void* text) {
    void* result = g_origFirestormAzureGetTranslateUrl
        ? g_origFirestormAzureGetTranslateUrl(self, resultString, fromLanguage, toLanguage, text)
        : resultString;

    std::string myLanguage;
    std::string targetLanguage;
    if (SnapshotRuntimeLanguageOverride(myLanguage, targetLanguage)) {
        if (ReplaceAzureEndpointLanguageInFirestormUrl(resultString, targetLanguage)) {
            unsigned long long generation = g_languageOverrideGeneration.load();
            if (generation != 0 && g_loggedAzureLanguageGeneration.exchange(generation) != generation) {
                LogMsg(("Firestorm live Azure endpoint language applied: " + targetLanguage).c_str());
            }
        }
    }
    return result ? result : resultString;
}

static bool InstallFirestormLiveLanguageHooks() {
    if (g_liveLanguageHooksInstalled.load()) return true;

    BYTE* getLanguage = FindFirestormGetTranslateLanguageFunction();
    BYTE* getAzureUrl = FindFirestormAzureGetTranslateUrlFunction();
    if (!getLanguage || !getAzureUrl) {
        LogMsg("Live language hooks: required Firestorm functions not found uniquely; runtime override disabled");
        return false;
    }

    HMODULE module = GetModuleHandleW(NULL);
    BYTE* imageBase = reinterpret_cast<BYTE*>(module);
    char line[240];
    sprintf_s(line, "Live language hooks: getLanguage=%p (RVA 0x%llX), getAzureUrl=%p (RVA 0x%llX)",
        getLanguage, (unsigned long long)(getLanguage - imageBase),
        getAzureUrl, (unsigned long long)(getAzureUrl - imageBase));
    LogMsg(line);

    MH_STATUS status = MH_CreateHook(getLanguage,
        reinterpret_cast<LPVOID>(&HookedFirestormGetTranslateLanguage),
        reinterpret_cast<LPVOID*>(&g_origFirestormGetTranslateLanguage));
    if (status != MH_OK) {
        sprintf_s(line, "Live language hooks: MH_CreateHook(getLanguage) failed: %d", status);
        LogMsg(line);
        return false;
    }

    status = MH_CreateHook(getAzureUrl,
        reinterpret_cast<LPVOID>(&HookedFirestormAzureGetTranslateUrl),
        reinterpret_cast<LPVOID*>(&g_origFirestormAzureGetTranslateUrl));
    if (status != MH_OK) {
        sprintf_s(line, "Live language hooks: MH_CreateHook(getAzureUrl) failed: %d", status);
        LogMsg(line);
        MH_RemoveHook(getLanguage);
        g_origFirestormGetTranslateLanguage = NULL;
        return false;
    }

    MH_STATUS languageEnable = MH_EnableHook(getLanguage);
    MH_STATUS azureEnable = MH_EnableHook(getAzureUrl);
    if (languageEnable != MH_OK || azureEnable != MH_OK) {
        sprintf_s(line, "Live language hooks: enable failed (language=%d azure=%d)",
            languageEnable, azureEnable);
        LogMsg(line);
        MH_DisableHook(getLanguage);
        MH_DisableHook(getAzureUrl);
        MH_RemoveHook(getLanguage);
        MH_RemoveHook(getAzureUrl);
        g_origFirestormGetTranslateLanguage = NULL;
        g_origFirestormAzureGetTranslateUrl = NULL;
        return false;
    }

    g_liveLanguageHooksInstalled.store(true);
    LogMsg("Live language hooks installed: HUD pair can bypass Preferences OK for this session");
    return true;
}

// Structural (register/offset-independent) scan for the TranslateChat gate,
// confirmed by disassembling five real Firestorm release builds spanning
// 7.1.11 through 7.2.4 (76496/78266/79439/80036/80712). All five compile the
// same C++ source (llviewermessage.cpp's TranslateChat/CHAT_SOURCE_SYSTEM
// check) into an identical five-instruction shape:
//
//   48 8B 05|0D|15|1D|25|2D|35|3D dd dd dd dd   mov  reg64, [rip+disp32]   (TranslateChat cached-control ptr)
//   80 78+r 48 00                                cmp  byte [reg64+0x48], 0 (.mValue)
//   0F 84 dd dd dd dd                            je   rel32
//   83 BD dd dd dd dd 00                         cmp  dword [rbp+disp32], 0 (chat.mSourceType != CHAT_SOURCE_SYSTEM)
//   0F 84 dd dd dd dd                            je   rel32
//
// Only the cached-control register (any of the 8 low GPRs with a plain
// REX.W prefix) and the two disp32 fields float between builds; the actual
// per-version stack offset for chat.mSourceType was observed at 0x140
// (7.2.4.80712), 0x420 (7.1.13.78266), 0x430 (7.1.11.76496), 0x810
// (7.2.2.79439) and 0x830 (7.2.3.80036) -- proof that a single fixed-offset
// byte signature can never generalise across releases, even though the
// underlying C++ is unchanged. Matching this shape directly, instead of a
// release-specific byte string, is what lets one build of this DLL patch
// every Firestorm release compiled with this code shape without needing a
// new signature per release. Unknown/future layouts still fail closed: if
// the shape isn't found (exactly once), no bytes are touched.
//
// Firestorm 7.2.5.81788 moved chat.mSourceType to a small stack offset
// (0x20), so MSVC emitted the short disp8 encoding of the same compare:
//   83 7D dd 00                                  cmp  dword [rbp+disp8], 0
// Both encodings are accepted below. Verified against all six release
// builds (7.1.11, 7.1.13, 7.2.2, 7.2.3, 7.2.4, 7.2.5): each yields exactly
// one match, and the five older builds match the same address as before.
static bool InstallAvatarOnlyLocalChatPatch() {
    if (g_avatarOnlyPatchInstalled.load()) return true;

    BYTE* textBase = NULL;
    size_t textSize = 0;
    if (!GetMainModuleTextSection(textBase, textSize)) {
        LogMsg("Avatar-only patch: Firestorm .text section not found; stock behaviour retained");
        return false;
    }

    struct StructuralMatch {
        BYTE* immediateAddr;     // the "00" immediate of the mSourceType compare, followed by "0F 84"
        int32_t stackOffset;     // the displacement in that cmp, for logging only
    };

    std::vector<StructuralMatch> hits;
    if (textSize > 30) {
        for (size_t i = 0; i + 30 <= textSize; ++i) {
            // 1) REX.W(48) 8B modrm(rip-relative, mod=00 rm=101) disp32
            if (textBase[i] != 0x48 || textBase[i + 1] != 0x8B) continue;
            BYTE modrm = textBase[i + 2];
            if ((modrm & 0xC7) != 0x05) continue; // mod=00, rm=101 required; reg field free
            BYTE reg = (modrm >> 3) & 0x7;

            size_t p = i + 7; // mov is REX(1)+opcode(1)+modrm(1)+disp32(4) = 7 bytes
            // 2) cmp byte [reg+0x48], 0  -> 80 (78|reg) 48 00
            if (textBase[p] != 0x80) continue;
            if ((BYTE)(textBase[p + 1] & 0xF8) != 0x78) continue;
            if ((BYTE)(textBase[p + 1] & 0x07) != reg) continue;
            if (textBase[p + 2] != 0x48 || textBase[p + 3] != 0x00) continue;

            size_t p2 = p + 4;
            // 3) je rel32
            if (textBase[p2] != 0x0F || textBase[p2 + 1] != 0x84) continue;

            size_t p3 = p2 + 6;
            // 4) cmp dword [rbp+disp], 0 -- either encoding:
            //    83 BD dd dd dd dd 00  (disp32, Firestorm 7.1.11 - 7.2.4)
            //    83 7D dd 00           (disp8,  Firestorm 7.2.5)
            if (textBase[p3] != 0x83) continue;
            size_t imm = 0;
            int32_t stackOff = 0;
            if (textBase[p3 + 1] == 0xBD) {
                imm = p3 + 6;
                memcpy(&stackOff, textBase + p3 + 2, sizeof(stackOff));
            } else if (textBase[p3 + 1] == 0x7D) {
                imm = p3 + 3;
                stackOff = (int8_t)textBase[p3 + 2];
            } else {
                continue;
            }
            if (textBase[imm] != 0x00) continue;

            // 5) je rel32 (falls through to the translate call when both branches are not taken)
            if (textBase[imm + 1] != 0x0F || textBase[imm + 2] != 0x84) continue;

            hits.push_back({ textBase + imm, stackOff });
        }
    }

    if (hits.size() != 1) {
        char msg[160];
        sprintf_s(msg, "Avatar-only patch: structural match count=%llu (need exactly 1); stock behaviour retained",
            (unsigned long long)hits.size());
        LogMsg(msg);
        return false;
    }

    // The matched instruction is "83 BD <disp32> 00" or "83 7D <disp8> 00":
    // cmp dword [rbp+disp], 0. Its last byte is the compared immediate (0x00); flipping it
    // to 0x01 turns the condition into "chat.mSourceType != CHAT_SOURCE_SYSTEM"
    // (unpatched, translates everything but system messages) into effectively
    // "chat.mSourceType != CHAT_SOURCE_AGENT" is NOT what we want here --
    // instead we widen the immediate compared against 0 to 1, and flip the
    // following je→jne, exactly mirroring the original 7.2.4-only patch:
    //   before: 00 0F 84  (cmp ...,0 ; je skip)   -> only source==0 (SYSTEM) skips
    //   after:  01 0F 85  (cmp ...,1 ; jne skip)  -> only source==1 (AGENT) proceeds
    BYTE* patchAddress = hits[0].immediateAddr; // points at the "00" immediate, followed by "0F 84"
    static const BYTE expected[3] = { 0x00, 0x0F, 0x84 };
    static const BYTE replacement[3] = { 0x01, 0x0F, 0x85 };
    if (memcmp(patchAddress, expected, sizeof(expected)) != 0) {
        LogMsg("Avatar-only patch: target bytes changed; stock behaviour retained");
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(patchAddress, sizeof(replacement), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogMsg("Avatar-only patch: VirtualProtect failed; stock behaviour retained");
        return false;
    }

    memcpy(g_avatarOnlyOriginalBytes, patchAddress, sizeof(g_avatarOnlyOriginalBytes));
    memcpy(patchAddress, replacement, sizeof(replacement));
    FlushInstructionCache(GetCurrentProcess(), patchAddress, sizeof(replacement));

    DWORD ignored = 0;
    VirtualProtect(patchAddress, sizeof(replacement), oldProtect, &ignored);

    g_avatarOnlyPatchAddress = patchAddress;
    g_avatarOnlyPatchInstalled.store(true);

    char msg[160];
    sprintf_s(msg, "Avatar-only patch installed at Firestorm RVA 0x%llX (stack offset 0x%X)",
        (unsigned long long)(patchAddress - reinterpret_cast<BYTE*>(GetModuleHandleW(NULL))),
        (unsigned int)hits[0].stackOffset);
    LogMsg(msg);
    return true;
}

static void RemoveAvatarOnlyLocalChatPatch() {
    if (!g_avatarOnlyPatchInstalled.exchange(false) || !g_avatarOnlyPatchAddress) return;

    DWORD oldProtect = 0;
    if (VirtualProtect(g_avatarOnlyPatchAddress, sizeof(g_avatarOnlyOriginalBytes), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(g_avatarOnlyPatchAddress, g_avatarOnlyOriginalBytes, sizeof(g_avatarOnlyOriginalBytes));
        FlushInstructionCache(GetCurrentProcess(), g_avatarOnlyPatchAddress, sizeof(g_avatarOnlyOriginalBytes));
        DWORD ignored = 0;
        VirtualProtect(g_avatarOnlyPatchAddress, sizeof(g_avatarOnlyOriginalBytes), oldProtect, &ignored);
    }
    g_avatarOnlyPatchAddress = NULL;
}

// ==========================================
// Persistent WinHTTP session/connection — reused across every /api/verify
// and /api/translate call instead of opening a brand new TCP+TLS connection
// per request. Opening fresh each time paid a full TCP handshake + TLS
// handshake (typically 100-400ms depending on RTT) on top of the actual
// request, on every single translated chat line. WinHTTP keeps the
// underlying socket alive between WinHttpOpenRequest calls on the same
// hConnect, so this turns most requests into "reuse existing connection,
// just send bytes" instead of "reconnect from scratch".
// ==========================================
// WinHTTP permits multiple requests on one connection handle, but closing the
// cached connection from one failing request while another thread is opening or
// using a request from that connection is unsafe. Serialize the complete
// request lifetime. recursive_mutex lets Ensure/Reset be called by code that
// already owns this request-level lock.
static std::recursive_mutex g_httpConnMutex;
static HINTERNET g_httpSession = NULL;
static HINTERNET g_httpConn = NULL;

static bool EnsureHttpConnection() {
    std::lock_guard<std::recursive_mutex> lock(g_httpConnMutex);
    if (g_httpSession && g_httpConn) return true;
    if (!g_httpSession) {
        g_httpSession = WinHttpOpen(L"SLT/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
        if (!g_httpSession) return false;
    }
    if (!g_httpConn) {
        g_httpConn = WinHttpConnect(g_httpSession, API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!g_httpConn) return false;
    }
    return true;
}

// Drops the cached connection so the next call re-establishes it. Used when
// a request on the persistent connection fails outright (e.g. server closed
// an idle keep-alive socket) so we do not keep retrying a dead handle.
static void ResetHttpConnection() {
    std::lock_guard<std::recursive_mutex> lock(g_httpConnMutex);
    if (g_httpConn) { WinHttpCloseHandle(g_httpConn); g_httpConn = NULL; }
    if (g_httpSession) { WinHttpCloseHandle(g_httpSession); g_httpSession = NULL; }
}


// ==========================================
// Status indicator — prefixes the Firestorm window title with the current
// on/off state (e.g. "[Çeviri: AÇIK] Firestorm-Releasex64 ... "). This is
// rendered by Windows/DWM (the title bar), not by Firestorm's own
// DirectX/OpenGL render loop, so — unlike a custom overlay window — it can
// never get painted over or hidden behind the game's frame buffer. No new
// window, no tray icon, minimal cost (one API call per state change).
static HWND g_fsMainWnd = NULL;
static std::wstring g_origWindowTitle;
static std::atomic<bool> g_titleCaptured(false);
static std::recursive_mutex g_overlayMutex;

// Finds the Firestorm main viewer window by walking top-level windows owned
// by this process (avoids relying on a specific window title/class that
// could change between viewer versions).
static HWND FindFirestormMainWindow() {
    struct FindCtx { DWORD pid; HWND best; } ctx = { GetCurrentProcessId(), NULL };
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        FindCtx* ctx = (FindCtx*)lParam;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != ctx->pid) return TRUE;
        if (!IsWindowVisible(hwnd)) return TRUE;
        RECT rc;
        GetWindowRect(hwnd, &rc);
        if ((rc.right - rc.left) < 300 || (rc.bottom - rc.top) < 200) return TRUE; // skip tiny/tool windows
        ctx->best = hwnd;
        return FALSE; // first sizeable visible top-level window is good enough
    }, (LPARAM)&ctx);
    return ctx.best;
}

static void CaptureOriginalTitleIfNeeded() {
    std::lock_guard<std::recursive_mutex> lock(g_overlayMutex);
    if (g_titleCaptured.load()) return;
    if (!g_fsMainWnd) g_fsMainWnd = FindFirestormMainWindow();
    if (!g_fsMainWnd) return;
    wchar_t buf[512] = {};
    GetWindowTextW(g_fsMainWnd, buf, 512);
    // Strip any stale status prefix from a previous run (e.g. DLL reloaded
    // without a full Firestorm restart) before storing the "original".
    std::wstring t(buf);
    size_t closeBracket = std::wstring::npos;
    if (!t.empty() && t[0] == L'[') closeBracket = t.find(L"] ");
    if (closeBracket != std::wstring::npos) t = t.substr(closeBracket + 2);
    g_origWindowTitle = t;
    g_titleCaptured.store(true);
}

static std::atomic<long long> g_daysRemaining(-1); // -1 = unknown yet

static void UpdateOverlayStatus(bool on) {
    std::lock_guard<std::recursive_mutex> lock(g_overlayMutex);
    if (!g_fsMainWnd) g_fsMainWnd = FindFirestormMainWindow();
    if (!g_fsMainWnd) return;

    // Read the title fresh every time instead of caching it once — Firestorm
    // sets its own title (region name, avatar name, etc.) asynchronously
    // during startup, so a value captured too early could be missing parts
    // (e.g. the avatar name) forever. Strip our own previous suffix (if any,
    // matched as a trailing " [...]" block so it works regardless of what
    // was inside it — ON/OFF only, or ON/OFF + days remaining) and
    // re-append the current status.
    wchar_t buf[512] = {};
    GetWindowTextW(g_fsMainWnd, buf, 512);
    std::wstring base(buf);
    if (!base.empty() && base.back() == L']') {
        size_t openPos = base.rfind(L" [");
        if (openPos != std::wstring::npos) base.resize(openPos);
    }
    g_origWindowTitle = base;

    std::wstring suffix = on ? L" [TR:ON" : L" [TR:OFF";
    long long days = g_daysRemaining.load();
    if (days >= 0) {
        suffix += L" " + std::to_wstring(days) + L"d";
    }
    suffix += L"]";

    std::wstring newTitle = base + suffix;
    SetWindowTextW(g_fsMainWnd, newTitle.c_str());
}

static void InitOverlay() {
    std::lock_guard<std::recursive_mutex> lock(g_overlayMutex);
    // Give Firestorm a moment to finish setting its own window title before
    // we capture it as the "original" to restore/prefix.
    g_fsMainWnd = FindFirestormMainWindow();
    CaptureOriginalTitleIfNeeded();

    // Firestorm periodically rewrites its own window title on its own
    // (region change, avatar name arriving late at login, etc.) using its
    // own "clean" string that knows nothing about our appended
    // "[TR:ON 5d]" suffix, silently wiping it out. UpdateOverlayStatus only
    // re-adds the suffix when the DLL itself triggers it (a /tr command,
    // login-time verify, or the 5-minute heartbeat) — leaving long gaps
    // where the title looks like the suffix "disappeared". Re-asserting it
    // every couple of seconds keeps it visible without waiting for a /tr
    // command and without relying on Firestorm to never touch its title.
    std::thread([]() {
        while (SleepUntilShutdown(2000)) {
            UpdateOverlayStatus(IsTranslationEnabled());
        }
    }).detach();
}


// ==========================================
// Debug log
// ==========================================
#ifdef SLT_DEBUG_BUILD
static std::atomic<bool> g_debugLog(true);  // DEBUG BUILD: logging on from startup
#else
static std::atomic<bool> g_debugLog(false); // Enable with /tr debug
#endif

void LogMsg(const char* msg) {
    if (!g_debugLog.load()) return;
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    std::string path = std::string(tmp) + "slt_debug.log";
    std::ofstream f(path, std::ios::app);
    f << msg << std::endl;
}

// ==========================================
// JSON helper
// ==========================================
static std::string JsonUnescapeString(const std::string& input);

static std::string JsonGet(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && std::isspace((unsigned char)json[pos])) pos++;
    if (pos >= json.size()) return "";
    if (json[pos] == '"') {
        std::string raw;
        bool escaped = false;
        for (size_t i = pos + 1; i < json.size(); ++i) {
            char ch = json[i];
            if (ch == '"' && !escaped) return JsonUnescapeString(raw);
            raw.push_back(ch);
            if (ch == '\\' && !escaped) escaped = true;
            else escaped = false;
        }
        return "";
    }
    size_t end = json.find_first_of(",}", pos);
    if (end == std::string::npos) end = json.size();
    while (end > pos && std::isspace((unsigned char)json[end - 1])) end--;
    return json.substr(pos, end - pos);
}

// ==========================================
// Settings.xml'den lisans key okuma
// ==========================================
static std::string FindFirestormSettings() {
    char appdata[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata);
    std::string base = std::string(appdata) + "\\Firestorm_x64\\user_settings\\settings.xml";
    std::ifstream f(base);
    if (f.good()) return base;
    return "";
}

static std::string ReadXmlValue(const std::string& content, const std::string& keyName, const std::string& field) {
    // Find <key>keyName</key> then look for <key>field</key> and extract <string>value</string>
    std::string keyTag = "<key>" + keyName + "</key>";
    size_t pos = content.find(keyTag);
    if (pos == std::string::npos) return "";

    // Find the field within the next <map>...</map>
    std::string fieldTag = "<key>" + field + "</key>";
    size_t fieldPos = content.find(fieldTag, pos);
    if (fieldPos == std::string::npos || fieldPos > pos + 2000) return "";

    // Find <string>value</string> after the field
    size_t strStart = content.find("<string>", fieldPos);
    if (strStart == std::string::npos || strStart > fieldPos + 500) return "";
    strStart += 8;
    size_t strEnd = content.find("</string>", strStart);
    if (strEnd == std::string::npos) return "";

    return content.substr(strStart, strEnd - strStart);
}

// Extracts the language code from the "Send Messages In" endpoint URL, e.g.
// "https://api.sltranslate.com/azure/es/" -> "es". Returns "" if the URL
// doesn't look like our own endpoint format (defensive: this field is a
// free-form combo box value we don't fully control the shape of).
static std::string ParseTargetLangFromEndpoint(const std::string& endpoint) {
    const std::string marker = "/azure/";
    size_t pos = endpoint.find(marker);
    if (pos == std::string::npos) return "";
    size_t start = pos + marker.size();
    size_t end = endpoint.find('/', start);
    if (end == std::string::npos || end <= start) return "";
    std::string lang = endpoint.substr(start, end - start);
    // Sanity check: a language code is short and alphabetic (en, tr, es...).
    if (lang.empty() || lang.size() > 5) return "";
    for (char c : lang) {
        if (!std::isalpha((unsigned char)c)) return "";
    }
    for (char& c : lang) c = (char)std::tolower((unsigned char)c);
    return lang;
}

static std::string LoadLicenseKeyFromSettings() {
    std::string path = FindFirestormSettings();
    if (path.empty()) return "";
    std::ifstream f(path);
    if (!f) return "";
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return ReadXmlValue(content, "AzureTranslateAPIKey", "id");
}

// ==========================================
// HWID
// ==========================================
static std::string RunCommandAndCapture(const char* cmd) {
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return "";
    STARTUPINFOA si = { sizeof(si) };
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    char cmdBuf[512];
    strncpy_s(cmdBuf, cmd, _TRUNCATE);
    if (!CreateProcessA(NULL, cmdBuf, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe); CloseHandle(hWritePipe); return "";
    }
    CloseHandle(hWritePipe);
    std::string output;
    char buf[256];
    ULONGLONG deadline = GetTickCount64() + 5000;
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(hReadPipe, NULL, 0, NULL, &available, NULL)) break;
        if (available > 0) {
            DWORD read = 0;
            DWORD toRead = (std::min)(available, (DWORD)sizeof(buf) - 1);
            if (!ReadFile(hReadPipe, buf, toRead, &read, NULL) || read == 0) break;
            buf[read] = 0;
            output.append(buf, read);
            continue;
        }

        DWORD exitCode = STILL_ACTIVE;
        if (!GetExitCodeProcess(pi.hProcess, &exitCode) || exitCode != STILL_ACTIVE) break;
        if (GetTickCount64() >= deadline) {
            TerminateProcess(pi.hProcess, ERROR_TIMEOUT);
            break;
        }
        Sleep(25);
    }
    CloseHandle(hReadPipe);
    WaitForSingleObject(pi.hProcess, 1000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return output;
}

static std::string GetHWID() {
    std::string uuid;
    const char* source = "none";
    // Method 1 (primary): Registry MachineGuid. This is present on every
    // Windows install (XP+) and does not depend on optional/deprecated
    // components, unlike wmic which was removed starting with recent
    // Windows 11 builds and can silently disappear after an update.
    {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char regVal[256] = {};
            DWORD regSize = sizeof(regVal);
            DWORD regType = 0;
            if (RegQueryValueExA(hKey, "MachineGuid", NULL, &regType, (LPBYTE)regVal, &regSize) == ERROR_SUCCESS &&
                (regType == REG_SZ || regType == REG_EXPAND_SZ)) {
                regVal[sizeof(regVal) - 1] = 0;
                uuid = std::string(regVal);
                source = "registry_machineguid";
            }
            RegCloseKey(hKey);
        }
    }
    // Method 2 (fallback): wmic csproduct uuid, only used if the registry
    // read failed for some reason.
    if (uuid.empty() || uuid.length() < 10) {
        std::string output = RunCommandAndCapture("wmic csproduct get uuid");
        if (!output.empty()) {
            size_t pos = output.find('\n');
            if (pos != std::string::npos) {
                std::string line = output.substr(pos + 1);
                size_t start = line.find_first_not_of(" \r\n\t");
                size_t end = line.find_last_not_of(" \r\n\t");
                if (start != std::string::npos && end != std::string::npos) {
                    uuid = line.substr(start, end - start + 1);
                    source = "wmic_csproduct";
                }
            }
        }
    }
    LogMsg((std::string("HWID source: ") + source + " raw='" + uuid + "'").c_str());
    if (uuid.length() < 10) return "";
    // Simple hash
    HCRYPTPROV hProv; HCRYPTHASH hHash;
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return uuid;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) { CryptReleaseContext(hProv, 0); return uuid; }
    CryptHashData(hHash, (BYTE*)uuid.c_str(), (DWORD)uuid.size(), 0);
    BYTE hash[32]; DWORD hashLen = 32;
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0) || hashLen != sizeof(hash)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return uuid;
    }
    CryptDestroyHash(hHash); CryptReleaseContext(hProv, 0);
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf_s(hex + i * 2, 3, "%02x", hash[i]);
    return std::string(hex);
}

// ==========================================
// Session ID
// ==========================================
static std::string GenerateSessionId() {
    GUID guid = {};
    if (FAILED(CoCreateGuid(&guid))) {
        char fallback[64];
        sprintf_s(fallback, "fallback-%lu-%llu", GetCurrentProcessId(),
            (unsigned long long)GetTickCount64());
        return std::string(fallback);
    }
    char buf[40];
    sprintf_s(buf, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        guid.Data1, guid.Data2, guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return std::string(buf);
}

// ==========================================
// License verify
// ==========================================
static int VerifyLicense(const std::string& key, const std::string& hwid) {
    std::string sessionId, targetLang, myLang;
    { std::lock_guard<std::mutex> lock(g_configMutex); sessionId = g_sessionId; targetLang = g_targetLang; myLang = g_myLang; }

    std::lock_guard<std::recursive_mutex> requestLock(g_httpConnMutex);
    if (!EnsureHttpConnection()) return 0;
    HINTERNET hRequest = WinHttpOpenRequest(g_httpConn, L"POST", L"/api/verify", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { ResetHttpConnection(); return 0; }

    DWORD timeout = 10000;
    WinHttpSetTimeouts(hRequest, timeout, timeout, timeout, timeout);

    // avatar_uuid is optional and omitted until UseCircuitCode/AgentUpdate
    // yields a real agent ID. Existing licences keep working if the server
    // ignores an unknown field; new licences can bind on first sight.
    std::string avatarUuid = GetOwnAgentUuidString();
    std::string body = "{\"license_key\":\"" + key + "\",\"hwid\":\"" + hwid +
        "\",\"session_id\":\"" + sessionId + "\",\"dll_version\":\"" SLT_VERSION "\"";
    if (!targetLang.empty()) {
        body += ",\"target_lang\":\"" + targetLang + "\"";
    }
    // "My Language" (what the user reads). target_lang + my_lang together are
    // the language pair the translation model keys on. Sent on every verify so
    // the server always has the current pair the moment the user picks it in
    // the Translate menu and presses Save & Apply (which re-verifies).
    if (!myLang.empty()) {
        body += ",\"my_lang\":\"" + myLang + "\"";
    }
    if (!avatarUuid.empty()) {
        body += ",\"avatar_uuid\":\"" + avatarUuid + "\"";
    }
    // The verify itself establishes licence validity, so report the desired
    // checkbox + command state rather than the pre-verify effective state
    // (which is necessarily false on first startup).
    body += std::string(",\"translation_active\":") +
        (g_active.load() && g_userEnabled.load() ? "true" : "false");
    body += "}";

#ifdef SLT_DEBUG_BUILD
    LogMsg(("VERIFY_BODY: " + body).c_str());
#endif

    bool sendOk = WinHttpSendRequest(hRequest, L"Content-Type: application/json\r\n", (DWORD)-1L,
        (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
        WinHttpReceiveResponse(hRequest, NULL);
    if (!sendOk) {
        WinHttpCloseHandle(hRequest);
        ResetHttpConnection(); // stale/dead keep-alive socket — reconnect next time
        return 0;
    }


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
    LogMsg(("Verify response: " + result).c_str());

    std::string status = JsonGet(result, "status");
    if (status == "session_conflict") return -2;
    if (status == "expired") return -1;
    if (status == "invalid" || status == "disabled" || status == "hwid_mismatch" ||
        status == "trial_hwid_reuse") return -3;
    if (status != "active") return 0; // transient/unknown server failure

    std::string expiresStr = JsonGet(result, "expires");
    if (!expiresStr.empty() && expiresStr != "null") {
        try {
            size_t parsed = 0;
            long long expires = std::stoll(expiresStr, &parsed);
            if (parsed == expiresStr.size() && expires >= 0) g_expires = expires;
            else return 0;
        } catch (...) {
            LogMsg("Verify response contained an invalid expires value");
            return 0;
        }
    }

    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    long long now = ((long long)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10000 - 11644473600000LL;
    if (g_expires > 0 && now > g_expires) return -1;

    // Recompute remaining days here, on every successful verify (both the
    // one-shot startup check and the periodic heartbeat), not just at
    // startup. This used to live only in the startup call site, so
    // g_daysRemaining stayed frozen at its initial -1 for the rest of the
    // session once the heartbeat took over — "/tr status" always reported
    // "Days left: unknown" even with a perfectly valid, active license.
    if (g_expires > 0) {
        long long remainingMs = g_expires - now;
        long long remainingDays = remainingMs > 0 ? remainingMs / (24LL * 60 * 60 * 1000) : 0;
        g_daysRemaining.store(remainingDays);
    }

    return 1;
}

// ==========================================
// Push on/off status to server
// ==========================================
// Fire-and-forget: tells the server whether /tr on|off currently allows
// translation for this license, so azure_translate() on the server can
// refuse to run Firestorm's own incoming-translation request through the
// model. This exists because /tr off cannot stop that request from
// reaching the server in the first place — it is built and sent by
// Firestorm's embedded libcurl/LLCore::HttpRequest networking, which never
// passes through this DLL's WinHTTP hooks (confirmed 25 Eyl: an
// unconditional, debug-flag-independent log line added to
// Hooked_WinHttpOpenRequest recorded zero hits for any "/azure/" URL while
// the server's own logs showed the request arriving and being translated
// during the same /tr off session). The server is the only place left
// where this DLL's on/off state can still take effect.
//
// Always runs on its own detached thread, never inline in Hooked_sendto's
// command-handling path — that path must return immediately so the /tr
// on|off keystroke itself is never delayed by a network round-trip.
static void PushTranslationStatusAsync(bool active) {
    std::string keySnapshot, hwidSnapshot, sessionSnapshot;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        keySnapshot = g_licenseKey;
        hwidSnapshot = g_hwid;
        sessionSnapshot = g_sessionId;
    }
    if (keySnapshot.empty()) return;
    unsigned long long generation = g_statusGeneration.fetch_add(1) + 1;

    std::thread([keySnapshot, hwidSnapshot, sessionSnapshot, active, generation]() {
        if (g_shutdown.load()) return;
        std::lock_guard<std::recursive_mutex> requestLock(g_httpConnMutex);
        // A newer local state was requested before this worker got the HTTP
        // lock. Dropping this stale write prevents rapid OFF -> ON clicks from
        // leaving the server in OFF due to thread scheduling order.
        if (generation != g_statusGeneration.load()) return;
        if (!EnsureHttpConnection()) return;
        HINTERNET hRequest = WinHttpOpenRequest(g_httpConn, L"POST", L"/api/set-status", NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) { ResetHttpConnection(); return; }

        DWORD timeout = 8000;
        WinHttpSetTimeouts(hRequest, timeout, timeout, timeout, timeout);

        std::string body = "{\"license_key\":\"" + keySnapshot +
            "\",\"hwid\":\"" + hwidSnapshot +
            "\",\"session_id\":\"" + sessionSnapshot +
            "\",\"active\":" + (active ? "true" : "false") + "}";

        bool sendOk = WinHttpSendRequest(hRequest, L"Content-Type: application/json\r\n", (DWORD)-1L,
            (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(hRequest, NULL);
        if (!sendOk) {
            WinHttpCloseHandle(hRequest);
            ResetHttpConnection();
            LogMsg("set-status: request failed");
            return;
        }

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
        LogMsg(("set-status response: " + result).c_str());
    }).detach();
}


// ==========================================
// HTTP translate — server dili biliyor, sadece key + text gönder
// ==========================================
static std::string HttpTranslate(const std::string& text) {
    std::string licenseKey, sessionId;
    { std::lock_guard<std::mutex> lock(g_configMutex); licenseKey = g_licenseKey; sessionId = g_sessionId; }

    std::lock_guard<std::recursive_mutex> requestLock(g_httpConnMutex);
    if (!EnsureHttpConnection()) return "";
    HINTERNET hRequest = WinHttpOpenRequest(g_httpConn, L"POST", L"/api/translate", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { ResetHttpConnection(); return ""; }

    DWORD timeout = 10000;
    WinHttpSetTimeouts(hRequest, timeout, timeout, timeout, timeout);

    std::string escaped;
    for (char c : text) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else escaped += c;
    }
    // Server dili verify'dan biliyor — target göndermiyoruz.
    // direction: /api/translate is only ever called from Hooked_sendto for
    // packets the viewer is sending out (chat/IM), never for incoming
    // messages (those go through Firestorm's own azure/ path instead), so
    // this is always "outgoing" — a fixed literal, not a guess.
    // avatar_uuid dropped (17 Eyl) — see VerifyLicense for rationale.
    // include_original: user-controlled via /tr src on|off. When true,
    // the server should prefix/append the untranslated source text to its
    // reply if there's room, so the user can see on their own screen what
    // they actually typed (the sim otherwise only ever echoes back the
    // translated text, even to the sender). The character-budget decision
    // (translation is never truncated, original is fit into whatever room
    // is left, dropped entirely if none) is made server-side — the DLL just
    // forwards whatever comes back.
    std::string body = "{\"license_key\":\"" + licenseKey + "\",\"text\":\"" + escaped +
                       "\",\"session_id\":\"" + sessionId +
                       "\",\"direction\":\"outgoing\"," +
                       "\"include_original\":" + (g_includeOriginal.load() ? "true" : "false") + "}";

    bool sendOk = WinHttpSendRequest(hRequest, L"Content-Type: application/json\r\n", (DWORD)-1L, (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
        WinHttpReceiveResponse(hRequest, NULL);
    if (!sendOk) {
        WinHttpCloseHandle(hRequest);
        ResetHttpConnection();
        return "";
    }

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
    return JsonGet(result, "translated");
}

// ==========================================
// SL UDP decoder (see sl_packet.h)
// ==========================================
using UuidBytes = slpacket::Uuid;
using SlPacketView = slpacket::PacketView;
using ChatFromSimulatorView = slpacket::ChatFromSimulatorView;

static const uint16_t SL_MSG_USE_CIRCUIT_CODE = slpacket::kMsgUseCircuitCode;
static const uint16_t SL_MSG_IMPROVED_IM = slpacket::kMsgImprovedInstantMessage;

static std::mutex g_avatarIdMutex;
static UuidBytes g_ownAgentId{};
static std::atomic<bool> g_ownAgentIdKnown(false);
static std::atomic<bool> g_ownAgentIdLogged(false);

static bool IsZeroUuid(const UuidBytes& id) {
    return slpacket::IsZeroUuid(id);
}

static std::string UuidToString(const UuidBytes& id) {
    char out[37] = {};
    sprintf_s(out, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7],
        id[8], id[9], id[10], id[11], id[12], id[13], id[14], id[15]);
    return out;
}

static bool DecodeSlPacket(const char* buf, int len, SlPacketView& out) {
    return slpacket::DecodePacket(buf, len, out);
}

static bool PacketMatchesLow(const SlPacketView& packet, uint16_t messageNumber) {
    return slpacket::MatchesLow(packet, messageNumber);
}

static bool CopyUuidFromDecoded(const SlPacketView& packet, size_t bodyRelativeOffset, UuidBytes& uuid) {
    return slpacket::CopyUuid(packet, bodyRelativeOffset, uuid);
}

static bool TryExtractOwnAgentUuid(const SlPacketView& packet, UuidBytes& uuid) {
    return slpacket::TryExtractOwnAgentUuid(packet, uuid);
}

static bool NoteOwnAgentUuid(const UuidBytes& uuid, const char* source) {
    if (IsZeroUuid(uuid)) return false;

    bool first = false;
    bool mismatch = false;
    UuidBytes previous{};
    {
        std::lock_guard<std::mutex> lock(g_avatarIdMutex);
        if (!g_ownAgentIdKnown.load()) {
            g_ownAgentId = uuid;
            g_ownAgentIdKnown.store(true);
            first = true;
        } else if (g_ownAgentId != uuid) {
            previous = g_ownAgentId;
            mismatch = true;
        }
    }

    if (first && !g_ownAgentIdLogged.exchange(true)) {
        LogMsg((std::string("Own avatar UUID from ") + source + ": " + UuidToString(uuid)).c_str());
    } else if (mismatch) {
        LogMsg((std::string("Own avatar UUID mismatch from ") + source +
                " have=" + UuidToString(previous) +
                " saw=" + UuidToString(uuid)).c_str());
    }
    return first || !mismatch;
}

static std::atomic<bool> g_sessionInWorld(false);

// Called once per Firestorm session when the avatar finishes entering the
// world (CompleteAgentMovement) and when a LogoutRequest is seen. For now
// these only track local session state; the server-side UUID binding
// (plan.md Faz 1) will later consume these boundaries.
static void OnSessionLogin() {
    g_sessionInWorld.store(true);
}

static void OnSessionLogout() {
    g_sessionInWorld.store(false);
}

static void ObserveOutgoingAgentIdentity(const char* buf, int len) {
    SlPacketView packet;
    if (!DecodeSlPacket(buf, len, packet)) return;
#ifdef SLT_DEBUG_BUILD
    // Debug build: log every decoded outgoing packet (freq + message number)
    // exactly once per distinct (freq,number) so we can see, on first launch,
    // which packets carry the avatar UUID and when they first appear.
    {
        static std::mutex s_seenMutex;
        static std::set<uint32_t> s_seen;
        uint32_t key = (uint32_t(packet.frequency) << 16) | packet.messageNumber;
        bool firstTime = false;
        {
            std::lock_guard<std::mutex> lk(s_seenMutex);
            firstTime = s_seen.insert(key).second;
        }
        if (firstTime) {
            const char* freq = packet.frequency == slpacket::Frequency::High ? "High"
                : packet.frequency == slpacket::Frequency::Medium ? "Medium" : "Low";
            char line[160];
            sprintf_s(line, "PKT_FIRST_SEEN freq=%s num=%u flags=0x%02X len=%d",
                freq, (unsigned)packet.messageNumber, (unsigned)packet.flags, len);
            LogMsg(line);
        }
    }
#endif
    UuidBytes uuid{};
    if (!::TryExtractOwnAgentUuid(packet, uuid)) {
        // Session-state signals that do not themselves carry our UUID:
        //   CompleteAgentMovement (Low 249) -> login finished, avatar is in-world
        //   LogoutRequest         (Low 252) -> user is leaving
        // These fire once each; used so the server can know session boundaries.
        if (PacketMatchesLow(packet, 249)) {
            static std::atomic<bool> s_loginLogged(false);
            if (!s_loginLogged.exchange(true)) {
                LogMsg("SESSION_LOGIN: CompleteAgentMovement seen (user entered world)");
                OnSessionLogin();
            }
        } else if (PacketMatchesLow(packet, 252)) {
            static std::atomic<bool> s_logoutLogged(false);
            if (!s_logoutLogged.exchange(true)) {
                LogMsg("SESSION_LOGOUT: LogoutRequest seen (user leaving)");
                OnSessionLogout();
            }
        }
        return;
    }
    const char* source = PacketMatchesLow(packet, SL_MSG_USE_CIRCUIT_CODE)
        ? "UseCircuitCode" : "AgentUpdate";
    NoteOwnAgentUuid(uuid, source);
}

static bool GetOwnAgentUuid(UuidBytes& uuid) {
    std::lock_guard<std::mutex> lock(g_avatarIdMutex);
    if (!g_ownAgentIdKnown.load()) return false;
    uuid = g_ownAgentId;
    return true;
}

static std::string GetOwnAgentUuidString() {
    UuidBytes uuid{};
    if (!GetOwnAgentUuid(uuid)) return "";
    return UuidToString(uuid);
}

static bool ExtractChatFromViewer(const SlPacketView& packet, std::string& message, int& channel) {
    slpacket::ChatFromViewerView chat;
    if (!slpacket::ExtractChatFromViewer(packet, chat)) return false;
    if (!IsZeroUuid(chat.agentId)) NoteOwnAgentUuid(chat.agentId, "ChatFromViewer");
    message = std::move(chat.message);
    channel = chat.channel;
    return true;
}

static bool ExtractChatFromSimulator(const SlPacketView& packet, ChatFromSimulatorView& out) {
    return slpacket::ExtractChatFromSimulator(packet, out);
}

static const DWORD OBJECT_CHAT_CACHE_TTL_MS = 20000;
static const size_t OBJECT_CHAT_CACHE_MAX = 64;

static std::mutex g_objectChatCacheMutex;
static std::deque<std::pair<std::string, DWORD>> g_objectChatCache;

// Chat extenders such as ChatSpread relay a real avatar's words through an
// in-world object. Firestorm therefore marks those lines CHAT_SOURCE_OBJECT,
// which is indistinguishable from ordinary HUD/game output if we only inspect
// SourceType. Learn the relay identity automatically from ChatSpread's own
// connection notice and keep it in RAM for this Firestorm session only.
struct TrustedChatSpreadIdentity {
    UuidBytes sourceId{};
    UuidBytes ownerId{};
    DWORD learnedAt = 0;
};

static const DWORD CHATSPREAD_IDENTITY_TTL_MS = 12 * 60 * 60 * 1000; // session safety net
static const size_t CHATSPREAD_IDENTITY_MAX = 8;
static std::mutex g_chatSpreadMutex;
static std::deque<TrustedChatSpreadIdentity> g_chatSpreadIdentities;

static std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return (char)std::tolower(ch);
    });
    return value;
}

static void PruneChatSpreadIdentitiesLocked(DWORD now) {
    g_chatSpreadIdentities.erase(
        std::remove_if(g_chatSpreadIdentities.begin(), g_chatSpreadIdentities.end(),
            [now](const TrustedChatSpreadIdentity& item) {
                return (now - item.learnedAt) > CHATSPREAD_IDENTITY_TTL_MS;
            }),
        g_chatSpreadIdentities.end());
    while (g_chatSpreadIdentities.size() > CHATSPREAD_IDENTITY_MAX) {
        g_chatSpreadIdentities.pop_front();
    }
}

static void LearnChatSpreadIdentity(const UuidBytes& sourceId, const UuidBytes& ownerId) {
    // A real object packet must carry both IDs. Refuse partial/zero identities
    // so a malformed packet cannot create a broad or unmatchable trust entry.
    if (IsZeroUuid(sourceId) || IsZeroUuid(ownerId)) return;

    DWORD now = GetTickCount();
    {
        std::lock_guard<std::mutex> lock(g_chatSpreadMutex);
        PruneChatSpreadIdentitiesLocked(now);

        for (TrustedChatSpreadIdentity& item : g_chatSpreadIdentities) {
            if (item.sourceId == sourceId && item.ownerId == ownerId) {
                item.sourceId = sourceId;
                item.ownerId = ownerId;
                item.learnedAt = now;
                return;
            }
        }

        TrustedChatSpreadIdentity item;
        item.sourceId = sourceId;
        item.ownerId = ownerId;
        item.learnedAt = now;
        g_chatSpreadIdentities.push_back(item);
        PruneChatSpreadIdentitiesLocked(now);
    }

    LogMsg(("ChatSpread learned in RAM: source=" + UuidToString(sourceId) +
            " owner=" + UuidToString(ownerId)).c_str());
}

static void ClearChatSpreadIdentities() {
    std::lock_guard<std::mutex> lock(g_chatSpreadMutex);
    g_chatSpreadIdentities.clear();
    LogMsg("ChatSpread identities cleared (disconnect notice)");
}

static bool IsTrustedChatSpreadIdentity(const UuidBytes& sourceId, const UuidBytes& ownerId) {
    DWORD now = GetTickCount();
    std::lock_guard<std::mutex> lock(g_chatSpreadMutex);
    PruneChatSpreadIdentitiesLocked(now);

    for (const TrustedChatSpreadIdentity& item : g_chatSpreadIdentities) {
        if (!IsZeroUuid(sourceId) && !IsZeroUuid(ownerId) &&
            item.sourceId == sourceId && item.ownerId == ownerId) return true;
    }
    return false;
}

static int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

static void AppendUtf8(std::string& out, unsigned int codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back((char)codepoint);
    } else if (codepoint <= 0x7FF) {
        out.push_back((char)(0xC0 | ((codepoint >> 6) & 0x1F)));
        out.push_back((char)(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back((char)(0xE0 | ((codepoint >> 12) & 0x0F)));
        out.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | ((codepoint >> 18) & 0x07)));
        out.push_back((char)(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (codepoint & 0x3F)));
    }
}

static std::string NormalizeChatText(const std::string& text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (char ch : text) {
        if (ch == '\0' || ch == '\r') continue;
        normalized.push_back(ch);
    }

    size_t start = 0;
    while (start < normalized.size() && std::isspace((unsigned char)normalized[start])) start++;
    size_t end = normalized.size();
    while (end > start && std::isspace((unsigned char)normalized[end - 1])) end--;
    return normalized.substr(start, end - start);
}

static std::string PreviewForLog(const std::string& text, size_t maxLen = 200) {
    std::string preview;
    preview.reserve((std::min)(text.size(), maxLen));
    for (char ch : text) {
        if (ch == '\r' || ch == '\n' || ch == '\t') preview.push_back(' ');
        else preview.push_back(ch);
        if (preview.size() >= maxLen) break;
    }
    if (text.size() > maxLen) preview += "...";
    return preview;
}

static void PruneObjectChatCacheLocked(DWORD now) {
    while (!g_objectChatCache.empty() && (now - g_objectChatCache.front().second) > OBJECT_CHAT_CACHE_TTL_MS) {
        g_objectChatCache.pop_front();
    }
    while (g_objectChatCache.size() > OBJECT_CHAT_CACHE_MAX) {
        g_objectChatCache.pop_front();
    }
}

static void CacheObjectChatText(const std::string& text) {
    std::string normalized = NormalizeChatText(text);
    if (normalized.empty()) return;

    DWORD now = GetTickCount();
    {
        std::lock_guard<std::mutex> lock(g_objectChatCacheMutex);
        PruneObjectChatCacheLocked(now);
        g_objectChatCache.emplace_back(normalized, now);
        PruneObjectChatCacheLocked(now);
    }

    LogMsg(("Object cache add: " + PreviewForLog(normalized)).c_str());
}

static bool IsObjectChatTextCached(const std::string& text) {
    std::string normalized = NormalizeChatText(text);
    if (normalized.empty()) return false;

    DWORD now = GetTickCount();
    std::lock_guard<std::mutex> lock(g_objectChatCacheMutex);
    PruneObjectChatCacheLocked(now);
    for (auto it = g_objectChatCache.rbegin(); it != g_objectChatCache.rend(); ++it) {
        if (it->first == normalized) return true;
    }
    return false;
}

static std::string JsonUnescapeString(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        char ch = input[i];
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }

        if (i + 1 >= input.size()) break;
        char esc = input[++i];
        switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u':
            if (i + 4 < input.size()) {
                int h0 = HexValue(input[i + 1]);
                int h1 = HexValue(input[i + 2]);
                int h2 = HexValue(input[i + 3]);
                int h3 = HexValue(input[i + 4]);
                if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
                    unsigned int codepoint = (unsigned int)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                    i += 4;
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
                        i + 6 < input.size() &&
                        input[i + 1] == '\\' && input[i + 2] == 'u') {
                        int l0 = HexValue(input[i + 3]);
                        int l1 = HexValue(input[i + 4]);
                        int l2 = HexValue(input[i + 5]);
                        int l3 = HexValue(input[i + 6]);
                        if (l0 >= 0 && l1 >= 0 && l2 >= 0 && l3 >= 0) {
                            unsigned int low = (unsigned int)((l0 << 12) | (l1 << 8) | (l2 << 4) | l3);
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low - 0xDC00));
                                i += 6;
                            }
                        }
                    }
                    AppendUtf8(out, codepoint);
                    break;
                }
            }
            out.push_back('u');
            break;
        default:
            out.push_back(esc);
            break;
        }
    }
    return out;
}

static bool TryExtractAzureText(const std::string& body, std::string& outText) {
    size_t keyPos = body.find("\"Text\"");
    if (keyPos == std::string::npos) return false;

    size_t colonPos = body.find(':', keyPos + 6);
    if (colonPos == std::string::npos) return false;

    size_t pos = colonPos + 1;
    while (pos < body.size() && std::isspace((unsigned char)body[pos])) pos++;
    if (pos >= body.size() || body[pos] != '"') return false;
    pos++;

    std::string raw;
    raw.reserve(body.size() - pos);
    bool escaped = false;
    for (; pos < body.size(); ++pos) {
        char ch = body[pos];
        if (ch == '"' && !escaped) {
            outText = JsonUnescapeString(raw);
            return true;
        }
        raw.push_back(ch);
        if (ch == '\\' && !escaped) escaped = true;
        else escaped = false;
    }
    return false;
}

// ==========================================
// WinHTTP hook — intercept Firestorm's Azure translation requests
// ==========================================
typedef BOOL (WINAPI* pWinHttpSendRequest)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
static pWinHttpSendRequest g_origWinHttpSendRequest = NULL;
typedef BOOL (WINAPI* pWinHttpCloseHandle)(HINTERNET);
static pWinHttpCloseHandle g_origWinHttpCloseHandle = NULL;

// Track the request handle → URL mapping
static std::mutex g_httpUrlMutex;
static std::unordered_map<HINTERNET, std::wstring> g_httpRequestUrls;

// Hook WinHttpOpenRequest to capture URL
typedef HINTERNET (WINAPI* pWinHttpOpenRequest)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
static pWinHttpOpenRequest g_origWinHttpOpenRequest = NULL;

BOOL WINAPI Hooked_WinHttpCloseHandle(HINTERNET hInternet) {
    {
        std::lock_guard<std::mutex> lock(g_httpUrlMutex);
        g_httpRequestUrls.erase(hInternet);
    }
    return g_origWinHttpCloseHandle ? g_origWinHttpCloseHandle(hInternet) : FALSE;
}

HINTERNET WINAPI Hooked_WinHttpOpenRequest(HINTERNET hConnect, LPCWSTR verb, LPCWSTR path, LPCWSTR version, LPCWSTR referrer, LPCWSTR* types, DWORD flags) {
    HINTERNET hRequest = g_origWinHttpOpenRequest(hConnect, verb, path, version, referrer, types, flags);
    if (hRequest && path) {
        std::wstring url(path);
        std::lock_guard<std::mutex> lock(g_httpUrlMutex);
        g_httpRequestUrls[hRequest] = url;

        // Convert to narrow string for logging
        char narrow[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow) - 1, NULL, NULL);
        LogMsg(("WinHTTP request: " + std::string(narrow)).c_str());

        // Check if this is our Azure endpoint
        std::string urlStr(narrow);
        if (urlStr.find("/azure/") != std::string::npos) {
            // Force-write unconditionally (regardless of g_debugLog). This
            // is the one piece of evidence that actually distinguishes the
            // two competing explanations for "/tr off didn't stop server
            // traffic": either (a) Firestorm's Azure request reached this
            // hook and BLOCKED-Firestorm-azure-request below fired but
            // nobody saw it because debug was off, or (b) this hook was
            // never called for that request at all because Firestorm's
            // "Translate Chat" networking bypasses WinHTTP (e.g. goes
            // through its embedded libcurl stack instead, the same
            // suspicion noted for the avatar-only local-chat patch). If
            // the server shows an /azure/ hit with zero matching line here,
            // that proves (b) and this whole interception approach cannot
            // work regardless of the g_active check added below.
            char tmp[MAX_PATH];
            GetTempPathA(MAX_PATH, tmp);
            std::string logPath = std::string(tmp) + "slt_debug.log";
            std::ofstream f(logPath, std::ios::app);
            f << "AZURE_REQUEST_SEEN_BY_HOOK: " << narrow << std::endl;
            // Extract language from URL: /azure/XX/translate
            size_t azurePos = urlStr.find("/azure/");
            size_t langStart = azurePos + 7; // after "/azure/"
            size_t langEnd = urlStr.find('/', langStart);
            if (langEnd != std::string::npos) {
                std::string lang = urlStr.substr(langStart, langEnd - langStart);
                if (!lang.empty() && lang != "off" && lang != "translate") {
                    LogMsg(("WinHTTP: detected lang=" + lang).c_str());
                }
            }
        }
    }
    return hRequest;
}

BOOL WINAPI Hooked_WinHttpSendRequest(HINTERNET hRequest, LPCWSTR headers, DWORD headersLen, LPVOID optional, DWORD optionalLen, DWORD totalLen, DWORD_PTR context) {
    // Check if this request is to our azure/off endpoint — block it
    bool blockRequest = false;
    bool inspectAzureBody = false;
    std::string url;
    {
        std::lock_guard<std::mutex> lock(g_httpUrlMutex);
        auto it = g_httpRequestUrls.find(hRequest);
        if (it != g_httpRequestUrls.end()) {
            char narrow[512] = {};
            WideCharToMultiByte(CP_UTF8, 0, it->second.c_str(), -1, narrow, sizeof(narrow) - 1, NULL, NULL);
            url = narrow;
            // Block azure/off requests (except verify — short text like "hello")
            if (url.find("/azure/off/") != std::string::npos) {
                blockRequest = true;
                LogMsg("WinHTTP: BLOCKED azure/off request (no server traffic)");
            } else if (url.find("/azure/") != std::string::npos) {
                // Firestorm's own built-in "Translate Chat" (Preferences > Chat)
                // reads gSavedSettings live from memory, not from settings.xml —
                // our /tr on|off chat command only flips this DLL's g_active
                // flag and cannot change Firestorm's in-memory control without
                // fragile, version-specific reverse engineering (gSavedSettings
                // address + LLControlVariable layout). Blocking Firestorm's own
                // Azure request here, at the network layer, when the user has
                // turned translation off via /tr gives the same net effect
                // (no server round-trip, no risk of a stale/garbled reply)
                // without touching Firestorm's internals at all.
                if (!IsTranslationEnabled()) {
                    blockRequest = true;
                    // Force-write: same reasoning as AZURE_REQUEST_SEEN_BY_HOOK
                    // above — this must be visible even when debug is off, to
                    // prove the block actually fired rather than silently
                    // not happening. LogMsg() alone would be swallowed
                    // whenever g_debugLog is false.
                    char tmp[MAX_PATH];
                    GetTempPathA(MAX_PATH, tmp);
                    std::string logPath = std::string(tmp) + "slt_debug.log";
                    std::ofstream f(logPath, std::ios::app);
                    f << "AZURE_REQUEST_BLOCKED (/tr off active)" << std::endl;
                } else {
                    inspectAzureBody = true;
                }
            }
        }
    }

    if (blockRequest) {
        // Don't send to server — return TRUE (success) to Firestorm
        // Firestorm will try to read response and fail gracefully
        SetLastError(ERROR_WINHTTP_OPERATION_CANCELLED);
        return FALSE;
    }

    if (inspectAzureBody && optional && optionalLen > 0) {
        std::string body((const char*)optional, (size_t)optionalLen);
        LogMsg(("WinHTTP body: " + PreviewForLog(body)).c_str());

        std::string candidate;
        if (!TryExtractAzureText(body, candidate)) candidate = body;

        std::string normalized = NormalizeChatText(candidate);
        if (IsObjectChatTextCached(normalized)) {
            LogMsg(("WinHTTP: BLOCKED object text: " + PreviewForLog(normalized)).c_str());
            SetLastError(ERROR_WINHTTP_OPERATION_CANCELLED);
            return FALSE;
        }

        LogMsg(("WinHTTP: passthrough " + PreviewForLog(normalized.empty() ? body : normalized)).c_str());
    }

    return g_origWinHttpSendRequest(hRequest, headers, headersLen, optional, optionalLen, totalLen, context);
}

// ==========================================
// Sendto / recvfrom hooks
// ==========================================
typedef int (WSAAPI* pSendTo)(SOCKET s, const char* buf, int len, int flags, const struct sockaddr* to, int tolen);
static pSendTo g_origSendTo = NULL;
typedef int (WSAAPI* pRecvFrom)(SOCKET s, char* buf, int len, int flags, struct sockaddr* from, int* fromlen);
static pRecvFrom g_origRecvfrom = NULL;

// ==========================================
// Async send queue
// ==========================================
struct QueuedSendTo {
    SOCKET s = INVALID_SOCKET;
    int flags = 0;
    bool hasTo = false;
    int tolen = 0;
    sockaddr_storage to{};
    std::vector<char> data;
};

static std::mutex g_sendQueueMutex;
static std::condition_variable g_sendQueueCv;
static std::queue<QueuedSendTo> g_sendQueue;
static std::atomic<bool> g_sendWorkerStop(false);
static std::atomic<bool> g_sendWorkerStarted(false);

static bool ContainsUrl(const std::string& text) {
    return text.find("http://") != std::string::npos ||
           text.find("https://") != std::string::npos ||
           text.find("www.") != std::string::npos;
}

static bool EnqueueSendTo(SOCKET s, const char* buf, int len, int flags, const struct sockaddr* to, int tolen) {
    if (!buf || len <= 0) return false;

    // Translation can take several seconds. Firestorm may close or recycle the
    // original SOCKET during teleport/logout before the worker sends. Hold an
    // independently owned duplicate referring to the same underlying socket so
    // the queued operation cannot accidentally target a reused handle value.
    WSAPROTOCOL_INFOW protocolInfo = {};
    if (WSADuplicateSocketW(s, GetCurrentProcessId(), &protocolInfo) == SOCKET_ERROR) return false;
    SOCKET queuedSocket = WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
        &protocolInfo, 0, WSA_FLAG_OVERLAPPED);
    if (queuedSocket == INVALID_SOCKET) return false;

    QueuedSendTo item;
    item.s = queuedSocket;
    item.flags = flags;
    item.data.assign(buf, buf + len);
    if (to && tolen > 0) {
        item.hasTo = true;
        item.tolen = (tolen > (int)sizeof(item.to)) ? (int)sizeof(item.to) : tolen;
        memcpy(&item.to, to, item.tolen);
    }
    { std::lock_guard<std::mutex> lock(g_sendQueueMutex); g_sendQueue.push(std::move(item)); }
    g_sendQueueCv.notify_one();
    return true;
}

// ==========================================
// Zero-coded stream reader (IM packets)
// ==========================================
class StreamReader {
public:
    const char* m_buf;
    int m_len;
    bool m_zeroCoded;
    int m_rawPos;
    int m_zeroRemaining = 0;
    bool m_valid = true;

    StreamReader(const char* buf, int len) : m_buf(buf), m_len(len), m_rawPos(0) {
        m_zeroCoded = ((unsigned char)m_buf[0] & 0x80) != 0;
    }
    void SetRawPos(int pos) {
        m_rawPos = pos;
        if (pos < 0 || pos > m_len) m_valid = false;
    }
    int RawPos() const { return m_rawPos; }
    bool IsValid() const { return m_valid; }
    unsigned char ReadByte() {
        if (m_zeroRemaining > 0) { m_zeroRemaining--; return 0x00; }
        if (!m_valid || m_rawPos >= m_len) { m_valid = false; return 0; }
        unsigned char b = (unsigned char)m_buf[m_rawPos];
        if (m_zeroCoded && b == 0x00) {
            if (m_rawPos + 1 >= m_len) { m_valid = false; return 0; }
            unsigned char count = (unsigned char)m_buf[m_rawPos + 1];
            if (count == 0) { m_valid = false; return 0; }
            m_rawPos += 2;
            if (count > 1) m_zeroRemaining = count - 1;
            return 0x00;
        }
        m_rawPos += 1;
        return b;
    }
    void Skip(int count) { for (int i = 0; i < count; ++i) ReadByte(); }
    std::string ReadStringV1() {
        unsigned char nameLen = ReadByte();
        if (nameLen == 0) return "";
        std::string str;
        str.reserve(nameLen);
        for (int i = 0; i < nameLen; ++i) str += (char)ReadByte();
        return str;
    }
    std::string ReadStringV2() {
        unsigned short strLen = (unsigned short)(ReadByte() | (ReadByte() << 8));
        if (strLen == 0) return "";
        std::string str;
        str.reserve(strLen);
        for (unsigned short i = 0; i < strLen; ++i) str += (char)ReadByte();
        if (!str.empty() && str.back() == '\0') str.pop_back();
        return str;
    }
};

// Forward declaration: defined further below (needs g_licenseKey, g_active,
// etc. that are declared in between), but InspectChatFromSimulatorPacket
// needs to call it for HUD llOwnerSay commands.
static bool ProcessTrCommand(const std::string& cmdRaw);

static void InspectChatFromSimulatorPacket(char* buf, int len) {
    if (!buf || len < 10) return;

    SlPacketView packet;
    if (!DecodeSlPacket(buf, len, packet)) return;

    ChatFromSimulatorView chat;
    if (!::ExtractChatFromSimulator(packet, chat)) return;

    std::string fromName = chat.fromName;
    UuidBytes sourceId = chat.sourceId;
    UuidBytes ownerId = chat.ownerId;
    unsigned char sourceType = chat.sourceType;
    unsigned char chatType = chat.chatType;
    unsigned char audible = chat.audible;
    std::string normalized = NormalizeChatText(chat.message);

    // HUD remote control: a script-attached HUD can send "/tr on|off|..."
    // via llOwnerSay(). llOwnerSay is only ever delivered to the object's
    // owner (never broadcast), so seeing CHAT_TYPE_OWNER here already proves
    // the object belongs to the local user — no UUID/identity check needed,
    // unlike the ChatSpread relay-trust logic below which handles untrusted
    // third-party objects. This intentionally runs before any other object
    // handling (ChatSpread learning, object-text caching) and returns
    // immediately so a HUD's command text is never treated as chat content.
    if (chatType == 8 /* CHAT_TYPE_OWNER */ && normalized.size() >= 3 &&
        strncmp(normalized.c_str(), "/tr", 3) == 0) {
        ProcessTrCommand(normalized);
        LogMsg(("HUD command via llOwnerSay: " + PreviewForLog(normalized)).c_str());
        return;
    }

    bool promotedChatSpreadRelay = false;
    if (sourceType == 2 && !normalized.empty()) {
        std::string lower = LowerAscii(normalized);
        std::string lowerFromName = LowerAscii(fromName);
        bool mentionsChatSpread = lower.find("chatspread") != std::string::npos;
        bool disconnected = mentionsChatSpread && lower.find("disconnected") != std::string::npos;
        // Trust only the exact product-specific notice observed from ChatSpread,
        // not arbitrary object text containing the broad words "connected" and
        // "chatspread". The sender-name and second sentence make accidental or
        // malicious false learning substantially less likely.
        bool connected = !disconnected &&
            lowerFromName == "you're" &&
            lower.find("/me now connected to chatspread.") == 0 &&
            lower.find("your local chat will be transmitted parcel wide") != std::string::npos;

        if (disconnected) {
            ClearChatSpreadIdentities();
        } else if (connected) {
            LearnChatSpreadIdentity(sourceId, ownerId);
        } else if (IsTrustedChatSpreadIdentity(sourceId, ownerId)) {
            // Reclassify only when SourceType occupies a real raw byte at a
            // known offset. ChatFromSimulator is Unencoded, so after ACK
            // stripping the decoded offset maps directly onto the wire.
            // Never rewrite a zero-coded datagram: the raw stream is a
            // compressed encoding, not the logical field layout.
            if ((packet.flags & 0x80) == 0) {
                size_t sourceTypeRawPos = 6 + chat.sourceTypeDecodedOffset;
                if (sourceTypeRawPos < packet.datagramDataEnd &&
                    (unsigned char)buf[sourceTypeRawPos] == 2) {
                    buf[sourceTypeRawPos] = 1;
                    promotedChatSpreadRelay = true;
                }
            }
        }
    }

    char logBuf[900];
    sprintf_s(logBuf, "recvfrom ChatFromSim src=%u%s chatType=%u audible=%u source=%s owner=%s from=%s msg=%s",
        (unsigned int)sourceType,
        promotedChatSpreadRelay ? "->1 ChatSpread" : "",
        (unsigned int)chatType,
        (unsigned int)audible,
        UuidToString(sourceId).c_str(),
        UuidToString(ownerId).c_str(),
        PreviewForLog(fromName, 80).c_str(),
        PreviewForLog(normalized).c_str());
    LogMsg(logBuf);

    if (sourceType == 2 && !promotedChatSpreadRelay && !normalized.empty()) {
        CacheObjectChatText(normalized);
    }
}

// ==========================================
// Chat packet translation
// ==========================================
static bool TryBuildTranslatedChatFromViewer(const char* buf, int len, std::vector<char>& outPkt) {
    if (!buf || len < 50) return false;

    SlPacketView packet;
    if (!DecodeSlPacket(buf, len, packet)) return false;

    std::string original;
    int channel = 0;
    if (!ExtractChatFromViewer(packet, original, channel)) return false;
    if (channel != 0 || original.size() <= 1 || original[0] == '/') return false;
    if (ContainsUrl(original)) return false;

    std::string translated = HttpTranslate(original);
    if (translated.empty() || translated == original) return false;

    LogMsg(("CHAT Translated: " + translated).c_str());

    // Guard against building a packet that exceeds SL's UDP MTU (1200 bytes,
    // see MTUBYTES in net.h). The original text is capped at 1023 chars by
    // Firestorm's own chat input box (DB_CHAT_MSG_STR_LEN), but a
    // translation can come back longer than the source (e.g. tr->de often
    // expands), and nothing downstream re-checks that before handing the
    // packet to sendto(). A message this long would very likely be silently
    // dropped or rejected by the simulator anyway (SL has no UDP
    // fragmentation), so falling back to the untranslated original is
    // strictly better than gambling on an oversized send.
    static const size_t kMaxSafeChatBytes = 1100; // headroom under 1200 MTU
    if (translated.length() > kMaxSafeChatBytes) {
        LogMsg("CHAT: translated text too long for one UDP packet, skipping");
        return false;
    }

    // Rewrite only uncompressed ChatFromViewer datagrams. Zero-coded packets
    // are decoded for identity/command inspection, but rewriting them would
    // require a matching encoder and risks a malformed send. Extra-header
    // packets are likewise left untouched.
    if ((packet.flags & 0x80) != 0 || packet.extraHeaderLength != 0) {
        LogMsg("CHAT: decoded a zero-coded/extra-header packet, skipping rewrite");
        return false;
    }

    size_t messageOffset = 6 + packet.bodyOffset + 32; // skip AgentID + SessionID
    if (messageOffset + 2 >= packet.datagramDataEnd) return false;
    unsigned short oldMsgLen = (unsigned short)(
        (unsigned char)buf[messageOffset] |
        ((unsigned char)buf[messageOffset + 1] << 8));
    if (oldMsgLen <= 1 || messageOffset + 2 + oldMsgLen + 1 + 4 > packet.datagramDataEnd) return false;

    int newMsgLen = (int)translated.length() + 1;
    size_t remainStart = messageOffset + 2 + oldMsgLen;
    size_t remainLen = (size_t)len - remainStart;
    size_t newPktLen = messageOffset + 2 + (size_t)newMsgLen + remainLen;
    outPkt.assign(newPktLen, 0);
    memcpy(outPkt.data(), buf, messageOffset);
    outPkt[messageOffset] = (char)(newMsgLen & 0xFF);
    outPkt[messageOffset + 1] = (char)((newMsgLen >> 8) & 0xFF);
    memcpy(outPkt.data() + messageOffset + 2, translated.c_str(), translated.length());
    outPkt[messageOffset + 2 + translated.length()] = 0;
    if (remainLen > 0) memcpy(outPkt.data() + messageOffset + 2 + newMsgLen, buf + remainStart, remainLen);
    return true;
}

// ==========================================
// IM packet translation
// ==========================================
static bool TryBuildTranslatedImprovedInstantMessage(const char* buf, int len, std::vector<char>& outPkt) {
    if (!buf || len < 80) return false;

    SlPacketView packet;
    if (!DecodeSlPacket(buf, len, packet)) return false;
    if (!PacketMatchesLow(packet, SL_MSG_IMPROVED_IM)) return false;

    UuidBytes agentId{};
    if (CopyUuidFromDecoded(packet, 0, agentId))
        NoteOwnAgentUuid(agentId, "ImprovedInstantMessage");

    // Extra headers shift the raw field layout. Decode for identity, but do
    // not rewrite those rare packets. Zero-coded packets with an ACK trailer
    // also fail closed: StreamReader cannot tell compressed payload from the
    // appended ACK list.
    if (packet.extraHeaderLength != 0) return false;
    if ((packet.flags & slpacket::kAckFlag) != 0 &&
        (packet.flags & slpacket::kZeroCodeFlag) != 0) return false;

    StreamReader reader(buf, (int)packet.datagramDataEnd);
    reader.SetRawPos(6);
    for (size_t i = 0; i < packet.messageNameLength; ++i) reader.ReadByte();
    reader.Skip(16); // AgentID
    reader.Skip(16); // SessionID
    reader.Skip(1);  // FromGroup
    reader.Skip(16); // ToAgentID
    reader.Skip(4);  // ParentEstateID
    reader.Skip(16); // RegionID
    reader.Skip(12); // Position
    reader.ReadByte(); // Offline
    reader.ReadByte(); // Dialog
    reader.Skip(16); // ID
    reader.Skip(4);  // Timestamp
    std::string fromAgentName = reader.ReadStringV1();
    if (!reader.IsValid()) return false;

    int msgLenPos = reader.m_rawPos;
    unsigned char len_lo = reader.ReadByte();
    unsigned char len_hi = reader.ReadByte();
    int msgLen = len_lo | (len_hi << 8);
    if (!reader.IsValid() || msgLen <= 1 || msgLen > 1024) return false;

    std::string original;
    if (msgLen > 0) {
        for (int i = 0; i < msgLen - 1; ++i) original += (char)reader.ReadByte();
        reader.ReadByte();
    }
    if (!reader.IsValid()) return false;
    int msgEndPos = reader.m_rawPos;

    if (original.empty() || original == "typing" || original.length() <= 1) return false;
    if (original[0] == '/') return false;
    if (ContainsUrl(original)) return false;

    std::string translated = HttpTranslate(original);
    if (translated.empty() || translated == original) return false;

    // Same MTU headroom guard as the chat path — see TryBuildTranslatedChatFromViewer
    // for the full rationale (SL has no UDP fragmentation, so an oversized
    // packet is more likely to be silently dropped than delivered).
    static const size_t kMaxSafeImBytes = 1100;
    if (translated.length() > kMaxSafeImBytes) {
        LogMsg("IM: translated text too long for one UDP packet, skipping");
        return false;
    }

    LogMsg(("IM Translated: " + translated).c_str());

    int newTextLen = (int)translated.length();
    int newMsgLen = newTextLen + 1;
    int headerLen = msgLenPos;
    int trailStart = msgEndPos;
    int trailLen = len - trailStart;
    if (trailLen < 0) trailLen = 0;

    outPkt.clear();
    outPkt.reserve((size_t)headerLen + 2 + translated.size() + 2 + (size_t)trailLen);
    outPkt.insert(outPkt.end(), buf, buf + headerLen);
    auto appendBodyByte = [&](unsigned char b) {
        if (reader.m_zeroCoded && b == 0) {
            outPkt.push_back(0x00);
            outPkt.push_back(0x01);
        } else {
            outPkt.push_back((char)b);
        }
    };
    appendBodyByte((unsigned char)(newMsgLen & 0xFF));
    appendBodyByte((unsigned char)((newMsgLen >> 8) & 0xFF));
    for (unsigned char b : translated) appendBodyByte(b);
    appendBodyByte(0x00);
    if (trailLen > 0) outPkt.insert(outPkt.end(), buf + trailStart, buf + trailStart + trailLen);
    return true;
}

// ==========================================
// Send worker thread
// ==========================================
static void SendWorkerLoop() {
    for (;;) {
        QueuedSendTo item;
        {
            std::unique_lock<std::mutex> lock(g_sendQueueMutex);
            g_sendQueueCv.wait(lock, []() { return g_sendWorkerStop.load() || !g_sendQueue.empty(); });
            if (g_sendWorkerStop.load() && g_sendQueue.empty()) return;
            item = std::move(g_sendQueue.front());
            g_sendQueue.pop();
        }
        if (!g_origSendTo) {
            if (item.s != INVALID_SOCKET) closesocket(item.s);
            continue;
        }

        const char* sendBuf = item.data.data();
        int sendLen = (int)item.data.size();
        std::vector<char> translatedPkt;
        if (IsTranslationEnabled()) {
            if (TryBuildTranslatedChatFromViewer(item.data.data(), sendLen, translatedPkt) ||
                TryBuildTranslatedImprovedInstantMessage(item.data.data(), sendLen, translatedPkt)) {
                sendBuf = translatedPkt.data();
                sendLen = (int)translatedPkt.size();
            }
        }
        const struct sockaddr* dest = item.hasTo ? (const struct sockaddr*)&item.to : nullptr;
        int destLen = item.hasTo ? item.tolen : 0;
        g_origSendTo(item.s, sendBuf, sendLen, item.flags, dest, destLen);
        closesocket(item.s);
    }
}

static void StartSendWorker() {
    bool expected = false;
    if (!g_sendWorkerStarted.compare_exchange_strong(expected, true)) return;
    g_sendWorkerStop.store(false);
    std::thread(SendWorkerLoop).detach();
}

static void StopSendWorker() {
    g_sendWorkerStop.store(true);
    {
        std::lock_guard<std::mutex> lock(g_sendQueueMutex);
        while (!g_sendQueue.empty()) {
            if (g_sendQueue.front().s != INVALID_SOCKET) closesocket(g_sendQueue.front().s);
            g_sendQueue.pop();
        }
    }
    g_sendQueueCv.notify_all();
}

// ==========================================
// Settings.xml change detection
// ==========================================
static std::string g_cachedSettingsPath;
static long long g_settingsModTime = 0;
static std::mutex g_settingsMutex;

static bool ReadTranslateChatEnabled(const std::string& content) {
    size_t tcPos = content.find("<key>TranslateChat</key>");
    if (tcPos == std::string::npos) return false;
    size_t valPos = content.find("<boolean>", tcPos);
    if (valPos == std::string::npos || valPos >= tcPos + 500) return false;
    valPos += 9;
    return valPos < content.size() && content[valPos] == '1';
}

static void CheckSettingsXmlUpdate() {
    std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
    // Cache the path on first call
    if (g_cachedSettingsPath.empty()) {
        g_cachedSettingsPath = FindFirestormSettings();
        if (g_cachedSettingsPath.empty()) return;
    }

    // Check mod time
    struct _stat st;
    if (_stat(g_cachedSettingsPath.c_str(), &st) != 0) return;
    long long mtime = (long long)st.st_mtime;
    if (mtime == g_settingsModTime) return;

    // Firestorm periodically rewrites settings.xml in place (unrelated
    // fields, e.g. window position/UI state) while the viewer keeps
    // running. That rewrite is not guaranteed atomic from a concurrent
    // reader's point of view: this function can be called (from every
    // Hooked_sendto, i.e. very frequently) at the exact moment the file is
    // only partially written, in which case <key>TranslateChat</key> or its
    // following <boolean> value may be truncated/missing entirely.
    // ReadTranslateChatEnabled() fails CLOSED in that case (returns false,
    // "translation off") by design, so a half-written file was being
    // misread as the user having disabled translation, which immediately
    // pushed a spurious active=false to the server (visible in server logs
    // as bursts of set-status active=false a few seconds apart, with no
    // corresponding /tr off from the user) even though settings.xml on
    // disk, once the rewrite finished, still had TranslateChat=1 the whole
    // time. Guard against this by re-stat'ing the file's size and mtime
    // immediately after reading it: if either changed while we were
    // reading, Firestorm was still writing, so discard this read entirely
    // and retry on the next call (Hooked_sendto runs constantly, so the
    // next attempt is only a few packets away) instead of acting on
    // possibly-torn content.
    long long sizeBeforeRead = (long long)st.st_size;

    // File changed — re-read settings
    std::ifstream f(g_cachedSettingsPath);
    if (!f) return;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    struct _stat stAfter;
    if (_stat(g_cachedSettingsPath.c_str(), &stAfter) != 0) return;
    if ((long long)stAfter.st_mtime != mtime || (long long)stAfter.st_size != sizeBeforeRead) {
        // Firestorm was still writing this file while we read it. Do not
        // update g_settingsModTime, so the next Hooked_sendto call treats
        // the file as still-changed and retries the read from scratch.
        char msg[256];
        sprintf_s(msg, "settings.xml torn read detected (mtime %lld->%lld, size %lld->%lld); retrying next call",
            mtime, (long long)stAfter.st_mtime, sizeBeforeRead, (long long)stAfter.st_size);
        LogMsg(msg);
        return;
    }
    // A settings.xml this small cannot possibly contain a real, complete
    // Firestorm configuration (the real file is tens of thousands of
    // lines) -- this only happens mid-rewrite when the file has been
    // truncated to zero and not yet refilled. Treat it the same as a torn
    // read above: skip this cycle rather than misreading TranslateChat/etc.
    // as absent-therefore-off.
    if (content.size() < 4096) {
        char msg[160];
        sprintf_s(msg, "settings.xml suspiciously small read (%zu bytes); retrying next call", content.size());
        LogMsg(msg);
        return;
    }
    // Firestorm serializes this file as a single LLSD XML document
    // (LLControlGroup::saveToFile -> LLSDSerialize::toPrettyXML) that always
    // ends with a "</llsd>" closing tag. The mtime/size re-check above
    // catches a rewrite still fully in flight when we read, but Windows
    // _stat()'s mtime resolution is coarse enough that a rewrite which
    // starts and finishes within the same tick could still look
    // unchanged to that check alone. Requiring the read content to end
    // with the expected closing tag is a second, content-level guard
    // against exactly that gap: a genuinely complete file must end this
    // way, so anything else (mid-write truncation, this read racing a
    // write that finished a moment later) is rejected here too.
    {
        size_t lastNonSpace = content.find_last_not_of(" \t\r\n");
        std::string tail = (lastNonSpace == std::string::npos)
            ? std::string()
            : content.substr(lastNonSpace >= 6 ? lastNonSpace - 6 : 0, 7);
        if (tail != "</llsd>") {
            LogMsg("settings.xml read did not end with </llsd> (incomplete write); retrying next call");
            return;
        }
    }

    g_settingsModTime = mtime;

    // Firestorm's TranslateChat checkbox is the main fuse. /tr on|off only
    // controls g_userEnabled, so it cannot bypass a disabled checkbox.
    bool settingsEnabled = ReadTranslateChatEnabled(content);
    bool previousSettingsEnabled = g_active.exchange(settingsEnabled);
    if (settingsEnabled != previousSettingsEnabled) {
        LogMsg(settingsEnabled ? "Settings: TranslateChat ON" : "Settings: TranslateChat OFF");
        UpdateOverlayStatus(IsTranslationEnabled());
        PushTranslationStatusAsync(IsTranslationEnabled());
    }

    // Outgoing language ("Send Messages In"): parsed from the endpoint URL
    // on every settings.xml change and re-verified when it actually
    // changes. Firestorm rewrites settings.xml periodically on its own
    // (unrelated fields), so this only acts when the parsed language code
    // itself differs from what we already have — a no-op re-write of the
    // same endpoint value does not trigger a spurious re-verify.
    std::string endpoint = ReadXmlValue(content, "AzureTranslateAPIKey", "endpoint");
    std::string parsedTarget = ParseTargetLangFromEndpoint(endpoint);
    // "My Language" (what I read) lives in a separate setting, TranslateLanguage.
    std::string parsedMyLang = ReadXmlValue(content, "TranslateLanguage", "Value");
    if (!g_runtimeLanguageOverride.load(std::memory_order_acquire)) {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            if (!parsedTarget.empty() && parsedTarget != g_targetLang) {
                g_targetLang = parsedTarget;
                changed = true;
            }
            if (!parsedMyLang.empty() && parsedMyLang != g_myLang) {
                g_myLang = parsedMyLang;
                changed = true;
            }
        }
        if (changed) {
            LogMsg(("Settings: language pair changed (my=" + parsedMyLang +
                    " target=" + parsedTarget + "), re-verifying").c_str());
            std::string keySnapshot, hwidSnapshot;
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                keySnapshot = g_licenseKey;
                hwidSnapshot = g_hwid;
            }
            if ((!keySnapshot.empty() && !hwidSnapshot.empty()) ||
                !GetOwnAgentUuidString().empty()) {
                // Off the hot path: sendto() must not block on a network
                // round-trip. VerifyLicense() opens its own request on the
                // shared persistent connection, which is mutex-guarded, so
                // running it from a detached thread is safe.
                std::thread([keySnapshot, hwidSnapshot]() {
                    VerifyLicense(keySnapshot, hwidSnapshot);
                }).detach();
            }
        }
    }

    // Read license key
    std::string newKey = ReadXmlValue(content, "AzureTranslateAPIKey", "id");
    if (!newKey.empty()) {
        bool keyChanged = false;
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            if (newKey != g_licenseKey) {
                g_licenseKey = newKey;
                keyChanged = true;
            }
        }
        if (keyChanged) {
            LogMsg(("Settings: key updated: " + newKey).c_str());
            g_licenseValid.store(false);
            UpdateOverlayStatus(false);

            std::string hwidSnapshot;
            { std::lock_guard<std::mutex> lock(g_configMutex); hwidSnapshot = g_hwid; }
            if (!hwidSnapshot.empty()) {
                std::thread([newKey, hwidSnapshot]() {
                    if (g_shutdown.load()) return;
                    int result = VerifyLicense(newKey, hwidSnapshot);
                    // Ignore a late result if settings changed to another key
                    // while this request was in flight.
                    {
                        std::lock_guard<std::mutex> lock(g_configMutex);
                        if (g_licenseKey != newKey) return;
                    }
                    if (result == 1) {
                        g_licenseValid.store(true);
                        UpdateOverlayStatus(IsTranslationEnabled());
                        PushTranslationStatusAsync(IsTranslationEnabled());
                    } else if (result < 0) {
                        g_licenseValid.store(false);
                        UpdateOverlayStatus(false);
                        PushTranslationStatusAsync(false);
                    }
                }).detach();
            }
        }
    }
}

static void ReverifyCurrentLanguagePairAsync(const char* reason) {
    std::string keySnapshot, hwidSnapshot;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        keySnapshot = g_licenseKey;
        hwidSnapshot = g_hwid;
    }

    std::string avatarSnapshot = GetOwnAgentUuidString();
    if ((keySnapshot.empty() || hwidSnapshot.empty()) && avatarSnapshot.empty()) {
        LogMsg("Language pair verify deferred: neither legacy key identity nor avatar UUID is available yet");
        return;
    }

    std::string reasonSnapshot = reason ? reason : "language pair change";
    std::thread([keySnapshot, hwidSnapshot, reasonSnapshot]() {
        if (g_shutdown.load()) return;
        LogMsg((reasonSnapshot + ": sending UUID + language pair verify").c_str());
        VerifyLicense(keySnapshot, hwidSnapshot);
    }).detach();
}

// ==========================================
// Shared /tr command handler
// ==========================================
// Runs the same command logic whether the text arrived from the user's own
// outgoing chat line (Hooked_sendto) or from a HUD's llOwnerSay (recvfrom,
// CHAT_TYPE_OWNER only — see InspectChatFromSimulatorPacket). llOwnerSay is
// only ever delivered to the object's owner, so reaching this from that path
// already proves the sending object belongs to the local user; no extra
// identity check is needed here.
static bool ProcessTrCommand(const std::string& cmdRaw) {
    // Trim trailing/leading whitespace and normalize to lowercase before
    // comparing. Firestorm's chat entry can leave a trailing space or the
    // client may send a slightly different byte sequence than expected;
    // without this, "/tr off " (with a trailing space) or "/TR OFF" would
    // silently fail the exact-string checks below and fall through as plain
    // text instead of actually toggling anything — this was reported as
    // "/tr on|off doesn't reliably take effect".
    size_t trimStart = cmdRaw.find_first_not_of(" \t");
    size_t trimEnd = cmdRaw.find_last_not_of(" \t");
    std::string cmdTrimmed = (trimStart == std::string::npos)
        ? std::string()
        : cmdRaw.substr(trimStart, trimEnd - trimStart + 1);
    std::string cmdLower = LowerAscii(cmdTrimmed);
    // Force-write every /tr command attempt to the log unconditionally
    // (regardless of g_debugLog), with the raw and normalized text side by
    // side. Recognized/unrecognized outcome and g_active before/after are
    // reported by the branches below, but if debug happens to be OFF when
    // the user runs "/tr off", the rest of this function's LogMsg() calls
    // are silently dropped — leaving zero evidence in slt_debug.log that
    // the command was even received. That gap made a real "/tr off doesn't
    // reliably take effect" report impossible to diagnose from the log.
    {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "slt_debug.log";
        std::ofstream f(path, std::ios::app);
        f << "TR_CMD received: raw='" << cmdRaw << "' normalized='" << cmdLower << "'" << std::endl;
    }
    bool recognized = true;
    if (cmdLower == "/tr lang auto") {
        g_runtimeLanguageOverride.store(false, std::memory_order_release);
        // Force the next settings check to consume the on-disk pair even if
        // Firestorm has not rewritten settings.xml since the HUD override.
        g_settingsModTime = 0;
        CheckSettingsXmlUpdate();
        LogMsg("HUD language override cleared; using Firestorm settings.xml again");
        ReverifyCurrentLanguagePairAsync("HUD language auto");
    } else if (cmdLower == "/tr lang" || cmdLower.rfind("/tr lang ", 0) == 0) {
        std::istringstream words(cmdLower.size() > 9 ? cmdLower.substr(9) : std::string());
        std::string myRaw, targetRaw, extra;
        std::string myLanguage, targetLanguage;
        bool valid = (words >> myRaw >> targetRaw) && !(words >> extra) &&
            NormalizeTwoLetterLanguageCode(myRaw, myLanguage) &&
            NormalizeTwoLetterLanguageCode(targetRaw, targetLanguage);

        if (!valid) {
            LogMsg("Invalid HUD language command; expected: /tr lang <my> <target> (example: /tr lang tr en)");
        } else {
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                g_myLang = myLanguage;
                g_targetLang = targetLanguage;
            }
            g_languageOverrideGeneration.fetch_add(1);
            g_runtimeLanguageOverride.store(true, std::memory_order_release);

            std::string state = "HUD language pair active: my=" + myLanguage +
                " target=" + targetLanguage;
            if (!g_liveLanguageHooksInstalled.load()) {
                state += " (server verify active, Firestorm live hooks unavailable)";
            }
            LogMsg(state.c_str());
            ReverifyCurrentLanguagePairAsync("HUD language verify");
        }
    } else if (cmdLower == "/tr off") {
        g_userEnabled.store(false);
        LogMsg("Translation command switch OFF");
        UpdateOverlayStatus(false);
        // Tell the server too — Firestorm's own Azure translate request
        // never touches this DLL's network hooks, so this is the only
        // place left where "off" can actually stop the server from
        // running it through the model. See PushTranslationStatusAsync's
        // declaration comment.
        PushTranslationStatusAsync(false);
    } else if (cmdLower == "/tr on") {
        g_userEnabled.store(true);
        bool effectiveOn = IsTranslationEnabled();
        LogMsg(effectiveOn ? "Translation command switch ON" :
            "Translation command switch ON, but checkbox/licence gate is OFF");
        UpdateOverlayStatus(effectiveOn);
        PushTranslationStatusAsync(effectiveOn);
    } else if (cmdLower == "/tr src on") {
        g_includeOriginal.store(true);
        LogMsg("Include-original ON (outgoing text will carry original + translation, server decides fit)");
    } else if (cmdLower == "/tr src off") {
        g_includeOriginal.store(false);
        LogMsg("Include-original OFF");
    } else if (cmdLower == "/tr debug on" || cmdLower == "/tr debug off" || cmdLower == "/tr debug") {
        // Explicit on/off avoids the ambiguity of a toggle: if a command is
        // ever missed (chat lag, duplicate send), a toggle silently leaves
        // debug in the wrong state with no way to tell from chat alone.
        // "/tr debug" (no argument) is kept as a toggle for backward
        // compatibility only.
        bool d;
        if (cmdLower == "/tr debug on") d = true;
        else if (cmdLower == "/tr debug off") d = false;
        else d = !g_debugLog.load();
        g_debugLog.store(d);
        // Force-write this one line even if debug was OFF, so the user gets
        // confirmation the command was received.
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "slt_debug.log";
        std::ofstream f(path, std::ios::app);
        f << (d ? "Debug ON" : "Debug OFF") << std::endl;
        // Debug toggle has no visual overlay effect; log line above is enough.
    } else if (cmdLower == "/tr status") {
        bool isOn = IsTranslationEnabled();
        long long daysLeft = g_daysRemaining.load();
        // std::to_string's returned temporary would be destroyed before
        // sprintf_s reads its .c_str() pointer below (dangling-pointer bug),
        // so materialize it into a named std::string that outlives the call.
        std::string daysLeftStr = daysLeft >= 0 ? std::to_string(daysLeft) : std::string("unknown");
        char st[560];
        std::string keySnapshot;
        std::string myLanguage, targetLanguage;
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            keySnapshot = g_licenseKey;
            myLanguage = g_myLang;
            targetLanguage = g_targetLang;
        }
        std::string avatarSnapshot = GetOwnAgentUuidString();
        if (avatarSnapshot.empty()) avatarSnapshot = "unknown";
        sprintf_s(st, "SLT: %s (checkbox=%s command=%s license=%s includeOriginal=%s) | Pair: %s>%s (%s, hooks=%s) | Key: %.8s... | Days left: %s | UUID: %s",
            isOn ? "ON" : "OFF",
            g_active.load() ? "1" : "0",
            g_userEnabled.load() ? "1" : "0",
            g_licenseValid.load() ? "1" : "0",
            g_includeOriginal.load() ? "1" : "0",
            myLanguage.c_str(), targetLanguage.c_str(),
            g_runtimeLanguageOverride.load() ? "HUD" : "settings",
            g_liveLanguageHooksInstalled.load() ? "ready" : "missing",
            keySnapshot.c_str(),
            daysLeftStr.c_str(),
            avatarSnapshot.c_str());
        // Always write status to log too, regardless of debug flag.
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "slt_debug.log";
        std::ofstream f(path, std::ios::app);
        f << st << std::endl;

        UpdateOverlayStatus(isOn);
    } else {
        // Unrecognized "/tr ..." text (typo, or the user actually means to
        // say something starting with "/tr" that isn't a command).
        recognized = false;
        LogMsg(("Unrecognized /tr command: " + cmdRaw).c_str());
    }
    return recognized;
}

// ==========================================
// Hooked sendto
// ==========================================
int WSAAPI Hooked_sendto(SOCKET s, const char* buf, int len, int flags, const struct sockaddr* to, int tolen) {
    CheckSettingsXmlUpdate();
    ObserveOutgoingAgentIdentity(buf, len);

    // /tr chat commands must always be reachable, even when g_active is
    // false (e.g. user never pressed Save & Apply in the settings floater,
    // or a heartbeat/verify failure turned translation off). This block
    // runs BEFORE the g_active gate below so the commands can actually
    // flip g_active themselves.
    if (buf && len >= 10) {
        SlPacketView packet;
        std::string cmdText;
        int cmdChannel = 0;
        if (DecodeSlPacket(buf, len, packet) &&
            ExtractChatFromViewer(packet, cmdText, cmdChannel) &&
            cmdChannel == 0 && cmdText.size() >= 3 && strncmp(cmdText.c_str(), "/tr", 3) == 0) {
            bool recognized = ProcessTrCommand(cmdText);
            if (recognized) {
                // Recognized /tr commands are for the local client only —
                // never let them reach the sim/chat, so nobody else sees
                // "/tr on" typed in local chat and the message doesn't
                // get routed through Firestorm's own translation path.
                return len;
            }
            // Fall through: not a real /tr command, let it continue as
            // a normal chat message (translated if active, same as any
            // other text).
        }
    }

    if (!IsTranslationEnabled() || !buf || len < 50)
        return g_origSendTo(s, buf, len, flags, to, tolen);

    // ChatFromViewer
    {
        SlPacketView packet;
        std::string chatText;
        int channel = 0;
        if (DecodeSlPacket(buf, len, packet) &&
            ExtractChatFromViewer(packet, chatText, channel) &&
            channel == 0 && !chatText.empty() && chatText[0] != '/' && g_userEnabled.load()) {
            LogMsg(("CHAT: " + chatText).c_str());
            StartSendWorker();
            if (EnqueueSendTo(s, buf, len, flags, to, tolen)) return len;
        }
    }

    // ImprovedInstantMessage
    {
        SlPacketView packet;
        if (DecodeSlPacket(buf, len, packet) &&
            PacketMatchesLow(packet, SL_MSG_IMPROVED_IM) &&
            g_userEnabled.load()) {
            StartSendWorker();
            if (EnqueueSendTo(s, buf, len, flags, to, tolen)) return len;
        }
    }

    return g_origSendTo(s, buf, len, flags, to, tolen);
}

int WSAAPI Hooked_recvfrom(SOCKET s, char* buf, int len, int flags, struct sockaddr* from, int* fromlen) {
    if (!g_origRecvfrom) {
        SetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    int received = g_origRecvfrom(s, buf, len, flags, from, fromlen);
    if (received > 10 && buf) {
        InspectChatFromSimulatorPacket(buf, received);
    }
    return received;
}

// ==========================================
// Auto-update from GitHub releases
// ==========================================
static void CheckForUpdate() {
    LogMsg("Checking for updates...");

    // Query GitHub API for latest release
    HINTERNET hSession = WinHttpOpen(L"SLT-Update/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return;
    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
        L"/repos/sltranslator-pro/sl-translator-pro/releases/latest",
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return; }

    DWORD timeout = 10000;
    WinHttpSetTimeouts(hRequest, timeout, timeout, timeout, timeout);
    WinHttpSendRequest(hRequest, L"User-Agent: SLT\r\n", (DWORD)-1L, NULL, 0, 0, 0);
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

    if (result.empty()) { LogMsg("Update check failed — no response"); return; }

    // Parse tag_name (version)
    std::string latestVersion = JsonGet(result, "tag_name");
    if (latestVersion.empty()) { LogMsg("Update check — no tag_name"); return; }
    // Remove 'v' prefix: "v1.4.3" → "1.4.3"
    if (latestVersion[0] == 'v') latestVersion = latestVersion.substr(1);

    LogMsg(("Current: " + std::string(SLT_VERSION) + " Latest: " + latestVersion).c_str());

    // Only offer an update when the remote version is STRICTLY NEWER than the
    // running one. Never prompt for an equal or older ("downgrade") version.
    auto parseVer = [](const std::string& v) {
        int a = 0, b = 0, c = 0;
        sscanf_s(v.c_str(), "%d.%d.%d", &a, &b, &c);
        return (a * 1000000) + (b * 1000) + c;
    };
    if (parseVer(latestVersion) <= parseVer(SLT_VERSION)) { LogMsg("Up to date"); return; }

    // Find download URL (browser_download_url)
    size_t urlPos = result.find("browser_download_url");
    if (urlPos == std::string::npos) { LogMsg("No download URL found"); return; }
    std::string downloadUrl = JsonGet(result, "browser_download_url");
    if (downloadUrl.empty()) return;

    LogMsg(("Update available: " + downloadUrl).c_str());

    // Ask user
    char msg[512];
    sprintf_s(msg, "SL Translator Pro update available!\n\nCurrent: v%s\nNew: v%s\n\nUpdate now? Firestorm will restart.", SLT_VERSION, latestVersion.c_str());
    int choice = MessageBoxA(NULL, msg, "SL Translator Pro Update", MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST);
    if (choice != IDYES) { LogMsg("Update declined"); return; }

    // Download installer to %TEMP%
    LogMsg("Downloading update...");

    // Parse host and path from downloadUrl
    // URL format: https://github.com/.../SLTranslatorPro_Setup_vX.X.X.exe
    std::wstring wUrl(downloadUrl.begin(), downloadUrl.end());

    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostBuf[256] = {};
    wchar_t pathBuf[1024] = {};
    urlComp.lpszHostName = hostBuf;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = pathBuf;
    urlComp.dwUrlPathLength = 1024;
    WinHttpCrackUrl(wUrl.c_str(), 0, 0, &urlComp);

    hSession = WinHttpOpen(L"SLT-Update/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return;
    hConnect = WinHttpConnect(hSession, urlComp.lpszHostName, urlComp.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return; }
    hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComp.lpszUrlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return; }

    WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);

    // Handle redirects (GitHub uses 302)
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &statusCode, &statusSize, NULL);

    if (statusCode == 302 || statusCode == 301) {
        wchar_t redirectUrl[2048] = {};
        DWORD redirectSize = sizeof(redirectUrl);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, NULL, redirectUrl, &redirectSize, NULL);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        // Follow redirect
        URL_COMPONENTS rComp = {};
        rComp.dwStructSize = sizeof(rComp);
        wchar_t rHost[256] = {};
        wchar_t rPath[2048] = {};
        rComp.lpszHostName = rHost;
        rComp.dwHostNameLength = 256;
        rComp.lpszUrlPath = rPath;
        rComp.dwUrlPathLength = 2048;
        WinHttpCrackUrl(redirectUrl, 0, 0, &rComp);

        hSession = WinHttpOpen(L"SLT-Update/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
        hConnect = WinHttpConnect(hSession, rComp.lpszHostName, rComp.nPort, 0);
        hRequest = WinHttpOpenRequest(hConnect, L"GET", rComp.lpszUrlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0);
        WinHttpReceiveResponse(hRequest, NULL);
    }

    // Save to temp
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string installerPath = std::string(tempPath) + "SLTranslatorPro_Setup.exe";

    FILE* fp = NULL;
    if (fopen_s(&fp, installerPath.c_str(), "wb") != 0 || !fp) { LogMsg("Failed to create temp file"); WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return; }

    do {
        WinHttpQueryDataAvailable(hRequest, &sz);
        if (sz == 0) break;
        std::vector<char> buf(sz);
        DWORD read = 0;
        WinHttpReadData(hRequest, buf.data(), sz, &read);
        fwrite(buf.data(), 1, read, fp);
    } while (sz > 0);

    fclose(fp);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    LogMsg(("Installer downloaded to: " + installerPath).c_str());

    // Get Firestorm exe path
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    // Create batch script for update
    std::string batPath = std::string(tempPath) + "slt_update.bat";
    std::ofstream bat(batPath);
    bat << "@echo off\r\n";
    bat << "echo Updating SL Translator Pro...\r\n";
    bat << "timeout /t 3 /nobreak >nul\r\n";
    bat << "\"" << installerPath << "\" /S\r\n";
    bat << "timeout /t 3 /nobreak >nul\r\n";
    bat << "start \"\" \"" << exePath << "\"\r\n";
    bat << "del \"%~f0\"\r\n";
    bat.close();

    // Launch batch and exit Firestorm
    LogMsg("Launching updater...");
    ShellExecuteA(NULL, "open", batPath.c_str(), NULL, NULL, SW_HIDE);
    ExitProcess(0);
}

// ==========================================
// Init thread
// ==========================================
static void InitThread(HMODULE hModule) {
    (void)hModule;
    // This proxy is designed to live for the whole Firestorm process. Pinning
    // prevents an unexpected FreeLibrary from unloading code while detached
    // worker threads are still executing. The OS still reclaims it at process
    // exit.
    HMODULE pinnedSelf = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&InitThread), &pinnedSelf);

    LogMsg("=== SL Translator DLL Init v" SLT_VERSION " ===");

    // Install UDP hooks immediately so UseCircuitCode (Low 3) and the later
    // AgentUpdate (High 4) stream can teach us the local avatar UUID. Licence
    // verify used to run first and therefore always omitted it.
    char buf[512];
    MH_STATUS st = MH_Initialize();
    sprintf_s(buf, "MH_Initialize: %d", st);
    LogMsg(buf);
    if (st != MH_OK) return;

    HMODULE hWs2 = GetModuleHandleA("ws2_32.dll");
    if (!hWs2) hWs2 = LoadLibraryA("ws2_32.dll");
    void* pSendTo = GetProcAddress(hWs2, "sendto");
    void* pRecvFrom = GetProcAddress(hWs2, "recvfrom");
    sprintf_s(buf, "ws2_32!sendto at %p, recvfrom at %p", pSendTo, pRecvFrom);
    LogMsg(buf);

    st = MH_CreateHook(pSendTo, (LPVOID)Hooked_sendto, (LPVOID*)&g_origSendTo);
    sprintf_s(buf, "MH_CreateHook(sendto): %d", st);
    LogMsg(buf);

    st = MH_EnableHook(pSendTo);
    sprintf_s(buf, "MH_EnableHook(sendto): %d", st);
    LogMsg(buf);

    if (pRecvFrom) {
        st = MH_CreateHook(pRecvFrom, (LPVOID)Hooked_recvfrom, (LPVOID*)&g_origRecvfrom);
        sprintf_s(buf, "MH_CreateHook(recvfrom): %d", st);
        LogMsg(buf);

        st = MH_EnableHook(pRecvFrom);
        sprintf_s(buf, "MH_EnableHook(recvfrom): %d", st);
        LogMsg(buf);
    }

    // Setup WinHTTP hooks — intercept Azure translation requests
    HMODULE hWinHttp = GetModuleHandleA("winhttp.dll");
    if (!hWinHttp) hWinHttp = LoadLibraryA("winhttp.dll");
    if (hWinHttp) {
        void* pOpenReq = GetProcAddress(hWinHttp, "WinHttpOpenRequest");
        void* pSendReq = GetProcAddress(hWinHttp, "WinHttpSendRequest");
        void* pClose = GetProcAddress(hWinHttp, "WinHttpCloseHandle");
        sprintf_s(buf, "WinHttpOpenRequest at %p, WinHttpSendRequest at %p, WinHttpCloseHandle at %p",
            pOpenReq, pSendReq, pClose);
        LogMsg(buf);

        if (pOpenReq) {
            st = MH_CreateHook(pOpenReq, (LPVOID)Hooked_WinHttpOpenRequest, (LPVOID*)&g_origWinHttpOpenRequest);
            sprintf_s(buf, "MH_CreateHook(WinHttpOpenRequest): %d", st);
            LogMsg(buf);
            MH_EnableHook(pOpenReq);
        }
        if (pSendReq) {
            st = MH_CreateHook(pSendReq, (LPVOID)Hooked_WinHttpSendRequest, (LPVOID*)&g_origWinHttpSendRequest);
            sprintf_s(buf, "MH_CreateHook(WinHttpSendRequest): %d", st);
            LogMsg(buf);
            MH_EnableHook(pSendReq);
        }
        if (pClose) {
            st = MH_CreateHook(pClose, (LPVOID)Hooked_WinHttpCloseHandle, (LPVOID*)&g_origWinHttpCloseHandle);
            sprintf_s(buf, "MH_CreateHook(WinHttpCloseHandle): %d", st);
            LogMsg(buf);
            MH_EnableHook(pClose);
        }
    }

    LogMsg("=== Hooks active ===");

    // Give Firestorm a few seconds to emit UseCircuitCode / AgentUpdate so
    // the first licence verify can carry avatar_uuid when it is already known.
    if (!SleepUntilShutdown(5000)) return;

    InitOverlay();

    // Local-only experiment: translate real avatar local chat, but leave
    // object/HUD/game output untouched. Structural scan (register/offset
    // independent, verified across Firestorm 7.1.11-7.2.4) makes this
    // effective on any Firestorm build compiling that C++ shape unchanged;
    // it fails closed (no bytes touched) on unrecognised builds.
    InstallAvatarOnlyLocalChatPatch();

    // Firestorm reads the incoming language and Azure endpoint from its live
    // control objects, but the Translation floater only writes those controls
    // when the user presses OK. These fail-closed hooks let a HUD Verify action
    // apply the selected pair immediately for the current session.
    InstallFirestormLiveLanguageHooks();

    // Check for updates
    CheckForUpdate();

    std::string initialHwid = GetHWID();
    std::string initialSession = GenerateSessionId();
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        g_hwid = initialHwid;
        g_sessionId = initialSession;
    }
    LogMsg(("HWID: " + initialHwid).c_str());
    LogMsg(("Session: " + initialSession).c_str());

    // Read license key and outgoing language from Firestorm's settings.xml
    std::string settingsPath = FindFirestormSettings();
    if (!settingsPath.empty()) {
        std::ifstream sf(settingsPath);
        if (sf) {
            std::string sc((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
            sf.close();
            std::string initialKey = ReadXmlValue(sc, "AzureTranslateAPIKey", "id");
            std::string endpoint = ReadXmlValue(sc, "AzureTranslateAPIKey", "endpoint");
            std::string parsedTarget = ParseTargetLangFromEndpoint(endpoint);
            std::string parsedMyLang = ReadXmlValue(sc, "TranslateLanguage", "Value");
            g_active.store(ReadTranslateChatEnabled(sc));
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                g_licenseKey = initialKey;
                if (!parsedTarget.empty()) g_targetLang = parsedTarget;
                if (!parsedMyLang.empty()) g_myLang = parsedMyLang;
            }
            if (!parsedTarget.empty() || !parsedMyLang.empty())
                LogMsg(("Initial language pair: my=" + parsedMyLang + " target=" + parsedTarget).c_str());
        }
    }
    std::string initialKey;
    { std::lock_guard<std::mutex> lock(g_configMutex); initialKey = g_licenseKey; }
    if (initialKey.empty()) {
        LogMsg("No license key in settings.xml — use SL Translator > Settings to enter key");
    } else {
        LogMsg(("License key: " + initialKey).c_str());
    }
    // Outgoing language controlled by server

    // Verify license if key found
    if (!initialKey.empty() && !initialHwid.empty()) {
        LogMsg("Verifying license...");
        int result = VerifyLicense(initialKey, initialHwid);
        if (result == 1) {
            g_licenseValid.store(true);
            LogMsg("License OK");

            // Compute remaining days and show it in the window title suffix
            // (e.g. "[Translate: ON, 12d left]") instead of a popup dialog —
            // it's visible without interrupting the user, and stays visible
            // the whole session instead of a one-shot message box.
            if (g_expires > 0) {
                FILETIME ft; GetSystemTimeAsFileTime(&ft);
                long long nowMs = ((long long)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10000 - 11644473600000LL;
                long long remainingMs = g_expires - nowMs;
                long long remainingDays = remainingMs > 0 ? remainingMs / (24LL * 60 * 60 * 1000) : 0;
                g_daysRemaining.store(remainingDays);
                LogMsg(("License days remaining: " + std::to_string(remainingDays)).c_str());
            }

            UpdateOverlayStatus(IsTranslationEnabled());
            PushTranslationStatusAsync(IsTranslationEnabled());
        } else {
            g_licenseValid.store(false);
            LogMsg("License verification failed");
            UpdateOverlayStatus(false);
            if (result < 0) PushTranslationStatusAsync(false);
        }
    }

    // Heartbeat thread
    std::thread([]() {
        while (SleepUntilShutdown(5 * 60 * 1000)) {
            std::string keySnapshot, hwidSnapshot;
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                keySnapshot = g_licenseKey;
                hwidSnapshot = g_hwid;
            }
            if (keySnapshot.empty()) {
                // Try reading key again (user may have entered it via floater)
                std::string newKey = LoadLicenseKeyFromSettings();
                if (!newKey.empty()) {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (newKey != g_licenseKey) {
                        g_licenseKey = newKey;
                        g_licenseValid.store(false);
                        LogMsg(("New key detected: " + newKey).c_str());
                    }
                    keySnapshot = g_licenseKey;
                    hwidSnapshot = g_hwid;
                }
            }
            if (!keySnapshot.empty() && !hwidSnapshot.empty()) {
                LogMsg("Heartbeat: verifying...");
                int r = VerifyLicense(keySnapshot, hwidSnapshot);
                if (r == 1) {
                    g_licenseValid.store(true);
                    LogMsg("Heartbeat: license OK");
                    UpdateOverlayStatus(IsTranslationEnabled());
                    PushTranslationStatusAsync(IsTranslationEnabled());
                } else if (r < 0) {
                    g_licenseValid.store(false);
                    LogMsg("Heartbeat: license FAILED — translation disabled");
                    UpdateOverlayStatus(false);
                    PushTranslationStatusAsync(false);
                } else {
                    // Transport/server outage. Keep the last known licence
                    // decision and retry on the next heartbeat.
                    LogMsg("Heartbeat: transient verify failure — retaining prior state");
                }
            }
        }
    }).detach();
}

// ==========================================
// DllMain
// ==========================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_shutdown.store(false);
        DisableThreadLibraryCalls(hModule);

        // Load original dinput8.dll
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
        g_shutdown.store(true);
        StopSendWorker();
        // lpReserved != NULL means process termination. Avoid loader-lock
        // cleanup there; Windows is reclaiming all process resources anyway.
        // Explicit unload is prevented by the module pin above, but retain a
        // conservative cleanup path in case initialization never reached it.
        if (lpReserved == NULL) {
            RemoveAvatarOnlyLocalChatPatch();
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            ResetHttpConnection();
            if (g_hOrigVersion) FreeLibrary(g_hOrigVersion);
            if (g_fsMainWnd && g_titleCaptured.load()) {
                SetWindowTextW(g_fsMainWnd, g_origWindowTitle.c_str());
            }
        }
    }
    return TRUE;
}
