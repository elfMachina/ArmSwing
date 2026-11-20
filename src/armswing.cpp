// armswing — the input half. The driver (driver_armswing.dll) cannot read input.
//
// Three jobs:
//
//  1. Left stick -> stick.txt (1/0). Via IVRInput, so it is independent of
//     VRChat's own bindings and of what game is running.
//  2. Wake signals -> hands.txt ("sensorL sensorR gripL gripR"). The input
//     pipeline is the one channel pose substitution does not touch, so this
//     is the only thing that can wake an arm already in the walk animation.
//  3. Watch GetTrackedDeviceIndexForControllerRole, which is SteamVR's real
//     answer to "which device is the left hand" and what VRChat resolves
//     through. This is the canary: it is what caught SteamVR handing a hand
//     role to one of our own proxies (v0.50), and with the driver's role set
//     to OptOut it must now read the real controllers forever. If a [ROLE]
//     line ever names ARMSWING_* or dev -1 again, that is the regression.
//  4. The movement gate: stick.txt says 1 only while VRChat is actually
//     moving the avatar (see "The movement gate" below).
//
// Each log is one session; the previous one is kept as *.log.prev.

#include <winsock2.h>       // must come before windows.h
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>         // types only: dnsapi is loaded at run time
#include <shellapi.h>
#include <openvr.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include "osc.h"

static char  g_exeDir[MAX_PATH];
static FILE* g_log = nullptr;

static void LogF(const char* tag, const char* fmt, ...)
{
    char msg[1024];
    va_list a; va_start(a, fmt);
    vsnprintf(msg, sizeof msg, fmt, a);
    va_end(a);

    char line[1152];
    snprintf(line, sizeof line, "[%-5s] %s\n", tag, msg);
    fputs(line, stdout); fflush(stdout);
    if (g_log) {
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(g_log, "%02d:%02d:%02d.%03d %s",
                t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
        fflush(g_log);
    }
}

static const char* RoleName(int r)
{
    switch (r) {
        case vr::TrackedControllerRole_Invalid:   return "none";
        case vr::TrackedControllerRole_LeftHand:  return "LEFT";
        case vr::TrackedControllerRole_RightHand: return "RIGHT";
        default:                                  return "other";
    }
}

static const char* ClassName(int c)
{
    switch (c) {
        case vr::TrackedDeviceClass_Invalid:           return "invalid";
        case vr::TrackedDeviceClass_HMD:               return "hmd";
        case vr::TrackedDeviceClass_Controller:        return "controller";
        case vr::TrackedDeviceClass_GenericTracker:    return "tracker";
        case vr::TrackedDeviceClass_TrackingReference: return "base";
        default:                                       return "?";
    }
}

// Full picture of every device SteamVR knows about, client side.
static void DumpDevices(vr::IVRSystem* sys, const char* tag)
{
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
    sys->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseRawAndUncalibrated,
                                         0.0f, poses,
                                         vr::k_unMaxTrackedDeviceCount);
    LogF("dev", "--- device inventory (%s) ---", tag);
    for (uint32_t i = 0; i < vr::k_unMaxTrackedDeviceCount; i++) {
        vr::ETrackedDeviceClass cls = sys->GetTrackedDeviceClass(i);
        if (cls == vr::TrackedDeviceClass_Invalid) continue;
        char serial[128] = {}, type[128] = {};
        sys->GetStringTrackedDeviceProperty(i, vr::Prop_SerialNumber_String,
                                            serial, sizeof serial);
        sys->GetStringTrackedDeviceProperty(i, vr::Prop_ControllerType_String,
                                            type, sizeof type);
        int role = sys->GetInt32TrackedDeviceProperty(
            i, vr::Prop_ControllerRoleHint_Int32);
        bool never = sys->GetBoolTrackedDeviceProperty(
            i, vr::Prop_NeverTracked_Bool);
        bool conn = sys->IsTrackedDeviceConnected(i);
        const auto& p = poses[i];
        LogF("dev", "  [%2u] %-10s role=%-5s never=%d conn=%d valid=%d res=%3d "
                    "pos=(%7.3f,%7.3f,%7.3f) type=%-24s serial=%s",
             i, ClassName(cls), RoleName(role), (int)never, (int)conn,
             (int)p.bPoseIsValid, (int)p.eTrackingResult,
             p.mDeviceToAbsoluteTracking.m[0][3],
             p.mDeviceToAbsoluteTracking.m[1][3],
             p.mDeviceToAbsoluteTracking.m[2][3],
             type, serial);
    }
    LogF("dev", "--- end inventory ---");
}

// THE decisive readout: which index SteamVR resolves each hand role to,
// and whether that index's pose is valid.  VRChat's hands follow this.
struct RoleState
{
    uint32_t idxL = vr::k_unTrackedDeviceIndexInvalid;
    uint32_t idxR = vr::k_unTrackedDeviceIndexInvalid;
    int      validL = -1, validR = -1;

    bool operator!=(const RoleState& o) const
    {
        return idxL != o.idxL || idxR != o.idxR ||
               validL != o.validL || validR != o.validR;
    }
};

static RoleState ReadRoles(vr::IVRSystem* sys)
{
    RoleState s;
    s.idxL = sys->GetTrackedDeviceIndexForControllerRole(
                 vr::TrackedControllerRole_LeftHand);
    s.idxR = sys->GetTrackedDeviceIndexForControllerRole(
                 vr::TrackedControllerRole_RightHand);

    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
    sys->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseRawAndUncalibrated,
                                         0.0f, poses,
                                         vr::k_unMaxTrackedDeviceCount);
    if (s.idxL != vr::k_unTrackedDeviceIndexInvalid)
        s.validL = poses[s.idxL].bPoseIsValid ? 1 : 0;
    if (s.idxR != vr::k_unTrackedDeviceIndexInvalid)
        s.validR = poses[s.idxR].bPoseIsValid ? 1 : 0;
    return s;
}

static void LogRoles(vr::IVRSystem* sys, const RoleState& s, const char* why)
{
    char sl[160] = {}, sr[160] = {};
    if (s.idxL != vr::k_unTrackedDeviceIndexInvalid)
        sys->GetStringTrackedDeviceProperty(s.idxL, vr::Prop_SerialNumber_String,
                                            sl, sizeof sl);
    if (s.idxR != vr::k_unTrackedDeviceIndexInvalid)
        sys->GetStringTrackedDeviceProperty(s.idxR, vr::Prop_SerialNumber_String,
                                            sr, sizeof sr);
    LogF("ROLE", "%s — LEFT hand -> dev %d (valid=%d) %s | RIGHT hand -> "
                 "dev %d (valid=%d) %s",
         why, (int)s.idxL, s.validL, sl[0] ? sl : "(none)",
         (int)s.idxR, s.validR, sr[0] ? sr : "(none)");
}

// Written in place.  The driver opens these files with read/write sharing, so
// this cannot collide with it; the old temp+rename did (11 logged failures),
// because deleting the target fails while the driver has it open.  A read
// that lands mid-write sees an empty file, which the driver takes as "nothing
// held": the safe answer.  Callers keep their old state and retry on failure.
static bool WriteFile1(const char* path, const char* body)
{
    FILE* f = fopen(path, "w");
    if (!f) return false;
    bool ok = fputs(body, f) >= 0;
    return (fclose(f) == 0) && ok;
}

// ── tray icon: the only UI. No console window. ──
#define WM_TRAY (WM_APP + 1)
static HWND            g_wnd;
static NOTIFYICONDATAA g_nid;
static char            g_stickPath[MAX_PATH];

static void Tray(const char* status, bool balloon = false)
{
    bool add = g_nid.cbSize == 0;
    if (add) {
        g_nid.cbSize = sizeof g_nid;
        g_nid.hWnd = g_wnd;
        g_nid.uID = 1;
        g_nid.uCallbackMessage = WM_TRAY;
        g_nid.hIcon = (HICON)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1),
            IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        if (!g_nid.hIcon) g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | (balloon ? NIF_INFO : 0);
    snprintf(g_nid.szTip, sizeof g_nid.szTip, "ArmSwing - %s", status);
    if (balloon) {
        snprintf(g_nid.szInfoTitle, sizeof g_nid.szInfoTitle, "ArmSwing");
        snprintf(g_nid.szInfo, sizeof g_nid.szInfo, "%s", status);
    }
    Shell_NotifyIconA(add ? NIM_ADD : NIM_MODIFY, &g_nid);
}

static void VrcShutdown();

// Every exit goes through here: a "1" left in stick.txt would keep the arms
// parked with nothing running to release them.
static void Bye(int code)
{
    WriteFile1(g_stickPath, "0\n");
    VrcShutdown();          // after the release is written, never before
    if (g_nid.cbSize) Shell_NotifyIconA(NIM_DELETE, &g_nid);
    vr::VR_Shutdown();
    if (g_log) fclose(g_log);
    ExitProcess(code);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_TRAY && (l == WM_RBUTTONUP || l == WM_LBUTTONUP)) {
        HMENU menu = CreatePopupMenu();
        AppendMenuA(menu, MF_STRING | MF_GRAYED, 0, g_nid.szTip);
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(menu, MF_STRING, 1, "Open ArmSwing folder (settings, logs)");
        AppendMenuA(menu, MF_STRING, 2, "Quit ArmSwing");
        POINT pt; GetCursorPos(&pt);
        SetForegroundWindow(h);
        int c = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                               pt.x, pt.y, 0, h, NULL);
        DestroyMenu(menu);
        if (c == 1) ShellExecuteA(NULL, "open", g_exeDir, NULL, NULL, SW_SHOWNORMAL);
        if (c == 2) { LogF("exit", "quit from tray"); Bye(0); }
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

// Sleep that keeps the tray icon answering.
static void Wait(DWORD ms)
{
    for (DWORD end = GetTickCount() + ms;;) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        DWORD now = GetTickCount();
        if ((int)(end - now) <= 0) break;
        Sleep(end - now < 50 ? end - now : 50);
    }
}

static void Fail(const char* text)
{
    MessageBoxA(NULL, text, "ArmSwing", MB_OK | MB_ICONWARNING);
    Bye(1);
}

// ── The movement gate ─────────────────────────────────────────────────────
// VRChat plays its walk only while the avatar actually moves.  With the stick
// pushed but the avatar still (into a wall, camera fly mode, a seat), the
// hidden arms got VRChat's idle, the A-pose, and handing a settled arm over
// while standing still flashed a bent-elbow pose for a frame first (diag.4).
// So the arms join the walk only while VRChat says it is moving the avatar,
// and are otherwise tracked exactly as with the stick untouched:
//
//     walking = stick && (VRChat moving || no word from VRChat)
//
// "Moving" is VRChat's avatar parameter VelocityMagnitude, sent as OSC.
// VRChat finds apps over mDNS (OSCQuery) and sends to each app's own port,
// alongside VRCFT and the rest, so ArmSwing never binds 9000/9001 and its
// sockets are loopback only.  No word from VRChat (closed, OSC off, another
// app in front, discovery failed) means the stick alone decides, as before the gate.
// Only the main loop logs; the HTTP thread and Windows' callbacks set flags.
//
// Crouched below VRChat's own crouch line (Upright 0.65: under it VRChat caps
// the walk at 2 m/s and, with Standable, slides a sitting body) the hidden
// arms got the A-pose too, so they stay tracked there as well.
//
// Full body only: in 3-point VRChat shows a hidden arm the T-pose whenever its
// walk isn't fully playing (each start, each wall), so there the arms stay
// tracked.  VRChat sends TrackingType only on an avatar change or a
// calibration; until it has, full body is assumed, as tested.
//
//     walking = stick && (no word from VRChat || (moving && upright && full body))
static const float MOVING  = 0.20f;     // m/s; walking is 2-4, a wall or menu reads 0.00
static const float UPRIGHT = 0.65f;     // measured: >= 0.65 walks at 4 m/s, < 0.645 at 2
static const int   FULL_BODY = 6;       // VRChat's TrackingType for full-body tracking

static double NowMs()
{
    static double ms = 0;
    if (ms == 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); ms = 1000.0 / (double)f.QuadPart; }
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * ms;
}

static const char* AppOf(uint32_t pid, char* buf, uint32_t n)
{
    buf[0] = 0;
    if (vr::IVRApplications* a = vr::VRApplications())
        a->GetApplicationKeyByProcessId(pid, buf, n);
    if (!buf[0]) snprintf(buf, n, "pid %u", pid);
    return buf;
}

static bool  g_vrcScene = false;        // VRChat is the app in front   (main loop only)
static bool  g_vrcHeard = false;        // ...and its OSC has arrived    (main loop only)
static float g_vrcSpeed = 0;            // its last VelocityMagnitude    (main loop only)
static float g_vrcUpright = 1;          // its last Upright, 1 = standing (main loop only)
static int   g_vrcTracking = -1;        // its last TrackingType, -1 = not sent yet

// A gate must never act in any other game, and each VRChat starts from no word.
static void VrcScene(const char* app)
{
    g_vrcScene = strstr(app, "438100") != nullptr;      // VRChat's Steam app id
    g_vrcHeard = false;
    g_vrcSpeed = 0;
    g_vrcUpright = 1;
    g_vrcTracking = -1;
}

static SOCKET           g_osc = INVALID_SOCKET, g_qHttp = INVALID_SOCKET;
static int              g_qOscPort = 0, g_qHttpPort = 0;
static char             g_qHostInfo[320];
static CRITICAL_SECTION g_qLock;
static volatile LONG    g_annWanted = 0;    // VRChat's service just showed up: announce to it
static bool             g_annSent = false;  // main loop only

// Answers VRChat's two questions (osc.h).  One connection at a time; a client
// that stalls gets one second.
static DWORD WINAPI QServe(LPVOID)
{
    for (;;) {
        SOCKET c = accept(g_qHttp, nullptr, nullptr);
        if (c == INVALID_SOCKET) { Sleep(100); continue; }
        DWORD to = 1000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof to);
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof to);
        char req[2048];
        int n = 0;
        req[0] = 0;
        while (n < (int)sizeof req - 1) {
            int r = recv(c, req + n, (int)sizeof req - 1 - n, 0);
            if (r <= 0) break;
            n += r;
            req[n] = 0;
            if (strstr(req, "\r\n\r\n")) break;
        }

        OscQueryAnswer ans = OscQueryAnswerFor(req, g_qHostInfo);
        int bodyLen = ans.body ? (int)strlen(ans.body) : 0;
        char head[192];
        int headLen = snprintf(head, sizeof head, "HTTP/1.1 %d %s\r\nContent-Type: "
                               "application/json\r\nContent-Length: %d\r\nConnection: "
                               "close\r\n\r\n", ans.code, ans.code == 200 ? "OK" : ans.code == 404
                               ? "Not Found" : "Method Not Allowed", bodyLen);
        bool sent = send(c, head, headLen, 0) == headLen &&
                    (!bodyLen || send(c, ans.body, bodyLen, 0) == bodyLen);
        shutdown(c, SD_SEND);
        closesocket(c);
        (void)sent;
    }
}

// Windows' own mDNS search, loaded at run time so a Windows without it still
// runs (with no gate).  It lists VRChat's service, which is the cue to
// announce ArmSwing to it.
typedef DNS_STATUS (WINAPI* QBrowseFn)(PDNS_SERVICE_BROWSE_REQUEST, PDNS_SERVICE_CANCEL);
typedef DNS_STATUS (WINAPI* QCancelFn)(PDNS_SERVICE_CANCEL);
typedef VOID       (WINAPI* QDnsFreeFn)(PVOID, DNS_FREE_TYPE);
static struct {
    QBrowseFn  browse;
    QCancelFn  browseCancel;
    QDnsFreeFn dnsFree;
} g_dns;

static const char* const kType[2] = { "_oscjson._tcp", "_osc._udp" };

static DNS_SERVICE_BROWSE_REQUEST g_qFind;
static DNS_SERVICE_CANCEL         g_qFindCancel;
static volatile LONG              g_qFinding = 0;
static double                     g_qFindAt = 0;
static char                       g_qSeen[16][96];      // guarded by g_qLock
static int                        g_qSeenN = 0;

static VOID WINAPI QBrowsed(DWORD status, PVOID, PDNS_RECORD list)
{
    char name[96] = "";
    DWORD ttl = 0;
    for (DNS_RECORDW* r = (DNS_RECORDW*)list; r; r = r->pNext) {
        if (r->wType == DNS_TYPE_PTR && r->Data.PTR.pNameHost && !name[0]) {
            if (WideCharToMultiByte(CP_UTF8, 0, r->Data.PTR.pNameHost, -1, name,
                                    (int)sizeof name - 1, nullptr, nullptr) <= 0)
                strcpy(name, "?");
            name[sizeof name - 1] = 0;
            ttl = r->dwTtl;
        }
    }
    if (list) g_dns.dnsFree(list, DnsFreeRecordList);
    if (status != ERROR_SUCCESS) {
        InterlockedExchange(&g_qFinding, 0);                 // the main loop restarts it
        return;
    }
    if (!name[0]) return;
    bool gone = ttl == 0, vrchat = _strnicmp(name, "VRChat-Client", 13) == 0;
    EnterCriticalSection(&g_qLock);
    int k = 0;
    while (k < g_qSeenN && strcmp(g_qSeen[k], name) != 0) k++;
    bool known = k < g_qSeenN;
    if (!gone && !known && g_qSeenN < 16) memcpy(g_qSeen[g_qSeenN++], name, sizeof name);
    if (gone && known && k != --g_qSeenN) memcpy(g_qSeen[k], g_qSeen[g_qSeenN], sizeof name);
    LeaveCriticalSection(&g_qLock);
    if (!gone && !known && vrchat) InterlockedExchange(&g_annWanted, 1);
}

static void QBrowse()
{
    g_qFindAt = NowMs();
    g_qFind = DNS_SERVICE_BROWSE_REQUEST();
    g_qFind.Version = DNS_QUERY_REQUEST_VERSION1;
    g_qFind.QueryName = L"_oscjson._tcp.local";
    g_qFind.pBrowseCallback = QBrowsed;
    InterlockedExchange(&g_qFinding, 1);
    DNS_STATUS st = g_dns.browse(&g_qFind, &g_qFindCancel);
    if (st != DNS_REQUEST_PENDING) {
        InterlockedExchange(&g_qFinding, 0);
        LogF("oscq", "could not search for VRChat (status %ld), retrying every 30 s", (long)st);
    }
}

static SOCKET QLoopback(int type, int* port)
{
    SOCKET s = socket(AF_INET, type, type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP);
    if (s == INVALID_SOCKET) return s;
    BOOL excl = TRUE;                                    // nobody can bind on top of us
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof excl);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);          // nothing off this PC can reach it
    int len = sizeof a;                                  // port 0: Windows picks one
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || getsockname(s, (sockaddr*)&a, &len) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    *port = ntohs(a.sin_port);
    return s;
}

// Any failure here leaves g_osc closed: no word from VRChat, the stick decides.
static void QStart()
{
    InitializeCriticalSection(&g_qLock);
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) {
        LogF("oscq", "OFF: WSAStartup failed. The stick alone decides (arms swing whenever the stick is pushed)");
        return;
    }
    g_osc = QLoopback(SOCK_DGRAM, &g_qOscPort);
    g_qHttp = QLoopback(SOCK_STREAM, &g_qHttpPort);
    if (g_osc == INVALID_SOCKET || g_qHttp == INVALID_SOCKET || listen(g_qHttp, 8) != 0) {
        LogF("oscq", "OFF: could not open loopback sockets (err %d). The stick alone "
                     "decides (arms swing whenever the stick is pushed)", WSAGetLastError());
        if (g_osc != INVALID_SOCKET) closesocket(g_osc);
        if (g_qHttp != INVALID_SOCKET) closesocket(g_qHttp);
        g_osc = g_qHttp = INVALID_SOCKET;
        return;
    }
    u_long nb = 1;
    ioctlsocket(g_osc, FIONBIO, &nb);
    int big = 1 << 20;
    setsockopt(g_osc, SOL_SOCKET, SO_RCVBUF, (const char*)&big, sizeof big);
    OscQueryHostInfo(g_qHostInfo, sizeof g_qHostInfo, g_qOscPort);
    HANDLE t = CreateThread(nullptr, 0, QServe, nullptr, 0, nullptr);
    if (!t) {
        LogF("oscq", "OFF: no thread to answer VRChat. The stick alone decides (arms swing whenever the stick is pushed)");
        return;
    }
    CloseHandle(t);
    LogF("oscq", "OSC in on 127.0.0.1:%d, OSCQuery HTTP on 127.0.0.1:%d (picked by Windows, "
                 "loopback only: never 9000/9001, nothing shared with VRCFT or any other app)",
         g_qOscPort, g_qHttpPort);
    if (HMODULE d = LoadLibraryA("dnsapi.dll")) {
        g_dns.browse       = (QBrowseFn)(void*)GetProcAddress(d, "DnsServiceBrowse");
        g_dns.browseCancel = (QCancelFn)(void*)GetProcAddress(d, "DnsServiceBrowseCancel");
        g_dns.dnsFree      = (QDnsFreeFn)(void*)GetProcAddress(d, "DnsFree");
    }
    if (g_dns.browse && g_dns.browseCancel && g_dns.dnsFree) QBrowse();
    else LogF("oscq", "OFF: no DnsServiceBrowse (needs Windows 10 1809+), so VRChat can't "
                      "be found. The stick alone decides (arms swing whenever the stick is pushed)");
}

// This PC's IPv4 addresses, loopback left out.
static int LocalIPv4(ULONG* out, int max)
{
    char host[256];
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (gethostname(host, sizeof host) != 0 || getaddrinfo(host, nullptr, &hints, &res) != 0)
        return 0;
    int n = 0;
    for (addrinfo* p = res; p && n < max; p = p->ai_next) {
        ULONG ip = ((sockaddr_in*)p->ai_addr)->sin_addr.s_addr;
        if ((ntohl(ip) >> 24) != 127) out[n++] = ip;
    }
    freeaddrinfo(res);
    return n;
}

// One send to 224.0.0.251:5353 out the interface that owns `via`, from a port
// Windows picks.  The socket lives only for the send, so nothing meant for
// another app lands on it.
static int MdnsSend(const uint8_t* pkt, size_t len, ULONG via)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return WSAGetLastError();
    DWORD loop = 1, hops = 255;
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(0xE00000FB);              // 224.0.0.251
    to.sin_port = htons(5353);
    int err = 0;
    if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&via, sizeof via) != 0 ||
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof loop) != 0 ||
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&hops, sizeof hops) != 0 ||
        sendto(s, (const char*)pkt, (int)len, 0, (sockaddr*)&to, sizeof to) != (int)len)
        err = WSAGetLastError();
    closesocket(s);
    return err;
}

// Both announcements (where to ask, where to send) out every network adapter;
// ttl 0 = goodbye.  Windows' own announcing left out our 127.0.0.1 address
// (diag.3); of the three ways diag.4 tried, this is the one VRChat answered.
static void AnnSend(uint32_t ttl)
{
    const int port[2] = { g_qHttpPort, g_qOscPort };
    uint8_t pkt[2][320];
    size_t len[2];
    for (int i = 0; i < 2; i++) {
        char service[32], instance[80];
        snprintf(service, sizeof service, "%s.local", kType[i]);
        snprintf(instance, sizeof instance, "ArmSwing-%lu.%s", GetCurrentProcessId(), service);
        len[i] = MdnsAnnounce(pkt[i], sizeof pkt[i], service, instance, (uint16_t)port[i], ttl);
    }
    ULONG via[16];
    int nVia = LocalIPv4(via, 16);
    char list[200] = "";
    size_t o = 0;
    int sent = 0, err = 0;
    for (int v = 0; v < nVia; v++) {
        int ok = 0;
        for (int i = 0; i < 2; i++) {
            int e = len[i] ? MdnsSend(pkt[i], len[i], via[v]) : -1;
            if (e) err = e; else ok++;
        }
        sent += ok;
        const unsigned char* b = (const unsigned char*)&via[v];
        if (o < sizeof list - 24)
            o += snprintf(list + o, sizeof list - o, " %u.%u.%u.%u%s", b[0], b[1], b[2], b[3],
                          ok == 2 ? "" : " (FAILED)");
    }
    LogF("oscq", "%s ArmSwing to VRChat: %d of %d packets out, via%s%s (last error %d)",
         ttl ? "announced" : "goodbye", sent, 2 * nVia, nVia ? "" : " no adapter found",
         list, err);
}

// VRChat's word, drained BEFORE the stick decision: it is an input now.
static void VrcDrain()
{
    if (g_osc == INVALID_SOCKET) return;
    uint8_t buf[8192];
    for (int k = 0; k < 1024; k++) {
        int n = recv(g_osc, (char*)buf, sizeof buf, 0);
        if (n <= 0) break;
        OscParse(buf, (size_t)n, [](const char* addr, const OscValue& v) {
            g_vrcHeard = true;
            if (v.type == 'f' && strcmp(addr, "/avatar/parameters/VelocityMagnitude") == 0)
                g_vrcSpeed = v.f;
            if (v.type == 'f' && strcmp(addr, "/avatar/parameters/Upright") == 0)
                g_vrcUpright = v.f;
            if (v.type == 'i' && strcmp(addr, "/avatar/parameters/TrackingType") == 0)
                g_vrcTracking = v.i;
        });
    }
}

// After the writes: announce when VRChat's service shows up, and restart
// Windows' search if it ever ends.
static void VrcTick()
{
    if (g_osc == INVALID_SOCKET) return;
    if (InterlockedExchange(&g_annWanted, 0)) {
        AnnSend(4500);
        g_annSent = true;
    }
    if (g_qFindAt > 0 && !InterlockedCompareExchange(&g_qFinding, 0, 0) &&
        NowMs() - g_qFindAt > 30000)
        QBrowse();
}

// A goodbye, so VRChat stops sending to a port nobody reads.
static void VrcShutdown()
{
    if (InterlockedCompareExchange(&g_qFinding, 0, 0)) g_dns.browseCancel(&g_qFindCancel);
    if (g_annSent) AnnSend(0);
}


static void FindExeDir()
{
    GetModuleFileNameA(NULL, g_exeDir, MAX_PATH);
    char* last = strrchr(g_exeDir, '\\');
    if (last) *(last + 1) = '\0';
    else g_exeDir[0] = '\0';
}

int main()
{
    // Next to the exe, never the working directory: the driver reads it from
    // its own folder, and a shortcut or scheduled task can start us anywhere.
    FindExeDir();
    snprintf(g_stickPath, sizeof g_stickPath, "%sstick.txt", g_exeDir);
    const char* outPath = g_stickPath;

    // Two copies would fight over stick.txt, and with no window a second
    // launch is easy to do by accident.
    CreateMutexA(NULL, FALSE, "ArmSwingWatcher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxA(NULL, "ArmSwing is already running.\n\nLook for its icon "
                    "in the system tray (bottom right, next to the clock).",
                    "ArmSwing", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "ArmSwingTray";
    RegisterClassA(&wc);
    g_wnd = CreateWindowA("ArmSwingTray", "ArmSwing", 0, 0, 0, 0, 0,
                          NULL, NULL, wc.hInstance, NULL);
    Tray("waiting for SteamVR");

    char logPath[MAX_PATH], prevPath[MAX_PATH + 8];
    snprintf(logPath, sizeof logPath, "%sarmswing-watcher.log", g_exeDir);
    snprintf(prevPath, sizeof prevPath, "%s.prev", logPath);
    MoveFileExA(logPath, prevPath, MOVEFILE_REPLACE_EXISTING);
    g_log = fopen(logPath, "w");

    LogF("init", "armswing v1.0");
    LogF("init", "log=%s", logPath);
    LogF("init", "stick file=%s", outPath);

    // SteamVR refuses connections for a while after it starts (error 306,
    // Shared IPC Compositor Connect Failed), so wait rather than making the
    // launch order something to get right by hand.
    vr::EVRInitError err = vr::VRInitError_None;
    vr::IVRSystem* sys = nullptr;
    for (int attempt = 1; attempt <= 30; attempt++) {
        err = vr::VRInitError_None;
        sys = vr::VR_Init(&err, vr::VRApplication_Overlay);
        if (sys) break;
        if (attempt == 1 || attempt % 5 == 0)
            LogF("init", "waiting for SteamVR (attempt %d, err=%d %s)",
                 attempt, (int)err,
                 vr::VR_GetVRInitErrorAsEnglishDescription(err));
        Wait(2000);
    }
    if (!sys) {
        LogF("ERROR", "VR_Init gave up after 60s: %d (%s)",
             (int)err, vr::VR_GetVRInitErrorAsEnglishDescription(err));
        Fail("Could not connect to SteamVR.\n\nStart SteamVR first, then "
             "run ArmSwing again.");
    }
    LogF("init", "connected to SteamVR");

    // Register the app manifest.  Retried, because VR_Init succeeding does
    // not mean the rest of SteamVR is answering yet: v0.41 got
    // VRApplicationError_IPCFailed (105) here and VRInputError_IPCError (7)
    // on the action manifest a second later, one second after a connect that
    // itself took seven seconds.
    if (vr::IVRApplications* apps = vr::VRApplications()) {
        char mf[MAX_PATH];
        snprintf(mf, sizeof mf, "%sarmswing.vrmanifest", g_exeDir);
        vr::EVRApplicationError aerr = vr::VRApplicationError_IPCFailed;
        for (int attempt = 1; attempt <= 20; attempt++) {
            aerr = apps->AddApplicationManifest(mf, true);
            if (aerr == vr::VRApplicationError_None ||
                aerr == vr::VRApplicationError_AppKeyAlreadyExists) break;
            LogF("init", "AddApplicationManifest attempt %d: err=%d — retrying",
                 attempt, (int)aerr);
            Wait(1000);
        }
        LogF("init", "AddApplicationManifest: err=%d path=%s", (int)aerr, mf);
        LogF("init", "app installed: %d",
             (int)apps->IsApplicationInstalled("armswing.watcher"));
    } else {
        LogF("warn", "IVRApplications unavailable");
    }

    // Overlay handle — makes SteamVR route input to us
    if (vr::IVROverlay* overlay = vr::VROverlay()) {
        vr::VROverlayHandle_t h = vr::k_ulOverlayHandleInvalid;
        vr::EVROverlayError oerr = overlay->CreateOverlay(
            "armswing.watcher", "ArmSwing", &h);
        LogF("init", "CreateOverlay: err=%d handle=%llu",
             (int)oerr, (unsigned long long)h);
    } else {
        LogF("warn", "IVROverlay unavailable");
    }

    vr::IVRInput* input = vr::VRInput();
    if (!input) {
        LogF("ERROR", "IVRInput unavailable");
        Fail("SteamVR's input system is not available.\n\nRestart SteamVR, "
             "then run ArmSwing again.");
    }

    char manifestPath[MAX_PATH];
    snprintf(manifestPath, sizeof manifestPath, "%sactions.json", g_exeDir);

    // Everything below is retried as one unit until SteamVR's input system
    // answers.  Previously any IPC hiccup here exited the process, which
    // left stick.txt frozen and made the whole feature look switched off
    // even though the driver was running fine.
    vr::VRActionSetHandle_t actionSet = vr::k_ulInvalidActionSetHandle;
    vr::VRActionHandle_t stickPos = vr::k_ulInvalidActionHandle;
    vr::EVRInputError ierr = vr::VRInputError_IPCError;

    for (int attempt = 1; attempt <= 60; attempt++) {
        ierr = input->SetActionManifestPath(manifestPath);
        if (ierr == vr::VRInputError_None) {
            input->GetActionSetHandle("/actions/armswing", &actionSet);
            input->GetActionHandle("/actions/armswing/in/left_stick_pos",
                                   &stickPos);
            if (actionSet != vr::k_ulInvalidActionSetHandle &&
                stickPos  != vr::k_ulInvalidActionHandle) break;
        }
        if (attempt == 1 || attempt % 5 == 0)
            LogF("init", "input not ready (attempt %d, manifest err=%d, "
                         "set=%llu stick=%llu) — retrying",
                 attempt, (int)ierr, (unsigned long long)actionSet,
                 (unsigned long long)stickPos);
        Wait(1000);
    }

    if (ierr != vr::VRInputError_None ||
        stickPos == vr::k_ulInvalidActionHandle) {
        LogF("ERROR", "input system never came up (manifest err=%d). err=7 is "
                      "VRInputError_IPCError, not a bad actions.json. Leave "
                      "SteamVR running and start this again.", (int)ierr);
        Fail("SteamVR's input system did not answer.\n\nLeave SteamVR "
             "running and start ArmSwing again.");
    }
    LogF("init", "action manifest loaded; set=%llu stick=%llu",
         (unsigned long long)actionSet, (unsigned long long)stickPos);

    // Capacitive touch sensors — l1_sensor/l2_sensor and r1_sensor/
    // r2_sensor from the profile.  Digital and immediate, so the lightest
    // touch registers; finger curl needed a threshold and always lagged a
    // real squeeze.
    vr::VRActionHandle_t senL = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t senR = vr::k_ulInvalidActionHandle;
    input->GetActionHandle("/actions/armswing/in/left_hand_sensor", &senL);
    input->GetActionHandle("/actions/armswing/in/right_hand_sensor", &senR);
    LogF("init", "sensor handles %llu/%llu",
         (unsigned long long)senL, (unsigned long long)senR);

    vr::VRActionHandle_t useL = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t useR = vr::k_ulInvalidActionHandle;
    ierr = input->GetActionHandle("/actions/armswing/in/left_hand_use", &useL);
    LogF("init", "GetActionHandle(left_hand_use): err=%d handle=%llu",
         (int)ierr, (unsigned long long)useL);
    ierr = input->GetActionHandle("/actions/armswing/in/right_hand_use", &useR);
    LogF("init", "GetActionHandle(right_hand_use): err=%d handle=%llu",
         (int)ierr, (unsigned long long)useR);
    if (useL == vr::k_ulInvalidActionHandle || useR == vr::k_ulInvalidActionHandle)
        LogF("WARN", "no grip actions — a lowered arm cannot be woken");

    DumpDevices(sys, "startup");
    RoleState roles = ReadRoles(sys);
    LogRoles(sys, roles, "startup");

    // Schmitt trigger on the stick magnitude. Start walking over START, keep
    // walking until under STOP. This is where flicker protection belongs: it
    // absorbs an analog dip mid-walk without delaying a real release, which
    // reads 0.000 and trips STOP on the same poll. The driver used to debounce
    // the release instead, and that delay was the A-pose leaving the animation.
    const float START = 0.15f, STOP = 0.10f;
    LogF("init", "watching left stick, start=%.2f stop=%.2f", START, STOP);
    QStart();
    char sceneApp[128] = "none";
    if (vr::IVRApplications* a = vr::VRApplications())
        if (uint32_t pid = a->GetCurrentSceneProcessId()) AppOf(pid, sceneApp, sizeof sceneApp);
    VrcScene(sceneApp);
    Tray("Running. Arms will swing while you walk. Right-click this icon "
         "to quit.", true);
    Tray("running");

    bool lastStick = false, lastWalking = false, lastGate = false;
    bool lastBtnL = false, lastBtnR = false;
    bool lastCurlL = false, lastCurlR = false;
    bool firstWrite  = true;
    bool firstHands  = true;
    bool lastActive  = true;
    bool writeFailing = false;     // log a failed write once, not every retry
    int  polls = 0;

    for (;;) {
        // Leave with SteamVR instead of lingering as a hidden process.
        vr::VREvent_t ev;
        while (sys->PollNextEvent(&ev, sizeof ev)) {
            if (ev.eventType == vr::VREvent_Quit) {
                sys->AcknowledgeQuit_Exiting();
                LogF("exit", "SteamVR is closing");
                Bye(0);
            }
            if (ev.eventType == vr::VREvent_SceneApplicationChanged)
                VrcScene(AppOf(ev.data.process.pid, sceneApp, sizeof sceneApp));
        }

        // ── the decisive probe: has SteamVR's hand role resolution moved? ──
        // Diagnostics only, and ReadRoles fetches poses for all 64 device
        // slots plus role and string properties.  At the 125Hz poll rate
        // that was the bulk of this process's CPU (and the fans with it),
        // so it now runs ~4x a second.  A role dropout lasts ~15ms and was
        // never reliably caught at any rate; the driver log is the record
        // that matters.
        if (polls % 32 == 0) {
            RoleState now = ReadRoles(sys);
            if (now != roles) {
                roles = now;
                LogRoles(sys, roles, "CHANGED");
            }
        }

        // Periodic full state, roughly every 15s
        if (polls > 0 && polls % 3750 == 0) {
            LogRoles(sys, roles, "periodic");
            DumpDevices(sys, "periodic");
        }

        vr::VRActiveActionSet_t active = {};
        active.ulActionSet = actionSet;
        input->UpdateActionState(&active, sizeof(active), 1);

        vr::InputAnalogActionData_t data = {};
        ierr = input->GetAnalogActionData(stickPos, &data, sizeof(data),
                                          vr::k_ulInvalidInputValueHandle);

        if (data.bActive != lastActive) {
            lastActive = data.bActive;
            LogF("stick", "action %s (err=%d)",
                 data.bActive ? "ACTIVE" : "INACTIVE — binding lost?", (int)ierr);
            Tray(data.bActive ? "running"
                              : "stick not bound, or SteamVR dashboard open");
        }

        // Losing the action set (the dashboard takes it) must stop the walk,
        // not freeze the last value: v0.50 held "1" for 3.5s and the arms
        // kept swinging through it.
        float mag = data.bActive
                  ? sqrtf(data.x * data.x + data.y * data.y) : 0.0f;
        bool stick = data.bActive && (mag > (lastStick ? STOP : START));
        lastStick = stick;

        // The movement gate: a pushed stick hides the arms only while VRChat
        // is moving the avatar, above its crouch line, in full body.
        // Releasing the stick still writes 0 at once.
        VrcDrain();
        bool gate = g_vrcScene && g_vrcHeard;
        bool fullBody = g_vrcTracking == FULL_BODY || g_vrcTracking < 0;
        bool walking = stick && (!gate || (g_vrcSpeed > MOVING && g_vrcUpright >= UPRIGHT &&
                                           fullBody));

        // State is committed only once the driver can actually read it: a
        // lost "0" used to strand the arms parked until the next push.
        if (walking != lastWalking || firstWrite) {
            bool wrote = WriteFile1(outPath, walking ? "1\n" : "0\n");
            if (wrote || !writeFailing)
                LogF("stick", "%s  x=%+.3f y=%+.3f mag=%.3f  VRChat %sspeed %.2f upright %.2f "
                     "tracking %d  write=%s", walking ? "WALKING" : "idle   ", data.x, data.y,
                     mag, gate ? "" : "(no word) ", g_vrcSpeed, g_vrcUpright, g_vrcTracking,
                     wrote ? "ok" : "FAILED, retrying");
            writeFailing = !wrote;
            if (wrote) {
                lastWalking = walking;
                firstWrite = false;
                RoleState r2 = ReadRoles(sys);
                LogRoles(sys, r2, walking ? "post-WALK" : "post-idle");
                roles = r2;
            }
        }

        // ── wake signals -> hands.txt (sensorL sensorR gripL gripR) ──
        if (useL != vr::k_ulInvalidActionHandle) {
            bool sL = false, sR = false, gL = false, gR = false;
            vr::VRActionHandle_t hs[4] = { senL, senR, useL, useR };
            bool* out[4] = { &sL, &sR, &gL, &gR };
            for (int i = 0; i < 4; i++) {
                if (hs[i] == vr::k_ulInvalidActionHandle) continue;
                vr::InputDigitalActionData_t d = {};
                if (input->GetDigitalActionData(
                        hs[i], &d, sizeof(d), vr::k_ulInvalidInputValueHandle)
                        == vr::VRInputError_None && d.bActive && d.bState)
                    *out[i] = true;
            }

            if (sL != lastCurlL || sR != lastCurlR ||
                gL != lastBtnL  || gR != lastBtnR  || firstHands) {
                char body[8], dst[512];
                snprintf(body, sizeof body, "%d%d%d%d\n", sL ? 1 : 0, sR ? 1 : 0,
                         gL ? 1 : 0, gR ? 1 : 0);
                snprintf(dst, sizeof dst, "%shands.txt", g_exeDir);
                // Same rule as stick.txt: a lost write is retried, else a
                // stale grip=1 keeps that arm awake (never animating).
                if (WriteFile1(dst, body)) {
                    lastCurlL = sL; lastCurlR = sR;
                    lastBtnL = gL;  lastBtnR = gR;
                    firstHands = false;
                    LogF("use", "sensor L=%d R=%d | grip L=%d R=%d",
                         (int)sL, (int)sR, (int)gL, (int)gR);
                }
            }
        }

        VrcTick();
        if (gate != lastGate) {
            lastGate = gate;
            LogF("oscq", gate ? "VRChat's OSC arriving: the arms join the walk only while "
                                "VRChat moves you"
                              : "no word from VRChat: the stick alone decides (arms swing whenever the stick is pushed)");
        }


        polls++;
        Wait(8);    // ~125 Hz; stick.txt only needs to be prompt, not fast
    }

    vr::VR_Shutdown();
    return 0;
}
