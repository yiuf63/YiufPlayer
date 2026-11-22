import os
import glob
import shutil
import struct
import sys
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

def find_install_dir():
    """找到 Yiuf Player 的实际安装目录。

    为什么不再写死 "/home/deck/muvik"：
      早先这里指向 /home/deck/muvik/muvik.sh，而**那个文件在仓库里根本不存在**
      （仓库只有 ./build-linux/yiufplayer 与安装出来的 /usr/bin/yiufplayer）。
      照 README 执行后，Steam 里会多出一个点进去直接报错的快捷方式。
      实测：`find . -name "muvik.sh"` 为空。

    查找顺序：
      1. 环境变量 MUVIK_DIR（显式指定，优先级最高）
      2. 脚本自身所在目录的上一级（在源码树里执行时）
      3. 常见安装位置
    """
    env_dir = os.environ.get("MUVIK_DIR")
    if env_dir:
        return os.path.abspath(os.path.expanduser(env_dir))

    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.dirname(here),                      # 源码树根（scripts/ 的上一级）
        os.path.expanduser("~/yiufplayer"),
        os.path.expanduser("~/muvik"),
        "/opt/yiufplayer",
    ]
    for d in candidates:
        if not os.path.isdir(d):
            continue
        # 目录里真有可执行文件才算数
        for exe in ("build-linux/yiufplayer", "yiufplayer", "muvik"):
            if os.path.isfile(os.path.join(d, exe)):
                return d
    return os.path.dirname(here)


INSTALL_DIR = find_install_dir()

# 真正要启动的东西：优先 build-linux/yiufplayer（在源码树里直接编译的场景），
# 其次目录下的 yiufplayer。都不存在就仍然记录首选路径，让用户自己改。
_exe_candidates = [
    os.path.join(INSTALL_DIR, "build-linux", "yiufplayer"),
    os.path.join(INSTALL_DIR, "yiufplayer"),
    os.path.join(INSTALL_DIR, "muvik"),
]
exe_raw = next((p for p in _exe_candidates if os.path.isfile(p)), _exe_candidates[0])

# 图标：优先用户给的 icon-512.png，退回 icon.png / icon-300.png
_icon_candidates = [
    os.path.join(INSTALL_DIR, "resources", "icon", n)
    for n in ("icon-512.png", "icon.png", "icon-300.png")
]
icon_path = next((p for p in _icon_candidates if os.path.isfile(p)), _icon_candidates[0])

appname = "Yiuf Player"

print(f"安装目录: {INSTALL_DIR}")
print(f"启动目标: {exe_raw}  ({'存在' if os.path.isfile(exe_raw) else '⚠ 不存在'})")
print(f"图标: {icon_path}  ({'存在' if os.path.isfile(icon_path) else '⚠ 不存在'})")
if not os.path.isfile(exe_raw):
    print("\n提示：可执行文件不在预期位置。请先编译（./scripts/build-linux.sh）")
    print("      或用 MUVIK_DIR=/path/to/install python3 scripts/add_to_steam.py 指定目录")
    print("      仍会写入快捷方式，但需要你之后手动修正路径。\n")

cand = exe_raw + appname
unsigned_aid = zlib.crc32(cand.encode("utf-8")) | 0x80000000
signed_aid = struct.unpack("<i", struct.pack("<I", unsigned_aid))[0]
grid_id = str(unsigned_aid)

print(f"Calculated AppID: signed={signed_aid}, unsigned={unsigned_aid}, grid_id={grid_id}")

# Steam 的 userdata 目录不要写死 /home/deck。
#
# SteamOS 上用户名通常是 deck，但本脚本也可能在 Arch 桌面、WSL2 或者
# 换了用户名的 Deck 上跑。写死 /home/deck 的结果是：glob 返回空列表，
# 循环一次都不进，脚本「成功退出」却什么也没加 —— 用户完全无感。
_steam_roots = [
    os.path.expanduser("~/.local/share/Steam"),
    os.path.expanduser("~/.steam/steam"),
    os.path.expanduser("~/.var/app/com.valvesoftware.Steam/.local/share/Steam"),  # Flatpak 版
]
user_dirs = []
for root in _steam_roots:
    found = glob.glob(os.path.join(root, "userdata", "*"))
    if found:
        user_dirs.extend(found)
        print(f"Steam userdata: {root} -> {len(found)} 个账户")
        break

if not user_dirs:
    print("\n未找到 Steam userdata 目录。已尝试：")
    for r in _steam_roots:
        print(f"  {r}/userdata/*")
    print("请确认：1) Steam 至少启动并登录过一次  2) 在执行本脚本的这台机器上")
    print("（Steam 未登录过时不会创建 userdata 目录。）")
    sys.exit(1)

# Update shortcuts.vdf in each userdata dir
new_entry = {
    "appid": signed_aid,
    "AppName": appname,
    "Exe": f"\"{exe_raw}\"",
    "StartDir": f"\"{INSTALL_DIR}/\"",
    "icon": icon_path,
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
    "sortas": appname,
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
        name = sc.get("AppName", "")
        exe = sc.get("Exe", "")
        # 认「本项目的任何历史名字/路径」，避免重复添加。
        # 旧版叫 Muvik、产物叫 muvik/muvik.sh；现在叫 Yiuf Player / yiufplayer。
        if (name in ("Muvik", "Yiuf Player")
                or "yiufplayer" in exe or "muvik" in exe.lower()):
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

print(f"{appname} successfully added to Steam shortcuts!")
