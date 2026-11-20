#!/usr/bin/env python3
"""Assembles a release zip from a build folder, in the layout users install from.

    py tools\\package.py out\\release armswing-v1.0

Writes out\\<name>.zip containing:
    README.txt (English), README.<language>.txt (translations), LICENSE.txt
    install.bat  uninstall.bat
    armswing\\  armswing.exe, openvr_api.dll, actions.json, bindings_*.json,
               armswing.vrmanifest, driver.vrdrivermanifest, config.txt,
               mode.txt, hands.txt, stick.txt, bin\\win64\\driver_armswing.dll
Every file but the two built binaries comes from readme\\, src\\, package\\
or the SDK, byte for byte.
"""
import pathlib, sys, zipfile

DEV = pathlib.Path(__file__).resolve().parent.parent


def main(build_dir, name):
    build = (DEV / build_dir).resolve()
    files = {"README.txt": DEV / "readme/README.md"}
    for p in sorted((DEV / "readme").glob("README.*.md")):     # README.ja.md -> README.ja.txt
        files[p.stem + ".txt"] = p
    files.update({
        "LICENSE.txt": DEV / "LICENSE",
        "armswing/openvr_api-LICENSE.txt": DEV / "openvr-sdk/LICENSE",   # BSD: ships with the DLL
        "install.bat": DEV / "src/install.bat",
        "uninstall.bat": DEV / "src/uninstall.bat",
        "armswing/armswing.exe": build / "armswing.exe",
        "armswing/bin/win64/driver_armswing.dll": build / "driver_armswing.dll",
        "armswing/openvr_api.dll": DEV / "openvr-sdk/bin/win64/openvr_api.dll",
        "armswing/actions.json": DEV / "src/actions.json",
        "armswing/armswing.vrmanifest": DEV / "src/armswing.vrmanifest",
    })
    for p in sorted((DEV / "src").glob("bindings_*.json")):
        files["armswing/" + p.name] = p
    for p in sorted((DEV / "package").iterdir()):
        files["armswing/" + p.name] = p
    missing = [str(p) for p in files.values() if not p.is_file()]
    if missing:
        raise SystemExit("missing: " + ", ".join(missing))
    out = DEV / "out" / f"{name}.zip"
    out.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for arc, p in sorted(files.items()):
            z.write(p, arc)
    print(f"wrote {out} ({len(files)} files)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
