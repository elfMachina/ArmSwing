// osc.h - minimal OSC 1.0 reader for VRChat's avatar-parameter output, and
// the answers to VRChat's two OSCQuery HTTP questions.
//
// The watcher's movement gate reads VRChat's speed through this.  Pure
// parsing, no I/O, so it is unit-tested on its own (test/osc_test.cpp).  Packets come from the network, so every length is
// checked before it is read and anything malformed is dropped, never guessed.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

struct OscValue {
    char    type = 0;     // 'f' float, 'i' int, 'T'/'F' bool, 's' string (value not kept)
    float   f = 0.0f;
    int32_t i = 0;
};

static inline uint32_t OscBE32(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static inline size_t OscPad4(size_t n) { return (n + 3) & ~(size_t)3; }

// Calls cb(address, value) for each message in the packet, bundles flattened.
// Only the first argument of a message is read: avatar parameters have one.
template <class F>
static void OscParse(const uint8_t* p, size_t n, F&& cb, int depth = 0)
{
    if (!p || n < 4 || depth > 4) return;

    if (n >= 16 && memcmp(p, "#bundle", 8) == 0) {
        size_t o = 16;                                   // "#bundle\0" + timetag
        while (o + 4 <= n) {
            uint32_t len = OscBE32(p + o);
            o += 4;
            if (len > n - o) return;                     // truncated: stop
            OscParse(p + o, len, cb, depth + 1);
            o += len;
        }
        return;
    }

    if (p[0] != '/') return;
    const char* addr = (const char*)p;
    size_t alen = strnlen(addr, n);
    if (alen == n) return;                               // unterminated address
    size_t o = OscPad4(alen + 1);
    if (o >= n || p[o] != ',') return;
    const char* tags = (const char*)p + o;
    size_t tlen = strnlen(tags, n - o);
    if (tlen == n - o || tlen < 2) return;               // unterminated, or no argument
    size_t a = o + OscPad4(tlen + 1);                    // first argument

    OscValue v;
    v.type = tags[1];
    switch (v.type) {
        case 'f': {
            if (a > n || n - a < 4) return;
            uint32_t u = OscBE32(p + a);
            memcpy(&v.f, &u, sizeof v.f);
            break;
        }
        case 'i':
            if (a > n || n - a < 4) return;
            v.i = (int32_t)OscBE32(p + a);
            break;
        case 'T': case 'F': case 's':
            break;
        default:
            return;                                      // types we never need
    }
    cb(addr, v);
}

// ── OSCQuery's HTTP side: what VRChat is told when it asks ──
// VRChat asks GET /?HOST_INFO (where to send OSC) and GET / (which addresses
// we take).  /avatar is what makes VRChat send /avatar/change and every
// /avatar/parameters/* message; nothing else is asked for.
#define OSCQ_CHANGE "{\"FULL_PATH\":\"/avatar/change\",\"ACCESS\":2,\"TYPE\":\"s\"," \
                    "\"DESCRIPTION\":\"\"}"
#define OSCQ_AVATAR "{\"FULL_PATH\":\"/avatar\",\"ACCESS\":2,\"DESCRIPTION\":\"\"," \
                    "\"CONTENTS\":{\"change\":" OSCQ_CHANGE "}}"
#define OSCQ_ROOT   "{\"FULL_PATH\":\"/\",\"ACCESS\":0,\"DESCRIPTION\":\"root node\"," \
                    "\"CONTENTS\":{\"avatar\":" OSCQ_AVATAR "}}"

static inline int OscQueryHostInfo(char* out, size_t n, int oscPort)
{
    return snprintf(out, n, "{\"NAME\":\"ArmSwing\",\"OSC_IP\":\"127.0.0.1\",\"OSC_PORT\":%d,"
                    "\"OSC_TRANSPORT\":\"UDP\",\"EXTENSIONS\":{\"ACCESS\":true,\"CLIPMODE\":"
                    "false,\"RANGE\":false,\"TYPE\":true,\"VALUE\":false}}", oscPort);
}

struct OscQueryAnswer {
    int         code;       // 200, 404, or 405 for anything but a GET
    const char* body;       // set only with 200
    bool        info;       // it was the HOST_INFO question
};

// request: the bytes received, NUL-terminated.  Never reads past the NUL.
static inline OscQueryAnswer OscQueryAnswerFor(const char* request, const char* hostInfo)
{
    if (!request || strncmp(request, "GET ", 4) != 0) return { 405, nullptr, false };
    const char* t = request + 4;
    size_t len = strcspn(t, " \r\n");
    char target[128];                                    // "/?HOST_INFO", "/", "/avatar"
    if (len >= sizeof target) return { 404, nullptr, false };
    memcpy(target, t, len);
    target[len] = 0;
    char* query = strchr(target, '?');
    if (query) *query++ = 0;                             // target is now just the path
    if (query && strstr(query, "HOST_INFO")) return { 200, hostInfo, true };
    const char* body = strcmp(target, "/") == 0              ? OSCQ_ROOT
                     : strcmp(target, "/avatar") == 0        ? OSCQ_AVATAR
                     : strcmp(target, "/avatar/change") == 0 ? OSCQ_CHANGE
                     : nullptr;
    return { body ? 200 : 404, body, false };
}

// ── mDNS: ArmSwing's announcement, in the shape VRChat reads ──
// diag.3: Windows' own announcement reached VRChat without a 127.0.0.1
// address record (VRChat's service carries one) and VRChat never asked for
// our info.  This packet is laid out like vrchat_osc's, which VRChat finds:
// the PTR as the answer; SRV (the port), TXT and A (127.0.0.1) as additional
// records, all owned by the instance name.  ttl 0 makes it a goodbye.
// Returns the length, or 0 if it does not fit or a name is not valid.
struct MdnsOut {
    uint8_t* p;
    size_t   cap, n;
    bool     ok;
    void put(const void* d, size_t k)
    {
        if (!ok || k > cap - n) { ok = false; return; }
        memcpy(p + n, d, k);
        n += k;
    }
    void u16(uint16_t v) { uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; put(b, 2); }
    void u32(uint32_t v) { u16((uint16_t)(v >> 16)); u16((uint16_t)v); }
    void name(const char* dotted)                        // "a.b.local" as DNS labels
    {
        for (const char* s = dotted; ok; s++) {
            const char* dot = strchr(s, '.');
            size_t k = dot ? (size_t)(dot - s) : strlen(s);
            if (k == 0 || k > 63) { ok = false; return; }
            uint8_t len = (uint8_t)k;
            put(&len, 1);
            put(s, k);
            if (!dot) break;
            s = dot;
        }
        uint8_t root = 0;
        put(&root, 1);
    }
    size_t record(const char* owner, uint16_t type, uint32_t ttl)   // where rdlength goes
    {
        name(owner);
        u16(type);
        u16(1);                                          // class IN
        u32(ttl);
        size_t at = n;
        u16(0);
        return at;
    }
    void close(size_t at)                                // fill in rdlength
    {
        if (!ok) return;
        size_t len = n - at - 2;
        p[at] = (uint8_t)(len >> 8);
        p[at + 1] = (uint8_t)len;
    }
};

static inline size_t MdnsAnnounce(uint8_t* out, size_t cap, const char* service,
                                  const char* instance, uint16_t port, uint32_t ttl)
{
    if (!out || !service || !instance) return 0;
    MdnsOut w = { out, cap, 0, true };
    static const uint8_t head[12] = { 0, 0, 0x84, 0, 0, 0, 0, 1, 0, 0, 0, 3 };
    w.put(head, sizeof head);                            // response, authoritative; 1 + 3 records
    size_t at = w.record(service, 12, ttl);              // PTR  service -> instance
    w.name(instance);
    w.close(at);
    at = w.record(instance, 33, ttl);                    // SRV  priority 0, weight 0, port
    w.u16(0);
    w.u16(0);
    w.u16(port);
    w.name(instance);                                    //      target: the A record below
    w.close(at);
    at = w.record(instance, 16, ttl);                    // TXT  txtvers=1
    w.put("\x09txtvers=1", 10);
    w.close(at);
    at = w.record(instance, 1, ttl);                     // A    127.0.0.1
    static const uint8_t lo[4] = { 127, 0, 0, 1 };
    w.put(lo, 4);
    w.close(at);
    return w.ok ? w.n : 0;
}
