/* ============================================================
   DeckPlayer · UI 评审台（原型交互）
   所有布局数值取自 src/view/mpv_player.cpp 的 namespace osd
   与 ui/design.hpp，缩放只发生在最外层 .screen 的 transform 上。
   ============================================================ */
(function () {
'use strict';

/* ── 图标：24×24 坐标系，与 ui/icons.cpp 同一套路径语义 ───── */

const ICONS = {
  play:      '<path d="M8 5.4 19 12 8 18.6Z" fill="currentColor" stroke="none"/>',
  pause:     '<rect x="6.7" y="5.2" width="4" height="13.6" rx="1.5" fill="currentColor" stroke="none"/><rect x="13.3" y="5.2" width="4" height="13.6" rx="1.5" fill="currentColor" stroke="none"/>',
  seekBack:  '<path d="M14 6 8 12l6 6M20 6l-6 6 6 6"/>',
  seekFwd:   '<path d="M10 6l6 6-6 6M4 6l6 6-6 6"/>',
  prev:      '<rect x="5.4" y="6" width="2" height="12" rx="0.8" fill="currentColor" stroke="none"/><path d="M18 6 9.4 12 18 18Z" fill="currentColor" stroke="none"/>',
  next:      '<rect x="16.6" y="6" width="2" height="12" rx="0.8" fill="currentColor" stroke="none"/><path d="M6 6l8.6 6L6 18Z" fill="currentColor" stroke="none"/>',
  subtitles: '<rect x="3" y="5.5" width="18" height="13" rx="2.8"/><rect x="6.6" y="12.9" width="4.6" height="2.2" rx="1.1" fill="currentColor" stroke="none"/><rect x="12.6" y="12.9" width="4.8" height="2.2" rx="1.1" fill="currentColor" stroke="none"/>',
  subsOff:   '<rect x="3" y="5.5" width="18" height="13" rx="2.8"/><rect x="6.6" y="12.9" width="4.6" height="2.2" rx="1.1" fill="currentColor" stroke="none"/><rect x="12.6" y="12.9" width="4.8" height="2.2" rx="1.1" fill="currentColor" stroke="none"/><path d="M3.6 20 20.4 4"/>',
  audio:     '<rect x="5.6" y="9" width="2.2" height="6" rx="1.1" fill="currentColor" stroke="none"/><rect x="10" y="5.5" width="2.2" height="13" rx="1.1" fill="currentColor" stroke="none"/><rect x="14.4" y="7.5" width="2.2" height="9" rx="1.1" fill="currentColor" stroke="none"/><rect x="18" y="9.8" width="2.2" height="4.4" rx="1.1" fill="currentColor" stroke="none"/>',
  volume:    '<path d="M3.6 9.4h3.6l4.4-4v13.2l-4.4-4H3.6Z"/><path d="M15.4 8.6a4.8 4.8 0 0 1 0 6.8M18.2 6.2a8.2 8.2 0 0 1 0 11.6"/>',
  mute:      '<path d="M3.6 9.4h3.6l4.4-4v13.2l-4.4-4H3.6Z"/><path d="M15.4 9.2l5 5.6M20.4 9.2l-5 5.6"/>',
  speed:     '<path d="M4.6 17.2a8.5 8.5 0 1 1 14.8 0"/><path d="M12 14 15.8 10.4"/><circle cx="12" cy="14.6" r="1.5" fill="currentColor" stroke="none"/>',
  aspect:    '<rect x="3.2" y="5.2" width="17.6" height="13.6" rx="2.4"/><rect x="6.6" y="8.4" width="10.8" height="7.2" rx="1.2"/>',
  playlist:  '<path d="M4 6.2h11M4 12h9M4 17.8h7"/><path d="M17.4 10.4v7.4"/><circle cx="15.6" cy="18" r="1.9" fill="currentColor" stroke="none"/>',
  lock:      '<rect x="5.4" y="10.4" width="13.2" height="10.2" rx="2.6"/><path d="M8.4 10.4V8a3.6 3.6 0 0 1 7.2 0v2.4"/>',
  unlock:    '<rect x="5.4" y="10.4" width="13.2" height="10.2" rx="2.6"/><path d="M8.4 10.4V8a3.6 3.6 0 0 1 6.9-1.3"/>',
  back:      '<path d="M15 5.5 8.5 12l6.5 6.5"/>',
  chevron:   '<path d="M9.5 5.5 16 12l-6.5 6.5"/>',
  folder:    '<path d="M3.5 7.6a2 2 0 0 1 2-2h3.4l2 2.5h7.6a2 2 0 0 1 2 2v6.7a2 2 0 0 1-2 2h-13a2 2 0 0 1-2-2Z"/>',
  video:     '<rect x="3" y="5" width="18" height="14" rx="3"/><path d="M10.2 9.6 15 12l-4.8 2.4Z" fill="currentColor" stroke="none"/>',
  music:     '<path d="M9.2 17.6V6.2l9.6-1.9v11.3"/><circle cx="6.6" cy="18" r="2.6"/><circle cx="16.2" cy="16.2" r="2.6"/>',
  clock:     '<circle cx="12" cy="12" r="8.4"/><path d="M12 7.2V12l3.2 2"/>',
  close:     '<path d="M6.5 6.5 17.5 17.5M17.5 6.5 6.5 17.5"/>',
  check:     '<path d="M5 12.4 9.6 17 19 6.6"/>',
  error:     '<circle cx="12" cy="12" r="8.4"/><path d="M12 7.6v5.2M12 16.4h.01"/>',
  info:      '<circle cx="12" cy="12" r="8.4"/><path d="M12 11.2v5M12 7.9h.01"/>',
  brightness:'<circle cx="12" cy="12" r="3.8"/><path d="M12 3.2v2M12 18.8v2M3.2 12h2M18.8 12h2M5.8 5.8l1.4 1.4M16.8 16.8l1.4 1.4M18.2 5.8l-1.4 1.4M7.2 16.8l-1.4 1.4"/>',
  drive:     '<rect x="3" y="6" width="18" height="12" rx="2.6"/><path d="M3.4 13.4h17.2"/><circle cx="17.2" cy="15.6" r="1.1" fill="currentColor" stroke="none"/>',
  plugin:    '<path d="M9 3.4v4.4M15 3.4v4.4M6.6 7.8h10.8v3.6a5.4 5.4 0 0 1-10.8 0Z"/><path d="M12 16.8v3.8"/>',
  search:    '<circle cx="10.8" cy="10.8" r="6.2"/><path d="M15.4 15.4 20 20"/>',
  plus:      '<path d="M12 5v14M5 12h14"/>',
  arrowUp:   '<path d="M12 19V6M6.5 11.5 12 6l5.5 5.5"/>',
  tv:        '<rect x="3" y="4.5" width="18" height="12.5" rx="2.4"/><path d="M8 20h8M12 17v3"/>',
};

function ic(name, size, sw) {
  size = size || 20;
  sw = sw || 1.9;
  const body = ICONS[name] || ICONS.info;
  return '<svg width="' + size + '" height="' + size + '" viewBox="0 0 24 24" fill="none" ' +
    'stroke="currentColor" stroke-width="' + sw + '" stroke-linecap="round" stroke-linejoin="round" ' +
    'aria-hidden="true">' + body + '</svg>';
}

/* ── 数据 ─────────────────────────────────────────────────── */

const VIDEOS = [
  { title: '01. 漂流 冒険の島!', file: '01. 漂流 冒険の島!.mp4', sub: '403.4 KB · 含字幕',
    badge: '已看 33%', progress: 0.33, dur: 206, pos: 68, meta: '640×360 · 软件解码', hw: false },
  { title: '02. Blade Runner 2049', file: '02. Blade Runner 2049.mp4', sub: '52.3 KB · 含字幕',
    badge: '已看 64%', progress: 0.64, dur: 955, pos: 611, meta: '1920×1080 · 硬件解码', hw: true },
  { title: '03. 大鱼海棠', file: '03. 大鱼海棠.mkv', sub: '162.5 KB · 含字幕',
    badge: '已看完', progress: 1, dur: 5460, pos: 5460, meta: '1920×804 · 硬件解码', hw: true },
  { title: '04. 你的名字', file: '04. 你的名字.mp4', sub: '208.1 KB',
    badge: '', progress: -1, dur: 6360, pos: 0, meta: '1920×1080 · 硬件解码', hw: true },
];

const MUSIC = [
  { title: '01. 夜に駆ける', sub: 'YOASOBI · 3:21 · MP3 320k', badge: '3:21' },
  { title: '02. Interstellar Theme', sub: 'Hans Zimmer · 4:20 · FLAC', badge: '4:20' },
  { title: '03. 花の唄', sub: 'Aimer · 3:10 · MP3 320k', badge: '3:10' },
];

const HISTORY = [
  { title: '01. 漂流 冒険の島!', sub: '已播 0:02 / 3:26', badge: '刚刚', progress: 0.01, idx: 0 },
  { title: '02. Blade Runner 2049', sub: '已播 10:11 / 15:55', badge: '今天', progress: 0.64, idx: 1 },
  { title: '03. 大鱼海棠', sub: '已看完 · 1:31:00', badge: '昨天', progress: 1, idx: 2 },
  { title: '04. 你的名字', sub: '未开始', badge: '3 天前', progress: -1, idx: 3 },
];

const SUBS = [
  { title: '关闭字幕', sub: '不显示任何字幕', icon: 'subsOff', off: true },
  { title: '简体中文', sub: '内嵌 · ASS', icon: 'subtitles' },
  { title: 'English', sub: '内嵌 · SUBRIP', icon: 'subtitles' },
  { title: '日本語', sub: '外挂 · 日本語.ass', icon: 'subtitles' },
];

const AUDIOS = [
  { title: '日语 5.1', sub: '内嵌 · AAC · 6 声道', icon: 'audio' },
  { title: '国语 立体声', sub: '内嵌 · AAC · 2 声道', icon: 'audio' },
];

const ASPECTS = ['适应屏幕', '填充画面', '拉伸', '原始尺寸'];
const SPEEDS = [1, 1.25, 1.5, 2, 0.75];

// 存储位置 → 目录浏览用的最小文件树（真机由 MediaScanner 提供）
const FS = {
  '/home/deck': {
    name: '内置存储',
    items: [
      { dir: true, name: 'Videos', path: '/home/deck/Videos' },
      { dir: true, name: 'Music', path: '/home/deck/Music' },
      { dir: true, name: 'Downloads', path: '/home/deck/Downloads' },
    ],
  },
  '/home/deck/Videos': {
    name: 'Videos',
    items: [
      { name: '01. 漂流 冒険の島!.mp4', sub: '403.4 KB · 含外挂字幕', v: 0 },
      { name: '02. Blade Runner 2049.mp4', sub: '52.3 KB · 含字幕', v: 1 },
      { name: '03. 大鱼海棠.mkv', sub: '162.5 KB', v: 2 },
      { name: '04. 你的名字.mp4', sub: '208.1 KB', v: 3 },
    ],
  },
  '/home/deck/Music': {
    name: 'Music',
    items: [
      { name: '01. 夜に駆ける.mp3', sub: 'YOASOBI · 3:21 · 6.2 MB', m: 0 },
      { name: '02. Interstellar Theme.flac', sub: 'Hans Zimmer · 4:20 · 38 MB', m: 1 },
      { name: '03. 花の唄.mp3', sub: 'Aimer · 3:10 · 5.9 MB', m: 2 },
    ],
  },
  '/home/deck/Downloads': { name: 'Downloads', items: [] },
  '/run/media/deck/SD256': {
    name: 'SD 卡',
    items: [{ dir: true, name: 'Videos', path: '/run/media/deck/SD256/Videos' }],
  },
  '/run/media/deck/SD256/Videos': {
    name: 'SD 卡 / Videos',
    items: [{ name: '纪录片 01.mp4', sub: '1.2 GB', v: 0 }],
  },
};

const PARENT = {
  '/home/deck': null,
  '/home/deck/Videos': '/home/deck',
  '/home/deck/Music': '/home/deck',
  '/home/deck/Downloads': '/home/deck',
  '/run/media/deck/SD256': null,
  '/run/media/deck/SD256/Videos': '/run/media/deck/SD256',
};

const DEVICES = {
  deck:     { label: 'Deck',   name: 'Steam Deck',      w: 1280, h: 800, pw: 1280, ph: 800 },
  hd:       { label: '1080p',  name: '1920×1080 桌面',  w: 1280, h: 720, pw: 1920, ph: 1080 },
  handheld: { label: '掌机',   name: '640×480 掌机',    w: 1280, h: 960, pw: 640,  ph: 480 },
  tablet:   { label: '4:3',    name: '1920×1440 平板',  w: 1280, h: 960, pw: 1920, ph: 1440 },
};

const TABS = [
  { key: 'sources', label: '存储位置' },
  { key: 'library', label: '视频库' },
  { key: 'audio',   label: '音乐库' },
  { key: 'history', label: '播放历史' },
];

const TAB_LABEL = { sources: '存储位置', library: '视频库', audio: '音乐库', history: '播放历史', plugins: '插件管理' };

const RISKS = [
  { tag: '小屏', name: '掌机上的物理缩水',
    text: '12.5 字号在 640×480 掌机上只有 6.25 物理像素；44 的触控目标缩到 22px。是否要给最小支持宽度，低于它切换成「大字模式」？' },
  { tag: '触控', name: '设置行的净空',
    text: '6 个 40px 圆钮、间距 12，相邻命中区间净空只剩 4 逻辑像素。字幕和音轨并排，是整屏最容易点错的地方。' },
  { tag: 'OSD', name: '8 秒自动隐藏',
    text: '触屏用户要在 8 秒内从 6 个图标里找到「画面比例」。手柄有焦点导航，触屏只能靠记忆。是否改成「点空白处才收起」？' },
  { tag: '输入', name: '亮度仍只有手势',
    text: 'LT/RT 已经接管音量，但亮度在没有触摸屏的设备上无法调节。要不要把亮度也放进 OSD，或做成「LT/RT + 长按」？' },
  { tag: '列表', name: '卡片没有封面',
    text: '现在只有 46px 的类型图标。媒体库条目一多，页面就退化成文件名列表，缺少「一眼认出这是什么片子」的能力。' },
  { tag: '对比', name: '亮场下的进度条轨道',
    text: '未播放段是 20% 白，压在雪景、天空这类高亮画面上几乎消失（切到「亮场」就能看到）。是否改成「深色槽 + 亮色细边」？' },
];

const STORE_KEY = 'deckplayer.ui-review.v1';

/* ── 状态 ─────────────────────────────────────────────────── */

const state = {
  tab: 'library',
  browse: null,
  theme: 'dark',
  device: 'deck',
  scene: 'dark',
  focus: { zone: 'main', i: 0 },
  annotate: false,
  zoom: 'fit',
  selected: null,
  composerTarget: '整体 · 视频库',
  comments: [],
  dialog: null,
  pluginPage: false,
  player: {
    open: false, playing: true, pos: 68, dur: 206, index: 0,
    osd: true, locked: false, speed: 1, aspect: 0, sub: 1, audio: 0,
  },
  np: { show: false, playing: true, pos: 78, dur: 201 },
  seekDrag: false,
};

try {
  const saved = JSON.parse(localStorage.getItem(STORE_KEY) || '[]');
  if (Array.isArray(saved)) state.comments = saved;
} catch (e) { /* 忽略损坏的本地数据 */ }

/* ── 工具 ─────────────────────────────────────────────────── */

const $ = (s, r) => (r || document).querySelector(s);
const $$ = (s, r) => Array.prototype.slice.call((r || document).querySelectorAll(s));
const esc = (s) => String(s).replace(/[&<>"']/g, (c) =>
  ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

function fmt(sec) {
  sec = Math.max(0, Math.round(sec));
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  const mm = String(m).padStart(2, '0');
  const ss = String(s).padStart(2, '0');
  return h > 0 ? h + ':' + mm + ':' + ss : m + ':' + ss;
}

function clock(seconds) {
  const d = new Date();
  return String(d.getHours()).padStart(2, '0') + ':' +
         String(d.getMinutes()).padStart(2, '0') +
         (seconds ? ':' + String(d.getSeconds()).padStart(2, '0') : '');
}

let toastTimer = null;
function toast(msg) {
  const el = $('#toast');
  el.textContent = msg;
  el.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.hidden = true; }, 2400);
}

function saveComments() {
  try { localStorage.setItem(STORE_KEY, JSON.stringify(state.comments)); } catch (e) {}
}

/* ── 应用列表渲染 ─────────────────────────────────────────── */

function cell(o) {
  const chev = o.chevron ? '<span class="chev">' + ic('chevron', 16, 1.8) + '</span>' : '';
  const badge = o.badge ? '<span class="badge">' + esc(o.badge) + '</span>' : '';
  const prog = (o.progress != null && o.progress >= 0)
    ? '<span class="pbar"><i style="width:' + Math.round(clamp(o.progress, 0, 1) * 100) + '%"></i></span>'
    : '';
  const risk = o.risk ? ' data-risk="' + esc(o.risk) + '"' : '';
  const spec = o.spec || '高 76 · 圆角 14 · 标题 17 / 副标题 12.5';
  return '<div class="cell focusable' + (o.progress >= 0 ? ' has-progress' : '') + '"' +
    ' data-action="' + o.action + '" data-id="' + (o.id != null ? o.id : '') + '"' +
    ' data-name="' + esc(o.name || '列表卡片') + '" data-spec="' + esc(spec) + '"' + risk + '>' +
    '<span class="tile">' + ic(o.icon, 23, 1.9) + '</span>' +
    '<span class="body"><span class="title">' + esc(o.title) + '</span>' +
    (o.sub ? '<span class="sub">' + esc(o.sub) + '</span>' : '') + '</span>' +
    badge + chev + prog + '</div>';
}

function section(title) {
  return '<div class="section-h" data-name="分组标题" ' +
    'data-spec="高 46 · 标题 13px / 字距 0.6 · 强调短条 3×12">' +
    '<span class="tick"></span>' + esc(title) + '</div>';
}

function emptyState(icon, title, hint) {
  return '<div class="empty-state">' +
    '<span class="ring">' + ic(icon, 32, 1.8) + '</span>' +
    '<span class="t">' + esc(title) + '</span>' +
    '<span class="h">' + esc(hint) + '</span></div>';
}

function navHTML() {
  return TABS.map((t, i) =>
    '<div class="nav-item' + (state.tab === t.key ? ' active' : '') + ' focusable" ' +
    'data-tab="' + t.key + '" data-name="侧边栏条目" ' +
    'data-spec="高 62 · 左内边距 48 · 字号 20 · 强调条 3×26">' +
    '<span class="bar"></span>' + t.label + '</div>').join('');
}

function listHTML() {
  const t = state.tab;
  if (t === 'sources') {
    if (state.browse) return browseHTML();
    return section('本地存储设备') +
      cell({ icon: 'drive', title: '内置存储', sub: '/home/deck · 1.2 TB 可用', chevron: true, action: 'browse', id: '/home/deck' }) +
      cell({ icon: 'drive', title: 'SD 卡', sub: '/run/media/deck/SD256 · 512 GB', chevron: true, action: 'browse', id: '/run/media/deck/SD256' }) +
      section('插件视频源') +
      cell({ icon: 'plugin', title: '示例视频源', sub: 'v1.0.0 · 视频源 · 含刮削器', chevron: true, action: 'source-plugin' }) +
      cell({ icon: 'plus', title: '添加 / 管理插件', sub: '从 ZIP 或目录安装视频源 / 刮削源插件', chevron: true, action: 'plugins' });
  }
  if (t === 'library') {
    return section('媒体文件夹') +
      cell({ icon: 'plus', title: '添加媒体文件夹', sub: '选择包含视频 / 音乐的目录，自动扫描并记住', action: 'add-folder' }) +
      cell({ icon: 'folder', title: '影片', sub: '/home/deck/Videos', chevron: true, action: 'folder' }) +
      cell({ icon: 'folder', title: '音乐', sub: '/home/deck/Music', chevron: true, action: 'folder' }) +
      cell({ icon: 'search', title: '重新扫描媒体库', sub: '更新新增或修改的文件', action: 'rescan' }) +
      section('视频 · ' + VIDEOS.length) +
      VIDEOS.map((v, i) => cell({
        icon: 'video', title: v.title, sub: v.sub, badge: v.badge, progress: v.progress,
        action: 'play', id: i, name: '视频卡片',
        risk: i === 0 ? '卡片只有类型图标，没有封面图：条目一多就退化成文件名列表。' : '',
      })).join('');
  }
  if (t === 'audio') {
    return section('音乐 · ' + MUSIC.length) +
      MUSIC.map((m, i) => cell({
        icon: 'music', title: m.title, sub: m.sub, badge: m.badge,
        action: 'play-music', id: i, name: '音乐卡片',
      })).join('');
  }
  return section('最近播放 · ' + HISTORY.length) +
    HISTORY.map((h) => cell({
      icon: 'clock', title: h.title, sub: h.sub, badge: h.badge,
      progress: h.progress, action: 'play', id: h.idx, name: '历史卡片',
    })).join('');
}

function browseHTML() {
  const node = FS[state.browse];
  if (!node) { state.browse = null; return listHTML(); }
  const parent = PARENT[state.browse];
  const up = cell({
    icon: 'back', title: '返回上一级', sub: parent || '存储位置',
    chevron: true, action: 'up',
  });
  const rows = node.items.map((it) => {
    if (it.dir) {
      return cell({ icon: 'folder', title: it.name, sub: '文件夹', chevron: true, action: 'browse', id: it.path });
    }
    if (it.m != null) {
      return cell({ icon: 'music', title: it.name, sub: it.sub, action: 'play-music', id: it.m });
    }
    return cell({ icon: 'video', title: it.name, sub: it.sub, action: 'play', id: it.v });
  }).join('');
  const empty = node.items.length ? ''
    : emptyState('folder', '这个文件夹是空的', '支持常见的视频 / 音频格式\n同名字幕会自动匹配');
  return up + section(node.name + ' · ' + node.items.length + ' 项') + rows + empty;
}

function hintsHTML() {
  const h = (k, label, accent) =>
    '<span class="hint"><span class="k' + (accent ? ' a' : '') + '">' + k + '</span>' + label + '</span>';
  const map = {
    sources: h('A', '打开', true) + h('B', '返回') + h('Y', '刷新'),
    library: h('A', '播放', true) + h('B', '返回') + h('X', '获取信息') + h('Y', '重新扫描'),
    audio:   h('A', '播放', true) + h('B', '返回') + h('X', '加入队列'),
    history: h('A', '继续播放', true) + h('B', '返回') + h('X', '移除记录'),
  };
  if (state.tab === 'sources' && state.browse) {
    return h('A', '打开', true) + h('B', '返回上一级') + h('X', '查看信息');
  }
  return map[state.tab] || map.library;
}

function npHTML() {
  if (!state.np.show) return '';
  const ratio = clamp(state.np.pos / state.np.dur, 0, 1);
  return '<div class="np" data-name="正在播放条" data-spec="高 64 · 距底 64（避开提示栏）· 进度线 2px">' +
    '<span class="tile">' + ic(state.np.playing ? 'pause' : 'play', 18, 2) + '</span>' +
    '<span class="meta"><span class="t">夜に駆ける</span><span class="s">' +
    (state.np.playing ? '正在播放 · 音乐库' : '已暂停 · 音乐库') + '</span></span>' +
    '<span class="time">' + fmt(state.np.pos) + ' / ' + fmt(state.np.dur) + '</span>' +
    '<span class="line"><i style="width:' + (ratio * 100).toFixed(1) + '%"></i></span>' +
    '</div>';
}

function renderApp() {
  const app = $('#app');
  app.innerHTML =
    '<header class="app-header" data-name="应用顶栏" data-spec="高 92 · 左右内边距 48 · 标题 29px">' +
      '<span class="mark">' + ic('video', 24, 1.8) + '</span>' +
      '<h1>DeckPlayer</h1>' +
      '<span class="ver"><i></i>GPL-3.0</span>' +
    '</header>' +
    '<div class="app-body">' +
      '<nav class="sidebar" id="sidebar" data-name="侧边栏" data-spec="宽 300 · 条目高 62 · 字号 20 · 强调条 3×26">' +
        navHTML() + '</nav>' +
      '<section class="main" id="main"><div class="list" id="list">' + listHTML() + '</div></section>' +
    '</div>' +
    npHTML() +
    '<footer class="app-footer" data-name="底部提示栏" data-spec="高 64 · 提示字号 13 · 左右内边距 40">' +
      '<span class="clock">' + ic('clock', 18, 1.7) + '<span id="appClock">' + clock(true) + '</span></span>' +
      '<span class="hints">' + hintsHTML() + '</span>' +
    '</footer>';

  applyFocus();
  // 重渲染后，之前选中的 DOM 已经不在文档里：清掉选中态，
  // 否则「当前元素」会显示 0×0 的幽灵规格。
  if (state.selected && !document.body.contains(state.selected)) clearSelection();
  decorate();
  bindAppEvents();
}

function bindAppEvents() {
  const app = $('#app');

  app.onclick = (e) => {
    if (state.annotate) return; // 标注模式由全局捕获处理
    const nav = e.target.closest('.nav-item');
    if (nav) { switchTab(nav.dataset.tab); return; }
    const c = e.target.closest('.cell');
    if (c) { activateCell(c); return; }
    if (e.target.closest('.np')) {
      state.np.playing = !state.np.playing;
      renderApp();
      toast(state.np.playing ? '继续播放 · 夜に駆ける' : '已暂停');
    }
  };
}

function activateCell(el) {
  const a = el.dataset.action;
  const id = parseInt(el.dataset.id || '0', 10);
  if (a === 'play') { openPlayer(id); return; }
  if (a === 'play-music') {
    state.np.show = true; state.np.playing = true; state.np.pos = 12;
    renderApp(); toast('开始播放 · ' + MUSIC[id].title);
    return;
  }
  if (a === 'plugins') { openPlugins(); return; }
  if (a === 'add-folder') { openDialog('folder'); return; }
  if (a === 'rescan') {
    toast('正在扫描媒体库…');
    setTimeout(() => toast('扫描完成 · 4 个视频 / 3 首音乐'), 1100);
    return;
  }
  if (a === 'folder') { toast('打开文件夹（原型未实现文件浏览）'); return; }
  if (a === 'browse') {
    state.browse = el.dataset.id;
    state.focus = { zone: 'main', i: 0 };
    renderApp();
    setTarget('存储位置 · ' + (FS[state.browse] ? FS[state.browse].name : '目录'));
    renderDock();
    return;
  }
  if (a === 'up') { goUp(); return; }
  if (a === 'source-plugin') { toast('视频源插件需要先在插件管理里配置'); return; }
}

function goUp() {
  if (!state.browse) return;
  const parent = PARENT[state.browse];
  state.browse = parent || null;
  state.focus = { zone: 'main', i: 0 };
  renderApp();
  setTarget(parent ? '存储位置 · ' + FS[parent].name : '整体 · 存储位置');
  renderDock();
}

/* ── 焦点 ─────────────────────────────────────────────────── */

function navItems() { return $$('.nav-item'); }
function mainItems() { return $$('#main .focusable'); }

function applyFocus() {
  const navs = $$('.nav-item');
  const mains = $$('#main .focusable');
  if (state.focus.zone === 'sidebar' && state.focus.i >= navs.length) {
    state.focus.i = Math.max(0, navs.length - 1);
  }
  if (state.focus.zone === 'main' && state.focus.i >= mains.length) {
    state.focus.i = Math.max(0, mains.length - 1);
  }
  navs.forEach((el, i) => {
    el.classList.toggle('focused', state.focus.zone === 'sidebar' && i === state.focus.i);
    el.classList.toggle('active', el.dataset.tab === state.tab);
  });
  mains.forEach((el, i) => {
    el.classList.toggle('focused', state.focus.zone === 'main' && i === state.focus.i);
  });
}

function moveFocus(d) {
  const items = state.focus.zone === 'sidebar' ? navItems() : mainItems();
  if (!items.length) return;
  state.focus.i = clamp(state.focus.i + d, 0, items.length - 1);
  applyFocus();
  const el = items[state.focus.i];
  if (el) el.scrollIntoView({ block: 'nearest' });
}

function activateFocused() {
  const items = state.focus.zone === 'sidebar' ? navItems() : mainItems();
  const el = items[state.focus.i];
  if (!el) return;
  if (state.focus.zone === 'sidebar') switchTab(el.dataset.tab);
  else el.click();
}

function switchTab(tab) {
  state.tab = tab;
  state.browse = null;
  state.pluginPage = false;
  state.focus = { zone: 'main', i: 0 };
  renderApp();
  setTarget('整体 · ' + TAB_LABEL[tab]);
  renderDock();
}

/* ── 播放器 ───────────────────────────────────────────────── */

function screenSize() {
  const d = DEVICES[state.device];
  return { w: d.w, h: d.h };
}

function osdGeom(w, h) {
  const m = clamp(w * 0.042, 24, 56);
  const barCY = h - 66;
  const timeCY = h - 34;
  const trCY = barCY - 64;
  const setCY = trCY - 66;
  return { m: m, barCY: barCY, timeCY: timeCY, trCY: trCY, setCY: setCY };
}

function openPlayer(index) {
  const v = VIDEOS[index] || VIDEOS[0];
  const p = state.player;
  const keep = p.open && p.index === index;
  if (state.dialog) closeDialog();
  p.open = true;
  p.index = index;
  p.dur = v.dur;
  p.pos = keep ? p.pos : (v.pos > 0 ? v.pos : 0);
  p.playing = keep ? p.playing : true;
  p.osd = true;
  p.locked = false;
  buildPlayer();
  $('#app').classList.add('hidden');
  setTarget('播放器 · ' + v.title);
  renderDock();
  pokeOSD();
}

function closePlayer() {
  state.player.open = false;
  state.player.locked = false;
  state.seekDrag = false;
  closeDialog();
  const pl = $('#player');
  if (pl) pl.remove();
  const app = $('#app');
  if (app) app.classList.remove('hidden');
  clearTimeout(osdTimer);
  setTarget('整体 · ' + TAB_LABEL[state.tab]);
  renderDock();
}

function buildPlayer() {
  const host = $('#screen');
  const old = $('#player');
  if (old) old.remove();

  const { w, h } = screenSize();
  const g = osdGeom(w, h);
  const p = state.player;
  const v = VIDEOS[p.index];

  const btn = (id, cx, cy, d, icon, size, cls, name, spec) =>
    '<button class="osd-btn ' + (cls || '') + '" id="' + id + '" data-name="' + esc(name) + '" data-spec="' + esc(spec) + '" ' +
    'style="left:' + (cx - d / 2) + 'px;top:' + (cy - d / 2) + 'px;width:' + d + 'px;height:' + d + 'px">' +
    ic(icon, size, 1.9) + '</button>';

  const settingsRight = w - g.m;
  const setBtns = [];
  let right = settingsRight;
  const place = (id, icon, name) => {
    const cx = right - 20;
    setBtns.push(btn(id, cx, g.setCY, 40, icon, 19, '', name,
      '直径 40 · 间距 12 · 命中区 48'));
    right = cx - 20 - 12;
  };
  place('btnLock', 'lock', '锁定触控');
  place('btnPlaylist', 'playlist', '播放列表');
  place('btnAspect', 'aspect', '画面比例');
  const pillLeft = right - 78;
  const speedLabel = (Math.round(p.speed * 10) % 10 === 0 ? p.speed.toFixed(0) : p.speed.toFixed(1)) + '×';
  const speedPill =
    '<button class="osd-pill" id="btnSpeed" data-name="倍速胶囊" data-spec="78×34 · 间距 12 · 命中区 86×42" ' +
    'style="left:' + pillLeft + 'px;top:' + (g.setCY - 17) + 'px;width:78px">' +
    ic('speed', 16, 1.8) + '<span id="speedText">' + speedLabel + '</span></button>';
  right = pillLeft - 12;
  place('btnAudio', 'audio', '音轨');
  place('btnSub', p.sub > 0 ? 'subtitles' : 'subsOff', '字幕');

  const fill = clamp(p.pos / p.dur, 0, 1) * 100;

  const html =
    '<div class="player on" id="player">' +
      '<div class="scene ' + state.scene + '" id="scene"><div class="grain"></div></div>' +

      '<div class="osd on fade" id="osd" data-name="播放器 OSD" ' +
        'data-spec="顶栏 112 / 底栏 238 / 进度条 5 / 主控 64·46·40 / 设置 40" ' +
        'data-risk="触屏用户要在 8 秒内从 6 个图标里找到目标；手柄有焦点导航，触屏只能靠记忆。">' +
        '<div class="osd-top"></div>' +
        '<div class="osd-bottom"></div>' +

        '<button class="osd-btn" id="btnBack" data-name="OSD 返回" data-spec="直径 38 · 命中 46 · 距左 54" ' +
          'style="left:' + g.m + 'px;top:25px;width:38px;height:38px">' + ic('back', 19, 2) + '</button>' +

        '<div class="osd-title" style="left:' + (g.m + 52) + 'px;top:28px;line-height:20px;' +
          'max-width:' + (w - (g.m + 52) - g.m - 84) + 'px">' + esc(v.file) + '</div>' +
        '<div class="osd-meta" style="left:' + (g.m + 52) + 'px;top:58.5px;line-height:12.5px">' +
          esc(v.meta) + '</div>' +
        '<div class="osd-clock" style="right:' + g.m + 'px;top:31px;line-height:14px">' + clock(false) + '</div>' +

        '<div class="seek-hit" id="seekHit" data-name="播放进度条" ' +
          'data-spec="可视高 5 · 命中高 46 · 左右边距 ' + Math.round(g.m) + '" ' +
          'style="left:' + g.m + 'px;top:' + (g.barCY - 23) + 'px;width:' + (w - g.m * 2) + 'px">' +
          '<div class="seek-track"><div class="seek-fill" id="seekFill" style="width:' + fill + '%"></div></div>' +
          '<div class="seek-knob" id="seekKnob" style="left:' + fill + '%"></div>' +
          '<div class="seek-bubble" id="seekBubble" style="left:0;display:none">' + fmt(p.pos) + '</div>' +
        '</div>' +

        '<div class="osd-time" id="timeNow" style="left:' + g.m + 'px;top:' + (g.timeCY - 7) + 'px;line-height:14px">' + fmt(p.pos) + '</div>' +
        '<div class="osd-time total" id="timeTotal" style="left:' + (w - g.m) + 'px;top:' + (g.timeCY - 7) + 'px;line-height:14px;transform:translateX(-100%)">' + fmt(p.dur) + '</div>' +

        btn('btnPrev', w / 2 - 124, g.trCY, 40, 'prev', 20, '', '上一集', '直径 40 · 距中心 124') +
        btn('btnBack10', w / 2 - 68, g.trCY, 46, 'seekBack', 21, '', '快退 10 秒', '直径 46 · 距中心 68') +
        btn('btnPlay', w / 2, g.trCY, 64, p.playing ? 'pause' : 'play', 28, 'play', '播放 / 暂停',
          '直径 64 · 全屏唯一的实心强调色主操作') +
        btn('btnFwd10', w / 2 + 68, g.trCY, 46, 'seekFwd', 21, '', '快进 10 秒', '直径 46 · 距中心 68') +
        btn('btnNext', w / 2 + 124, g.trCY, 40, 'next', 20, '', '下一集', '直径 40 · 距中心 124') +

        setBtns.join('') + speedPill +
      '</div>' +

      /* HUD 与锁定提示必须挂在 .osd 之外：
         它们要在 OSD 收起（甚至锁定后）时仍然可见。 */
      '<div class="hud" id="hud"></div>' +
      '<div id="lockBadge" class="lock-badge" style="left:' + g.m + 'px;top:16px;display:none">' + ic('lock', 17, 1.9) + '</div>' +
      '<div id="lockHint" class="lock-hint" style="left:50%;top:18px;transform:translateX(-50%);display:none">' +
        ic('unlock', 18, 1.9) + '<span>触控已锁定 · 双击解锁</span></div>' +
    '</div>';

  host.insertAdjacentHTML('beforeend', html);
  const el = $('#player');
  if (!el) return;

  el.addEventListener('click', onPlayerClick);
  el.addEventListener('dblclick', onPlayerDblClick);
  el.addEventListener('mousemove', () => pokeOSD(false));
  el.addEventListener('wheel', onWheel, { passive: false });

  $('#btnBack').onclick = (e) => { e.stopPropagation(); closePlayer(); };
  $('#btnPlay').onclick = (e) => { e.stopPropagation(); togglePlay(); };
  $('#btnPrev').onclick = (e) => { e.stopPropagation(); stepTrack(-1); };
  $('#btnNext').onclick = (e) => { e.stopPropagation(); stepTrack(1); };
  $('#btnBack10').onclick = (e) => { e.stopPropagation(); seekBy(-10); };
  $('#btnFwd10').onclick = (e) => { e.stopPropagation(); seekBy(10); };
  $('#btnLock').onclick = (e) => { e.stopPropagation(); toggleLock(); };
  $('#btnPlaylist').onclick = (e) => { e.stopPropagation(); openDialog('playlist'); };
  $('#btnAspect').onclick = (e) => { e.stopPropagation(); cycleAspect(); };
  $('#btnSpeed').onclick = (e) => { e.stopPropagation(); cycleSpeed(); };
  $('#btnAudio').onclick = (e) => { e.stopPropagation(); openDialog('audio'); };
  $('#btnSub').onclick = (e) => { e.stopPropagation(); openDialog('subs'); };

  bindSeek();

  if (state.player.locked) {
    $('#lockBadge').style.display = 'grid';
    $('#osd').classList.remove('on');
  }
  syncOSD();
  decorate();
}

/* 播放控制 */

function updatePlayIcon() {
  const b = $('#btnPlay');
  if (b) b.innerHTML = ic(state.player.playing ? 'pause' : 'play', 28, 2.2);
}

function togglePlay() {
  state.player.playing = !state.player.playing;
  updatePlayIcon();
  showHUD(state.player.playing ? 'play' : 'pause', state.player.playing ? '播放' : '暂停');
  pokeOSD();
}

function seekBy(delta) {
  const p = state.player;
  p.pos = clamp(p.pos + delta, 0, p.dur);
  updateSeekUI();
  showHUD(delta < 0 ? 'seekBack' : 'seekFwd', (delta < 0 ? '快退 ' : '快进 ') + Math.abs(delta) + ' 秒');
  pokeOSD();
}

function stepTrack(d) {
  const p = state.player;
  const next = p.index + d;
  if (next < 0 || next >= VIDEOS.length) {
    showHUD('info', d < 0 ? '已经是第一个' : '已经是最后一个');
    pokeOSD();
    return;
  }
  const v = VIDEOS[next];
  p.index = next;
  p.dur = v.dur;
  p.pos = 0;
  updateTitle();
  updateSeekUI();
  showHUD(d < 0 ? 'prev' : 'next', d < 0 ? '上一集' : '下一集');
  pokeOSD();
}

function updateTitle() {
  const v = VIDEOS[state.player.index];
  const t = $('.osd-title');
  const m = $('.osd-meta');
  if (t) t.textContent = v.file;
  if (m) m.textContent = v.meta;
  renderDock();
}

function cycleSpeed() {
  const p = state.player;
  const i = SPEEDS.indexOf(p.speed);
  p.speed = SPEEDS[(i + 1) % SPEEDS.length];
  const label = (p.speed % 1 === 0 ? p.speed.toFixed(0) : p.speed.toFixed(1)) + '×';
  $('#speedText').textContent = label;
  showHUD('speed', '倍速 ' + label.replace('×', '×'), (i + 1) / SPEEDS.length);
  pokeOSD();
}

function cycleAspect() {
  const p = state.player;
  p.aspect = (p.aspect + 1) % ASPECTS.length;
  showHUD('aspect', '画面 · ' + ASPECTS[p.aspect], (p.aspect + 1) / ASPECTS.length);
  pokeOSD();
}

function toggleLock() {
  const p = state.player;
  p.locked = !p.locked;
  if (p.locked) {
    p.osd = false;
    closeDialog();
    $('#lockBadge').style.display = 'grid';
    showHUD('lock', '触控已锁定');
    setTimeout(() => { const h = $('#hud'); if (h) h.classList.remove('show'); }, 1200);
  } else {
    $('#lockBadge').style.display = 'none';
    showHUD('unlock', '触控已解锁');
    pokeOSD();
  }
  syncOSD();
  renderDock();
}

function onPlayerClick(e) {
  if (e.target.closest('.osd-btn, .osd-pill, .seek-hit, .hud, .dialog-layer')) return;
  if (e.detail > 1) return;
  const p = state.player;
  if (p.locked) {
    const hint = $('#lockHint');
    hint.style.display = 'flex';
    clearTimeout(lockHintTimer);
    lockHintTimer = setTimeout(() => { hint.style.display = 'none'; }, 2600);
    return;
  }
  clearTimeout(clickTimer);
  clickTimer = setTimeout(() => {
    p.osd = !p.osd;
    syncOSD();
    if (p.osd) pokeOSD();
  }, 180);
}

function onPlayerDblClick(e) {
  if (e.target.closest('.osd-btn, .osd-pill, .seek-hit, .dialog-layer')) return;
  clearTimeout(clickTimer);
  if (state.player.locked) {
    toggleLock();
    $('#lockHint').style.display = 'none';
  } else {
    togglePlay();
  }
}

function onWheel(e) {
  e.preventDefault();
  if (state.player.locked) return;
  const step = e.deltaY < 0 ? 5 : -5;
  if (e.shiftKey) {
    state.brightness = clamp((state.brightness == null ? 100 : state.brightness) + step, 10, 100);
    const scene = $('#scene');
    if (scene) scene.style.filter = 'brightness(' + (state.brightness / 100) + ')';
    showHUD('brightness', '亮度 ' + state.brightness + '%', state.brightness / 100);
  } else {
    state.volume = clamp((state.volume == null ? 60 : state.volume) + step, 0, 100);
    showHUD(state.volume === 0 ? 'mute' : 'volume', '音量 ' + state.volume + '%', state.volume / 100);
  }
  pokeOSD();
}

/* 进度条拖动 */

function bindSeek() {
  const hit = $('#seekHit');
  if (!hit) return;
  let rect = null;

  const ratioAt = (x) => clamp((x - rect.left) / rect.width, 0, 1);

  const move = (e) => {
    if (!state.seekDrag) return;
    const r = ratioAt(e.clientX);
    state.dragRatio = r;
    const pos = r * state.player.dur;
    const fill = r * 100;
    $('#seekFill').style.width = fill + '%';
    $('#seekKnob').style.left = fill + '%';
    $('#timeNow').textContent = fmt(pos);
    const b = $('#seekBubble');
    b.style.display = 'grid';
    b.textContent = fmt(pos);
    b.style.left = clamp(fill, 4, 96) + '%';
  };

  const up = () => {
    if (!state.seekDrag) return;
    state.seekDrag = false;
    hit.classList.remove('dragging');
    $('#seekBubble').style.display = 'none';
    state.player.pos = (state.dragRatio || 0) * state.player.dur;
    updateSeekUI();
    pokeOSD();
  };

  hit.addEventListener('pointerdown', (e) => {
    if (state.player.locked) return;
    rect = hit.getBoundingClientRect();
    state.seekDrag = true;
    state.dragRatio = ratioAt(e.clientX);
    hit.classList.add('dragging');
    hit.setPointerCapture(e.pointerId);
    move(e);
    clearTimeout(osdTimer);
    e.stopPropagation();
  });
  hit.addEventListener('pointermove', move);
  hit.addEventListener('pointerup', up);
  hit.addEventListener('pointercancel', up);
}

function updateSeekUI() {
  const p = state.player;
  const r = clamp(p.pos / p.dur, 0, 1) * 100;
  const fill = $('#seekFill');
  if (fill) fill.style.width = r + '%';
  const knob = $('#seekKnob');
  if (knob) knob.style.left = r + '%';
  const t = $('#timeNow');
  if (t) t.textContent = fmt(p.pos);
}

/* OSD 显隐 */

let osdTimer = null, clickTimer = null, lockHintTimer = null;

function syncOSD() {
  const osd = $('#osd');
  if (!osd) return;
  osd.classList.toggle('on', state.player.osd && !state.player.locked);
}

function pokeOSD(show) {
  const p = state.player;
  if (!p.open || p.locked) return;
  if (show !== false) { p.osd = true; syncOSD(); }
  clearTimeout(osdTimer);
  osdTimer = setTimeout(() => {
    if (state.dialog) { pokeOSD(false); return; }
    p.osd = false;
    syncOSD();
  }, 8000);
}

/* HUD */

let hudTimer = null;
function showHUD(iconName, text, progress) {
  const hud = $('#hud');
  if (!hud) return;
  const hasBar = typeof progress === 'number';
  hud.className = 'hud' + (hasBar ? ' has-bar' : '');
  hud.innerHTML =
    '<span class="hud-icon">' + ic(iconName, 24, 1.9) + '</span>' +
    '<span class="hud-text">' + esc(text) + '</span>' +
    (hasBar ? '<span class="hud-bar"><i style="width:' + Math.round(clamp(progress, 0, 1) * 100) + '%"></i></span>' : '');
  requestAnimationFrame(() => hud.classList.add('show'));
  clearTimeout(hudTimer);
  hudTimer = setTimeout(() => hud.classList.remove('show'), 1500);
}

/* ── 弹层 ─────────────────────────────────────────────────── */

function openDialog(kind) {
  state.dialog = kind;
  if (state.player.open && !state.player.locked) {
    state.player.osd = false;
    syncOSD();
  }
  renderDialog();
  setTarget((state.player.open ? '播放器 · ' : '主界面 · ') +
    ({ playlist: '播放列表', subs: '字幕选择', audio: '音轨选择', folder: '选择文件夹' }[kind] || ''));
  renderDock();
}

function closeDialog() {
  state.dialog = null;
  const layer = $('#dialogLayer');
  if (layer) {
    layer.classList.remove('on');
    setTimeout(() => { layer.remove(); }, 160);
  }
  if (state.player.open && !state.player.locked) pokeOSD();
  renderDock();
}

function renderDialog() {
  let layer = $('#dialogLayer');
  if (!layer) {
    $('#screen').insertAdjacentHTML('beforeend', '<div class="dialog-layer" id="dialogLayer"></div>');
    layer = $('#dialogLayer');
  }
  layer.innerHTML = '<div class="dialog">' + dialogHTML() + '</div>';
  layer.onclick = (e) => { if (e.target === layer) closeDialog(); };
  layer.classList.add('on');

  layer.querySelectorAll('[data-row]').forEach((row) => {
    row.onclick = () => {
      const kind = state.dialog;
      const i = parseInt(row.dataset.row, 10);
      if (kind === 'playlist') {
        const v = VIDEOS[i];
        Object.assign(state.player, { index: i, dur: v.dur, pos: 0 });
        updateTitle(); updateSeekUI();
        closeDialog(); showHUD('playlist', '切换到 ' + v.title); pokeOSD();
      } else if (kind === 'subs') {
        state.player.sub = i === 0 ? -1 : i;
        const btn = $('#btnSub');
        if (btn) btn.innerHTML = ic(state.player.sub > 0 ? 'subtitles' : 'subsOff', 19, 1.9);
        closeDialog();
        showHUD(state.player.sub > 0 ? 'subtitles' : 'subsOff',
          state.player.sub > 0 ? '字幕 · ' + SUBS[i].title : '字幕已关闭');
        pokeOSD();
      } else if (kind === 'audio') {
        state.player.audio = i;
        closeDialog(); showHUD('audio', '音轨 · ' + AUDIOS[i].title); pokeOSD();
      } else if (kind === 'folder') {
        closeDialog(); toast('已选择文件夹（原型演示）');
      }
    };
  });

  const closeBtn = $('#dialogClose');
  if (closeBtn) closeBtn.onclick = closeDialog;
  const doneBtn = $('#dialogDone');
  if (doneBtn) doneBtn.onclick = closeDialog;
  const addBtn = $('#dialogAdd');
  if (addBtn) addBtn.onclick = () => { closeDialog(); toast('已添加媒体文件夹（原型演示）'); };
}

function dialogHTML() {
  const k = state.dialog;
  const head = (title, sub) =>
    '<div class="dialog-head"><h2>' + title + '</h2>' +
    '<button class="close" id="dialogClose">' + ic('close', 18, 1.8) + '</button></div>' +
    (sub ? '<div class="dialog-sub">' + sub + '</div>' : '');

  const row = (i, o) =>
    '<div class="cell' + (o.active ? ' focused' : '') + '" data-row="' + i + '">' +
      '<span class="tile">' + ic(o.icon, 20, 1.8) + '</span>' +
      '<span class="body"><span class="title">' + esc(o.title) + '</span>' +
      (o.sub ? '<span class="sub">' + esc(o.sub) + '</span>' : '') + '</span>' +
      (o.active ? '<span class="badge">使用中</span>' : '') +
    '</div>';

  if (k === 'playlist') {
    return head('播放列表', '共 ' + VIDEOS.length + ' 项 · 当前第 ' + (state.player.index + 1) + ' 项') +
      '<div class="dialog-list">' + section('队列') +
      VIDEOS.map((v, i) => row(i, {
        icon: 'video', title: v.title, sub: v.sub,
        active: i === state.player.index,
      })).join('') + '</div>' +
      '<div class="dialog-foot"><button id="dialogDone">关闭</button></div>';
  }
  if (k === 'subs') {
    return head('字幕', '选择字幕轨道 · 当前 ' + (state.player.sub > 0 ? SUBS[state.player.sub].title : '关闭')) +
      '<div class="dialog-list">' + section('可用轨道') +
      SUBS.map((s, i) => row(i, {
        icon: s.icon, title: s.title, sub: s.sub,
        active: (i === 0 && state.player.sub <= 0) || i === state.player.sub,
      })).join('') + '</div>' +
      '<div class="dialog-foot"><button id="dialogDone">关闭</button></div>';
  }
  if (k === 'audio') {
    return head('音轨', '选择音频轨道') +
      '<div class="dialog-list">' + section('可用轨道') +
      AUDIOS.map((a, i) => row(i, {
        icon: 'audio', title: a.title, sub: a.sub, active: i === state.player.audio,
      })).join('') + '</div>' +
      '<div class="dialog-foot"><button id="dialogDone">关闭</button></div>';
  }
  return head('选择媒体文件夹', '原型演示：实机会打开系统文件选择器') +
    '<div class="dialog-list">' + section('常用位置') +
    row(0, { icon: 'folder', title: 'Videos', sub: '/home/deck/Videos', active: false }) +
    row(1, { icon: 'folder', title: 'Movies', sub: '/home/deck/Movies', active: false }) +
    row(2, { icon: 'drive', title: 'SD 卡', sub: '/run/media/deck/SD256/Videos', active: false }) +
    '</div>' +
    '<div class="dialog-foot"><button id="dialogDone">取消</button>' +
    '<button class="primary" id="dialogAdd">添加此文件夹</button></div>';
}

/* ── 插件管理页 ───────────────────────────────────────────── */

function openPlugins() {
  closePlayer();
  state.pluginPage = true;
  const host = $('#screen');
  const old = $('#pluginPage');
  if (old) old.remove();

  host.insertAdjacentHTML('beforeend',
    '<div class="page on" id="pluginPage">' +
      '<header class="app-header">' +
        '<button class="back" id="pageBack">' + ic('back', 19, 2) + '</button>' +
        '<h1>插件管理</h1>' +
        '<span class="ver">' + ic('plugin', 15, 1.8) + 'API v1</span>' +
      '</header>' +
      '<section class="main"><div class="list" id="pluginList">' +
        section('已安装 · 2') +
        cell({ icon: 'plugin', title: '示例视频源', sub: 'v1.0.0 · 视频源 · 含刮削器 · 已启用', badge: '配置', chevron: true, action: 'plug-cfg' }) +
        cell({ icon: 'plugin', title: 'TMDB 刮削', sub: 'v0.4.1 · 刮削器 · 已启用', badge: '配置', chevron: true, action: 'plug-cfg' }) +
        section('操作') +
        cell({ icon: 'plus', title: '从 ZIP 安装', sub: '选择本地插件包，校验后解压到插件目录', action: 'plug-zip' }) +
        cell({ icon: 'folder', title: '打开插件目录', sub: '~/.local/share/deck-player/plugins', chevron: true, action: 'plug-dir' }) +
        section('兼容性') +
        cell({ icon: 'info', title: '插件 API 版本', sub: 'v1 · 支持 2026.10 之后构建的插件', action: 'plug-info' }) +
      '</div></section>' +
      '<footer class="app-footer"><span class="clock">' + ic('clock', 18, 1.7) + clock(true) + '</span>' +
        '<span class="hints"><span class="hint"><span class="k a">A</span>打开</span>' +
        '<span class="hint"><span class="k">B</span>返回</span></span></footer>' +
    '</div>');

  $('#pageBack').onclick = closePlugins;
  const firstCell = $('#pluginList .cell');
  if (firstCell) firstCell.classList.add('focused');
  $('#pluginList').onclick = (e) => {
    const c = e.target.closest('.cell');
    if (!c) return;
    if (c.dataset.action === 'plug-zip') toast('选择插件 ZIP（原型未接入文件选择器）');
    else if (c.dataset.action === 'plug-dir') toast('打开 ~/.local/share/deck-player/plugins');
    else if (c.dataset.action === 'plug-cfg') toast('打开插件配置对话框');
    else toast('插件 API v1 · 与 v1.0 以上兼容');
  };
  setTarget('插件管理');
  renderDock();
}

function closePlugins() {
  state.pluginPage = false;
  const p = $('#pluginPage');
  if (p) p.remove();
  setTarget('整体 · ' + TAB_LABEL[state.tab]);
  renderDock();
}

/* ── 评审台 ───────────────────────────────────────────────── */

function setTarget(t) {
  state.composerTarget = t;
  const el = $('#composerTarget');
  if (el) el.textContent = t;
}

function renderDock() {
  const active = state.dialog ? state.dialog
    : (state.player.open ? 'player' : (state.pluginPage ? 'plugins' : state.tab));

  const scenarios = [
    { k: 'library', label: '视频库', icon: 'video' },
    { k: 'sources', label: '存储位置', icon: 'folder' },
    { k: 'audio', label: '音乐库', icon: 'music' },
    { k: 'history', label: '播放历史', icon: 'clock' },
    { k: 'player', label: '播放器 OSD', icon: 'play' },
    { k: 'playlist', label: '播放列表', icon: 'playlist' },
    { k: 'subs', label: '字幕选择', icon: 'subtitles' },
    { k: 'plugins', label: '插件管理', icon: 'plugin' },
  ];
  const box = $('#scenarios');
  box.innerHTML = scenarios.map((s) =>
    '<button class="scenario' + (active === s.k ? ' on' : '') + '" data-go="' + s.k + '">' +
    ic(s.icon, 15, 1.8) + '<span>' + s.label + '</span></button>').join('');
  box.onclick = (e) => {
    const b = e.target.closest('[data-go]');
    if (b) goScenario(b.dataset.go);
  };

  renderRisks();
  renderComments();
  renderSpec();
  updateCounts();
}

function goScenario(k) {
  if (window.innerWidth <= 860) openDock(false);
  if (k === 'player') { openPlayer(state.player.open ? state.player.index : 0); return; }
  if (k === 'playlist' || k === 'subs') {
    if (!state.player.open) openPlayer(state.player.index || 0);
    openDialog(k === 'subs' ? 'subs' : 'playlist');
    return;
  }
  if (k === 'plugins') { openPlugins(); return; }
  closePlugins();
  if (state.player.open) closePlayer();
  if (state.dialog) closeDialog();
  switchTab(k);
}

function renderRisks() {
  const box = $('#risks');
  box.innerHTML = RISKS.map((r, i) =>
    '<div class="risk-item"><span class="tag">' + r.tag + '</span>' +
    '<span><b style="color:#C9D0DA;font-weight:500">' + esc(r.name) + '</b><br>' + esc(r.text) + '</span>' +
    '<button data-risk="' + i + '" title="记入需求单">＋</button></div>').join('');
  box.onclick = (e) => {
    const b = e.target.closest('[data-risk]');
    if (!b) return;
    const r = RISKS[parseInt(b.dataset.risk, 10)];
    setTarget('待确认 · ' + r.name);
    const ta = $('#commentInput');
    ta.value = r.text;
    ta.focus();
    if (window.innerWidth <= 860) openDock(true);
    toast('已填入需求单，可修改后保存');
  };
}

function renderComments() {
  const box = $('#comments');
  const list = state.comments.slice().reverse();
  box.innerHTML = list.length ? list.map((c) =>
    '<div class="comment"><div class="meta"><b>' + esc(c.target) + '</b><span>' + esc(c.time) + '</span>' +
    '<button data-del="' + c.id + '" title="删除">×</button></div>' +
    '<p>' + esc(c.text) + '</p></div>').join('')
    : '<p class="dock-note" style="margin:0">还没有需求。点任意元素，或从「待确认」里挑一条。</p>';

  box.onclick = (e) => {
    const b = e.target.closest('[data-del]');
    if (!b) return;
    state.comments = state.comments.filter((c) => String(c.id) !== b.dataset.del);
    saveComments(); renderComments(); updateCounts();
  };
}

function updateCounts() {
  const n = state.comments.length;
  ['#count', '#dockCount'].forEach((sel) => {
    const el = $(sel);
    if (!el) return;
    el.textContent = n;
    el.classList.toggle('has', n > 0);
  });
}

function currentScreen() {
  if (state.player.open) return state.dialog ? '播放器 · 对话框' : '播放器 OSD';
  if (state.pluginPage) return '插件管理';
  if (state.dialog) return '主界面 · 对话框';
  return TAB_LABEL[state.tab];
}

function addComment() {
  const ta = $('#commentInput');
  const text = ta.value.trim();
  if (!text) { ta.focus(); return; }
  state.comments.push({
    id: Date.now() + '-' + Math.random().toString(36).slice(2, 6),
    target: state.composerTarget,
    screen: currentScreen(),
    text: text,
    time: new Date().toLocaleString('zh-CN', { month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' }),
  });
  saveComments();
  ta.value = '';
  renderComments();
  updateCounts();
  toast('已记入需求单');
}

function exportMd() {
  const d = DEVICES[state.device];
  const lines = [
    '# DeckPlayer UI 评审',
    '',
    '- 设备：' + d.name + '（逻辑 1280×' + d.h + ' · windowScale ' + (d.pw / 1280).toFixed(2) + '）',
    '- 主题：' + (state.theme === 'oled' ? 'OLED 真黑' : '深色'),
    '- 时间：' + new Date().toLocaleString('zh-CN'),
    '- 条目：' + state.comments.length,
    '',
  ];
  const groups = {};
  state.comments.forEach((c) => {
    const key = c.screen || c.target.split(' · ')[0];
    (groups[key] = groups[key] || []).push(c);
  });
  Object.keys(groups).forEach((g) => {
    lines.push('## ' + g, '');
    groups[g].forEach((c, i) => {
      lines.push((i + 1) + '. **' + c.target + '** — ' + c.text.replace(/\n/g, '\n   '));
    });
    lines.push('');
  });
  const md = lines.join('\n');

  const done = () => {
    try {
      const blob = new Blob([md], { type: 'text/markdown;charset=utf-8' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = 'deckplayer-ui-review.md';
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 1000);
      toast('已复制并下载 Markdown');
    } catch (e) { toast('已复制到剪贴板'); }
  };
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(md).then(done).catch(done);
  } else { done(); }
}

/* ── 标注 ─────────────────────────────────────────────────── */

function toggleAnnotate(force) {
  state.annotate = typeof force === 'boolean' ? force : !state.annotate;
  const btn = $('#annotateBtn');
  btn.classList.toggle('on', state.annotate);
  $('#screen').classList.toggle('annotating', state.annotate);
  if (!state.annotate) clearSelection();
  decorate();
  renderSpec();
  toast(state.annotate ? '标注模式：点任意元素查看规格' : '标注模式已关闭');
}

function decorate() {
  $$('.risk-dot', $('#screen')).forEach((d) => d.remove());
  if (!state.annotate) return;
  $$('#screen [data-risk]').forEach((el) => {
    if (el.querySelector(':scope > .risk-dot')) return;
    if (getComputedStyle(el).position === 'static') el.style.position = 'relative';
    const b = document.createElement('b');
    b.className = 'risk-dot';
    b.textContent = '!';
    el.appendChild(b);
  });
}

function clearSelection() {
  state.selected = null;
  $$('.selected', $('#screen')).forEach((el) => el.classList.remove('selected'));
}

function selectElement(el) {
  clearSelection();
  state.selected = el;
  el.classList.add('selected');
  const name = el.dataset.name || '元素';
  setTarget('界面 · ' + name);
  renderSpec();
  renderDock();
}

function renderSpec() {
  const box = $('#specBox');
  if (!box) return;
  if (!state.selected) {
    box.className = 'spec-empty';
    box.textContent = '打开「标注」后，点任意卡片 / 按钮，这里显示它的实际尺寸与出处。';
    return;
  }
  const el = state.selected;
  const w = el.offsetWidth;
  const h = el.offsetHeight;
  const x = el.offsetLeft;
  const y = el.offsetTop;
  const name = el.dataset.name || '元素';
  const spec = el.dataset.spec || '—';
  const risk = el.dataset.risk || '';
  const path = el.tagName.toLowerCase() +
    (el.id ? '#' + el.id : '') +
    (el.classList.length ? '.' + Array.from(el.classList).slice(0, 2).join('.') : '');

  box.className = 'spec-card';
  box.innerHTML =
    '<div class="name">' + esc(name) + '</div>' +
    '<div class="path">' + esc(path) + '</div>' +
    '<dl><dt>实际尺寸</dt><dd>' + w + ' × ' + h + ' 逻辑 px</dd>' +
    '<dt>位置</dt><dd>x ' + x + ' · y ' + y + '</dd>' +
    '<dt>规格</dt><dd>' + esc(spec) + '</dd></dl>' +
    (risk ? '<div class="risk"><b>待确认</b>' + esc(risk) + '</div>' : '') +
    '<button class="use" id="specUse">针对这里提需求</button>';

  $('#specUse').onclick = () => {
    setTarget('界面 · ' + name);
    const ta = $('#commentInput');
    ta.focus();
    if (window.innerWidth <= 860) openDock(true);
    toast('已选择：' + name);
  };
}

/* 标注模式下的拾取：捕获阶段拦截，避免触发真实操作 */
document.addEventListener('click', (e) => {
  if (!state.annotate) return;
  const el = e.target.closest('#screen [data-spec]');
  if (!el) return;
  e.preventDefault();
  e.stopPropagation();
  selectElement(el);
}, true);

document.addEventListener('mouseover', (e) => {
  if (!state.annotate) return;
  const el = e.target.closest('#screen [data-spec]');
  const tip = $('#tooltip');
  if (!el) { tip.hidden = true; return; }
  const name = el.dataset.name || '元素';
  tip.innerHTML = '<b>' + esc(name) + '</b><em>' + el.offsetWidth + '×' + el.offsetHeight +
    '</em> · ' + esc(el.dataset.spec || '');
  tip.hidden = false;
  const r = e.clientX + 14;
  const t = e.clientY + 16;
  tip.style.left = Math.min(r, window.innerWidth - tip.offsetWidth - 12) + 'px';
  tip.style.top = Math.min(t, window.innerHeight - tip.offsetHeight - 12) + 'px';
}, true);

document.addEventListener('mouseout', (e) => {
  if (!state.annotate) return;
  if (!e.relatedTarget || !e.relatedTarget.closest || !e.relatedTarget.closest('#screen [data-spec]')) {
    $('#tooltip').hidden = true;
  }
}, true);

/* ── 设备 / 主题 / 面板 ───────────────────────────────────── */

function buildDeviceSeg() {
  const seg = $('#deviceSeg');
  seg.innerHTML = Object.keys(DEVICES).map((k) =>
    '<button data-dev="' + k + '"' + (k === state.device ? ' class="on"' : '') + '>' +
    DEVICES[k].label + '</button>').join('');
  seg.onclick = (e) => {
    const b = e.target.closest('[data-dev]');
    if (!b) return;
    setDevice(b.dataset.dev);
  };
}

function setDevice(key) {
  if (state.device === key) return;
  state.device = key;
  buildDeviceSeg();
  applyDevice();
  toast(DEVICES[key].name + ' · 逻辑 1280×' + DEVICES[key].h);
}

function applyDevice() {
  const d = DEVICES[state.device];
  const screen = $('#screen');
  screen.style.width = d.w + 'px';
  screen.style.height = d.h + 'px';
  if (state.dialog) closeDialog();
  if (state.player.open) buildPlayer();
  fit();
  updateStageMeta();
  applyFocus();
  decorate();
}

function setZoom(z) {
  state.zoom = z;
  $$('#stageZoom button').forEach((b) => b.classList.toggle('on', b.dataset.zoom === z));
  const stage = $('#stage');
  stage.classList.toggle('zoomed', z === 'actual');
  if (z === 'actual') {
    stage.scrollTo({ top: 0, left: 0 });
    toast('1:1 逻辑像素 · 可左右拖动查看细节');
  }
  fit();
}

function fit() {
  const stage = $('#stage');
  const d = DEVICES[state.device];
  const screen = $('#screen');
  const dev = $('#device');

  // 1:1：按逻辑像素真实大小渲染，舞台滚动查看细节。
  // 1280 宽的界面在手机上只能整体缩到 0.3，字号会小到不可读。
  if (state.zoom === 'actual') {
    screen.style.transform = 'scale(1)';
    dev.style.width = (d.w + 22) + 'px';
    dev.style.height = (d.h + 22) + 'px';
    state.scale = 1;
    return;
  }

  const padX = window.innerWidth <= 860 ? 16 : 56;
  const padY = 78;
  const s = Math.min(
    (stage.clientWidth - padX) / d.w,
    (stage.clientHeight - padY) / d.h,
    1.18
  );
  const scale = Math.max(0.1, s);
  screen.style.transform = 'scale(' + scale + ')';
  dev.style.width = (d.w * scale + 22) + 'px';
  dev.style.height = (d.h * scale + 22) + 'px';
  state.scale = scale;
}

function updateStageMeta() {
  const d = DEVICES[state.device];
  const ws = (d.pw / 1280).toFixed(2);
  const minFont = (12.5 * d.pw / 1280).toFixed(2);
  const touch = (44 * d.pw / 1280).toFixed(0);
  const warn = d.pw < 1280;
  const portrait = window.innerWidth <= 860 && window.innerHeight > window.innerWidth;
  $('#stageMeta').innerHTML =
    '<span class="chip">逻辑 <b>1280 × ' + d.h + '</b></span>' +
    '<span class="chip">物理 <b>' + d.pw + ' × ' + d.ph + '</b> · windowScale <b>' + ws + '</b></span>' +
    '<span class="chip' + (warn ? ' warn' : '') + '">最小字号 12.5 → <b>' + minFont + ' px</b></span>' +
    '<span class="chip' + (warn ? ' warn' : '') + '">触控 44 → <b>' + touch + ' px</b></span>' +
    (portrait ? '<span class="chip warn">竖屏看不清细节 · 建议横屏或切 <b>1:1</b></span>' : '');
}

function setTheme(t) {
  state.theme = t;
  $('#screen').dataset.theme = t;
  $('#themeLabel').textContent = t === 'oled' ? 'OLED' : '深色';
  $('#themeDot').classList.toggle('oled', t === 'oled');
  toast(t === 'oled' ? 'OLED 真黑：背景纯黑，表面只提 2–3 个灰阶' : '标准深色主题');
}

function applyTheme() {
  $('#screen').dataset.theme = state.theme;
  $('#themeLabel').textContent = state.theme === 'oled' ? 'OLED' : '深色';
  $('#themeDot').classList.toggle('oled', state.theme === 'oled');
}

function openDock(open) {
  const wb = $('#workbench');
  const isOpen = !wb.classList.contains('dock-closed');
  const next = typeof open === 'boolean' ? open : !isOpen;
  wb.classList.toggle('dock-closed', !next);
  if (next) {
    const body = $('.dock-body');
    if (body) body.scrollTop = 0;
  }
  setTimeout(fit, 240);
}

/* ── 键盘 ─────────────────────────────────────────────────── */

function onKey(e) {
  const tag = (e.target.tagName || '').toLowerCase();
  if (tag === 'textarea' || tag === 'input') {
    if (e.key === 'Escape') e.target.blur();
    return;
  }

  const k = e.key;

  if (k === 'Escape') {
    if (state.dialog) { closeDialog(); e.preventDefault(); return; }
    if (state.pluginPage) { closePlugins(); return; }
    if (state.player.open) { closePlayer(); return; }
  }

  // 弹层打开时，播放快捷键与焦点导航都暂停，避免「看不见的控件被操作」
  if (state.dialog) return;

  // 目录浏览里按 Esc / B 回上一级
  if (k === 'Escape' && state.browse) { e.preventDefault(); goUp(); return; }

  if (state.player.open) {
    if (k === ' ' || k === 'Enter') { e.preventDefault(); togglePlay(); return; }
    if (k === 'ArrowLeft') { e.preventDefault(); seekBy(-10); return; }
    if (k === 'ArrowRight') { e.preventDefault(); seekBy(10); return; }
    if (k === 'ArrowUp') { e.preventDefault(); onWheel({ preventDefault() {}, deltaY: -1, shiftKey: e.shiftKey }); return; }
    if (k === 'ArrowDown') { e.preventDefault(); onWheel({ preventDefault() {}, deltaY: 1, shiftKey: e.shiftKey }); return; }
    if (k === 'o' || k === 'O') {
      if (!state.player.locked) { state.player.osd = !state.player.osd; syncOSD(); if (state.player.osd) pokeOSD(); }
      return;
    }
    if (k === 'l' || k === 'L') { toggleLock(); return; }
    if (k === 'p' || k === 'P') { openDialog('playlist'); return; }
    if (k === 's' || k === 'S') { openDialog('subs'); return; }
    if (k === 'a' || k === 'A') { openDialog('audio'); return; }
  }

  if (state.pluginPage) {
    if (k === 'ArrowDown' || k === 'ArrowUp') { movePluginFocus(k === 'ArrowDown' ? 1 : -1); e.preventDefault(); }
    if (k === 'Enter') { const f = $('.page .cell.focused'); if (f) f.click(); }
    return;
  }

  if (k === 'ArrowDown') { e.preventDefault(); moveFocus(1); return; }
  if (k === 'ArrowUp') { e.preventDefault(); moveFocus(-1); return; }
  if (k === 'ArrowRight') {
    e.preventDefault();
    if (state.focus.zone === 'sidebar') { state.focus = { zone: 'main', i: 0 }; applyFocus(); }
    return;
  }
  if (k === 'ArrowLeft') {
    e.preventDefault();
    if (state.focus.zone === 'main') {
      state.focus = { zone: 'sidebar', i: TABS.findIndex((t) => t.key === state.tab) };
      applyFocus();
    }
    return;
  }
  if (k === 'Enter') { e.preventDefault(); activateFocused(); return; }
  if (k === 'n' || k === 'N') { toggleAnnotate(); return; }
  if (k === 'd' || k === 'D') { openDock(); return; }
}

function movePluginFocus(d) {
  const items = $$('.page .cell');
  if (!items.length) return;
  const cur = items.findIndex((el) => el.classList.contains('focused'));
  items.forEach((el) => el.classList.remove('focused'));
  const i = clamp((cur < 0 ? 0 : cur) + d, 0, items.length - 1);
  items[i].classList.add('focused');
  items[i].scrollIntoView({ block: 'nearest' });
}

/* ── 初始化 ───────────────────────────────────────────────── */

function init() {
  buildDeviceSeg();
  applyTheme();

  $('#screen').innerHTML = '<div class="app" id="app"></div>';
  renderApp();
  applyDevice();
  renderDock();
  if (window.innerWidth <= 860) $('#workbench').classList.add('dock-closed');

  $('#themeBtn').onclick = () => setTheme(state.theme === 'dark' ? 'oled' : 'dark');
  $('#annotateBtn').onclick = () => toggleAnnotate();
  $('#dockBtn').onclick = () => openDock();
  $('#dockClose').onclick = () => openDock(false);

  $('#stageZoom').onclick = (e) => {
    const b = e.target.closest('[data-zoom]');
    if (b) setZoom(b.dataset.zoom);
  };
  $('#addComment').onclick = addComment;
  $('#exportMd').onclick = exportMd;
  $('#clearComments').onclick = () => {
    if (!state.comments.length) return;
    if (confirm('清空全部 ' + state.comments.length + ' 条评审意见？')) {
      state.comments = []; saveComments(); renderComments(); updateCounts();
    }
  };
  $('#commentInput').addEventListener('keydown', (e) => {
    if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') addComment();
  });

  $('#sceneSeg').onclick = (e) => {
    const b = e.target.closest('[data-scene]');
    if (!b) return;
    state.scene = b.dataset.scene;
    $$('#sceneSeg button').forEach((x) => x.classList.toggle('on', x === b));
    if (!state.player.open) openPlayer(state.player.index || 0);
    const sc = $('#scene');
    if (sc) sc.className = 'scene ' + state.scene;
    toast('画面：' + ({ dark: '暗场', bright: '亮场', bars: '彩条' }[state.scene]));
  };

  document.addEventListener('keydown', onKey);

  let raf = null;
  const onResize = () => {
    if (raf) return;
    raf = requestAnimationFrame(() => { raf = null; fit(); updateStageMeta(); });
  };
  window.addEventListener('resize', onResize);
  if (window.ResizeObserver) new ResizeObserver(onResize).observe($('#stage'));

  // 时钟每秒走一格；播放器进度每 200ms 推一次
  let last = performance.now();
  setInterval(() => {
    const now = performance.now();
    const dt = Math.min(1, (now - last) / 1000);
    last = now;

    const c = $('#appClock');
    if (c) c.textContent = clock(true);

    if (state.player.open && state.player.playing && !state.seekDrag && !state.dialog) {
      const p = state.player;
      p.pos = Math.min(p.dur, p.pos + dt * p.speed);
      if (p.pos >= p.dur) { p.playing = false; updatePlayIcon(); }
      updateSeekUI();
    }

    if (state.np.show && state.np.playing) {
      state.np.pos = (state.np.pos + dt) % state.np.dur;
      const bar = $('.np .line i');
      if (bar) bar.style.width = (state.np.pos / state.np.dur * 100).toFixed(1) + '%';
      const t = $('.np .time');
      if (t) t.textContent = fmt(state.np.pos) + ' / ' + fmt(state.np.dur);
    }
  }, 200);
}

init();

})();
