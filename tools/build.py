#!/usr/bin/env python3
"""Builds ArmSwing exactly the way the shipped binaries were built.

    py tools\\build.py            release driver + watcher  -> out\\release\\
    py tools\\build.py --diag     diagnostic builds         -> out\\diag\\
    py tools\\build.py --test     parser tests + invariants (+ JSON/DNS decode checks)

Run it from anywhere; paths are relative to the dev folder this file sits in.

Toolchain: zig 0.17.1 through Python (same version = same bytes as the
published release):   py -m pip install ziglang==0.17.1
With the same version, a release build matches the shipped exe and DLL in
every code and data section; only the PE timestamp and the random .buildid
differ (check with tools\\pecmp.py).
"""
import json, os, pathlib, subprocess, sys

DEV = pathlib.Path(__file__).resolve().parent.parent
ZIG = [sys.executable, "-m", "ziglang"]
WIN = ["-target", "x86_64-windows-gnu"]


def run(args, **kw):
    args = [str(a) for a in args]
    print(">", " ".join(args), flush=True)
    return subprocess.run(args, cwd=DEV, check=True, **kw)


def build(diag):
    out = pathlib.Path("out") / ("diag" if diag else "release")
    (DEV / out).mkdir(parents=True, exist_ok=True)
    flag = ["-DARMSWING_DIAG"] if diag else []
    # Output names matter: the debug-file name ("driver_armswing.pdb") is
    # stored inside the binary, so another name changes .rdata.
    run(ZIG + ["rc", "--", "src/armswing.rc", out / "armswing.res"])
    run(ZIG + ["c++", *WIN, "-shared", "-O2", "-std=c++17", "-Wall", *flag,
               "-Iopenvr-sdk/headers", "src/driver_armswing.cpp",
               "-o", out / "driver_armswing.dll"])
    run(ZIG + ["c++", *WIN, "-O2", "-Wall", *flag, "-Iopenvr-sdk/headers",
               "src/armswing.cpp", out / "armswing.res", "openvr-sdk/lib/win64/openvr_api.lib",
               "-lws2_32", "-Wl,--subsystem,windows",
               "-o", out / "armswing.exe"])
    for junk in ("*.pdb", "*.lib"):              # never shipped
        for p in (DEV / out).glob(junk):
            p.unlink()
    print(f"built {out}\\armswing.exe and {out}\\driver_armswing.dll")


def test():
    exe = DEV / "out" / ("osc_test.exe" if os.name == "nt" else "osc_test")
    exe.parent.mkdir(exist_ok=True)
    run(ZIG + ["c++", "-std=c++17", "-O1", "-Wall", "-Wextra", "test/osc_test.cpp", "-o", exe])
    run([exe])
    run([sys.executable, "test/invariants.py"])

    # The OSCQuery answers must be valid JSON, VRChat parses them.
    lines = run([exe, "--json"], capture_output=True, text=True).stdout.split("\n")
    bodies = [json.loads(l) for l in lines if l.strip()]
    assert bodies[0]["OSC_IP"] == "127.0.0.1" and bodies[0]["OSC_PORT"] == 54321
    assert bodies[1]["CONTENTS"]["avatar"]["FULL_PATH"] == "/avatar"
    print("  ok    OSCQuery bodies are valid JSON with /avatar in the tree")

    # The mDNS announcement must decode in a real DNS library, as VRChat's will.
    try:
        import dns.message, dns.rdatatype as T
    except ImportError:
        print("  skip  mDNS decode check (py -m pip install dnspython to run it)")
        return
    hexes = run([exe, "--mdns"], capture_output=True, text=True).stdout.split()
    for h, (svc, port, ttl) in zip(hexes, [("_oscjson._tcp", 62770, 4500),
                                          ("_osc._udp", 57192, 4500),
                                          ("_oscjson._tcp", 62770, 0)]):
        m = dns.message.from_wire(bytes.fromhex(h))
        inst = f"ArmSwing-21604.{svc}.local."
        (ptr,) = m.answer
        add = {r.rdtype: r for r in m.additional}
        assert str(ptr[0].target) == inst and ptr.ttl == ttl
        assert add[T.SRV][0].port == port and add[T.A][0].address == "127.0.0.1"
        assert all(str(r.name) == inst for r in add.values())
    print("  ok    mDNS announcements decode: PTR answer; SRV, TXT, A 127.0.0.1 additional")


if __name__ == "__main__":
    if "--test" in sys.argv:
        test()
    else:
        build(diag="--diag" in sys.argv)
