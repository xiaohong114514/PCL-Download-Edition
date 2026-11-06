// PCL-Download-Edition (PCL DE) - rewritten UI
// C++17 / Win32 API / WinHTTP / nlohmann/json
// Style: PCL2-like white sidebar + blue accent.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shldisp.h>
#include <shlobj.h>

#include <string>
#include <vector>
#include <functional>
#include <fstream>
#include <sstream>
#include <atomic>
#include <algorithm>

#include "nlohmann/json.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

using json = nlohmann::json;

// ---- colors (PCL-like) ----
#define CLR_BG        RGB(245, 247, 250)
#define CLR_SIDEBAR   RGB(255, 255, 255)
#define CLR_ACCENT    RGB(0, 120, 215)   // #0078D7
#define CLR_ACCENT_LT RGB(229, 243, 255) // light blue selected bg
#define CLR_TEXT      RGB(30, 30, 30)
#define CLR_TEXT_DIM  RGB(120, 120, 120)

// ---- control IDs ----
#define ID_NAV_DOWNLOAD   1001
#define ID_NAV_INSTANCE    1002
#define ID_NAV_ABOUT       1003
#define ID_LIST_VERSIONS   1101
#define ID_BTN_REFRESH     1102
#define ID_BTN_DOWNLOAD    1103
#define ID_PROGRESS        1104
#define ID_STATUS          1105
#define ID_FILTER          1106
#define ID_LOADER          1107
#define ID_BTN_CANCEL      1108
#define ID_CUSTOM_NAME     1109
#define ID_BTN_RENAME      1205
#define ID_LIST_INSTANCES  1201
#define ID_BTN_OPENFOLDER  1202
#define ID_BTN_DELINST     1203
#define ID_BTN_REFRESHINST 1204

#define WM_APP_VERSIONS_READY (WM_APP + 10)
#define WM_APP_PROGRESS       (WM_APP + 11)
#define WM_APP_STATUS         (WM_APP + 12)
#define WM_APP_DONE           (WM_APP + 13)

#define SIDEBAR_W 180

// ---- globals ----
static HINSTANCE g_hInst = NULL;
static HWND g_hMain = NULL;

static HWND g_hNav[3] = {0}; // download, instance, about
static HWND g_hTitle = NULL;

static HWND g_hLstVer=NULL, g_hBtnRefresh=NULL, g_hBtnDownload=NULL;
static HWND g_hProg=NULL, g_hStatus=NULL, g_hFilter=NULL, g_hLblFilter=NULL;
static HWND g_hLoader=NULL, g_hLblLoader=NULL, g_hBtnCancel=NULL;
static HWND g_hCustomName=NULL, g_hLblName=NULL;
static std::atomic<bool> g_cancel{false};
static std::wstring g_pendingName;

static HWND g_hLstInst=NULL, g_hBtnOpen=NULL, g_hBtnDel=NULL, g_hBtnRefreshInst=NULL;
static HWND g_hAboutText=NULL;

static int g_currentPage = 0;

struct McVersion {
    std::wstring id, type, releaseTime, url;
};
static std::vector<McVersion> g_versions;
static CRITICAL_SECTION g_versionsMtx;
static std::vector<std::wstring> g_instances;

static std::wstring g_statusText;
static std::atomic<bool> g_downloading{false};

static HFONT g_fontUI=NULL, g_fontTitle=NULL, g_fontBold=NULL;

// ---- helpers ----
static std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}
static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH]; GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p(buf);
    size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? L"." : p.substr(0, i);
}
static std::wstring GetWorkDir() { return GetExeDir() + L"\\.minecraft"; }
static void MakeDirRecursive(const std::wstring& path) {
    std::wstring cur;
    for (size_t i = 0; i < path.size(); i++) {
        cur += path[i];
        if (path[i] == L'\\' || path[i] == L'/' || i == path.size()-1)
            CreateDirectoryW(cur.c_str(), NULL);
    }
}
static void ReplaceAll(std::wstring& s, const std::wstring& a, const std::wstring& b) {
    size_t p = 0;
    while ((p = s.find(a, p)) != std::wstring::npos) { s.replace(p, a.size(), b); p += b.size(); }
}
static std::wstring BmclRewrite(std::wstring u) {
    ReplaceAll(u, L"https://piston-meta.mojang.com",   L"https://bmclapi2.bangbang93.com");
    ReplaceAll(u, L"https://launchermeta.mojang.com", L"https://bmclapi2.bangbang93.com");
    ReplaceAll(u, L"https://launcher.mojang.com",      L"https://bmclapi2.bangbang93.com");
    ReplaceAll(u, L"https://piston-data.mojang.com",    L"https://bmclapi2.bangbang93.com");
    ReplaceAll(u, L"https://libraries.minecraft.net",  L"https://bmclapi2.bangbang93.com/maven");
    ReplaceAll(u, L"http://libraries.minecraft.net",    L"https://bmclapi2.bangbang93.com/maven");
    ReplaceAll(u, L"https://resources.download.minecraft.net", L"https://bmclapi2.bangbang93.com/assets");
    ReplaceAll(u, L"http://resources.download.minecraft.net",  L"https://bmclapi2.bangbang93.com/assets");
    return u;
}

// ---- HTTP ----
struct HttpResult { bool ok=false; std::vector<BYTE> data; std::wstring err; };

static HttpResult HttpGet(const std::wstring& url) {
    HttpResult r;
    std::wstring curUrl = url;
    for (int redirects = 0; redirects < 6; redirects++) {
        URL_COMPONENTSW uc = { sizeof(uc) };
        wchar_t host[256]={0}, path[2048]={0};
        uc.lpszHostName=host; uc.dwHostNameLength=255;
        uc.lpszUrlPath=path;  uc.dwUrlPathLength=2047;
        if (!WinHttpCrackUrl(curUrl.c_str(), 0, 0, &uc)) {
            r.err = L"URL解析失败"; return r;
        }
        bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        HINTERNET hS = WinHttpOpen(L"PCLDE/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hS) { r.err=L"WinHttpOpen失败"; return r; }
        DWORD t=20000; WinHttpSetTimeouts(hS,t,t,t,t);
        HINTERNET hC = WinHttpConnect(hS, host, uc.nPort, 0);
        HINTERNET hR = WinHttpOpenRequest(hC, L"GET", path, NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, isHttps ? WINHTTP_FLAG_SECURE : 0);
        if (hR) {
            DWORD protos = 0;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
            protos |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#endif
            if (protos) WinHttpSetOption(hR, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
            DWORD sec = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                        SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(hR, WINHTTP_OPTION_SECURITY_FLAGS, &sec, sizeof(sec));
            DWORD rp = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption(hR, WINHTTP_OPTION_REDIRECT_POLICY, &rp, sizeof(rp));

            if (WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,0,0,0) &&
                WinHttpReceiveResponse(hR, NULL)) {
                DWORD sc=0,sl=sizeof(sc);
                WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sl, WINHTTP_NO_HEADER_INDEX);
                if (sc==200) {
                    std::vector<BYTE> buf; DWORD avail=0;
                    while (WinHttpQueryDataAvailable(hR, &avail) && avail>0) {
                        std::vector<BYTE> chunk(avail); DWORD rd=0;
                        if (!WinHttpReadData(hR, chunk.data(), avail, &rd) || rd==0) break;
                        chunk.resize(rd); buf.insert(buf.end(), chunk.begin(), chunk.end());
                    }
                    r.ok=true; r.data=std::move(buf);
                    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
                    return r;
                }
                if (sc==301||sc==302||sc==307||sc==308) {
                    wchar_t loc[2048]={0}; DWORD ll=2047;
                    if (WinHttpQueryHeaders(hR, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                            loc, &ll, WINHTTP_NO_HEADER_INDEX)) {
                        curUrl = loc;
                        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
                        continue;
                    }
                }
                r.err = L"HTTP "+std::to_wstring(sc);
            } else {
                r.err = L"请求失败("+std::to_wstring(GetLastError())+L")";
            }
            WinHttpCloseHandle(hR);
        }
        WinHttpCloseHandle(hC);
        WinHttpCloseHandle(hS);
    }
    if (r.err.empty()) r.err = L"重定向过多";
    return r;
}
static bool WriteFileW(const std::wstring& path, const std::vector<BYTE>& data) {
    size_t sl = path.find_last_of(L"\\/");
    if (sl != std::wstring::npos) MakeDirRecursive(path.substr(0,sl));
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w=0; if (!data.empty()) WriteFile(h, data.data(), (DWORD)data.size(), &w, NULL);
    CloseHandle(h); return w == data.size();
}

// Extract a zip (or jar) to destDir using tar.exe (built into Win10+)
static bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir) {
    MakeDirRecursive(destDir);
    std::wstring cmd = L"tar.exe -xf \"" + zipPath + L"\" -C \"" + destDir + L"\"";
    STARTUPINFOW si={sizeof(si)}; PROCESS_INFORMATION pi={0};
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code=0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return code == 0;
}

// ---- UI ----
static void SetStatus(const std::wstring& s) {
    g_statusText = s;
    if (g_hMain) PostMessage(g_hMain, WM_APP_STATUS, 0, 0);
}
static inline LPARAM GetItemData(HWND list, int i) {
    LVITEMW it={0}; it.mask=LVIF_PARAM; it.iItem=i;
    SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)&it); return it.lParam;
}

void RefreshVersionList();
void RefreshInstanceList();

static void SwitchPage(int page) {
    g_currentPage = page;
    InvalidateRect(g_hMain, NULL, TRUE);
    BOOL dl=(page==0), inst=(page==1), ab=(page==2);
    ShowWindow(g_hLstVer,      dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hBtnRefresh,  dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hBtnDownload, dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hBtnCancel,   dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hProg,        dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hStatus,      dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hFilter,      dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hLblFilter,   dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hLoader,      dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hLblLoader,   dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hCustomName,  dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hLblName,     dl?SW_SHOW:SW_HIDE);
    ShowWindow(g_hLstInst,       inst?SW_SHOW:SW_HIDE);
    ShowWindow(g_hBtnOpen,        inst?SW_SHOW:SW_HIDE);
    ShowWindow(g_hBtnDel,         inst?SW_SHOW:SW_HIDE);
    ShowWindow(g_hBtnRefreshInst, inst?SW_SHOW:SW_HIDE);
    ShowWindow(g_hAboutText, ab?SW_SHOW:SW_HIDE);
    if (page==1) RefreshInstanceList();
}

// ---- threads ----
static DWORD WINAPI FetchVersionsThread(LPVOID) {
    SetStatus(L"正在从 BMCLAPI 获取版本列表…");
    HttpResult r = HttpGet(L"https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json");
    if (!r.ok) {
        SetStatus(L"获取版本列表失败: "+r.err);
        MessageBoxW(g_hMain, (L"无法连接 BMCLAPI。\n错误: "+r.err+
            L"\n\n请检查网络后点击「刷新列表」重试。").c_str(),
            L"PCL DE - 网络错误", MB_OK|MB_ICONERROR);
        PostMessage(g_hMain, WM_APP_VERSIONS_READY, 0, 0);
        return 0;
    }
    try {
        json j = json::parse(std::string(r.data.begin(), r.data.end()));
        std::vector<McVersion> list;
        for (auto& v : j["versions"]) {
            McVersion mv;
            mv.id = Utf8ToW(v.value("id",""));
            mv.type = Utf8ToW(v.value("type",""));
            mv.releaseTime = Utf8ToW(v.value("releaseTime",""));
            mv.url = BmclRewrite(Utf8ToW(v.value("url","")));
            list.push_back(mv);
        }
        EnterCriticalSection(&g_versionsMtx);
        g_versions = std::move(list);
        LeaveCriticalSection(&g_versionsMtx);
        SetStatus(L"就绪。共 "+std::to_wstring(g_versions.size())+L" 个版本，在筛选框输入版本号即可过滤。");
    } catch (const std::exception& e) {
        SetStatus(L"解析失败: "+Utf8ToW(e.what()));
    }
    PostMessage(g_hMain, WM_APP_VERSIONS_READY, 0, 0);
    return 0;
}

static bool RulesAllow(const json& lib) {
    if (!lib.contains("rules")) return true;
    bool allow=false;
    for (auto& r : lib["rules"]) {
        bool match=true;
        if (r.contains("os") && r["os"].is_object() && r["os"].contains("name"))
            match = std::string(r["os"]["name"])=="windows";
        if (match) allow = (std::string(r["action"])=="allow");
    }
    return allow;
}

// Recursively copy all files from srcDir to dstDir
static void CopyDirRecursive(const std::wstring& srcDir, const std::wstring& dstDir) {
    MakeDirRecursive(dstDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((srcDir+L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName,L".")||!wcscmp(fd.cFileName,L"..")) continue;
        std::wstring src = srcDir+L"\\"+fd.cFileName;
        std::wstring dst = dstDir+L"\\"+fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyDirRecursive(src, dst);
        } else {
            MakeDirRecursive(dst.substr(0, dst.find_last_of(L"\\")));
            CopyFileW(src.c_str(), dst.c_str(), FALSE);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// Install loader after vanilla is downloaded.
// loaderSel: 0=vanilla, 1=fabric, 2=forge, 3=neoforge, 4=optifine
static bool InstallLoader(const std::wstring& mcVer, int loaderSel, const std::wstring& workDir,
                          const std::wstring& customName,
                          std::wstring& outNewVerId, std::wstring& outErr) {
    if (loaderSel == 0) { outNewVerId = mcVer; return true; }

    SetStatus(L"正在安装加载器…");

    if (loaderSel == 1) {
        HttpResult lr = HttpGet(L"https://bmclapi2.bangbang93.com/fabric-meta/v2/versions/loader");
        if (!lr.ok) { outErr=L"获取 Fabric 列表失败: "+lr.err; return false; }
        try {
            json arr = json::parse(std::string(lr.data.begin(), lr.data.end()));
            if (arr.empty()) { outErr=L"Fabric 无可用版本"; return false; }
            std::wstring loaderVer = Utf8ToW(arr[0].value("version",""));
            std::wstring profUrl = L"https://bmclapi2.bangbang93.com/fabric-meta/v2/versions/loader/"
                + mcVer + L"/" + loaderVer + L"/profile/json";
            HttpResult pr = HttpGet(profUrl);
            if (!pr.ok) { outErr=L"获取 Fabric profile 失败: "+pr.err; return false; }
            json prof = json::parse(std::string(pr.data.begin(), pr.data.end()));

            std::wstring vjsonPath = workDir + L"\\versions\\" + mcVer + L"\\" + mcVer + L".json";
            std::ifstream vf(WToUtf8(vjsonPath), std::ios::binary);
            if (!vf) { outErr=L"找不到原版 version.json"; return false; }
            std::stringstream vss; vss << vf.rdbuf();
            json vj = json::parse(vss.str());

            if (prof.contains("libraries")) {
                json merged = json::array();
                for (auto& lib : prof["libraries"]) merged.push_back(lib);
                if (vj.contains("libraries"))
                    for (auto& lib : vj["libraries"]) merged.push_back(lib);
                vj["libraries"] = merged;
            }
            if (prof.contains("mainClass")) vj["mainClass"] = prof["mainClass"];
            if (prof.contains("arguments")) {
                if (!vj.contains("arguments")) vj["arguments"] = json::object();
                for (auto it = prof["arguments"].begin(); it != prof["arguments"].end(); ++it) {
                    if (!vj["arguments"].contains(it.key())) vj["arguments"][it.key()] = it.value();
                    else for (auto& a : it.value()) vj["arguments"][it.key()].push_back(a);
                }
            }
            std::wstring newId = mcVer + L"-fabric-" + loaderVer;
            if (!customName.empty()) newId = customName;
            vj.erase("inheritsFrom");
            vj["id"] = WToUtf8(newId);

            std::wstring fdir = workDir + L"\\versions\\" + newId;
            MakeDirRecursive(fdir);
            std::string s = vj.dump();
            WriteFileW(fdir+L"\\"+newId+L".json", std::vector<BYTE>(s.begin(),s.end()));

            std::wstring srcJar = workDir + L"\\versions\\" + mcVer + L"\\" + mcVer + L".jar";
            std::wstring dstJar = fdir + L"\\" + newId + L".jar";
            if (GetFileAttributesW(srcJar.c_str()) != INVALID_FILE_ATTRIBUTES)
                CopyFileW(srcJar.c_str(), dstJar.c_str(), FALSE);

            if (prof.contains("libraries")) {
                int total = (int)prof["libraries"].size(), done=0;
                for (auto& lib : prof["libraries"]) {
                    done++;
                    if (!lib.contains("name")) continue;
                    std::string name = lib["name"];
                    std::string path;
                    {
                        size_t p1 = name.find(':'); size_t p2 = name.find(':', p1+1);
                        if (p1==std::string::npos||p2==std::string::npos) continue;
                        std::string grp = name.substr(0,p1);
                        std::string art = name.substr(p1+1,p2-p1-1);
                        std::string ver = name.substr(p2+1);
                        for (auto& c : grp) if (c=='.') c='/';
                        path = grp+"/"+art+"/"+ver+"/"+art+"-"+ver+".jar";
                    }
                    std::wstring base = L"https://maven.fabricmc.net/";
                    std::string grp = name.substr(0, name.find(':'));
                    if (grp.find("net.fabricmc") != 0) base = L"https://repo1.maven.org/maven2/";
                    std::wstring url = base + Utf8ToW(path);
                    std::wstring wpath = Utf8ToW(path);
                    std::replace(wpath.begin(), wpath.end(), L'/', L'\\');
                    if (GetFileAttributesW((workDir+L"\\libraries\\"+wpath).c_str()) != INVALID_FILE_ATTRIBUTES) continue;
                    SetStatus(L"[Fabric "+std::to_wstring(done)+L"/"+std::to_wstring(total)+L"] "+wpath);
                    HttpResult d = HttpGet(url);
                    if (d.ok) WriteFileW(workDir+L"\\libraries\\"+wpath, d.data);
                }
            }
            outNewVerId = newId;
            {
                std::wstring from = workDir + L"\\versions\\" + mcVer;
                from.push_back(L'\0'); from.push_back(L'\0');
                SHFILEOPSTRUCTW fos={0}; fos.wFunc=FO_DELETE; fos.pFrom=from.c_str();
                fos.fFlags=FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT;
                SHFileOperationW(&fos);
            }
            return true;
        } catch (const std::exception& e) {
            outErr=L"Fabric 安装失败: "+Utf8ToW(e.what()); return false;
        }
    }

    if (loaderSel == 2 || loaderSel == 3) {
        std::wstring listUrl;
        if (loaderSel == 2) listUrl = L"https://bmclapi2.bangbang93.com/forge/minecraft/" + mcVer;
        else listUrl = L"https://bmclapi2.bangbang93.com/neoforge/list/" + mcVer;
        HttpResult lr = HttpGet(listUrl);
        if (!lr.ok) { outErr=L"获取加载器列表失败: "+lr.err; return false; }
        try {
            json arr = json::parse(std::string(lr.data.begin(), lr.data.end()));
            if (!arr.is_array() || arr.empty()) { outErr=L"该 MC 版本无此加载器"; return false; }
            json chosen = arr[0];
            long bestBuild = 0;
            for (auto& a : arr) {
                long b = a.value("build", 0);
                if (b >= bestBuild) { bestBuild = b; chosen = a; }
            }
            std::wstring forgeVer = Utf8ToW(chosen.value("version",""));
            if (forgeVer.empty()) { outErr=L"解析加载器版本失败"; return false; }

            std::wstring instUrl;
            if (loaderSel == 2) {
                instUrl = L"https://maven.minecraftforge.net/net/minecraftforge/forge/"
                    + mcVer + L"-" + forgeVer + L"/forge-" + mcVer + L"-" + forgeVer + L"-installer.jar";
            } else {
                std::string ipath = chosen.value("installerPath", "");
                if (ipath.empty()) { outErr=L"NeoForge 无 installer 路径"; return false; }
                instUrl = L"https://bmclapi2.bangbang93.com" + Utf8ToW(ipath);
            }
            SetStatus(L"下载加载器安装包 "+forgeVer+L"…");
            HttpResult jr = HttpGet(instUrl);
            if (!jr.ok) { outErr=L"下载安装包失败: "+jr.err+L"  URL:"+instUrl; return false; }

            std::wstring tmpZip = GetExeDir()+L"\\_forge_installer.jar";
            std::wstring tmpDir = GetExeDir()+L"\\_forge_tmp";
            WriteFileW(tmpZip, jr.data);
            {
                std::wstring from = tmpDir; from.push_back(L'\0'); from.push_back(L'\0');
                SHFILEOPSTRUCTW fos={0}; fos.wFunc=FO_DELETE; fos.pFrom=from.c_str();
                fos.fFlags=FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT;
                SHFileOperationW(&fos);
            }
            if (!ExtractZip(tmpZip, tmpDir)) { outErr=L"解压安装包失败"; return false; }

            for (int i=0; i<60; i++) {
                if (GetFileAttributesW((tmpDir+L"\\version.json").c_str()) != INVALID_FILE_ATTRIBUTES) break;
                Sleep(500);
            }
            if (GetFileAttributesW((tmpDir+L"\\version.json").c_str()) == INVALID_FILE_ATTRIBUTES) {
                outErr=L"解压超时，version.json 未出现"; return false;
            }

            CopyDirRecursive(tmpDir+L"\\maven", workDir+L"\\libraries");

            std::wstring vjsonPath = tmpDir+L"\\version.json";
            std::ifstream vf(WToUtf8(vjsonPath), std::ios::binary);
            if (!vf) { outErr=L"安装包里没有 version.json"; return false; }
            std::stringstream vss; vss << vf.rdbuf();
            json vj = json::parse(vss.str());

            if (vj.contains("libraries")) {
                int total = (int)vj["libraries"].size(), done=0;
                for (auto& lib : vj["libraries"]) {
                    done++;
                    if (!lib.contains("name")) continue;
                    std::string name = lib["name"];
                    size_t p1 = name.find(':'); size_t p2 = name.find(':', p1+1);
                    if (p1==std::string::npos||p2==std::string::npos) continue;
                    std::string grp = name.substr(0,p1);
                    std::string art = name.substr(p1+1,p2-p1-1);
                    std::string ver = name.substr(p2+1);
                    std::string path = grp;
                    for (auto& c : path) if (c=='.') c='/';
                    path += "/"+art+"/"+ver+"/"+art+"-"+ver+".jar";
                    std::wstring wpath = Utf8ToW(path);
                    std::replace(wpath.begin(), wpath.end(), L'/', L'\\');
                    if (GetFileAttributesW((workDir+L"\\libraries\\"+wpath).c_str()) != INVALID_FILE_ATTRIBUTES) continue;
                    SetStatus(L"[Forge libs "+std::to_wstring(done)+L"/"+std::to_wstring(total)+L"] "+wpath);
                    std::wstring base;
                    if (grp.find("net.minecraftforge")==0 || grp.find("cpw.mods")==0 || grp.find("net.neoforged")==0)
                        base = L"https://maven.minecraftforge.net/";
                    else
                        base = L"https://repo1.maven.org/maven2/";
                    std::wstring url = base + Utf8ToW(path);
                    HttpResult d = HttpGet(url);
                    if (d.ok) WriteFileW(workDir+L"\\libraries\\"+wpath, d.data);
                }
            }

            std::wstring newId;
            if (loaderSel == 2) newId = mcVer + L"-forge-" + forgeVer;
            else newId = forgeVer;
            if (!customName.empty()) newId = customName;
            std::wstring vdir = workDir+L"\\versions\\"+newId;
            MakeDirRecursive(vdir);
            vj["id"] = WToUtf8(newId);
            std::string s = vj.dump();
            WriteFileW(vdir+L"\\"+newId+L".json", std::vector<BYTE>(s.begin(),s.end()));

            DeleteFileW(tmpZip.c_str());
            {
                std::wstring from = tmpDir; from.push_back(L'\0'); from.push_back(L'\0');
                SHFILEOPSTRUCTW fos={0}; fos.wFunc=FO_DELETE; fos.pFrom=from.c_str();
                fos.fFlags=FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT;
                SHFileOperationW(&fos);
            }
            outNewVerId = newId;
            return true;
        } catch (const std::exception& e) {
            outErr=L"加载器安装失败: "+Utf8ToW(e.what()); return false;
        }
    }

    if (loaderSel == 4) {
        HttpResult lr = HttpGet(L"https://bmclapi2.bangbang93.com/optifine/"+mcVer);
        if (!lr.ok) { outErr=L"获取 OptiFine 列表失败: "+lr.err; return false; }
        try {
            json arr = json::parse(std::string(lr.data.begin(), lr.data.end()));
            if (!arr.is_array()||arr.empty()) { outErr=L"该 MC 版本无 OptiFine"; return false; }
            json chosen = arr[arr.size()-1];
            std::wstring type = Utf8ToW(chosen.value("type","HD_U"));
            std::wstring patch = Utf8ToW(chosen.value("patch",""));
            std::wstring ofName = L"OptiFine_"+mcVer+L"_"+type+L"_"+patch;
            std::wstring ofUrl = L"https://bmclapi2.bangbang93.com/maven/com/optifine/"+mcVer+L"/"+ofName+L"/"+ofName+L".jar";
            SetStatus(L"下载 "+ofName+L"…");
            HttpResult jr = HttpGet(ofUrl);
            if (!jr.ok) { outErr=L"下载 OptiFine 失败: "+jr.err; return false; }
            std::wstring libPath = L"libraries/optifine/OptiFine/"+mcVer+L"_"+type+L"_"+patch+L"/"+ofName+L".jar";
            std::replace(libPath.begin(), libPath.end(), L'/', L'\\');
            WriteFileW(workDir+L"\\"+libPath, jr.data);

            std::wstring newId = mcVer+L"-OptiFine";
            if (!customName.empty()) newId = customName;
            std::wstring vdir = workDir+L"\\versions\\"+newId;
            MakeDirRecursive(vdir);
            json vj;
            vj["id"] = WToUtf8(newId);
            vj["inheritsFrom"] = WToUtf8(mcVer);
            vj["mainClass"] = "net.fabricmc.loader.impl.launch.knot.KnotClient";
            json libs = json::array();
            json ofLib;
            ofLib["name"] = "optifine:OptiFine:"+WToUtf8(mcVer+L"_"+type+L"_"+patch);
            libs.push_back(ofLib);
            vj["libraries"] = libs;
            std::string s = vj.dump();
            WriteFileW(vdir+L"\\"+newId+L".json", std::vector<BYTE>(s.begin(),s.end()));
            outNewVerId = newId;
            return true;
        } catch (const std::exception& e) {
            outErr=L"OptiFine 安装失败: "+Utf8ToW(e.what()); return false;
        }
    }
    return false;
}

struct DlFile { std::wstring url, rel; };
struct DlCtx {
    std::vector<DlFile>* q;
    std::wstring* workDir;
    int next;
    CRITICAL_SECTION mtx;
    std::atomic<int>* done;
    std::atomic<bool>* cancel;
};
static DWORD WINAPI DlWorker(LPVOID p) {
    DlCtx* c = (DlCtx*)p;
    for (;;) {
        EnterCriticalSection(&c->mtx);
        int i = c->next++;
        LeaveCriticalSection(&c->mtx);
        if (i >= (int)c->q->size()) break;
        if (*c->cancel) break;
        DlFile& f = (*c->q)[i];
        HttpResult d = HttpGet(f.url);
        if (d.ok) WriteFileW(*c->workDir+L"\\"+f.rel, d.data);
        int dn = ++(*c->done);
        if (dn % 10 == 0 || dn == (int)c->q->size())
            PostMessage(g_hMain, WM_APP_PROGRESS, (WPARAM)dn, (LPARAM)(int)c->q->size());
    }
    return 0;
}

static DWORD WINAPI DownloadThread(LPVOID param) {
    int packed = (int)(INT_PTR)param;
    int sel = packed & 0xFFFF;
    int loaderSel = (packed >> 16) & 0xFFFF;
    EnterCriticalSection(&g_versionsMtx);
    if (sel < 0 || sel >= (int)g_versions.size()) { LeaveCriticalSection(&g_versionsMtx); SetStatus(L"未选中版本"); PostMessage(g_hMain, WM_APP_DONE, 0,0); return 0; }
    McVersion ver = g_versions[sel];
    LeaveCriticalSection(&g_versionsMtx);
    g_downloading = true;

    std::wstring workDir = GetWorkDir();
    std::vector<DlFile> queue;
    int total=0; std::atomic<int> done{0};
    auto report = [&](const std::wstring& s) {
        SetStatus(L"["+std::to_wstring(done)+L"/"+std::to_wstring(total)+L"] "+s);
        PostMessage(g_hMain, WM_APP_PROGRESS, (WPARAM)done, (LPARAM)total);
    };

    bool ok=true; std::wstring fail;
    HttpResult vj = HttpGet(ver.url);
    if (!vj.ok) { ok=false; fail=vj.err; goto fin; }
    try {
        json vj2 = json::parse(std::string(vj.data.begin(), vj.data.end()));
        std::wstring verDir = workDir+L"\\versions\\"+ver.id;
        MakeDirRecursive(verDir);
        {
            std::string s = vj2.dump();
            WriteFileW(verDir+L"\\"+ver.id+L".json", std::vector<BYTE>(s.begin(),s.end()));
        }
        if (vj2.contains("downloads") && vj2["downloads"].contains("client")) {
            std::wstring u = BmclRewrite(Utf8ToW(vj2["downloads"]["client"].value("url","")));
            if (!u.empty()) queue.push_back({u, L"versions\\"+ver.id+L"\\"+ver.id+L".jar"});
        }
        if (vj2.contains("libraries")) {
            for (auto& lib : vj2["libraries"]) {
                if (!RulesAllow(lib)) continue;
                if (!lib.contains("downloads")) continue;
                auto& dl = lib["downloads"];
                auto addArt = [&](json& art) {
                    if (!art.contains("path")) return;
                    std::wstring u = BmclRewrite(Utf8ToW(art.value("url","")));
                    std::wstring p = Utf8ToW(art.value("path",""));
                    if (u.empty()||p.empty()) return;
                    std::replace(p.begin(), p.end(), L'/', L'\\');
                    queue.push_back({u, L"libraries\\"+p});
                };
                if (dl.contains("artifact")) addArt(dl["artifact"]);
                if (dl.contains("classifiers")) {
                    for (auto it = dl["classifiers"].begin(); it != dl["classifiers"].end(); ++it) {
                        if (it.key().find("natives-windows") == std::string::npos) continue;
                        addArt(it.value());
                    }
                }
            }
        }
        std::wstring aiId;
        if (vj2.contains("assetIndex")) {
            aiId = Utf8ToW(vj2["assetIndex"].value("id",""));
            std::wstring u = BmclRewrite(Utf8ToW(vj2["assetIndex"].value("url","")));
            if (!u.empty()) queue.push_back({u, L"assets\\indexes\\"+aiId+L".json"});
        }
        total = (int)queue.size();
        report(L"开始下载 "+ver.id+L"（共 "+std::to_wstring(total)+L" 个基础文件）…");

        DlCtx ctx;
        ctx.q = &queue; ctx.workDir = &workDir; ctx.next = 0;
        ctx.done = &done; ctx.cancel = &g_cancel;
        InitializeCriticalSection(&ctx.mtx);
        HANDLE th[8];
        for (int i=0;i<8;i++) th[i]=CreateThread(NULL,0,DlWorker,&ctx,0,NULL);
        WaitForMultipleObjects(8, th, TRUE, INFINITE);
        for (int i=0;i<8;i++) CloseHandle(th[i]);
        DeleteCriticalSection(&ctx.mtx);
        if (g_cancel) { goto fin; }
        std::vector<DlFile> objs;
        if (!aiId.empty()) {
            std::wstring idxPath = workDir+L"\\assets\\indexes\\"+aiId+L".json";
            std::ifstream ifs(WToUtf8(idxPath), std::ios::binary);
            if (ifs) {
                std::stringstream ss; ss << ifs.rdbuf();
                try {
                    json idx = json::parse(ss.str());
                    for (auto it = idx["objects"].begin(); it != idx["objects"].end(); ++it) {
                        std::string h = it.value().value("hash","");
                        if (h.size()<2) continue;
                        std::wstring hw = Utf8ToW(h);
                        std::wstring sub = hw.substr(0,2);
                        std::wstring rel = L"assets\\objects\\"+sub+L"\\"+hw;
                        if (GetFileAttributesW((workDir+L"\\"+rel).c_str()) != INVALID_FILE_ATTRIBUTES) continue;
                        objs.push_back({L"https://bmclapi2.bangbang93.com/assets\\"+sub+L"/"+hw, rel});
                    }
                } catch(...) {}
            }
        }
        total = done + (int)objs.size();
        SetStatus(L"下载资源对象（"+std::to_wstring(objs.size())+L" 个）…");
        {
            DlCtx ctx2;
            ctx2.q = &objs; ctx2.workDir = &workDir; ctx2.next = done;
            ctx2.done = &done; ctx2.cancel = &g_cancel;
            InitializeCriticalSection(&ctx2.mtx);
            HANDLE th2[8];
            for (int i=0;i<8;i++) th2[i]=CreateThread(NULL,0,DlWorker,&ctx2,0,NULL);
            WaitForMultipleObjects(8, th2, TRUE, INFINITE);
            for (int i=0;i<8;i++) CloseHandle(th2[i]);
            DeleteCriticalSection(&ctx2.mtx);
        }
        if (g_cancel) { goto fin; }
    } catch (const std::exception& e) {
        ok=false; fail=Utf8ToW(std::string("解析:")+e.what());
    }
    if (ok && loaderSel != 0) {
        std::wstring newVerId, loaderErr;
        if (!InstallLoader(ver.id, loaderSel, workDir, g_pendingName, newVerId, loaderErr)) {
            ok = false; fail = loaderErr;
        } else {
            SetStatus(L"✅ "+ver.id+L"（含加载器）下载完成！新实例: "+newVerId);
        }
    }
fin:
    g_downloading = false;
    EnableWindow(g_hBtnDownload, TRUE);
    ShowWindow(g_hBtnCancel, SW_HIDE);
    if (g_cancel) { SetStatus(L"⏹ 已取消"); ok=false; }
    else if (ok && loaderSel == 0) SetStatus(L"✅ "+ver.id+L" 下载完成！文件位于 "+workDir);
    else if (!ok && !g_cancel) SetStatus(L"❌ 失败: "+fail);
    PostMessage(g_hMain, WM_APP_PROGRESS, (WPARAM)total, (LPARAM)total);
    PostMessage(g_hMain, WM_APP_DONE, ok?1:0, 0);
    return 0;
}

void RefreshVersionList() {
    SendMessage(g_hLstVer, LVM_DELETEALLITEMS, 0, 0);
    std::wstring flt;
    int n = GetWindowTextLengthW(g_hFilter);
    if (n>0) { flt.resize(n); GetWindowTextW(g_hFilter, &flt[0], n+1); }
    EnterCriticalSection(&g_versionsMtx);
    int i=0;
    for (size_t j=0; j<g_versions.size(); j++) {
        auto& v = g_versions[j];
        if (!flt.empty() && v.id.find(flt)==std::wstring::npos) continue;
        LVITEMW it={0}; it.mask=LVIF_TEXT|LVIF_PARAM; it.iItem=i;
        it.pszText=(LPWSTR)v.id.c_str(); it.lParam=(LPARAM)j;
        ListView_InsertItem(g_hLstVer, &it);
        ListView_SetItemText(g_hLstVer, i, 1, (LPWSTR)v.type.c_str());
        ListView_SetItemText(g_hLstVer, i, 2, (LPWSTR)v.releaseTime.substr(0,10).c_str());
        i++;
    }
    LeaveCriticalSection(&g_versionsMtx);
}
void RefreshInstanceList() {
    SendMessage(g_hLstInst, LVM_DELETEALLITEMS, 0, 0);
    g_instances.clear();
    std::wstring root = GetWorkDir()+L"\\versions";
    MakeDirRecursive(root);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root+L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (!wcscmp(fd.cFileName,L".")||!wcscmp(fd.cFileName,L"..")) continue;
            std::wstring nm = fd.cFileName;
            std::wstring p1 = root+L"\\"+nm+L"\\"+nm+L".jar";
            std::wstring p2 = root+L"\\"+nm+L"\\"+nm+L".json";
            if (GetFileAttributesW(p1.c_str())==INVALID_FILE_ATTRIBUTES &&
                GetFileAttributesW(p2.c_str())==INVALID_FILE_ATTRIBUTES) continue;
            int idx = (int)g_instances.size();
            g_instances.push_back(nm);
            LVITEMW it={0}; it.mask=LVIF_TEXT|LVIF_PARAM; it.iItem=idx;
            it.pszText=(LPWSTR)nm.c_str(); it.lParam=idx;
            ListView_InsertItem(g_hLstInst, &it);
            std::wstring rel = L"versions\\"+nm;
            ListView_SetItemText(g_hLstInst, idx, 1, (LPWSTR)rel.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

// ---- custom nav button drawing ----
static void DrawNavButton(LPDRAWITEMSTRUCT di) {
    bool selected = (g_currentPage == (di->CtlID - ID_NAV_DOWNLOAD));
    wchar_t text[64]; GetWindowTextW(di->hwndItem, text, 63);

    HBRUSH bg = CreateSolidBrush(selected ? CLR_ACCENT_LT : CLR_SIDEBAR);
    FillRect(di->hDC, &di->rcItem, bg);
    DeleteObject(bg);

    if (selected) {
        RECT bar = { di->rcItem.left, di->rcItem.top+6, di->rcItem.left+4, di->rcItem.bottom-6 };
        HBRUSH ab = CreateSolidBrush(CLR_ACCENT);
        FillRect(di->hDC, &bar, ab);
        DeleteObject(ab);
    }
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, selected ? CLR_ACCENT : CLR_TEXT);
    HFONT old = (HFONT)SelectObject(di->hDC, selected ? g_fontBold : g_fontUI);
    RECT tr = di->rcItem; tr.left += 18;
    DrawTextW(di->hDC, text, -1, &tr, DT_VCENTER|DT_SINGLELINE|DT_LEFT);
    SelectObject(di->hDC, old);
}

// ---- window proc ----
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hMain = hWnd;
        g_fontUI = CreateFontW(19,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_SWISS,L"Microsoft YaHei");
        g_fontTitle = CreateFontW(30,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_SWISS,L"Microsoft YaHei");
        g_fontBold = CreateFontW(19,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_SWISS,L"Microsoft YaHei");

        g_hTitle = CreateWindowExW(0, L"STATIC", L"PCL DE",
            WS_CHILD|WS_VISIBLE|SS_LEFT, 20, 24, 140, 36, hWnd, (HMENU)0, g_hInst, NULL);
        SendMessage(g_hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

        const wchar_t* navLabels[3] = { L"下载", L"实例管理", L"关于" };
        for (int i=0;i<3;i++) {
            g_hNav[i] = CreateWindowExW(0, L"BUTTON", navLabels[i],
                WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                0, 90+i*48, SIDEBAR_W, 44, hWnd, (HMENU)(ID_NAV_DOWNLOAD+i), g_hInst, NULL);
        }

        g_hLblFilter = CreateWindowExW(0, L"STATIC", L"筛选:",
            WS_CHILD|WS_VISIBLE, 20, 18, 40, 26, hWnd, (HMENU)0, g_hInst, NULL);
        g_hFilter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 62, 15, 140, 28, hWnd, (HMENU)ID_FILTER, g_hInst, NULL);
        g_hLblLoader = CreateWindowExW(0, L"STATIC", L"加载器:",
            WS_CHILD|WS_VISIBLE, 215, 18, 50, 26, hWnd, (HMENU)0, g_hInst, NULL);
        g_hLoader = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL, 268, 14, 120, 200, hWnd, (HMENU)ID_LOADER, g_hInst, NULL);
        SendMessage(g_hLoader, CB_ADDSTRING, 0, (LPARAM)L"原版");
        SendMessage(g_hLoader, CB_ADDSTRING, 0, (LPARAM)L"Fabric");
        SendMessage(g_hLoader, CB_ADDSTRING, 0, (LPARAM)L"Forge");
        SendMessage(g_hLoader, CB_ADDSTRING, 0, (LPARAM)L"NeoForge");
        SendMessage(g_hLoader, CB_ADDSTRING, 0, (LPARAM)L"OptiFine");
        SendMessage(g_hLoader, CB_SETCURSEL, 0, 0);
        g_hLblName = CreateWindowExW(0, L"STATIC", L"实例名:",
            WS_CHILD|WS_VISIBLE, 395, 18, 55, 26, hWnd, (HMENU)0, g_hInst, NULL);
        g_hCustomName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 455, 15, 140, 28, hWnd, (HMENU)ID_CUSTOM_NAME, g_hInst, NULL);
        g_hBtnRefresh = CreateWindowExW(0, L"BUTTON", L"刷新列表",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 400, 14, 90, 28, hWnd, (HMENU)ID_BTN_REFRESH, g_hInst, NULL);

        g_hLstVer = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
            20, 50, 760, 430, hWnd, (HMENU)ID_LIST_VERSIONS, g_hInst, NULL);
        SendMessage(g_hLstVer, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        ListView_SetExtendedListViewStyle(g_hLstVer, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
        LVCOLUMNW col={0}; col.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;
        col.iSubItem=0; col.cx=260; col.pszText=(LPWSTR)L"版本";
        ListView_InsertColumn(g_hLstVer,0,&col);
        col.iSubItem=1; col.cx=120; col.pszText=(LPWSTR)L"类型";
        ListView_InsertColumn(g_hLstVer,1,&col);
        col.iSubItem=2; col.cx=180; col.pszText=(LPWSTR)L"发布时间";
        ListView_InsertColumn(g_hLstVer,2,&col);

        g_hBtnDownload = CreateWindowExW(0, L"BUTTON", L"下载选中版本",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 20, 492, 130, 32, hWnd, (HMENU)ID_BTN_DOWNLOAD, g_hInst, NULL);
        g_hBtnCancel = CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD|BS_PUSHBUTTON, 155, 492, 70, 32, hWnd, (HMENU)ID_BTN_CANCEL, g_hInst, NULL);
        ShowWindow(g_hBtnCancel, SW_HIDE);
        g_hProg = CreateWindowExW(0, PROGRESS_CLASSW, L"",
            WS_CHILD|WS_VISIBLE, 160, 495, 620, 22, hWnd, (HMENU)ID_PROGRESS, g_hInst, NULL);
        g_hStatus = CreateWindowExW(0, L"STATIC", L"正在启动…",
            WS_CHILD|WS_VISIBLE|SS_LEFTNOWORDWRAP, 20, 530, 760, 20, hWnd, (HMENU)ID_STATUS, g_hInst, NULL);

        g_hLstInst = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
            20, 18, 760, 460, hWnd, (HMENU)ID_LIST_INSTANCES, g_hInst, NULL);
        SendMessage(g_hLstInst, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        ListView_SetExtendedListViewStyle(g_hLstInst, LVS_EX_FULLROWSELECT);
        col.iSubItem=0; col.cx=280; col.pszText=(LPWSTR)L"已下载实例";
        ListView_InsertColumn(g_hLstInst,0,&col);
        col.iSubItem=1; col.cx=300; col.pszText=(LPWSTR)L"路径";
        ListView_InsertColumn(g_hLstInst,1,&col);

        g_hBtnRefreshInst = CreateWindowExW(0, L"BUTTON", L"刷新",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 20, 490, 90, 30, hWnd, (HMENU)ID_BTN_REFRESHINST, g_hInst, NULL);
        g_hBtnOpen = CreateWindowExW(0, L"BUTTON", L"打开文件夹",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 120, 490, 110, 30, hWnd, (HMENU)ID_BTN_OPENFOLDER, g_hInst, NULL);
        g_hBtnDel = CreateWindowExW(0, L"BUTTON", L"删除实例",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 240, 490, 100, 30, hWnd, (HMENU)ID_BTN_DELINST, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"重命名",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 350, 490, 90, 30, hWnd, (HMENU)ID_BTN_RENAME, g_hInst, NULL);

        {
            std::wstring about =
                L"Plain Craft Launcher-download-Edition\n"
                L"版本: V1.0.0\n"
                L"\n"
                L"极简的Minecraft下载器及实例管理器！（豆包最有用的一次）\n"
                L"本启动器为PCL 2.99.0关闭下载Minecraft入口而生！\n"
                L"使用了部分PCL 2的代码！\n"
                L"本下载器仅供下载Minecraft使用，无任何Minecraft源码，与mojang，微软和网易无关！\n"
                L"如果你是下载PCL时，下载到了这个下载器，请支持PCL官方！\n"
                L"使用了BMCLAPI（bmclapi2.bangbang93.com）！\n"
                L"\n"
                L"功能:\n"
                L"1.下载全版本Minecraft java版！支持fabric、neoforge、forge以及OF（高清优化）！\n"
                L"2.实例的打开与删除，一键搞定！";
            g_hAboutText = CreateWindowExW(0, L"STATIC", about.c_str(),
                WS_CHILD|WS_VISIBLE|SS_LEFT, 40, 40, 720, 400, hWnd, (HMENU)0, g_hInst, NULL);
            SendMessage(g_hAboutText, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        }

        SwitchPage(0);
        {
            HWND child = GetWindow(hWnd, GW_CHILD);
            while (child) {
                if (child != g_hTitle)
                    SendMessage(child, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
                child = GetWindow(child, GW_HWNDNEXT);
            }
            SendMessage(g_hTitle, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
        }
        CreateThread(NULL, 0, FetchVersionsThread, NULL, 0, NULL);
        break;
    }
    case WM_SIZE: {
        int cw = LOWORD(lp), ch = HIWORD(lp);
        int rx = SIDEBAR_W + 20;
        int rw = cw - rx - 16;
        MoveWindow(g_hTitle, 20, 22, 140, 36, TRUE);
        for (int i=0;i<3;i++) MoveWindow(g_hNav[i], 0, 90+i*48, SIDEBAR_W, 44, TRUE);

        MoveWindow(g_hLblFilter, rx, 18, 40, 26, TRUE);
        MoveWindow(g_hFilter, rx+42, 15, 140, 28, TRUE);
        MoveWindow(g_hLblLoader, rx+190, 18, 50, 26, TRUE);
        MoveWindow(g_hLoader, rx+245, 15, 120, 200, TRUE);
        MoveWindow(g_hLblName, rx+375, 18, 55, 26, TRUE);
        MoveWindow(g_hCustomName, rx+435, 15, 140, 28, TRUE);
        MoveWindow(g_hBtnRefresh, rx+585, 14, 90, 28, TRUE);
        MoveWindow(g_hLstVer, rx, 50, rw, ch-140, TRUE);
        MoveWindow(g_hBtnDownload, rx, ch-92, 130, 32, TRUE);
        MoveWindow(g_hBtnCancel, rx+140, ch-92, 70, 32, TRUE);
        MoveWindow(g_hProg, rx+220, ch-89, rw-220, 22, TRUE);
        MoveWindow(g_hStatus, rx, ch-55, rw, 20, TRUE);

        MoveWindow(g_hLstInst, rx, 18, rw, ch-80, TRUE);
        MoveWindow(g_hBtnRefreshInst, rx, ch-65, 90, 30, TRUE);
        MoveWindow(g_hBtnOpen, rx+100, ch-65, 110, 30, TRUE);
        MoveWindow(g_hBtnDel, rx+220, ch-65, 100, 30, TRUE);

        MoveWindow(g_hAboutText, rx+20, 40, rw-20, ch-60, TRUE);
        if (g_hLstVer) {
            RECT rc; GetClientRect(g_hLstVer, &rc);
            int w = rc.right - rc.left;
            ListView_SetColumnWidth(g_hLstVer, 0, 260);
            ListView_SetColumnWidth(g_hLstVer, 1, 120);
            ListView_SetColumnWidth(g_hLstVer, 2, std::max(100, w-260-120-20));
        }
        if (g_hLstInst) {
            RECT rc; GetClientRect(g_hLstInst, &rc);
            int w = rc.right - rc.left;
            ListView_SetColumnWidth(g_hLstInst, 0, 280);
            ListView_SetColumnWidth(g_hLstInst, 1, std::max(100, w-280-20));
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lp;
        if (di->CtlID >= ID_NAV_DOWNLOAD && di->CtlID <= ID_NAV_ABOUT) {
            DrawNavButton(di);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id>=ID_NAV_DOWNLOAD && id<=ID_NAV_ABOUT) SwitchPage(id-ID_NAV_DOWNLOAD);
        else if (id==ID_BTN_REFRESH) CreateThread(NULL,0,FetchVersionsThread,NULL,0,NULL);
        else if (id==ID_FILTER && HIWORD(wp)==EN_CHANGE) RefreshVersionList();
        else if (id==ID_BTN_DOWNLOAD) {
            if (g_downloading) { break; }
            int sel = ListView_GetNextItem(g_hLstVer, -1, LVNI_SELECTED);
            if (sel==-1) { MessageBoxW(hWnd,L"请先在列表选一个版本。",L"PCL DE",MB_OK|MB_ICONINFORMATION); break; }
            int idx = (int)GetItemData(g_hLstVer, sel);
            int loader = (int)SendMessage(g_hLoader, CB_GETCURSEL, 0, 0);
            wchar_t nameBuf[256]={0}; GetWindowTextW(g_hCustomName, nameBuf, 255);
            g_pendingName = nameBuf;
            g_cancel = false;
            EnableWindow(g_hBtnDownload, FALSE);
            ShowWindow(g_hBtnCancel, SW_SHOW);
            CreateThread(NULL,0,DownloadThread,(LPVOID)(INT_PTR)(idx | (loader<<16)),0,NULL);
        }
        else if (id==ID_BTN_CANCEL) {
            g_cancel = true;
            SetStatus(L"正在取消…");
        }
        else if (id==ID_BTN_REFRESHINST) RefreshInstanceList();
        else if (id==ID_BTN_OPENFOLDER) {
            int sel = ListView_GetNextItem(g_hLstInst,-1,LVNI_SELECTED);
            if (sel==-1) ShellExecuteW(hWnd,L"open",GetWorkDir().c_str(),NULL,NULL,SW_SHOW);
            else {
                int idx=(int)GetItemData(g_hLstInst,sel);
                if (idx>=0 && idx<(int)g_instances.size()) {
                    std::wstring p = GetWorkDir()+L"\\versions\\"+g_instances[idx];
                    ShellExecuteW(hWnd,L"open",p.c_str(),NULL,NULL,SW_SHOW);
                }
            }
        }
        else if (id==ID_BTN_RENAME) {
            int sel = ListView_GetNextItem(g_hLstInst,-1,LVNI_SELECTED);
            if (sel==-1) break;
            int idx=(int)GetItemData(g_hLstInst,sel);
            if (idx<0||idx>=(int)g_instances.size()) break;
            std::wstring oldName = g_instances[idx];
            HWND hPop = CreateWindowExW(WS_EX_DLGMODALFRAME, L"#32770", L"重命名实例",
                WS_POPUP|WS_CAPTION|WS_SYSMENU, CW_USEDEFAULT,CW_USEDEFAULT,360,140,
                hWnd, NULL, g_hInst, NULL);
            CreateWindowExW(0, L"STATIC", L"新名称:",
                WS_CHILD, 15,15,60,20, hPop, NULL, g_hInst, NULL);
            HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", oldName.c_str(),
                WS_CHILD|ES_AUTOHSCROLL, 80,12,250,24, hPop, NULL, g_hInst, NULL);
            CreateWindowExW(0, L"BUTTON", L"确定",
                WS_CHILD|BS_DEFPUSHBUTTON, 80,50,80,28, hPop, (HMENU)1, g_hInst, NULL);
            CreateWindowExW(0, L"BUTTON", L"取消",
                WS_CHILD, 180,50,80,28, hPop, (HMENU)2, g_hInst, NULL);
            SendMessage(hEdit, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
            ShowWindow(hPop, SW_SHOW);
            EnableWindow(hWnd, FALSE);
            MSG msg; BOOL done=FALSE;
            while (!done && GetMessage(&msg, NULL,0,0)>0) {
                if (msg.hwnd==hPop && msg.message==WM_COMMAND) {
                    if (LOWORD(msg.wParam)==1) {
                        wchar_t buf[256]={0}; GetWindowTextW(hEdit, buf, 255);
                        std::wstring newName = buf;
                        if (!newName.empty() && newName != oldName) {
                            std::wstring oldPath = GetWorkDir()+L"\\versions\\"+oldName;
                            std::wstring newPath = GetWorkDir()+L"\\versions\\"+newName;
                            if (GetFileAttributesW(newPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                                MessageBoxW(hPop, L"已存在同名实例！", L"错误", MB_OK|MB_ICONERROR);
                            } else {
                                MoveFileW(oldPath.c_str(), newPath.c_str());
                                DeleteFileW((newPath+L"\\"+oldName+L".json").c_str());
                                std::ifstream jf(WToUtf8(newPath+L"\\"+oldName+L".json"), std::ios::binary);
                                if (jf) {
                                    std::stringstream js; js<<jf.rdbuf();
                                    try { json j=json::parse(js.str()); j["id"]=WToUtf8(newName);
                                        std::string s=j.dump();
                                        WriteFileW(newPath+L"\\"+newName+L".json", std::vector<BYTE>(s.begin(),s.end()));
                                    } catch(...) {}
                                }
                                RefreshInstanceList();
                            }
                        }
                        done=TRUE;
                    } else if (LOWORD(msg.wParam)==2) done=TRUE;
                } else {
                    if (!IsDialogMessage(hPop, &msg)) { TranslateMessage(&msg); DispatchMessage(&msg); }
                }
            }
            EnableWindow(hWnd, TRUE);
            DestroyWindow(hPop);
            SetForegroundWindow(hWnd);
            break;
        }
        else if (id==ID_BTN_DELINST) {
            int sel = ListView_GetNextItem(g_hLstInst,-1,LVNI_SELECTED);
            if (sel==-1) break;
            int idx=(int)GetItemData(g_hLstInst,sel);
            if (idx<0||idx>=(int)g_instances.size()) break;
            std::wstring nm = g_instances[idx];
            std::wstring p = GetWorkDir()+L"\\versions\\"+nm;
            std::wstring msg = L"确定删除实例「"+nm+L"」吗？此操作不可撤销。";
            if (MessageBoxW(hWnd,msg.c_str(),L"删除确认",MB_YESNO|MB_ICONWARNING)==IDYES) {
                std::wstring from = p; from.push_back(L'\0'); from.push_back(L'\0');
                SHFILEOPSTRUCTW fos={0};
                fos.hwnd=hWnd; fos.wFunc=FO_DELETE; fos.pFrom=from.c_str();
                fos.fFlags=FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT;
                SHFileOperationW(&fos);
                RefreshInstanceList();
            }
        }
        break;
    }
    case WM_APP_VERSIONS_READY: RefreshVersionList(); InvalidateRect(g_hMain,NULL,TRUE); break;
    case WM_APP_STATUS: SetWindowTextW(g_hStatus, g_statusText.c_str()); break;
    case WM_APP_PROGRESS:
        SendMessage(g_hProg, PBM_SETRANGE32, 0, (LPARAM)(lp>0?lp:1));
        SendMessage(g_hProg, PBM_SETPOS, (WPARAM)wp, 0);
        break;
    case WM_APP_DONE: EnableWindow(g_hBtnDownload, TRUE); break;
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hWnd,&rc);
        HDC hdc = (HDC)wp;
        HBRUSH bg = CreateSolidBrush(CLR_BG);
        RECT cr = { SIDEBAR_W, 0, rc.right, rc.bottom };
        FillRect(hdc, &cr, bg);
        DeleteObject(bg);
        HBRUSH sb = CreateSolidBrush(CLR_SIDEBAR);
        RECT sr = { 0, 0, SIDEBAR_W, rc.bottom };
        FillRect(hdc, &sr, sb);
        DeleteObject(sb);
        return 1;
    }
    case WM_DESTROY:
        if (g_fontUI) DeleteObject(g_fontUI);
        if (g_fontTitle) DeleteObject(g_fontTitle);
        if (g_fontBold) DeleteObject(g_fontBold);
        DeleteCriticalSection(&g_versionsMtx);
        PostQuitMessage(0);
        break;
    default: return DefWindowProcW(hWnd,msg,wp,lp);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hI, HINSTANCE, LPWSTR, int nShow) {
    g_hInst = hI;
    InitializeCriticalSection(&g_versionsMtx);
    INITCOMMONCONTROLSEX icc={sizeof(icc), ICC_WIN95_CLASSES|ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc={0};
    wc.lpfnWndProc=WndProc; wc.hInstance=hI;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName=L"PCLDE_Main";
    wc.hIcon=LoadIcon(NULL,IDI_APPLICATION);
    RegisterClassW(&wc);

    g_hMain = CreateWindowExW(0, L"PCLDE_Main",
        L"PCL-Download-Edition  (PCL DE)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,940,620,
        NULL,NULL,hI,NULL);
    ShowWindow(g_hMain, nShow);
    UpdateWindow(g_hMain);

    MSG m;
    while (GetMessage(&m,NULL,0,0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
