import os
import glob
import shutil
import struct

def parse_vdf(data, pos=0):
    res = {}
    while pos < len(data):
        t = data[pos]
        pos += 1
        if t == 8:
            return res, pos
        null_idx = data.find(b'\x00', pos)
        key = data[pos:null_idx].decode('utf-8', errors='ignore')
        pos = null_idx + 1
        if t == 0:
            val, pos = parse_vdf(data, pos)
            res[key] = val
        elif t == 1:
            null_idx = data.find(b'\x00', pos)
            val = data[pos:null_idx].decode('utf-8', errors='ignore')
            pos = null_idx + 1
            res[key] = val
        elif t == 2:
            val = struct.unpack('<i', data[pos:pos+4])[0]
            pos += 4
            res[key] = val
    return res, pos

def serialize_vdf(obj):
    buf = bytearray()
    for k, v in obj.items():
        if isinstance(v, dict):
            buf.append(0)
            buf.extend(k.encode('utf-8') + b'\x00')
            buf.extend(serialize_vdf(v))
            buf.append(8)
        elif isinstance(v, str):
            buf.append(1)
            buf.extend(k.encode('utf-8') + b'\x00')
            buf.extend(v.encode('utf-8') + b'\x00')
        elif isinstance(v, int):
            buf.append(2)
            buf.extend(k.encode('utf-8') + b'\x00')
            buf.extend(struct.pack('<i', v))
    return bytes(buf)

# 1. Update / create .desktop files
desktop_content = """[Desktop Entry]
Name=Muvik
Comment=Steam Deck 音视频播放器
Exec=/home/deck/muvik/muvik.sh
Icon=/home/deck/muvik/resources/icon/icon-512.png
Terminal=false
Type=Application
Categories=AudioVideo;Player;
Path=/home/deck/muvik
StartupNotify=true
"""

app_desktop = "/home/deck/.local/share/applications/muvik.desktop"
os.makedirs(os.path.dirname(app_desktop), exist_ok=True)
with open(app_desktop, "w") as f:
    f.write(desktop_content)
os.chmod(app_desktop, 0o755)

old_desktop = "/home/deck/.local/share/applications/deck-player.desktop"
if os.path.exists(old_desktop):
    os.remove(old_desktop)
    print(f"Removed old desktop file {old_desktop}")

# 2. Update shortcuts.vdf
for p in glob.glob('/home/deck/.local/share/Steam/userdata/*/config/shortcuts.vdf'):
    print('Processing:', p)
    with open(p, 'rb') as f:
        data = f.read()
    parsed, _ = parse_vdf(data)
    shortcuts = parsed.get('shortcuts', {})
    
    new_shortcuts = {}
    found_muvik = False
    idx_counter = 0
    
    for k in sorted(shortcuts.keys(), key=lambda x: int(x) if x.isdigit() else 999):
        sc = shortcuts[k]
        name = sc.get('AppName', '')
        exe = sc.get('exe') or sc.get('Exe', '')
        
        # Check if this entry is DeckPlayer or Muvik
        is_target = ('DeckPlayer' in name) or ('Muvik' in name) or ('muvik' in exe) or ('deck-player' in exe)
        
        if is_target:
            if not found_muvik:
                found_muvik = True
                # Keep original appid if it was DeckPlayer (-631112096), otherwise use standard
                aid = sc.get('appid', -631112096)
                muvik_entry = {
                    'appid': aid,
                    'AppName': 'Muvik',
                    'Exe': '"/home/deck/muvik/muvik.sh"',
                    'StartDir': '"/home/deck/muvik/"',
                    'icon': '/home/deck/muvik/resources/icon/icon-512.png',
                    'ShortcutPath': app_desktop,
                    'LaunchOptions': '',
                    'IsHidden': 0,
                    'AllowDesktopConfig': 1,
                    'AllowOverlay': 1,
                    'OpenVR': 0,
                    'Devkit': 0,
                    'DevkitGameID': '',
                    'DevkitOverrideAppID': 0,
                    'LastPlayTime': sc.get('LastPlayTime', 0),
                    'FlatpakAppID': '',
                    'sortas': 'Muvik',
                    'tags': {'0': 'Media'},
                }
                new_shortcuts[str(idx_counter)] = muvik_entry
                idx_counter += 1
                print(f"  Updated entry to Muvik (appid={aid})")
            else:
                print(f"  Removed duplicate target entry {name}")
        else:
            new_shortcuts[str(idx_counter)] = sc
            idx_counter += 1
            
    parsed['shortcuts'] = new_shortcuts
    out = serialize_vdf(parsed) + b'\x08'
    shutil.copy2(p, p + '.bak2')
    with open(p, 'wb') as f:
        f.write(out)
    print(f"Saved {p} with {len(new_shortcuts)} entries")

# 3. Grid art sync for both IDs
grid_dirs = glob.glob('/home/deck/.local/share/Steam/userdata/*/config/grid')
source_grid_dirs = ['/home/deck/muvik/steam/grid', '/home/deck/muvik/grid']
source_dir = None
for sd in source_grid_dirs:
    if os.path.isdir(sd):
        source_dir = sd
        break

for gdir in grid_dirs:
    os.makedirs(gdir, exist_ok=True)
    if source_dir:
        for f in os.listdir(source_dir):
            if f.endswith('.png'):
                shutil.copy2(os.path.join(source_dir, f), os.path.join(gdir, f))
    for suffix in ['.png', 'p.png', '_hero.png', '_logo.png']:
        src = os.path.join(gdir, f'3663855200{suffix}')
        dst = os.path.join(gdir, f'3963364876{suffix}')
        if os.path.exists(src) and not os.path.exists(dst):
            shutil.copy2(src, dst)
        elif os.path.exists(dst) and not os.path.exists(src):
            shutil.copy2(dst, src)
print('Grid art synchronized for both AppIDs')
