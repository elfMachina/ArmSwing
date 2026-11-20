// Unit test for src/osc.h.  Build and run natively:
//   python3 -m ziglang c++ -std=c++17 -O1 -fsanitize=undefined -o /tmp/osc_test test/osc_test.cpp && /tmp/osc_test
#include "../src/osc.h"
#include <cstdio>
#include <string>
#include <vector>

static int fails = 0;
static void check(const char* name, bool ok)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok) fails++;
}

typedef std::vector<uint8_t> Bytes;
static void str(Bytes& b, const char* s)
{
    size_t n = strlen(s) + 1;
    b.insert(b.end(), s, s + n);
    while (b.size() % 4) b.push_back(0);
}
static void be32(Bytes& b, uint32_t u)
{
    for (int k = 3; k >= 0; k--) b.push_back((uint8_t)(u >> (8 * k)));
}
static Bytes msgF(const char* a, float f)
{
    Bytes b; str(b, a); str(b, ",f"); uint32_t u; memcpy(&u, &f, 4); be32(b, u); return b;
}
static Bytes msgI(const char* a, int32_t i) { Bytes b; str(b, a); str(b, ",i"); be32(b, (uint32_t)i); return b; }
static Bytes msgB(const char* a, bool t) { Bytes b; str(b, a); str(b, t ? ",T" : ",F"); return b; }
static Bytes msgS(const char* a, const char* s) { Bytes b; str(b, a); str(b, ",s"); str(b, s); return b; }
static Bytes bundle(const std::vector<Bytes>& parts)
{
    Bytes b; str(b, "#bundle"); be32(b, 0); be32(b, 1);
    for (auto& p : parts) { be32(b, (uint32_t)p.size()); b.insert(b.end(), p.begin(), p.end()); }
    return b;
}

struct Got { std::string addr; OscValue v; };
static std::vector<Got> parse(const Bytes& b, size_t cut = (size_t)-1)
{
    std::vector<Got> out;
    size_t n = cut < b.size() ? cut : b.size();
    OscParse(b.data(), n, [&](const char* a, const OscValue& v) { out.push_back({a, v}); });
    return out;
}

int main(int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "--json") == 0) {   // every body, for an outside JSON check
        char info[320];
        OscQueryHostInfo(info, sizeof info, 54321);
        printf("%s\n%s\n%s\n%s\n", info, OSCQ_ROOT, OSCQ_AVATAR, OSCQ_CHANGE);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--mdns") == 0) {   // hex packets, for an outside DNS decode
        uint8_t p[512];
        const struct { const char* svc; const char* inst; uint16_t port; uint32_t ttl; } k[] = {
            { "_oscjson._tcp.local", "ArmSwing-21604._oscjson._tcp.local", 62770, 4500 },
            { "_osc._udp.local",     "ArmSwing-21604._osc._udp.local",     57192, 4500 },
            { "_oscjson._tcp.local", "ArmSwing-21604._oscjson._tcp.local", 62770, 0 } };
        for (auto& e : k) {
            size_t n = MdnsAnnounce(p, sizeof p, e.svc, e.inst, e.port, e.ttl);
            for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
            printf("\n");
        }
        return 0;
    }
    printf("OSC parser\n");
    auto g = parse(msgF("/avatar/parameters/VelocityZ", 1.625f));
    check("float message", g.size() == 1 && g[0].addr == "/avatar/parameters/VelocityZ"
                           && g[0].v.type == 'f' && g[0].v.f == 1.625f);
    g = parse(msgF("/avatar/parameters/VelocityX", -0.5f));
    check("negative float", g.size() == 1 && g[0].v.f == -0.5f);
    g = parse(msgI("/avatar/parameters/TrackingType", 6));
    check("int message", g.size() == 1 && g[0].v.type == 'i' && g[0].v.i == 6);
    g = parse(msgB("/avatar/parameters/Grounded", true));
    check("bool True", g.size() == 1 && g[0].v.type == 'T');
    g = parse(msgB("/avatar/parameters/Seated", false));
    check("bool False", g.size() == 1 && g[0].v.type == 'F');
    g = parse(msgS("/avatar/change", "avtr_0123"));
    check("string message reported, value ignored", g.size() == 1 && g[0].v.type == 's');
    // address length 4k-1, 4k, 4k+1 exercise the padding rule
    g = parse(msgF("/abc", 2.0f));
    check("address padded to exactly 8", g.size() == 1 && g[0].v.f == 2.0f);
    g = parse(msgF("/abcdefg", 3.0f));
    check("address needing a full extra pad word", g.size() == 1 && g[0].v.f == 3.0f);

    Bytes b = bundle({ msgF("/avatar/parameters/VelocityX", 0.25f),
                       msgB("/avatar/parameters/Grounded", false),
                       bundle({ msgF("/avatar/parameters/Upright", 0.5f) }) });
    g = parse(b);
    check("bundle with nested bundle flattens in order",
          g.size() == 3 && g[0].v.f == 0.25f && g[1].v.type == 'F'
          && g[2].addr == "/avatar/parameters/Upright" && g[2].v.f == 0.5f);

    // hostile / broken input must never read past the end or crash
    Bytes m = msgF("/avatar/parameters/VelocityZ", 1.0f);
    bool safe = true;
    for (size_t cut = 0; cut < m.size(); cut++)
        if (!parse(m, cut).empty()) safe = false;
    check("every truncation of a message yields nothing", safe);
    safe = true;
    for (size_t cut = 0; cut < b.size(); cut++) {
        auto r = parse(b, cut);
        for (auto& x : r) if (x.addr.empty() || x.addr[0] != '/') safe = false;
    }
    check("every truncation of a bundle yields only whole messages", safe);
    Bytes junk = { '/', 'a', 'b', 'c' };   // no terminator
    check("unterminated address is dropped", parse(junk).empty());
    Bytes notags; str(notags, "/x"); str(notags, "f");   // tag string without ','
    check("type tag without comma is dropped", parse(notags).empty());
    Bytes noarg; str(noarg, "/x"); str(noarg, ",");
    check("message with no argument is dropped", parse(noarg).empty());
    Bytes lie = bundle({ msgF("/x", 1.0f) });
    lie[16] = 0x7f;                                       // element length far too big
    check("bundle element longer than packet is dropped", parse(lie).empty());
    Bytes deep = msgF("/x", 1.0f);
    for (int k = 0; k < 8; k++) deep = bundle({ deep });
    check("absurdly nested bundles are cut off", parse(deep).empty());
    check("null pointer is ignored", [] { int c = 0; OscParse(nullptr, 16, [&](const char*, const OscValue&) { c++; }); return c == 0; }());

    printf("\nOSCQuery HTTP answers\n");
    char info[320];
    int il = OscQueryHostInfo(info, sizeof info, 54321);
    check("HOST_INFO fits and names our OSC port",
          il > 0 && il < (int)sizeof info && strstr(info, "\"OSC_PORT\":54321,")
          && strstr(info, "\"OSC_IP\":\"127.0.0.1\"") && strstr(info, "\"OSC_TRANSPORT\":\"UDP\""));
    auto ask = [&](const char* r) { return OscQueryAnswerFor(r, info); };
    OscQueryAnswer a = ask("GET /?HOST_INFO HTTP/1.1\r\nHost: 127.0.0.1:5000\r\n\r\n");
    check("GET /?HOST_INFO -> host info", a.code == 200 && a.info && a.body == info);
    a = ask("GET / HTTP/1.1\r\nHost: 127.0.0.1:5000\r\n\r\n");
    check("GET / -> the tree, which holds /avatar",
          a.code == 200 && !a.info && a.body == std::string(OSCQ_ROOT)
          && strstr(a.body, "\"FULL_PATH\":\"/avatar\""));
    check("GET /avatar and /avatar/change -> their nodes",
          ask("GET /avatar HTTP/1.1\r\n\r\n").body == std::string(OSCQ_AVATAR)
          && ask("GET /avatar/change HTTP/1.1\r\n\r\n").body == std::string(OSCQ_CHANGE));
    check("an address we do not list -> 404",
          ask("GET /avatar/parameters/VelocityMagnitude HTTP/1.1\r\n\r\n").code == 404
          && ask("GET /avatarx HTTP/1.1\r\n\r\n").code == 404);
    check("not a GET, empty, or null -> 405, no body",
          ask("POST / HTTP/1.1\r\n\r\n").code == 405 && ask("").code == 405
          && ask("GE").code == 405 && OscQueryAnswerFor(nullptr, info).code == 405
          && ask("").body == nullptr);
    check("request cut off after the path still answers", ask("GET /").code == 200
          && ask("GET /?HOST_INFO").info);
    std::string longReq = "GET /" + std::string(500, 'a') + "?HOST_INFO HTTP/1.1\r\n\r\n";
    check("an overlong target is refused, not copied", ask(longReq.c_str()).code == 404
          && !ask(longReq.c_str()).info);

    printf("\nmDNS announcement\n");
    uint8_t pk[512];
    const char* svc = "_oscjson._tcp.local";
    const char* inst = "ArmSwing-21604._oscjson._tcp.local";
    size_t pn = MdnsAnnounce(pk, sizeof pk, svc, inst, 62770, 4500);
    check("builds, with a response header: 1 answer, 3 additional",
          pn > 12 && pn < 300 && pk[2] == 0x84 && pk[7] == 1 && pk[11] == 3
          && pk[4] == 0 && pk[5] == 0 && pk[8] == 0 && pk[9] == 0);
    bool fits = true;
    for (size_t cap = 0; cap < pn; cap++) {               // every buffer too small
        std::vector<uint8_t> small(cap ? cap : 1, 0xAA);
        if (MdnsAnnounce(small.data(), cap, svc, inst, 62770, 4500) != 0) fits = false;
    }
    check("every buffer too small returns 0 and writes nothing past it", fits);
    check("exact-size buffer still builds", MdnsAnnounce(pk, pn, svc, inst, 62770, 4500) == pn);
    std::string longLabel = std::string(64, 'a') + "._oscjson._tcp.local";
    check("a label over 63 characters is refused",
          MdnsAnnounce(pk, sizeof pk, svc, longLabel.c_str(), 1, 4500) == 0);
    check("empty labels (\"..\", trailing dot, empty name) are refused",
          MdnsAnnounce(pk, sizeof pk, svc, "a..local", 1, 1) == 0
          && MdnsAnnounce(pk, sizeof pk, svc, "a.local.", 1, 1) == 0
          && MdnsAnnounce(pk, sizeof pk, "", inst, 1, 1) == 0
          && MdnsAnnounce(nullptr, 9, svc, inst, 1, 1) == 0);

    printf(fails ? "\n%d FAILED\n" : "\nall OSC checks hold\n", fails);
    return fails ? 1 : 0;
}
