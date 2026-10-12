"""Validate the built APK using Android's signing and alignment tools."""
import os
import pathlib
import struct
import subprocess
import sys
import zipfile

apk = pathlib.Path(sys.argv[1]).resolve()
with zipfile.ZipFile(apk) as archive:
    assert archive.testzip() is None
    assert not any(name.lower().endswith((".gba", ".gb", ".gbc")) for name in archive.namelist())
    libraries = [name for name in archive.namelist() if name.endswith(".so")]
    assert libraries == ["lib/arm64-v8a/libwl4_native.so"]
    elf = archive.read(libraries[0])
    assert elf[:4] == b"\x7fELF" and elf[4] == 2 and elf[5] == 1
    assert struct.unpack_from("<H", elf, 18)[0] == 183
    offset = struct.unpack_from("<Q", elf, 32)[0]
    size, count = struct.unpack_from("<HH", elf, 54)
    loads = [offset+i*size for i in range(count) if struct.unpack_from("<I", elf, offset+i*size)[0] == 1]
    assert loads
    for at in loads:
        alignment = struct.unpack_from("<Q", elf, at+48)[0]
        file_offset, address = struct.unpack_from("<QQ", elf, at+8)
        assert alignment >= 16384 and address % 16384 == file_offset % 16384

root = pathlib.Path(os.environ["ANDROID_HOME"]) / "build-tools"
directories = [folder for folder in root.iterdir() if (folder/"apksigner").exists() and (folder/"zipalign").exists()]
assert directories, "Android build tools missing"
tools = sorted(directories, key=lambda folder: folder.name)[-1]
subprocess.run([str(tools/"apksigner"), "verify", "--verbose", "--min-sdk-version", "26", str(apk)], check=True)
subprocess.run([str(tools/"zipalign"), "-c", "-P", "16", "4", str(apk)], check=True)
print("Signed ARM64 APK: 16 KiB ELF and ZIP alignment; no bundled ROM")
