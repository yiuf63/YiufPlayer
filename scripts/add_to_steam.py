import os
import glob
import shutil
import struct
import zlib

def parse_vdf(data, pos=0):
    res = {}
    while pos < len(data):
        t = data[pos]
        pos += 1
        if t == 8:
            return res, pos
        null_idx = data.find(b"\x00", pos)
        key = data[pos:null_idx].decode("utf-8", errors="ignore")
        pos = null_idx + 1
        if t == 0:
            val, pos = parse_vdf(data, pos)
            res[key] = val
        elif t == 1:
            null_idx = data.find(b"\x00", pos)
            val = data[pos:null_idx].decode("utf-8", errors="ignore")
            pos = null_idx + 1
            res[key] = val
        elif t == 2:
            val = struct.unpack("<i", data[pos:pos+4])[0]
            pos += 4
            res[key] = val
    return res, pos

def serialize_vdf(obj):
    buf = bytearray()
    for k, v in obj.items():
        if isinstance(v, dict):
            buf.append(0)
            buf.extend(k.encode("utf-8") + b"\x00")
            buf.extend(serialize_vdf(v))
            buf.append(8)
        elif isinstance(v, str):
            buf.append(1)
            buf.extend(k.encode("utf-8") + b"\x00")
            buf.extend(v.encode("utf-8") + b"\x00")
        elif isinstance(v, int):
            buf.append(2)
            buf.extend(k.encode("utf-8") + b"\x00")
            buf.extend(struct.pack("<i", v))
    return bytes(buf)

exe_raw = "/home/deck/muvik/muvik.sh"
appname = "Muvik"
cand = exe_raw + appname
unsigned_aid = zlib.crc32(cand.encode("utf-8")) | 0x80000000
signed_aid = struct.unpack("<i", struct.pack("<I", unsigned_aid))[0]
grid_id = str(unsigned_aid)

print(f"Calculated AppID: signed={signed_aid}, unsigned={unsigned_aid}, grid_id={grid_id}")

user_dirs = glob.glob("/home/deck/.local/share/Steam/userdata/*")

# Update shortcuts.vdf in each userdata dir
new_entry = {
    "appid": signed_aid,
    "AppName": "Muvik",
    "Exe": f"\"{exe_raw}\"",
    "StartDir": "\"/home/deck/muvik/\"",
    "icon": "/home/deck/muvik/resources/icon/icon-512.png",
    "ShortcutPath": "",
    "LaunchOptions": "",
    "IsHidden": 0,
    "AllowDesktopConfig": 1,
    "AllowOverlay": 1,
    "OpenVR": 0,
    "Devkit": 0,
    "DevkitGameID": "",
    "DevkitOverrideAppID": 0,
    "LastPlayTime": 0,
    "FlatpakAppID": "",
    "sortas": "Muvik",
    "tags": {"0": "Media"},
}

for udir in user_dirs:
    conf_dir = os.path.join(udir, "config")
    vdf_file = os.path.join(conf_dir, "shortcuts.vdf")
    if not os.path.exists(conf_dir):
        continue
    
    parsed = {"shortcuts": {}}
    if os.path.exists(vdf_file):
        with open(vdf_file, "rb") as f:
            data = f.read()
        if data:
            try:
                parsed, _ = parse_vdf(data)
            except Exception as e:
                print(f"Error parsing {vdf_file}: {e}")
                parsed = {"shortcuts": {}}
        shutil.copy2(vdf_file, vdf_file + ".bak")
        print(f"Backed up {vdf_file} to .bak")
    
    shortcuts = parsed.setdefault("shortcuts", {})
    target_idx = None
    for idx, sc in shortcuts.items():
        if sc.get("AppName") == "Muvik" or "muvik.sh" in sc.get("Exe", ""):
            target_idx = idx
            break
    
    if target_idx is not None:
        print(f"Updating existing entry at index [{target_idx}] in {vdf_file}")
        shortcuts[target_idx].update(new_entry)
    else:
        new_idx = str(len(shortcuts))
        print(f"Adding new entry at index [{new_idx}] in {vdf_file}")
        shortcuts[new_idx] = new_entry
    
    new_data = serialize_vdf(parsed) + b"\x08"
    tmp_file = vdf_file + ".tmp"
    with open(tmp_file, "wb") as f:
        f.write(new_data)
    os.replace(tmp_file, vdf_file)
    print(f"Saved {vdf_file} successfully! (Total entries: {len(shortcuts)})")

print("Muvik successfully added to Steam shortcuts!")
