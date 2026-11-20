# ArmSwing

**Natural arm swing for VRChat full-body tracking.**

English | [日本語](README.ja.md) | [한국어](README.ko.md) | [简体中文](README.zh-CN.md) | [Español](README.es.md) | [Português (BR)](README.pt-BR.md) | [Русский](README.ru.md)

In VRChat with full-body tracking, walking with the thumbstick leaves your
arms hanging stiffly while your body slides along. ArmSwing hands your
lowered arms to VRChat's own walk animation while you walk, so they swing
naturally. Raise a hand, or hold its grip or trigger, and that arm follows
your controller again on the same frame.

## Requirements

- Windows 10 (version 1809 or later) or Windows 11, with SteamVR
- VRChat (Steam version) with **full-body tracking calibrated**: trackers,
  SlimeVR, Standable, or anything else VRChat calibrates as full body
- A controller with a thumbstick (see [Supported controllers](#supported-controllers))
- **VRChat's OSC turned on**: Action Menu > Options > OSC > Enabled

Without full-body tracking (3-point), ArmSwing leaves your arms tracked as
usual. 3-point support is being worked on.

## Install

1. Close SteamVR.
2. Extract the whole zip to a permanent folder, for example `C:\Tools\ArmSwing`.
   You should see the `armswing` folder, `install.bat`, `uninstall.bat` and
   the README files.
3. Double-click `install.bat`.
4. Start SteamVR, then run `armswing\armswing.exe`.

No window opens: ArmSwing runs in the system tray, next to the clock.
Right-click its icon to quit. It closes by itself when SteamVR closes.

**Updating:** close SteamVR, extract the new zip, and run its `install.bat`.
It unregisters older copies for you. Then delete the old folder.

**If Windows warns you:** ArmSwing isn't code-signed yet, so Windows
SmartScreen may show "Windows protected your PC" for `install.bat` or
`armswing.exe`. Click **More info**, then **Run anyway**. Some antivirus
programs may also flag the driver, because it hooks into SteamVR the way
driver add-ons do. The full source code is public, and each release lists
SHA-256 checksums so you can confirm your download is the original.

## Using it

- Walk with the left stick while your arms are lowered: they swing.
- Raise an arm above roughly waist height, or hold its grip or trigger: that
  arm follows your controller again, instantly, even mid-walk.
- When the stick is pushed but VRChat isn't actually moving you (walking into
  a wall, camera fly mode, seated, crouched very low), your arms stay tracked.
- ArmSwing works in every SteamVR game while it runs. Quit it from the tray
  icon before playing other games.

## Supported controllers

Ready to use, no setup needed: PlayStation VR2 Sense, Meta Quest and Rift
Touch (Quest Link, Air Link, Steam Link, Virtual Desktop, ALVR), Valve Index,
Steam Frame, Vive Cosmos, HP Reverb G2, Windows Mixed Reality. Pico and
Vive Focus 3 / XR Elite controllers (through Steam Link) use the Quest preset
automatically.

Other controllers with a stick: SteamVR > Settings > Controllers > Manage
Controller Bindings > ArmSwing, and bind your movement stick to
`left_stick_pos`. Vive wands are not supported (they have a trackpad, not a
stick).

## Check your setup

Find your setup below. The settings mentioned are explained in
[Settings](#settings).

| Your setup | What to do |
|---|---|
| A controller from the list above | Nothing. |
| Another controller with a stick | Bind `left_stick_pos` (see above). |
| Controllers you can let go of (strapped, e.g. Globular Cluster, loose Index straps) | Optional: `wake_mode=touch`, so an arm stays yours while your hand touches its controller. |
| Your arms don't swing when lowered | Make `raise_drop` smaller (e.g. `0.50`). |
| Your arms swing when you want to keep them | Make `raise_drop` larger (e.g. `0.90`). |
| No full-body tracking (3-point) | Not supported yet: your arms stay tracked as usual. |
| You move with the right stick | Not supported yet. |
| VRChat from outside Steam, or VRChat's OSC off | Arms swing whenever the stick is pushed, even against walls and in menus. Use the Steam version with OSC on for the full behaviour. |
| Install folder with unusual characters (emoji, or a script other than your Windows language) | Install to a simple path such as `C:\Tools\ArmSwing`. |

## Settings

Edit `armswing\config.txt` in any text editor. You can save it while in VR:
changes apply within about a second.

| Setting | Default | What it does |
|---|---|---|
| `raise_drop` | `0.70` | How far below your headset, in metres, an arm counts as raised. Smaller: you must lift an arm higher to keep it. |
| `wake_mode` | `grip` | `grip`: holding grip or trigger keeps an arm at any height. `touch`: the controller's touch sensors also count; use it only with controllers you can let go of (strapped). |
| `wake_hold` | `120` | How long, in ms, an arm stays yours after you let go of grip or trigger. |
| `walk_hold` | `90` | How long, in ms, the stick must be pushed before the arms swing. Ignores stick twitches. |

To switch ArmSwing off without uninstalling, write `off` in
`armswing\mode.txt` (write `9` to switch it back on).

## Troubleshooting

Hover over the tray icon for its status. The logs are in the `armswing`
folder: `armswing-watcher.log` (the tray app) and `armswing.log` (the
SteamVR driver). The previous session's logs are kept as `*.prev`.

- **Arms swing against walls, in menus or in camera mode:** VRChat's OSC is
  off. Turn it on (Action Menu > Options > OSC > Enabled). When it works,
  `armswing-watcher.log` shows `VRChat's OSC arriving`.
- **Arms never swing:** look for `[stick] action INACTIVE` in
  `armswing-watcher.log`. It means no binding matched your controller; see
  [Supported controllers](#supported-controllers). Also check that
  full-body tracking is calibrated in VRChat.
- **One arm never swings:** you may be holding its grip or trigger, or using
  `wake_mode=touch` with a controller you're holding.
- **Anything else:** open an issue and attach `armswing-watcher.log` and
  `armswing.log`, plus their `.prev` copies.

## Compatibility and privacy

- **Other OSC apps** (VRCFaceTracking and others) keep working: ArmSwing
  never uses ports 9000 or 9001. VRChat finds it through OSCQuery and sends
  to ArmSwing's own port.
- **Other SteamVR add-ons** keep working: the only thing ArmSwing changes is
  whether your two hands read as tracked while you walk. It adds no devices.
- **Network:** ArmSwing listens on 127.0.0.1 only, so nothing outside your PC
  can connect to it. So that VRChat can find it, it announces itself with
  mDNS on your local network, the same way VRChat and other OSC apps do. It
  never connects to the internet and collects no data.

## Uninstall

Close SteamVR, run `uninstall.bat`, then delete the folder.

## How it works

- `armswing\bin\win64\driver_armswing.dll` is a SteamVR driver. While you
  walk, it marks each lowered hand as untracked, and VRChat then animates
  that arm with its walk cycle. One decision per hand:
  `hide = walking && !raised && !grip`.
- `armswing\armswing.exe` is the tray app. It reads the left stick through
  SteamVR Input, and VRChat's `VelocityMagnitude`, `Upright` and
  `TrackingType` through OSC. It tells the driver to walk only while VRChat is
  actually moving you, above its crouch line, in full-body tracking. Releasing
  the stick hands the arms back on the same frame.

## Building from source

Needs Python 3 and zig 0.17.1 (`py -m pip install ziglang==0.17.1`). From
the repository folder:

    py tools\build.py                           release build -> out\release\
    py tools\build.py --test                    parser tests and invariants
    py tools\package.py out\release armswing-v1.0   the release zip -> out\

## License

ArmSwing is MIT-licensed; see [LICENSE](LICENSE). It ships Valve's
`openvr_api.dll`, under the BSD 3-Clause license in
`openvr-sdk/LICENSE` (`armswing\openvr_api-LICENSE.txt` in the zip).
