// armswing — VRChat walk-cycle arm swing while stick-walking.
//
// HOW IT WORKS.  VRChat plays its walk-cycle arm animation for any hand that
// reads untracked.  Our DLL is loaded inside vrserver, the same process the
// real controller driver publishes poses through, so at startup we re-point
// the server's TrackedDevicePoseUpdated table entry at Intercept().  Every
// pose the real driver publishes for a hand passes through it; while that arm
// should animate we clear poseIsValid on the way past and change nothing
// else.  The real pose stays visible to us throughout, so an arm is woken by
// simply raising it.  Per hand, because VRChat animates each arm on its own.
//
// The whole driver is one decision per hand, once per frame:
//
//     park = walking && !raised && !awake
//
// WHAT WAS TRIED FIRST, so nobody tries it again (v0.13 - v0.78, all measured):
//
//  - Substituting the hand with a proxy device through a TrackingOverride
//    works, but VRChat drops hand IK for one frame whenever the override
//    engages or disengages: the avatar flashes its rest pose and, with full
//    body calibrated, the view clips sideways.  That flash survived every
//    change to the published pose (position to 0-2mm, rotation, velocity,
//    validity, timing), and a substituted hand cannot be read back anywhere
//    in SteamVR, so it could not be woken by raising it either.
//  - Calling TrackedDevicePoseUpdated on another driver's device is discarded.
//  - A bare validity change on the real device — this build — does not flash.
//
// STILL TRUE HERE:
//
//  - Release must be IMMEDIATE.  A delay between the stick centring and the
//    arm being handed back leaves it untracked while VRChat's locomotion
//    animator heads for idle, which shows as a sustained rest pose after
//    every walk.  Three builds shipped a stop delay (600, 150, 450ms) and all
//    three did this, so only the stick's rising edge is debounced.
//  - RunFrame runs at ~45-70Hz, not the headset's refresh rate.  Durations
//    are milliseconds off GetTickCount64, never frame counts.
//
// mode.txt: off = inert (nothing intercepted at startup), 0 = always park,
//           1 = never park, 9 = auto (follow the stick).

#include <windows.h>
#include <openvr_driver.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

using namespace vr;

#define HS_VERSION      "1.0"
#define WALK_HOLD_DEF   90    // stick held this long before the arms park
#define WAKE_HOLD_DEF   120   // a wake input keeps its arm awake this long
#define RAISE_SLACK     0.05f // metres of hysteresis under the raise line
#define POLL_MS         16
#define MODE_MS         1000
#define BEAT_MS         3000

static std::string g_dir;
static FILE*       g_log = nullptr;

// RunFrame thread only.  Intercept() runs on the real driver's threads and
// must never call this.
static void Log(const char* tag, const char* fmt, ...)
{
    char msg[768];
    va_list a; va_start(a, fmt);
    vsnprintf(msg, sizeof msg, fmt, a);
    va_end(a);
    if (!g_log) {
        char line[896];
        snprintf(line, sizeof line, "[%-5s] %s\n", tag, msg);
        if (VRDriverLog()) VRDriverLog()->Log(line);
        return;
    }
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d [%-5s] %s\n",
            t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, tag, msg);
    fflush(g_log);
}

static void FindDriverDir()
{
    HMODULE m = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&FindDriverDir, &m)) return;
    char p[MAX_PATH];
    DWORD n = GetModuleFileNameA(m, p, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    std::string s(p, n);
    for (int i = 0; i < 3; i++) {          // bin\win64\driver.dll -> driver root
        size_t k = s.find_last_of("\\/");
        if (k == std::string::npos) return;
        s.resize(k);
    }
    g_dir = s;
}

static std::string PathOf(const char* f)
{ return g_dir.empty() ? std::string(f) : g_dir + "\\" + f; }

static int ReadMode()
{
    FILE* f = fopen(PathOf("mode.txt").c_str(), "r");
    if (!f) return 9;
    char b[16] = {};
    fgets(b, sizeof b, f);
    fclose(f);
    if (b[0] == 'o' || b[0] == 'O') return -1;
    if (b[0] == '0') return 0;
    if (b[0] == '1') return 1;
    return 9;
}

struct Config {
    float    raiseDrop = 0.70f;
    bool     touchWake = false;
    uint64_t walkHold  = WALK_HOLD_DEF;
    uint64_t wakeHold  = WAKE_HOLD_DEF;
};

static void ReadConfig(Config& c)
{
    c = Config{};
    FILE* f = fopen(PathOf("config.txt").c_str(), "r");
    if (!f) return;
    char line[160] = {}, word[32] = {};
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "raise_drop=%f", &c.raiseDrop) == 1) continue;
        if (sscanf(line, "walk_hold=%llu", &c.walkHold) == 1) continue;
        if (sscanf(line, "wake_hold=%llu", &c.wakeHold) == 1) continue;
        if (sscanf(line, "wake_mode=%31s", word) == 1)
            c.touchWake = (strncmp(word, "touch", 5) == 0);
    }
    fclose(f);
    // Clamps are the trust boundary: config.txt is hand-edited.
    if (c.raiseDrop < 0.05f || c.raiseDrop > 2.0f) c.raiseDrop = 0.70f;
    if (c.walkHold > 3000) c.walkHold = WALK_HOLD_DEF;
    if (c.wakeHold > 5000) c.wakeHold = WAKE_HOLD_DEF;
}

// Written by armswing.exe.  "sensorL sensorR gripL gripR".
static void ReadWake(bool& cL, bool& cR, bool& gL, bool& gR)
{
    FILE* f = fopen(PathOf("hands.txt").c_str(), "r");
    if (!f) return;
    char b[16] = {};
    if (fgets(b, sizeof b, f) && strlen(b) >= 4) {
        cL = b[0] == '1'; cR = b[1] == '1';
        gL = b[2] == '1'; gR = b[3] == '1';
    }
    fclose(f);
}

static bool ReadStick()
{
    FILE* f = fopen(PathOf("stick.txt").c_str(), "r");
    if (!f) return false;
    int c = fgetc(f);
    fclose(f);
    return c == '1';
}

// ---------------------------------------------------------------------------
// The intercept.  Shared between the real driver's pose threads (Intercept)
// and vrserver's RunFrame thread (Provider), so everything here is atomic and
// nothing here logs or blocks.
// ---------------------------------------------------------------------------
struct Side {
    std::atomic<uint32_t> real{k_unTrackedDeviceIndexInvalid};
    std::atomic<bool>     park{false};    // RunFrame writes, Intercept reads
    std::atomic<bool>     raised{false};  // Intercept writes, RunFrame reads
    std::atomic<float>    y{0.0f};        // last real height, for the log
    std::atomic<uint64_t> seen{0};        // poses that passed through
    std::atomic<uint64_t> foreign{0};     // ...with a struct size we don't know
};
static Side               g_side[2];             // 0 = left, 1 = right
static std::atomic<float> g_gate{1.0e9f};        // raise line; high = nobody raised

typedef void (*PoseFn)(void*, uint32_t, const DriverPose_t&, uint32_t);
static PoseFn g_orig[2] = {};
static void** g_slot[2] = {};

// Height in the tracking universe: rotate the driver-space position by
// qWorldFromDriverRotation, add the translation, keep Y.  Same space as the
// headset pose RunFrame derives the raise line from.
static float WorldY(const DriverPose_t& p)
{
    const HmdQuaternion_t& q = p.qWorldFromDriverRotation;
    const double* v = p.vecPosition;
    double y = v[1];
    if (q.w != 0.0 || q.x != 0.0 || q.y != 0.0 || q.z != 0.0) {
        double tx = 2.0 * (q.y * v[2] - q.z * v[1]);
        double ty = 2.0 * (q.z * v[0] - q.x * v[2]);
        double tz = 2.0 * (q.x * v[1] - q.y * v[0]);
        y = v[1] + q.w * ty + (q.z * tx - q.x * tz);
    }
    return (float)(y + p.vecWorldFromDriverTranslation[1]);
}


static void Intercept(PoseFn orig, void* self, uint32_t id,
                      const DriverPose_t& pose, uint32_t size)
{
    for (Side& s : g_side) {
        if (id != s.real.load(std::memory_order_relaxed)) continue;
        s.seen.fetch_add(1, std::memory_order_relaxed);
        // A driver built against a different DriverPose_t layout: pass it
        // through untouched rather than guess where the fields are.
        if (size != sizeof(DriverPose_t)) {
            s.foreign.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        if (pose.poseIsValid) {
            float y = WorldY(pose), gate = g_gate.load(std::memory_order_relaxed);
            s.y.store(y, std::memory_order_relaxed);
            if (y > gate)                    s.raised.store(true);
            else if (y < gate - RAISE_SLACK) s.raised.store(false);
        }
        if (!s.park.load()) break;
        DriverPose_t p = pose;               // the ONLY change we ever make
        p.poseIsValid = false;
        p.result = TrackingResult_Running_OutOfRange;
        orig(self, id, p, size);
        return;
    }
    orig(self, id, pose, size);
}

static void Hook0(void* s, uint32_t id, const DriverPose_t& p, uint32_t n)
{ Intercept(g_orig[0], s, id, p, n); }
static void Hook1(void* s, uint32_t id, const DriverPose_t& p, uint32_t n)
{ Intercept(g_orig[1], s, id, p, n); }

// TrackedDevicePoseUpdated is entry 1 of IVRServerDriverHost in both
// interface versions drivers are built against.  The real controller driver
// may use either, so both tables are re-pointed; if they share one table the
// second pass sees our entry and skips.
static void Install()
{
    const char* vers[2] = { "IVRServerDriverHost_006", "IVRServerDriverHost_005" };
    PoseFn      hook[2] = { Hook0, Hook1 };
    for (int i = 0; i < 2; i++) {
        EVRInitError e = VRInitError_None;
        void* host = VRDriverContext()->GetGenericInterface(vers[i], &e);
        if (!host) { Log("hook", "%s: not offered (err=%d)", vers[i], (int)e); continue; }
        void** slot = *(void***)host + 1;
        if (*slot == (void*)Hook0 || *slot == (void*)Hook1) {
            Log("hook", "%s: shares a table already intercepted", vers[i]);
            continue;
        }
        DWORD old = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
            Log("hook", "%s: VirtualProtect failed (%lu) — not intercepted",
                vers[i], GetLastError());
            continue;
        }
        g_orig[i] = (PoseFn)*slot;
        g_slot[i] = slot;
        *slot = (void*)hook[i];
        VirtualProtect(slot, sizeof(void*), old, &old);
        Log("hook", "%s: intercepted", vers[i]);
    }
}

// Put the table back, but only where the entry is still ours: if another tool
// chained on after us, pulling our entry out would cut it off too.
static void Uninstall()
{
    PoseFn hook[2] = { Hook0, Hook1 };
    for (int i = 0; i < 2; i++) {
        if (!g_slot[i]) continue;
        DWORD old = 0;
        if (*g_slot[i] == (void*)hook[i] &&
            VirtualProtect(g_slot[i], sizeof(void*), PAGE_READWRITE, &old)) {
            *g_slot[i] = (void*)g_orig[i];
            VirtualProtect(g_slot[i], sizeof(void*), old, &old);
        }
        g_slot[i] = nullptr;
    }
}

// ---------------------------------------------------------------------------
class Provider : public IServerTrackedDeviceProvider
{
public:
    EVRInitError Init(IVRDriverContext* ctx) override
    {
        VR_INIT_SERVER_DRIVER_CONTEXT(ctx);
        FindDriverDir();
        if (!g_dir.empty()) {
            // Keep the previous session's log: problems get reported after
            // SteamVR has already been restarted.
            std::string log = g_dir + "\\armswing.log";
            MoveFileExA(log.c_str(), (log + ".prev").c_str(), MOVEFILE_REPLACE_EXISTING);
            g_log = fopen(log.c_str(), "w");
        }
        m_now = m_startAt = GetTickCount64();
        Log("init", "armswing v%s", HS_VERSION);

        m_mode = ReadMode();
        if (m_mode == -1) {
            Log("init", "mode.txt=off — inert, nothing intercepted");
            m_disabled = true;
            return VRInitError_None;
        }
        ReadConfig(m_cfg);
        Log("init", "mode=%d  raise line = headset Y - %.2fm  wake_mode=%s",
            m_mode, m_cfg.raiseDrop, m_cfg.touchWake ? "touch" : "grip");
        Log("init", "walk_hold=%llums  wake_hold=%llums  release=immediate",
            m_cfg.walkHold, m_cfg.wakeHold);
        Install();
        return VRInitError_None;
    }

    void Cleanup() override
    {
        g_side[0].park = g_side[1].park = false;
        Uninstall();
        Log("exit", "cleanup — intercept removed");
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
        if (g_log) { fclose(g_log); g_log = nullptr; }
    }

    const char* const* GetInterfaceVersions() override
    { return k_InterfaceVersions; }

    void RunFrame() override
    {
        if (m_disabled) return;
        m_now = GetTickCount64();
        m_frames++;

        if (!m_ready) {
            if (Due(m_findAt, 300)) Discover();
            if (!m_ready) return;
        }
        if (Due(m_modeAt, MODE_MS)) PollMode();
        if (m_disabled) return;
        if (Due(m_pollAt, POLL_MS)) PollInputs();

        TrackedDevicePose_t all[k_unMaxTrackedDeviceCount];
        VRServerDriverHost()->GetRawTrackedDevicePoses(
            0.0f, all, k_unMaxTrackedDeviceCount);
        if (all[0].bPoseIsValid)
            g_gate = all[0].mDeviceToAbsoluteTracking.m[1][3] - m_cfg.raiseDrop;

        // Rising edge debounced by walk_hold; falling edge NOT debounced —
        // a stop delay strands the arms in the animation (see header).
        bool want = (m_mode == 0) ? true : (m_mode == 1) ? false : m_stick;
        if (want != m_walking && (!want || m_now - m_stickAt >= m_cfg.walkHold)) {
            m_walking = want;
            Log("stick", "%s", want ? "WALKING" : "STOPPED");
        }

        for (int i = 0; i < 2; i++) {
            // THE decision.  `raised` is live even while parked, because the
            // real pose passes through Intercept() the whole time.
            bool park = m_walking && !g_side[i].raised && !Awake(i);
            if (park != g_side[i].park.exchange(park))
                Log("park", "%s %s", i ? "R" : "L",
                    park ? "-> walk animation" : "-> real hand");
        }

        if (Due(m_beatAt, BEAT_MS)) Heartbeat(all);
    }

    bool ShouldBlockStandbyMode() override { return false; }
    void EnterStandby() override {}
    void LeaveStandby() override {}

private:
    bool Due(uint64_t& slot, uint64_t ms)
    {
        if (m_now - slot < ms) return false;
        slot = m_now;
        return true;
    }

    bool Awake(int i) const
    {
        return m_now - m_gripAt[i]  < m_cfg.wakeHold ||
               m_now - m_touchAt[i] < m_cfg.wakeHold;
    }

    void PollInputs()
    {
        bool s = ReadStick();
        if (s != m_stick) {
            m_stick = s;
            m_stickAt = m_now;
            Log("stick", "%s", s ? "pushed" : "released");
        }
        bool c[2] = {}, g[2] = {};
        ReadWake(c[0], c[1], g[0], g[1]);
        for (int i = 0; i < 2; i++) {
            if (g[i]) m_gripAt[i] = m_now;
            if (m_cfg.touchWake && c[i]) m_touchAt[i] = m_now;
        }
    }

    void PollMode()
    {
        Config c; ReadConfig(c);
        if (c.raiseDrop != m_cfg.raiseDrop || c.walkHold != m_cfg.walkHold ||
            c.wakeHold  != m_cfg.wakeHold  || c.touchWake != m_cfg.touchWake)
            Log("cfg", "raise_drop=%.2fm walk_hold=%llums wake_hold=%llums "
                       "wake_mode=%s",
                c.raiseDrop, c.walkHold, c.wakeHold,
                c.touchWake ? "touch" : "grip");
        m_cfg = c;

        int mode = ReadMode();
        if (mode == -1) {
            // The intercept stays in the table but passes everything through.
            Log("mode", "off — both hands released, passing poses through");
            g_side[0].park = g_side[1].park = false;
            m_disabled = true;
            return;
        }
        if (mode == m_mode) return;
        Log("mode", "%d -> %d", m_mode, mode);
        m_mode = mode;
        m_stickAt = 0;          // apply the new mode on the next frame
    }

    // `seen` climbing is the proof the real driver's poses pass through us;
    // `srv v` is what the server reports for the hand afterwards, which must
    // read 0 while parked and 1 otherwise.
    void Heartbeat(const TrackedDevicePose_t* all)
    {
        double secs = (m_now - m_startAt) / 1000.0;
        Log("beat", "mode=%d %s stick=%d  raise line y=%.2f  hz=%.0f",
            m_mode, m_walking ? "WALKING" : "idle", (int)m_stick,
            g_gate.load(), secs > 0 ? m_frames / secs : 0.0);
        for (int i = 0; i < 2; i++) {
            Side& s = g_side[i];
            uint32_t d = s.real;
            Log("beat", "  %s dev%u park=%d raised=%d awake=%d  real y=%.2f  "
                        "srv v=%d  seen=%llu foreign=%llu",
                i ? "R" : "L", d, (int)s.park, (int)s.raised, (int)Awake(i),
                s.y.load(),
                (int)all[d < k_unMaxTrackedDeviceCount ? d : 0].bPoseIsValid,
                s.seen.load(), s.foreign.load());
        }
    }

    // Find the real hands by role hint: any controller-class device with a
    // hand role, so this works on any headset.
    void Discover()
    {
        uint32_t found[2] = { k_unTrackedDeviceIndexInvalid,
                              k_unTrackedDeviceIndexInvalid };
        char name[2][160] = {};

        for (uint32_t i = 0; i < k_unMaxTrackedDeviceCount; i++) {
            PropertyContainerHandle_t c =
                VRProperties()->TrackedDeviceToPropertyContainer(i);
            if (c == k_ulInvalidPropertyContainer) continue;
            ETrackedPropertyError e;
            if (VRProperties()->GetInt32Property(c, Prop_DeviceClass_Int32, &e)
                    != TrackedDeviceClass_Controller || e != TrackedProp_Success)
                continue;
            int32_t role = VRProperties()->GetInt32Property(
                c, Prop_ControllerRoleHint_Int32, &e);
            if (e != TrackedProp_Success) continue;
            int side = role == TrackedControllerRole_LeftHand  ? 0
                     : role == TrackedControllerRole_RightHand ? 1 : -1;
            if (side < 0 || found[side] != k_unTrackedDeviceIndexInvalid) continue;
            found[side] = i;
            VRProperties()->GetStringProperty(c, Prop_SerialNumber_String,
                                              name[side], sizeof name[side], &e);
        }

        if (found[0] == k_unTrackedDeviceIndexInvalid ||
            found[1] == k_unTrackedDeviceIndexInvalid) {
            if (Due(m_waitAt, 10000))
                Log("init", "waiting for real controllers (L=%d R=%d)",
                    (int)found[0], (int)found[1]);
            return;
        }
        g_side[0].real = found[0];
        g_side[1].real = found[1];
        m_ready = true;
        Log("init", "real controllers: L=%u %s | R=%u %s",
            found[0], name[0], found[1], name[1]);
    }


    bool     m_ready = false, m_disabled = false;
    int      m_mode = 9;
    Config   m_cfg;
    bool     m_stick = false, m_walking = false;
    uint64_t m_gripAt[2] = {}, m_touchAt[2] = {};
    uint64_t m_now = 0, m_startAt = 0, m_frames = 0, m_stickAt = 0;
    uint64_t m_pollAt = 0, m_modeAt = 0, m_beatAt = 0, m_findAt = 0, m_waitAt = 0;
};

static Provider g_provider;

extern "C" __declspec(dllexport)
void* HmdDriverFactory(const char* name, int* returnCode)
{
    if (name && strcmp(name, IServerTrackedDeviceProvider_Version) == 0)
        return &g_provider;
    if (returnCode) *returnCode = VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
