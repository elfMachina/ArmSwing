#!/usr/bin/env python3
"""handswing invariants.  Run: python3 test/invariants.py

SHAPE greps the driver source; MODEL mirrors its decision and checks behaviour.
The shape checks exist so the model cannot drift from the driver unnoticed.

Guards the regressions that reached a headset:
  v0.77  a stop delay stranded the arms in the walk animation after every walk.
  v0.13-v0.78  the TrackingOverride/proxy design flashed the rest pose on every
         transition; none of that machinery may come back.
  v1.2   a left-hand start rule froze the other arm whenever the walk began
         with the left hand raised; nothing but the stick may gate a start.
"""
import pathlib, re, sys

SRC = (pathlib.Path(__file__).parent.parent / "src" / "driver_armswing.cpp").read_text()
CODE = re.sub(r"//[^\n]*", "", SRC)          # the header names deleted things on purpose
fails = []


def check(name, cond, detail=""):
    print(f"  {'ok  ' if cond else 'FAIL'}  {name}" + (f"   {detail}" if detail and not cond else ""))
    if not cond:
        fails.append(name)


print("SHAPE (source must match the model)")
check("the decision is one line of three terms",
      "bool park = m_walking && !g_side[i].raised && !Awake(i);" in CODE)
check("the stick's falling edge has NO delay",
      "if (want != m_walking && (!want || m_now - m_stickAt >= m_cfg.walkHold))" in CODE)
check("the only pose change is clearing validity",
      "p.poseIsValid = false;" in CODE and "deviceIsConnected" not in CODE
      and "vecPosition[" not in CODE.replace("p.vecPosition;", ""))
check("nothing but the stick gates a walk start (v1.2's left-hand rule stays deleted)",
      re.search(r"bool want = \(m_mode == 0\) \? true : \(m_mode == 1\) \? false : m_stick;\s*"
                r"if \(want != m_walking", CODE) is not None
      and "g_side[0].raised" not in CODE)
check("a pose of unknown layout is passed through untouched",
      "if (size != sizeof(DriverPose_t))" in CODE)
check("Intercept never logs (it runs on another driver's thread)",
      "Log(" not in CODE[CODE.index("static void Intercept"):CODE.index("static void Hook0")])
for gone in ("TrackedDeviceAdded", "ITrackedDeviceServerDriver", "SetString",
             "stop_hold", "stopHold", "BasePose", "m_blend", "ParkPos"):
    check(f"override-era machinery stays deleted: {gone}", gone not in CODE)


class Sim:
    """Mirrors RunFrame's debounce + decision and Intercept's raise hysteresis."""
    SLACK = 0.05

    def __init__(self, walk_hold=90, wake_hold=120):
        self.walk_hold, self.wake_hold = walk_hold, wake_hold
        self.stick = self.walking = self.park = self.raised = False
        self.stick_at = self.grip_at = -10**9
        self.t = self.flips = 0

    def tick(self, stick=None, grip=False, y=None, gate=0.0):
        self.t += 1
        if stick is not None and stick != self.stick:
            self.stick, self.stick_at = stick, self.t
        if grip:
            self.grip_at = self.t
        if y is not None:                      # live even while parked
            if y > gate:
                self.raised = True
            elif y < gate - self.SLACK:
                self.raised = False
        want = self.stick
        if want != self.walking and (not want or self.t - self.stick_at >= self.walk_hold):
            self.walking = want
        park = self.walking and not self.raised and self.t - self.grip_at >= self.wake_hold
        self.flips += park != self.park
        self.park = park
        return park


print("\nMODEL (behaviour of the decision)")
s = Sim()
for _ in range(300):
    s.tick(stick=True, y=-0.3)
check("parks while walking with the arm down", s.park)
s.tick(stick=False)
check("release is IMMEDIATE on the stick's falling edge (v0.77)", not s.park)

s = Sim(walk_hold=90)
s.tick(stick=True, y=-0.3)
check("entry waits walk_hold", not any(s.tick() for _ in range(88)))
check("entry fires just after walk_hold", any(s.tick() for _ in range(4)))

s = Sim()
for _ in range(300):
    s.tick(stick=True, y=-0.3)
s.tick(y=0.2)
check("raising an arm WHILE it animates wakes it on that frame", not s.park)
s.tick(y=-0.02)
check("hovering just under the line does not re-park it (hysteresis)", not s.park)
s.tick(y=-0.3)
check("lowering it properly parks it again", s.park)

s = Sim()
for _ in range(300):
    s.tick(stick=True, y=-0.3)
s.tick(grip=True)
check("a grip wakes the arm on the same frame", not s.park)
check("stays awake for wake_hold", not any(s.tick() for _ in range(117)))
check("rejoins just after wake_hold", any(s.tick() for _ in range(4)))

s = Sim()
s.tick(stick=True, y=-0.3)
for _ in range(2000):
    s.tick()
check("one transition per uninterrupted walk", s.flips == 1, f"flips={s.flips}")


# ── v1.3: the watcher's movement gate ──────────────────────────────────────
# Guards diag.4's finding: parking an arm while VRChat isn't moving the avatar
# (wall, camera fly mode, seat) shows the A-pose, and handing a settled arm
# over at speed 0 flashed a bent-elbow pose first.  The gate's whole rule:
#     walking = stick && (VRChat moving || no word from VRChat)
def release_only(src):
    """The source as a release build sees it (tools/strip_diag.py's rule)."""
    out, state = [], []
    for ln in src.split("\n"):
        s = ln.strip()
        if s == "#ifdef ARMSWING_DIAG": state.append("diag"); continue
        if state and s == "#else" and state[-1] == "diag": state[-1] = "else"; continue
        if state and s == "#endif": state.pop(); continue
        if not state or state[-1] == "else": out.append(ln)
    return "\n".join(out)


WSRC = (pathlib.Path(__file__).parent.parent / "src" / "armswing.cpp").read_text(encoding="utf-8")
W = re.sub(r"//[^\n]*", "", release_only(WSRC))
MAIN = W[W.index("int main()"):]


def body(code, head):
    """The braces-balanced body of the function whose definition starts with head."""
    i = code.index("{", code.index(head))
    depth = 0
    for j in range(i, len(code)):
        depth += {"{": 1, "}": -1}.get(code[j], 0)
        if depth == 0:
            return code[i:j + 1]


print("\nSHAPE (watcher: the movement gate)")
check("the gate: stick && (no word from VRChat || (moving && upright && full body))",
      re.search(r"bool walking = stick && \(!gate \|\| \(g_vrcSpeed > MOVING && "
                r"g_vrcUpright >= UPRIGHT &&\s*fullBody\)\);", MAIN) is not None)
check("full body means TrackingType 6, or VRChat hasn't said yet (as tested)",
      "bool fullBody = g_vrcTracking == FULL_BODY || g_vrcTracking < 0;" in MAIN
      and "static const int   FULL_BODY = 6;" in W)
check("the gate needs VRChat in front AND its OSC heard; otherwise v1.2.1",
      "bool gate = g_vrcScene && g_vrcHeard;" in MAIN)
check("VRChat's word is drained before the stick decision",
      0 <= MAIN.index("VrcDrain();") < MAIN.index("bool walking ="))
check("the Schmitt trigger follows the stick, not the gated state",
      "bool stick = data.bActive && (mag > (lastStick ? STOP : START));" in MAIN)
scene = body(W, "static void VrcScene(")
check("only VRChat counts, and a scene change forgets VRChat's word",
      'strstr(app, "438100")' in scene and "g_vrcHeard = false;" in scene
      and "g_vrcSpeed = 0;" in scene and "g_vrcUpright = 1;" in scene
      and "g_vrcTracking = -1;" in scene)
check("a scene change is acted on in the release event loop",
      "VREvent_SceneApplicationChanged" in MAIN and "VrcScene(AppOf(" in MAIN)
check("every exit writes 0 to stick.txt before anything else",
      body(W, "static void Bye(").split(";")[0].strip("{ \n") == 'WriteFile1(g_stickPath, "0\\n")')
lo = body(W, "static SOCKET QLoopback(")
check("sockets are loopback only, on ports Windows picks (never 9000/9001)",
      "htonl(INADDR_LOOPBACK)" in lo and "sin_port =" not in lo and "htons(900" not in W)

START, STOP = (float(x) for x in re.search(
    r"const float START = ([\d.]+)f, STOP = ([\d.]+)f;", W).groups())
MOVING = float(re.search(r"static const float MOVING\s*= ([\d.]+)f;", W).group(1))
UPRIGHT = float(re.search(r"static const float UPRIGHT\s*= ([\d.]+)f;", W).group(1))


class Watcher:
    """Mirrors the main loop's stick Schmitt trigger and the gate."""
    def __init__(self):
        self.last_stick = False

    def tick(self, mag, scene=True, heard=True, speed=0.0, upright=1.0, tracking=-1):
        stick = mag > (STOP if self.last_stick else START)
        self.last_stick = stick
        full_body = tracking == 6 or tracking < 0
        return stick and (not (scene and heard) or
                          (speed > MOVING and upright >= UPRIGHT and full_body))


def run(steps, **kw):
    """steps: [(ms, mag, speed)] or [(ms, mag, speed, upright)].  The driver's park per ms."""
    w, s, parks = Watcher(), Sim(), []
    for ms, mag, speed, *up in steps:
        for _ in range(ms):
            parks.append(s.tick(stick=w.tick(mag, speed=speed, upright=up[0] if up else 1.0,
                                              **kw), y=-0.3))
    return parks


print("\nMODEL (behaviour of the gate, feeding the driver model)")
walk = [(500, 1.0, 3.8)]
check("no word from VRChat: parks after walk_hold, exactly as v1.2.1",
      run(walk, heard=False) == run(walk, scene=False) == [t >= 90 for t in range(500)])
wall = run([(2000, 1.0, 0.0)])
check("stick pushed, VRChat not moving you (wall, camera, seat): never parks",
      not any(wall))
p = run([(30, 1.0, 0.0), (500, 1.0, 3.8)])
check("parks walk_hold after VRChat starts moving you", p.index(True) == 30 + 90)
p = run([(500, 1.0, 3.8), (200, 1.0, 0.0)])
check("hitting a wall mid-walk hands the arms back on that frame",
      p[499] and not any(p[500:]))
p = run([(500, 1.0, 3.8), (50, 0.0, 3.0)])
check("releasing the stick hands them back at once, even while still sliding",
      p[499] and not any(p[500:]))
p = run([(500, 1.0, 3.8), (60, 1.0, 0.0), (300, 1.0, 3.8)])
check("a direction change through speed 0: tracked through it, rejoins walk_hold after",
      not any(p[500:560]) and p.index(True, 560) == 560 + 90)
check("a crouch-walk at VRChat's standing line (Upright 0.65) still parks, as diag.4's did",
      run([(500, 1.0, 3.3, 0.65)]).index(True) == 90)
check("sliding below VRChat's crouch line (Standable's sit): never parks",
      not any(run([(2000, 1.0, 1.7, 0.645)])))
p = run([(500, 1.0, 3.8, 1.0), (300, 1.0, 1.7, 0.58), (300, 1.0, 3.8, 1.0)])
check("dropping below the line mid-walk hands the arms back on that frame; rising rejoins",
      p[499] and not any(p[500:800]) and p.index(True, 800) == 800 + 90)
check("full body (TrackingType 6) walks as tested",
      run(walk, tracking=6) == run(walk) == [t >= 90 for t in range(500)])
check("3-point (TrackingType 3): never parks, the arms stay tracked (no T-pose)",
      not any(run([(2000, 1.0, 4.0)], tracking=3)))
check("no word from VRChat in 3-point is still v1.2.1 (the gate only ever holds arms back)",
      run(walk, heard=False, tracking=3) == run(walk, heard=False))

def replay(session, want, label, least):
    """Replays recorded pushes whose every sample matches want(speed, upright):
    the gate must park none of them, and v1.2.1's rule must park some (so the
    check can fail).  Rows hold until the next one, as the watcher saw them."""
    path = pathlib.Path(__file__).parent.parent / "history" / session / "armswing-gate.csv"
    if not path.exists():
        print(f"  skip  {label} (history/{session}/armswing-gate.csv missing)")
        return
    import csv
    pushes, cur, up = [], None, 1.0
    for r in csv.DictReader(open(path, newline="")):     # one push per stick=1 run
        t, st, v = float(r["t_ms"]), r["stick"] == "1", r["VelocityMagnitude"]
        up = float(r["Upright"]) if r.get("Upright") else up
        pt = (t, float(v) if v else 0.0, up)
        if st and cur is None:
            cur = [pt]
        elif cur is not None:
            cur.append(pt)
            if not st:
                pushes.append(cur)
                cur = None
    n = parked = parked_old = 0
    for pts in pushes:
        if not all(want(sp, u) for _, sp, u in pts[:-1]):
            continue
        steps = [(max(1, round(b[0] - a[0])), 1.0, a[1], a[2]) for a, b in zip(pts, pts[1:])]
        n += 1
        parked += any(run(steps))
        parked_old += any(run(steps, heard=False))       # v1.2.1's rule on the same pushes
    check(f"{label}: of {n} such pushes, v1.2.1 parked {parked_old}; the gate parks none",
          n >= least and parked == 0 and parked_old > 0, f"{parked} parked")


replay("diag4-session-2026-11-20", lambda sp, u: sp <= MOVING,
       "diag.4 replay, pushes VRChat never moved (the wall clip and A-pose)", 100)
replay("v1.3-diag1-crouch-2026-11-20", lambda sp, u: u < UPRIGHT,
       "crouch replay, pushes below VRChat's crouch line (Standable's sit, A-pose)", 10)

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all invariants hold")
