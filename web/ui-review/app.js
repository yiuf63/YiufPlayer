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
  listLines: '<path d="M4 7h16M4 12h16M4 17h11"/>',
  grid2:     '<rect x="3.8" y="3.8" width="7" height="7" rx="1.6"/><rect x="13.2" y="3.8" width="7" height="7" rx="1.6"/><rect x="3.8" y="13.2" width="7" height="7" rx="1.6"/><rect x="13.2" y="13.2" width="7" height="7" rx="1.6"/>',
  settings:  '<circle cx="12" cy="12" r="3.1"/><path d="M12 3.4v2.2M12 18.4v2.2M3.4 12h2.2M18.4 12h2.2M6.2 6.2l1.6 1.6M16.2 16.2l1.6 1.6M17.8 6.2l-1.6 1.6M7.8 16.2l-1.6 1.6"/>',
  file:      '<path d="M6.4 3.6h7.2l4.6 4.6v11.4a.8.8 0 0 1-.8.8H6.4a.8.8 0 0 1-.8-.8V4.4a.8.8 0 0 1 .8-.8Z"/><path d="M13.4 3.8v4.8h4.6"/>',
  filter:    '<path d="M3.6 6.4h16.8L14 13.2v5.6l-4 2v-7.6Z"/>',
  more:      '<circle cx="5.6" cy="12" r="1.6" fill="currentColor" stroke="none"/><circle cx="12" cy="12" r="1.6" fill="currentColor" stroke="none"/><circle cx="18.4" cy="12" r="1.6" fill="currentColor" stroke="none"/>',
  cloud:     '<path d="M7.4 18.4a3.9 3.9 0 0 1-.4-7.8 5.2 5.2 0 0 1 10-1.4 3.8 3.8 0 0 1 3 5.4"/>',
  check2:    '<circle cx="12" cy="12" r="8.4"/><path d="M8.2 12.2 11 15l4.8-5.4"/>',
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
    badge: '已看 33%', progress: 0.33, dur: 206, pos: 68, meta: '640×360 · 软件解码', hw: false, tone: 't0',
    folder: '/home/deck/Videos', size: 0.4, added: 1 },
  { title: '02. Blade Runner 2049', file: '02. Blade Runner 2049.mp4', sub: '52.3 KB · 含字幕',
    badge: '已看 64%', progress: 0.64, dur: 955, pos: 611, meta: '1920×1080 · 硬件解码', hw: true, tone: 't1',
    folder: '/home/deck/Videos', size: 52.3, added: 2 },
  { title: '03. 大鱼海棠', file: '03. 大鱼海棠.mkv', sub: '162.5 KB · 含字幕',
    badge: '已看完', progress: 1, dur: 5460, pos: 5460, meta: '1920×804 · 硬件解码', hw: true, tone: 't2',
    folder: '/home/deck/Videos', size: 162.5, added: 3 },
  { title: '04. 你的名字', file: '04. 你的名字.mp4', sub: '208.1 KB',
    badge: '', progress: -1, dur: 6360, pos: 0, meta: '1920×1080 · 硬件解码', hw: true, tone: 't3',
    folder: '/home/deck/Videos', size: 208.1, added: 12 },
  { title: '05. 千与千寻', file: '05. 千与千寻.mkv', sub: '2.4 GB · 含字幕',
    badge: '', progress: -1, dur: 7380, pos: 0, meta: '1920×1038 · 硬件解码', hw: true, tone: 't4',
    folder: '/home/deck/Videos', size: 2458, added: 15 },
  { title: '06. 攻壳机动队', file: '06. 攻壳机动队.mp4', sub: '1.8 GB · 含字幕',
    badge: '已看 12%', progress: 0.12, dur: 4980, pos: 598, meta: '1920×1080 · 硬件解码', hw: true, tone: 't7',
    folder: '/home/deck/Videos', size: 1843, added: 21 },
  { title: '07. 蓝色巨星', file: '07. 蓝色巨星.mkv', sub: '3.1 GB',
    badge: '', progress: -1, dur: 7200, pos: 0, meta: '1920×816 · 硬件解码', hw: true, tone: 't5',
    folder: '/run/media/deck/SD256/Anime', size: 3174, added: 30 },
  { title: '08. 沙丘', file: '08. 沙丘.mp4', sub: '4.7 GB · 含字幕',
    badge: '已看完', progress: 1, dur: 9300, pos: 9300, meta: '3840×1600 · 硬件解码', hw: true, tone: 't6',
    folder: '/run/media/deck/SD256/Anime', size: 4813, added: 33 },
  { title: '09. 星际穿越', file: '09. 星际穿越.mkv', sub: '5.2 GB · 含字幕',
    badge: '已看 78%', progress: 0.78, dur: 10140, pos: 7909, meta: '1920×800 · 硬件解码', hw: true, tone: 't9',
    folder: '/run/media/deck/SD256/Live', size: 5325, added: 40 },
  { title: '10. 蜘蛛侠：平行宇宙', file: '10. 蜘蛛侠：平行宇宙.mp4', sub: '2.9 GB',
    badge: '', progress: -1, dur: 7020, pos: 0, meta: '1920×804 · 硬件解码', hw: true, tone: 't8',
    folder: '/run/media/deck/SD256/Live', size: 2970, added: 45 },
];

/* 音乐条目同样带 folder / size / added：
   媒体库现在要按文件夹聚合，缺这两个字段就没法算「这个文件夹里有多少、多大」。 */
const MUSIC = [
  { title: '01. 夜に駆ける', sub: 'YOASOBI · 3:21 · MP3 320k', badge: '3:21', tone: 'm0',
    dur: 201, folder: '/home/deck/Music', size: 6.2, added: 4 },
  { title: '02. Interstellar Theme', sub: 'Hans Zimmer · 4:20 · FLAC', badge: '4:20', tone: 'm1',
    dur: 260, folder: '/home/deck/Music', size: 38, added: 9 },
  { title: '03. 花の唄', sub: 'Aimer · 3:10 · MP3 320k', badge: '3:10', tone: 'm2',
    dur: 190, folder: '/home/deck/Music', size: 5.9, added: 18 },
  { title: '04. 千本桜 (Live)', sub: '初音ミク · 4:02 · FLAC', badge: '4:02', tone: 'm0',
    dur: 242, folder: '/run/media/deck/SD256/Live', size: 41, added: 41 },
  { title: '05. 残酷な天使のテーゼ', sub: '高橋洋子 · 4:05 · MP3 320k', badge: '4:05', tone: 'm1',
    dur: 245, folder: '/run/media/deck/SD256/Live', size: 9.4, added: 42 },
  { title: '06. 打上花火', sub: 'DAOKO × 米津玄師 · 4:40 · MP3 320k', badge: '4:40', tone: 'm2',
    dur: 280, folder: '/run/media/deck/SD256/Live', size: 10.7, added: 44 },
];

/* 文件夹：媒体库「按文件夹」分组的依据。
   实机来自 MediaFolder 表（扫描时写入），这里手写一份以便演示混合内容 ——
   Live 目录里既有视频又有音乐，正是「全放一起会乱」的现场。 */
const FOLDERS = [
  { path: '/home/deck/Videos', name: '影视收藏', drive: '内置存储' },
  { path: '/home/deck/Music', name: '音乐收藏', drive: '内置存储' },
  { path: '/run/media/deck/SD256/Anime', name: '动画 · SD 卡', drive: 'SD 卡' },
  { path: '/run/media/deck/SD256/Live', name: '演唱会 · SD 卡', drive: 'SD 卡' },
];

const FOLDER_BY_PATH = FOLDERS.reduce((m, f) => { m[f.path] = f; return m; }, {});

/* 媒体库的统一条目：视频与音乐进同一个池子，靠 kind 区分。
   没有这一层，按文件夹聚合就只能分别算两遍再拼，数量永远对不上。 */
const MEDIA = VIDEOS
  .map((v, i) => Object.assign({}, v, {
    idx: i, kind: 'video', search: (v.title + ' ' + v.file).toLowerCase(),
  }))
  .concat(MUSIC.map((m, i) => Object.assign({}, m, {
    idx: i, kind: 'audio', search: m.title.toLowerCase(),
  })));


const HISTORY = [
  { title: '01. 漂流 冒険の島!', sub: '已播 0:02 / 3:26', badge: '刚刚', progress: 0.01, idx: 0 },
  { title: '02. Blade Runner 2049', sub: '已播 10:11 / 15:55', badge: '今天', progress: 0.64, idx: 1 },
  { title: '03. 大鱼海棠', sub: '已看完 · 1:31:00', badge: '昨天', progress: 1, idx: 2 },
  { title: '04. 你的名字', sub: '未开始', badge: '3 天前', progress: -1, idx: 3 },
];

/* 字幕轨道。
   实机轨道来自 mpv 的 track-list：内嵌轨有 id，外挂轨带 "external" 标记。
   loadExternalSubtitle() 走 `sub-add <path> select`，会新增一条 external 轨，
   不会替换已有轨道 —— 所以「已加载的外挂字幕」可以和内嵌轨并存。 */
const SUBS = [
  { title: '关闭字幕', sub: '不显示任何字幕', icon: 'subsOff', off: true },
  { title: '简体中文', sub: '内嵌 · ASS', icon: 'subtitles' },
  { title: 'English', sub: '内嵌 · SUBRIP', icon: 'subtitles' },
];

/* 字幕文件选择器的候选：当前视频所在目录。
   实机里默认就停在这个目录 —— 同名字幕有 90% 的概率已经在那里了，
   这才是「加载外挂字幕」的正确起点，而不是让用户从 / 开始翻。 */
const SUB_DIR = '/home/deck/Videos';
const SUB_FILES = [
  { name: '02. Blade Runner 2049.chs.ass', sub: 'ASS · 84.2 KB · 与片名匹配', best: true },
  { name: '02. Blade Runner 2049.eng.srt', sub: 'SRT · 61.7 KB · 与片名匹配' },
  { name: 'Blade Runner 2049 (2017).zh-Hans.ass', sub: 'ASS · 92.4 KB' },
  { name: '简体中文.srt', sub: 'SRT · 58.1 KB' },
  { name: 'English.srt', sub: 'SRT · 49.3 KB' },
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
      { name: '05. 千与千寻.mkv', sub: '2.4 GB · 含字幕', v: 4 },
      { name: '06. 攻壳机动队.mp4', sub: '1.8 GB · 含字幕', v: 5 },
      { name: '07. 蓝色巨星.mkv', sub: '3.1 GB', v: 6 },
      { name: '08. 沙丘.mp4', sub: '4.7 GB · 含字幕', v: 7 },
      { name: '09. 星际穿越.mkv', sub: '5.2 GB · 含字幕', v: 8 },
      { name: '10. 蜘蛛侠：平行宇宙.mp4', sub: '2.9 GB', v: 9 },
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

/* 信息架构（2026-11 重排）
 *
 * 旧：存储位置 / 视频库 / 音乐库 / 播放历史
 *   问题一：两个入口都能播视频（存储位置能进目录播，视频库能播），用户不知道去哪。
 *   问题二：「视频库」里塞了「添加媒体文件夹」「重新扫描」—— 这是配置，不是内容。
 *   问题三：「存储位置」里塞了插件视频源和插件管理 —— 插件不是存储位置。
 *   问题四：库一大就没有搜索、筛选、排序，等于不可用。
 *
 * 新：媒体库 / 文件 / 历史 / 设置
 *   按「你在这屏要干什么」切分，而不是按「数据从哪来」切分：
 *     媒体库 → 消费内容（搜索、筛选、按类型看，网格或列表）
 *     文件   → 按位置找东西（存储设备 → 目录树，MX Player 式）
 *     历史   → 接着看
 *     设置   → 一切配置：媒体文件夹、内容源与插件、字幕、外观、播放
 *   配置项全部从内容页撤出，内容页只剩内容。
 */
const TABS = [
  { key: 'media', label: '媒体库', icon: 'video' },
  { key: 'files', label: '文件', icon: 'folder' },
  { key: 'history', label: '历史', icon: 'clock' },
  { key: 'settings', label: '设置', icon: 'settings' },
];

const TAB_LABEL = {
  media: '媒体库', files: '文件', history: '历史', settings: '设置',
  plugins: '插件管理', player: '播放器',
};

/* 媒体库的类型、排序与筛选的选项定义在「工具行与视图菜单」一节，
   和呼出菜单放在一起 —— 选项和它的 UI 分开写，改一项总要翻两个地方。 */

const RISKS = [
  { tag: '小屏', name: '掌机上的物理缩水',
    text: '12.5 字号在 640×480 掌机上只有 6.25 物理像素；44 的触控目标缩到 22px。是否要给最小支持宽度，低于它切换成「大字模式」？' },
  { tag: '触控', name: '设置行的净空',
    text: '6 个 40px 圆钮、间距 12，相邻命中区间净空只剩 4 逻辑像素。字幕和音轨并排，是整屏最容易点错的地方。' },
  { tag: 'OSD', name: '8 秒自动隐藏',
    text: '触屏用户要在 8 秒内从 6 个图标里找到「画面比例」。手柄有焦点导航，触屏只能靠记忆。是否改成「点空白处才收起」？' },
  { tag: '输入', name: '亮度仍只有手势',
    text: 'LT/RT 已经接管音量，但亮度在没有触摸屏的设备上无法调节。要不要把亮度也放进 OSD，或做成「LT/RT + 长按」？' },
  { tag: '缩略图', name: '首帧多半是黑场',
    text: '抽「第一帧」会得到片头黑场或淡入前的纯黑，整面网格会有一半是黑的。实机按 min(时长×0.1, 30s) 抽帧，缓存到 ~/.cache/deck-player/thumbs。' },
  { tag: '主题', name: '浅色只管浏览页',
    text: '浅色主题不作用于播放页 OSD：亮底控件压在亮画面上会直接消失。切浅色时播放器仍然是暗的，这是有意为之，不是漏做。' },
  { tag: '视图', name: '视图设置不记忆',
    text: '分组 / 排序 / 筛选只活在当前会话，刷新即回默认。实机应写进配置；要不要连「上次进的是哪个文件夹」一起记住？MX Player 是会记的。' },
  { tag: '缩略图', name: '文件夹卡要 4 张抽帧',
    text: '四宫格拼贴意味着每个目录至少要抽 4 张缩略图，生成成本是网格视图的 4 倍。是否改成「取最新一条的封面 + 计数」，省下四分之三？' },
  { tag: '对比', name: '亮场下的进度条轨道',
    text: '未播放段是 20% 白，压在雪景、天空这类高亮画面上几乎消失（切到「亮场」就能看到）。是否改成「深色槽 + 亮色细边」？' },
];

const STORE_KEY = 'deckplayer.ui-review.v1';

/* ── 状态 ─────────────────────────────────────────────────── */

const state = {
  tab: 'media',
  browse: null,
  theme: 'dark',
  device: 'deck',
  scene: 'dark',
  layout: 'grid',
  kind: 'all',
  filter: 'all',
  /* 视图：决定「媒体库这一页怎么显示」。
     MX Player 把这套东西收在一个呼出菜单里（分组 / 排序 / 筛选 / 视图形态），
     而不是像我们上一版那样在工具行上摊四个胶囊 —— 摊开时那条工具行占了
     一整行，能选的也只有「视频/音乐/全部」这几个字。 */
  group: 'folder',   // folder | flat
  sort: 'name',      // name | added | dur | size
  desc: false,
  libFolder: null,   // 已进入的文件夹路径（null = 文件夹总览）
  sheet: false,      // 视图面板是否展开
  query: '',
  focus: { zone: 'main', i: 0 },
  annotate: false,
  zoom: 'fit',
  selected: null,
  composerTarget: '整体 · 媒体库',
  comments: [],
  dialog: null,
  pluginPage: false,
  player: {
    open: false, playing: true, pos: 68, dur: 206, index: 0,
    osd: true, locked: false, speed: 1, aspect: 0, sub: 1, audio: 0,
    subPanel: false,        // 字幕面板（内嵌轨 + 外挂 + 延时）
    subPicker: false,       // 字幕文件选择器
    subDelay: 0,
    subFiles: SUB_FILES.slice(),
  },
  np: { show: false, playing: true, pos: 78, dur: 201 },
  seekDrag: false,
  autoSub: true,
  rememberProgress: true,
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

/* 列表卡片。
 *
 * 2026-11：视频 / 音乐行改为自带 16:9 缩略图（96×54），行高 76 → 72。
 * 之前只有 46px 的类型图标，条目一多就退化成文件名列表，扫一眼认不出片子。
 * 文件夹、设置项这类没有画面的条目仍然用图标块 —— 强行给它们配图只会更乱。
 */
function cell(o) {
  const chev = o.chevron ? '<span class="chev">' + ic('chevron', 16, 1.8) + '</span>' : '';
  const badge = o.badge ? '<span class="badge">' + esc(o.badge) + '</span>' : '';
  const val = o.value != null ? '<span class="val">' + esc(o.value) + '</span>' : '';
  const sw = o.toggle != null
    ? '<span class="sw' + (o.toggle ? ' on' : '') + '"><i></i></span>' : '';
  const prog = (o.progress != null && o.progress >= 0)
    ? '<span class="pbar"><i style="width:' + Math.round(clamp(o.progress, 0, 1) * 100) + '%"></i></span>'
    : '';
  const risk = o.risk ? ' data-risk="' + esc(o.risk) + '"' : '';
  const spec = o.spec || (o.tone
    ? '高 72 · 缩略图 88×50（16:9）· 圆角 8 · 标题 17 / 副标题 12.5'
    : '高 72 · 图标块 40 · 圆角 14 · 标题 17 / 副标题 12.5');
  /* 进度条压在缩略图底边，不再横贯整行。
     横贯整行时它会从缩略图右侧一直拉到卡片右缘，和右侧的「已看 33%」
     是同一个信息的两种表示，还额外切断了行的横向阅读线。
     压在图上就是「这一格画面我看了多少」，语义也对上了。 */
  const lead = o.tone
    ? '<span class="thumb-sm"><span class="frame ' + o.tone + '"></span>' + prog + '</span>'
    : '<span class="tile">' + ic(o.icon, 23, 1.9) + '</span>';
  /* 混排时给音乐加一个类型标：视频行和音乐行长得太像，
     一眼扫过去「3:21」既可能是短片也可能是歌，必须有个记号。 */
  const mark = o.mark
    ? '<span class="typechip" title="音乐">' + ic(o.mark, 11, 2.1) + '</span>' : '';
  return '<div class="cell focusable' + (o.tone ? ' has-thumb' : '') +
    (o.progress >= 0 ? ' has-progress' : '') + '"' +
    ' data-action="' + o.action + '" data-id="' + (o.id != null ? o.id : '') + '"' +
    ' data-name="' + esc(o.name || '列表卡片') + '" data-spec="' + esc(spec) + '"' + risk + '>' +
    lead +
    '<span class="body"><span class="titlerow">' + mark +
      '<span class="title">' + esc(o.title) + '</span></span>' +
    (o.sub ? '<span class="sub">' + esc(o.sub) + '</span>' : '') + '</span>' +
    badge + val + sw + chev + '</div>';
}

/* 网格卡片：234 × 176 = 16:9 缩略图 131.6 + 标题区 44。
   缩略图用渐变示意「按 10% 时长抽的那一帧」，不是真实视频帧 ——
   原型不引入 ffmpeg.wasm，接入成本远大于它带来的评审价值。 */
function gcard(o) {
  const seen = o.progress === 1 ? '<span class="seen">已看完</span>' : '';
  const dur = o.dur ? '<span class="dur">' + fmt(o.dur) + '</span>' : '';
  const pl = (o.progress != null && o.progress > 0 && o.progress < 1)
    ? '<span class="pl"><i style="width:' + Math.round(clamp(o.progress, 0, 1) * 100) + '%"></i></span>'
    : '';
  // 列宽随侧栏宽度浮动（现约 248），不写死像素 —— 写死了改一处就过期一处
  const spec = o.spec || '网格卡片 · 4 列自适应 · 缩略图 16:9 · 圆角 14 · 标题 14';
  const tmark = o.mark
    ? '<span class="tmark" title="音乐">' + ic(o.mark, 13, 2.1) + '</span>' : '';
  return '<div class="gcard focusable" data-action="' + o.action + '"' +
    ' data-id="' + (o.id != null ? o.id : '') + '"' +
    ' data-name="' + esc(o.name || '网格卡片') + '" data-spec="' + esc(spec) + '"' +
    (o.risk ? ' data-risk="' + esc(o.risk) + '"' : '') + '>' +
    '<span class="thumb"><span class="frame ' + (o.tone || 't0') + '"></span>' +
    tmark + seen + dur + pl + '</span>' +
    '<span class="cap"><span class="tt">' + esc(o.title) + '</span>' +
    '<span class="ss">' + esc(o.sub) + '</span></span></div>';
}

function grid(items) {
  return '<div class="grid" data-cols="4">' + items.join('') + '</div>';
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
  return TABS.map((t) =>
    '<div class="nav-item' + (state.tab === t.key ? ' active' : '') + ' focusable" ' +
    'data-tab="' + t.key + '" data-name="侧边栏条目" ' +
    'data-spec="高 56 · 图标 18 + 间距 11 · 字号 17 · 强调条 3×26">' +
    '<span class="bar"></span>' + ic(t.icon, 18, 1.8) + '<span>' + t.label + '</span></div>').join('');
}

function listHTML() {
  const t = state.tab;
  if (t === 'media') return mediaHTML();
  if (t === 'files') return state.browse ? browseHTML() : filesHTML();
  if (t === 'history') return historyHTML();
  return settingsHTML();
}

/* ── 媒体库 ───────────────────────────────────────────────────
   内容页只放内容。
   搜索留在工具行；「怎么显示」全部收进呼出菜单（分组 / 内容 / 排序 / 筛选）。
   「添加媒体文件夹」「重新扫描」「插件」是配置，全部在设置页。

   为什么要有「按文件夹」：一台掌机上接了内置存储 + SD 卡，媒体文件夹四五个，
   全部拉平之后视频和音乐混在一起、十个「01. 开头」的文件排成一列，
   找东西只剩搜索。MX Player 的解法就是给列表加一层文件夹，并把显示规则
   收进一个呼出菜单 —— 这里照做。 */
function mediaHTML() {
  const pool = mediaPool();

  if (!pool.length) {
    return emptyState('search', state.query ? '没有匹配的内容' : '这个筛选下没有内容',
      state.query ? '换个关键词，或清空搜索框' : '去「设置 → 媒体文件夹」添加目录');
  }

  /* 搜索一旦有内容，就临时摊平成结果列表：
     在文件夹卡片里搜「沙丘」却看到一张文件夹卡，是搜索最反直觉的结果。 */
  if (state.query.trim() || state.group === 'flat') {
    const groups = kindGroups(pool);
    return groups.map((g) =>
      section(g.label + ' · ' + g.items.length +
        (state.libFolder ? ' · ' + FOLDER_BY_PATH[state.libFolder].name : '')) +
      (state.layout === 'grid' ? grid(g.items.map(itemCard)) : g.items.map(itemRow).join(''))
    ).join('') + (state.libFolder ? '' : (groups.length > 1 ? flatHint() : ''));
  }

  // 已进入某个文件夹
  if (state.libFolder) {
    const f = FOLDER_BY_PATH[state.libFolder];
    const groups = kindGroups(pool);
    return libBackRow(f) +
      groups.map((g) =>
        section(g.label + ' · ' + g.items.length) +
        (state.layout === 'grid' ? grid(g.items.map(itemCard)) : g.items.map(itemRow).join(''))
      ).join('');
  }

  // 文件夹总览
  const groups = folderGroups(pool);
  const recent = recentItems(8);
  return section('文件夹 · ' + groups.length) +
    (state.layout === 'grid'
      ? grid(groups.map((g) => folderCard(g.folder, g.items)))
      : groups.map((g) => folderRow(g.folder, g.items)).join('')) +
    (recent.length
      ? section('最近添加 · ' + recent.length) +
        (state.layout === 'grid' ? grid(recent.map(itemCard)) : recent.map(itemRow).join(''))
      : '');
}

/* 「最近添加」：只取固定条目，不跟着排序选项变 ——
   这一段的语义就是「最新的几样」，让它按名字重排就自相矛盾了。
   它同时解决了文件夹总览的另一个问题：四个目录占不满一屏，下面是空的。 */
function recentItems(n) {
  return MEDIA
    .filter((x) => state.kind === 'all' || x.kind === state.kind)
    .slice()
    .sort((a, b) => (a.added || 0) - (b.added || 0))
    .slice(0, n);
}

/* 条目池：内容类型 → 文件夹 → 搜索 → 筛选 → 排序。
   顺序不能反：先按文件夹收窄再排序，否则「最近添加」会把不同目录穿插起来。 */
function mediaPool() {
  const q = state.query.trim().toLowerCase();
  let items = MEDIA.filter((x) => state.kind === 'all' || x.kind === state.kind);
  if (state.libFolder) items = items.filter((x) => x.folder === state.libFolder);
  if (q) items = items.filter((x) => x.search.indexOf(q) >= 0);
  if (state.filter === 'resume') items = items.filter((x) => x.progress > 0 && x.progress < 1);
  if (state.filter === 'finished') items = items.filter((x) => x.progress === 1);
  return sortMedia(items);
}

function sortMedia(arr) {
  const dir = state.desc ? -1 : 1;
  const by = {
    name:  (a, b) => String(a.title).localeCompare(String(b.title), 'zh-Hans-CN'),
    // added 记的是「几天前」，小的更新 → 升序天然就是「最新在前」
    added: (a, b) => (a.added || 0) - (b.added || 0),
    dur:   (a, b) => (a.dur || 0) - (b.dur || 0),
    size:  (a, b) => (a.size || 0) - (b.size || 0),
  }[state.sort] || ((a, b) => 0);
  return arr.slice().sort((a, b) => by(a, b) * dir);
}

function kindGroups(items) {
  const v = items.filter((x) => x.kind === 'video');
  const a = items.filter((x) => x.kind === 'audio');
  const out = [];
  if (v.length) out.push({ key: 'video', label: '视频', items: v });
  if (a.length) out.push({ key: 'audio', label: '音乐', items: a });
  return out;
}

function folderGroups(items) {
  const map = {};
  items.forEach((x) => { (map[x.folder] = map[x.folder] || []).push(x); });
  return FOLDERS.filter((f) => map[f.path])
    .map((f) => ({ folder: f, items: map[f.path] }));
}

function fcount(items) {
  const v = items.filter((x) => x.kind === 'video').length;
  const a = items.filter((x) => x.kind === 'audio').length;
  const parts = [];
  if (v) parts.push(v + ' 个视频');
  if (a) parts.push(a + ' 首音乐');
  return parts.join(' · ');
}

/* 四宫格拼贴：文件夹没有「一张封面」这件事 ——
   取第一个文件当封面会让文件夹长得像那部片，四格才读得出「这里装着东西」。
   不足 4 项时按序补齐，不留空格：空格会被读成「这里缺图/损坏」，
   补位只是重复，重复读起来是「就这几样」。只有 1 项时干脆整张给它。 */
function collage(items, cls) {
  if (items.length === 1) {
    return '<span class="' + cls + ' single"><span class="frame ' + items[0].tone + '"></span></span>';
  }
  const cells = [];
  for (let i = 0; i < 4; i++) {
    const it = items[i % items.length];
    cells.push('<span class="c"><span class="frame ' + it.tone + '"></span></span>');
  }
  return '<span class="' + cls + '">' + cells.join('') + '</span>';
}

function folderCard(f, items) {
  const multi = items.filter((x) => x.kind === 'audio').length > 0 &&
                items.filter((x) => x.kind === 'video').length > 0;
  return '<div class="gcard foldercard focusable" data-action="open-folder" data-id="' + esc(f.path) + '"' +
    ' data-name="文件夹卡片" data-spec="文件夹卡片 · 四宫格拼贴 · 圆角 14 · 计数角标 12"' +
    (multi ? ' data-risk="这个目录里视频和音乐混放。平铺模式下它们会被打散在一起，只有文件夹视图能分开。"' : '') + '>' +
    '<span class="thumb">' + collage(items, 'collage') +
      '<span class="fc-badge">' + items.length + '</span>' +
    '</span>' +
    '<span class="cap"><span class="tt">' + esc(f.name) + '</span>' +
    '<span class="ss">' + esc(fcount(items)) + '</span></span></div>';
}

function folderRow(f, items) {
  return '<div class="cell focusable has-thumb" data-action="open-folder" data-id="' + esc(f.path) + '"' +
    ' data-name="文件夹行" data-spec="高 72 · 拼贴 50×50 · 标题 17 / 副标题 12.5">' +
    collage(items, 'thumb-sm collage') +
    '<span class="body"><span class="title">' + esc(f.name) + '</span>' +
    '<span class="sub">' + esc(f.drive + ' · ' + f.path) + '</span></span>' +
    '<span class="val">' + items.length + ' 项</span>' +
    '<span class="chev">' + ic('chevron', 16, 1.8) + '</span></div>';
}

function libBackRow(f) {
  return '<div class="cell focusable" data-action="lib-up" data-name="返回文件夹总览"' +
    ' data-spec="高 72 · 返回行 · 与列表卡片同一规格">' +
    '<span class="tile">' + ic('back', 22, 1.9) + '</span>' +
    '<span class="body"><span class="title">全部文件夹</span>' +
    '<span class="sub">' + esc(f.name + ' · ' + f.path) + '</span></span></div>';
}

function itemCard(x) {
  return gcard({
    action: x.kind === 'audio' ? 'play-music' : 'play',
    id: x.idx, tone: x.tone,
    title: x.title, sub: x.sub, dur: x.dur, progress: x.progress,
    mark: x.kind === 'audio' ? 'music' : '',
    name: x.kind === 'audio' ? '音乐缩略图卡片' : '视频缩略图卡片',
    risk: x.progress === -1 && x.kind === 'video'
      ? '缩略图按 min(时长×0.1, 30s) 抽帧。取第一帧会得到片头黑场，网格会花掉一半。'
      : '',
  });
}

function itemRow(x) {
  return cell({
    icon: x.kind === 'audio' ? 'music' : 'video',
    tone: x.tone,
    title: x.title, sub: x.sub, badge: x.badge, progress: x.progress,
    mark: x.kind === 'audio' ? 'music' : '',
    action: x.kind === 'audio' ? 'play-music' : 'play', id: x.idx,
    name: x.kind === 'audio' ? '音乐行' : '视频行',
  });
}

/* 平铺模式的一句提示：说清「为什么这里是分区而不是混排」 */
function flatHint() {
  return '<div class="note">' + ic('info', 15, 1.8) +
    '<span>平铺模式把视频和音乐分成两段。若想让它们按目录归拢，' +
    '在「视图」里切回「按文件夹」。</span></div>';
}

/* ── 文件 ─────────────────────────────────────────────────────
   按位置浏览，不经过媒体库。这是 MX Player 的那条路径：
   想找「昨天拷进 SD 卡的那个文件」时用这里，而不是去媒体库里翻。 */
function filesHTML() {
  return section('存储设备') +
    cell({ icon: 'drive', title: '内置存储', sub: '/home/deck · 1.2 TB 可用',
      chevron: true, action: 'browse', id: '/home/deck', name: '存储设备' }) +
    cell({ icon: 'drive', title: 'SD 卡', sub: '/run/media/deck/SD256 · 512 GB',
      chevron: true, action: 'browse', id: '/run/media/deck/SD256', name: '存储设备' }) +
    '<div class="note">' + ic('info', 15, 1.8) +
      '<span>这里是原始目录，不受媒体库扫描范围限制。媒体文件夹在「设置 → 媒体文件夹」里管理。</span></div>';
}

function historyHTML() {
  const body = state.layout === 'grid'
    ? grid(HISTORY.map((h) => gcard({
        action: 'play', id: h.idx, tone: 't' + (h.idx % 8),
        title: h.title, sub: h.sub,
        dur: (VIDEOS[h.idx] || {}).dur, progress: h.progress,
        name: '历史缩略图卡片',
      })))
    : HISTORY.map((h) => cell({
        icon: 'clock', tone: 't' + (h.idx % 8), title: h.title, sub: h.sub,
        badge: h.badge, progress: h.progress, action: 'play', id: h.idx, name: '历史行',
      })).join('');
  return section('继续观看 · ' + HISTORY.length) + body;
}

/* ── 设置 ─────────────────────────────────────────────────────
   所有「配置」都收在这里。内容页里一个都不留。 */
function settingsHTML() {
  return section('媒体库') +
    cell({ icon: 'folder', title: '媒体文件夹', value: '2 个', chevron: true,
      action: 'open-folders', name: '设置项' }) +
    cell({ icon: 'search', title: '重新扫描媒体库', sub: '更新新增或修改的文件',
      action: 'rescan', name: '设置项' }) +
    cell({ icon: 'video', title: '元数据刮削', value: '自动', chevron: true,
      action: 'set-scrape', name: '设置项' }) +

    section('内容源') +
    cell({ icon: 'plugin', title: '插件视频源', value: '1 个', chevron: true,
      action: 'plugins', name: '设置项' }) +
    cell({ icon: 'cloud', title: '网络存储', value: '未配置', chevron: true,
      action: 'set-network', name: '设置项' }) +

    section('字幕') +
    cell({ icon: 'subtitles', title: '默认字幕语言', value: '简体中文', chevron: true,
      action: 'set-sub-lang', name: '设置项',
      risk: '实机目前没有这一项：每次都从轨道列表里手动挑，换一部片就要重挑一次。' }) +
    cell({ icon: 'check2', title: '自动加载同名外挂字幕', sub: '同目录下片名相同即自动挂载',
      toggle: true, action: 'toggle-auto-sub', name: '设置项',
      risk: '扫描时其实已经记录了 matched_subtitles（卡片上会显示「含字幕」），但没有自动挂载 —— 数据有，动作没接。' }) +
    cell({ icon: 'subtitles', title: '字幕字号', value: '屏高 5.5%', chevron: true,
      action: 'set-sub-size', name: '设置项' }) +

    section('外观') +
    cell({ icon: 'tv', title: '主题', value: state.theme === 'light' ? '浅色' : state.theme === 'oled' ? 'OLED 真黑' : '深色',
      chevron: true, action: 'set-theme', name: '设置项' }) +
    cell({ icon: 'grid2', title: '默认视图', value: state.layout === 'grid' ? '网格' : '列表',
      chevron: true, action: 'set-layout', name: '设置项' }) +

    section('播放') +
    cell({ icon: 'clock', title: '记住播放进度', toggle: true, action: 'toggle-resume', name: '设置项' }) +
    cell({ icon: 'play', title: '硬件解码', value: '自动', chevron: true, action: 'set-hw', name: '设置项' }) +
    cell({ icon: 'info', title: '关于 DeckPlayer', value: 'GPL-3.0', chevron: true,
      action: 'about', name: '设置项' });
}

function browseHTML() {
  const node = FS[state.browse];
  if (!node) { state.browse = null; return listHTML(); }
  const parent = PARENT[state.browse];
  const up = cell({
    icon: 'back', title: '返回上一级', sub: parent || '存储设备',
    chevron: true, action: 'up',
  });

  const dirs = node.items.filter((it) => it.dir);
  const videos = node.items.filter((it) => !it.dir && it.v != null);
  const tracks = node.items.filter((it) => it.m != null);

  const dirBlock = dirs.length
    ? section('子文件夹') + dirs.map((it) =>
        cell({ icon: 'folder', title: it.name, sub: '文件夹', chevron: true, action: 'browse', id: it.path })).join('')
    : '';

  let vidBlock = '';
  if (videos.length) {
    vidBlock = state.layout === 'grid'
      ? grid(videos.map((it, k) => gcard({
          action: 'play', id: it.v, tone: 't' + (k % 8),
          title: it.name, sub: it.sub,
          dur: (VIDEOS[it.v] || {}).dur,
          progress: (VIDEOS[it.v] || {}).progress,
          name: '文件缩略图卡片',
        })))
      : videos.map((it, k) =>
          cell({ icon: 'video', tone: 't' + (k % 8), title: it.name, sub: it.sub,
            action: 'play', id: it.v, name: '文件行' })).join('');
  }

  const trackBlock = tracks.length
    ? tracks.map((it, k) =>
        cell({ icon: 'music', tone: 'm' + (k % 3), title: it.name, sub: it.sub,
          action: 'play-music', id: it.m, name: '音频行' })).join('')
    : '';

  const empty = node.items.length ? ''
    : emptyState('folder', '这个文件夹是空的', '支持常见的视频 / 音频格式\n同名字幕会自动匹配');

  return up + dirBlock +
    (videos.length ? section('视频 · ' + videos.length) + vidBlock : '') +
    (tracks.length ? section('音频 · ' + tracks.length) + trackBlock : '') +
    empty;
}

function hintsHTML() {
  const h = (k, label, accent) =>
    '<span class="hint"><span class="k' + (accent ? ' a' : '') + '">' + k + '</span>' + label + '</span>';
  const map = {
    media:    h('A', '播放', true) + h('B', '打开文件夹') + h('Y', '视图菜单'),
    files:    h('A', '打开', true) + h('B', '返回上一级'),
    history:  h('A', '继续播放', true) + h('B', '返回') + h('X', '移除记录'),
    settings: h('A', '进入', true) + h('B', '返回'),
  };
  if (state.tab === 'media' && state.libFolder) {
    return h('A', '播放', true) + h('B', '全部文件夹') + h('Y', '视图菜单');
  }
  if (state.tab === 'files' && state.browse) {
    return h('A', '播放', true) + h('B', '返回上一级') + h('X', '查看信息');
  }
  return map[state.tab] || map.media;
}

/* ── 工具行与视图菜单 ─────────────────────────────────────────
   上一版把「视频 / 音乐 / 全部」和四个筛选胶囊摊在工具行上，
   一条 58px 的带子被占满，能表达的却只有五个词。
   MX Player 把这些收进一个呼出菜单：入口只有一个，里面分「分组 / 排序 / 筛选」。
   工具行因此只剩两样东西 —— 搜索框和这个入口。 */

const GROUPS = [
  { key: 'folder', label: '按文件夹' },
  { key: 'flat',   label: '全部平铺' },
];

const KINDS = [
  { key: 'all',   label: '视频与音乐' },
  { key: 'video', label: '仅视频' },
  { key: 'audio', label: '仅音乐' },
];

const SORTS = [
  { key: 'name',  label: '名称' },
  { key: 'added', label: '最近添加' },
  { key: 'dur',   label: '时长' },
  { key: 'size',  label: '大小' },
];

const FILTERS = [
  { key: 'all',      label: '不限' },
  { key: 'resume',   label: '未看完' },
  { key: 'finished', label: '已看完' },
];

function toolbarHTML() {
  if (state.tab !== 'media') return '';
  return '<div class="toolbar" data-name="媒体库工具行" data-spec="高 58 · 搜索框 264×36 · 视图入口 高 36">' +
      '<label class="searchbox">' + ic('search', 17, 1.9) +
        '<input id="searchInput" type="text" placeholder="搜索标题或文件名" ' +
          'value="' + esc(state.query) + '" autocomplete="off" spellcheck="false">' +
        /* 始终渲染，靠 :placeholder-shown 控制显隐 ——
           如果按 state.query 决定要不要输出，输入时只重建列表不重建工具行，
           这个按钮就会一直不出现。 */
        '<button class="clear" id="searchClear" title="清空">' + ic('close', 14, 2) + '</button>' +
      '</label>' +
      '<button class="view-btn' + (state.sheet ? ' on' : '') + '" id="viewBtn"' +
        ' data-name="视图入口" data-spec="高 36 · 图标 15 + 摘要 13 + 箭头 · 点击呼出菜单">' +
        ic('filter', 15, 1.9) +
        '<span class="vb-txt">' + esc(viewSummary()) + '</span>' +
        ic('chevron', 13, 2) +
      '</button>' +
      '<span class="count">' + mediaCount() + ' 项</span>' +
    '</div>';
}

function viewSummary() {
  if (state.libFolder) return FOLDER_BY_PATH[state.libFolder].name;
  const g = GROUPS.filter((x) => x.key === state.group)[0] || GROUPS[0];
  const s = SORTS.filter((x) => x.key === state.sort)[0] || SORTS[0];
  return g.label + ' · ' + s.label + (state.desc ? ' ↓' : '');
}

/* 呼出菜单：点工具行右侧的入口，菜单从屏幕底部中间升起。
   故意做成「一次只显示一组、每组只有 2–4 个选项」——
   手柄导航时上下只需要按几下，触屏也不用瞄准小胶囊。 */
function viewSheetHTML() {
  if (!state.sheet) return '';
  const row = (label, attr, opts, cur) =>
    '<div class="vs-row"><span class="vs-label">' + label + '</span>' +
    '<span class="vs-opts">' + opts.map((o) =>
      '<button class="vs-opt' + (o.key === cur ? ' on' : '') + '" data-' + attr + '="' + o.key + '">' +
        (o.key === cur ? ic('check', 11, 2.6) : '') +
        '<span>' + o.label + '</span></button>').join('') +
    '</span></div>';

  return '<div class="vs-layer" id="vsLayer">' +
    '<div class="vs-scrim" id="vsScrim"></div>' +
    '<div class="view-sheet on" id="viewSheet" data-name="视图呼出菜单"' +
    ' data-spec="宽 460 · 贴底居中 · 四组选项 · 行高 40">' +
    '<div class="vs-head">' + ic('filter', 15, 1.9) + '<span>视图与排序</span>' +
      '<button class="vs-close" id="vsClose" title="收起">' + ic('close', 13, 2.2) + '</button></div>' +
    row('分组', 'vgroup', GROUPS, state.group) +
    row('内容', 'vkind', KINDS, state.kind) +
    row('排序', 'vsort', SORTS, state.sort) +
    row('筛选', 'vfilter', FILTERS, state.filter) +
    '<div class="vs-foot">' +
      '<button class="vs-dir' + (state.desc ? ' on' : '') + '" data-vdesc="1">' +
        '<span class="ic-flip">' + ic('arrowUp', 13, 2.2) + '</span>' +
        (state.desc ? '降序' : '升序') + '</button>' +
      '<button class="vs-reset" data-vreset="1">恢复默认</button>' +
    '</div></div>' +
    '</div>';
}

/* 菜单挂在独立的图层上，不放在工具行里：
   工具行在改选项时会整块重建，菜单跟着重建就会每次闪一下入场动画。 */
function refreshSheet(animate) {
  const layer = $('#sheetLayer');
  if (!layer) return;
  layer.innerHTML = viewSheetHTML();
  const s = $('#viewSheet');
  if (s && animate === false) s.style.animation = 'none';
}

function setSheet(on, animate) {
  const was = state.sheet;
  state.sheet = !!on;
  if (state.sheet && !was) state.focus = { zone: 'main', i: 0 };  // 焦点落进菜单
  refreshToolbar();
  refreshSheet(animate);
  applyFocus();
}

function mediaCount() {
  return mediaPool().length;
}

function mediaCount() {
  return mediaPool().length;
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
  const layoutSeg =
    '<div class="layout-seg" id="layoutSeg" role="group" aria-label="布局">' +
      '<button data-layout="list" class="' + (state.layout === 'list' ? 'on' : '') + '" ' +
        'title="列表：一行一部片，信息密度高">' + ic('listLines', 17, 1.9) + '</button>' +
      '<button data-layout="grid" class="' + (state.layout === 'grid' ? 'on' : '') + '" ' +
        'title="网格：缩略图墙，一眼认出片子">' + ic('grid2', 17, 1.9) + '</button>' +
    '</div>';

  app.innerHTML =
    '<header class="app-header" data-name="应用顶栏" data-spec="高 92 · 左右内边距 48 · 标题 29px">' +
      '<span class="mark">' + ic('video', 24, 1.8) + '</span>' +
      '<h1>DeckPlayer</h1>' +
      (state.tab === 'media' || state.tab === 'history' ? layoutSeg : '') +
      '<span class="ver"><i></i>GPL-3.0</span>' +
    '</header>' +
    '<div class="app-body">' +
      '<nav class="sidebar" id="sidebar" data-name="侧边栏" ' +
        'data-spec="宽 160（1280 的 12.5%）· 条目高 56 · 图标 18 + 文字 17 · 强调条 3×26">' +
        navHTML() + '</nav>' +
      '<section class="main" id="main">' +
        '<div id="toolbar">' + toolbarHTML() + '</div>' +
        '<div class="list" id="list">' + listHTML() + '</div>' +
      '</section>' +
    '</div>' +
    npHTML() +
    '<div id="sheetLayer">' + viewSheetHTML() + '</div>' +
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

function setLayout(v) {
  if (state.layout === v) return;
  state.layout = v;
  state.sheet = false;   // 布局切换走整页重建，菜单跟着收起（否则要重放入场动画）
  state.focus = { zone: 'main', i: state.focus.i };
  renderApp();
  toast(v === 'grid' ? '网格：缩略图墙 · 一屏 3 行 × 4 列' : '列表：一行一部片 · 信息密度更高');
}

function bindAppEvents() {
  const app = $('#app');

  app.onclick = (e) => {
    if (state.annotate) return; // 标注模式由全局捕获处理
    const lay = e.target.closest('[data-layout]');
    if (lay) { setLayout(lay.dataset.layout); return; }
    const nav = e.target.closest('.nav-item');
    if (nav) { switchTab(nav.dataset.tab); return; }

    /* 视图菜单：入口开合、四组选项、升序降序、恢复默认。
       全部走 refreshView()，它同时重建工具行（摘要变了）和列表。 */
    if (e.target.closest('#viewBtn')) {
      setSheet(!state.sheet, true);
      return;
    }
    if (e.target.closest('#vsClose')) { setSheet(false, false); return; }
    const vg = e.target.closest('[data-vgroup]');
    if (vg) { state.group = vg.dataset.vgroup; state.libFolder = null; refreshView(); return; }
    const vk = e.target.closest('[data-vkind]');
    if (vk) { state.kind = vk.dataset.vkind; refreshView(); return; }
    const vs = e.target.closest('[data-vsort]');
    if (vs) { state.sort = vs.dataset.vsort; refreshView(); return; }
    const vf = e.target.closest('[data-vfilter]');
    if (vf) { state.filter = vf.dataset.vfilter; refreshView(); return; }
    if (e.target.closest('[data-vdesc]')) { state.desc = !state.desc; refreshView(); return; }
    if (e.target.closest('[data-vreset]')) {
      state.group = 'folder'; state.kind = 'all'; state.sort = 'name';
      state.desc = false; state.filter = 'all'; state.libFolder = null;
      refreshView();
      toast('已恢复默认视图');
      return;
    }
    // 点菜单和列表之外的地方收起菜单
    if (state.sheet && !e.target.closest('#viewSheet')) {
      setSheet(false, false);
    }

    if (e.target.closest('#searchClear')) {
      state.query = ''; refreshToolbar(); refreshList();
      const si = $('#searchInput'); if (si) si.focus();
      return;
    }
    const c = e.target.closest('.cell, .gcard');
    if (c) { activateCell(c); return; }
    if (e.target.closest('.np')) {
      state.np.playing = !state.np.playing;
      renderApp();
      toast(state.np.playing ? '继续播放 · 夜に駆ける' : '已暂停');
    }
  };

  // 搜索框：输入时只重建列表，不重建输入框本身，否则每敲一个字就丢焦点
  const si = $('#searchInput');
  if (si) {
    si.oninput = () => { state.query = si.value; refreshList(); };
    // 方向键在输入框里是移动光标，不能触发侧栏/卡片导航
    si.onkeydown = (e) => {
      if (e.key === 'Escape') { si.value = ''; state.query = ''; refreshList(); refreshToolbar(); }
      e.stopPropagation();
    };
  }
}

/* 只重建列表：保住搜索框的 DOM 与光标位置 */
function refreshList() {
  const list = $('#list');
  if (!list) return;
  list.innerHTML = listHTML();
  const cnt = $('.toolbar .count');
  if (cnt) cnt.textContent = mediaCount() + ' 项';
  applyFocus();
  decorate();
}

function refreshToolbar() {
  const tb = $('#toolbar');
  if (!tb) return;
  tb.innerHTML = toolbarHTML();
  const si = $('#searchInput');
  if (si) {
    si.oninput = () => { state.query = si.value; refreshList(); };
    si.onkeydown = (e) => {
      if (e.key === 'Escape') { si.value = ''; state.query = ''; refreshList(); refreshToolbar(); }
      e.stopPropagation();
    };
  }
}

/* 视图变化：工具行（摘要文字）和列表都要重建。
   菜单本身留在 DOM 里，否则每选一项它就自动关掉 —— 连续改两项就得重开两次。 */
function refreshView() {
  const tb = $('#toolbar');
  if (tb) {
    tb.innerHTML = toolbarHTML();
    const si = $('#searchInput');
    if (si) {
      si.oninput = () => { state.query = si.value; refreshList(); };
      si.onkeydown = (e) => {
        if (e.key === 'Escape') { si.value = ''; state.query = ''; refreshList(); refreshToolbar(); }
        e.stopPropagation();
      };
    }
  }
  refreshList();
  refreshSheet(false);   // 只换选项的选中态，不重放入场动画
  const list = $('#list');
  if (list) list.scrollTop = 0;
}

function activateCell(el) {
  const a = el.dataset.action;
  const id = parseInt(el.dataset.id || '0', 10);
  if (a === 'open-folder') {
    state.libFolder = el.dataset.id;
    state.sheet = false;
    state.focus = { zone: 'main', i: 0 };
    refreshView();
    toast('已进入 · ' + FOLDER_BY_PATH[state.libFolder].name);
    return;
  }
  if (a === 'lib-up') {
    state.libFolder = null;
    state.focus = { zone: 'main', i: 0 };
    refreshView();
    return;
  }
  if (a === 'play') { openPlayer(id); return; }
  if (a === 'play-music') {
    state.np.show = true; state.np.playing = true; state.np.pos = 12;
    renderApp(); toast('开始播放 · ' + MUSIC[id].title);
    return;
  }
  if (a === 'plugins') { openPlugins(); return; }
  if (a === 'open-folders') { openDialog('folders'); return; }
  if (a === 'add-folder') { openDialog('folder'); return; }
  if (a === 'rescan') {
    toast('正在扫描媒体库…');
    setTimeout(() => toast('扫描完成 · 4 个视频 / 3 首音乐'), 1100);
    return;
  }
  if (a === 'set-theme') {
    setTheme(state.theme === 'dark' ? 'oled' : state.theme === 'oled' ? 'light' : 'dark');
    renderApp();
    return;
  }
  if (a === 'set-layout') { setLayout(state.layout === 'grid' ? 'list' : 'grid'); return; }
  if (a === 'toggle-auto-sub') {
    state.autoSub = !state.autoSub;
    renderApp();
    toast(state.autoSub ? '已开启：同名字幕自动挂载' : '已关闭自动挂载');
    return;
  }
  if (a === 'toggle-resume') {
    state.rememberProgress = !state.rememberProgress;
    renderApp();
    toast(state.rememberProgress ? '已开启：记住播放进度' : '已关闭：每次从头播放');
    return;
  }
  if (a.indexOf('set-') === 0) { toast('这一项在原型里只占位，等实机功能定了再做面板'); return; }
  if (a === 'about') { toast('DeckPlayer · GPL-3.0 · 基于 borealis + libmpv'); return; }
  if (a === 'browse') {
    state.browse = el.dataset.id;
    state.focus = { zone: 'main', i: 0 };
    renderApp();
    setTarget('文件 · ' + (FS[state.browse] ? FS[state.browse].name : '目录'));
    renderDock();
    return;
  }
  if (a === 'up') { goUp(); return; }
}

function goUp() {
  if (!state.browse) return;
  const parent = PARENT[state.browse];
  state.browse = parent || null;
  state.focus = { zone: 'main', i: 0 };
  renderApp();
  setTarget(parent ? '文件 · ' + FS[parent].name : '整体 · 文件');
  renderDock();
}

/* ── 焦点 ─────────────────────────────────────────────────── */

function navItems() { return $$('.nav-item'); }
/* 菜单打开时，方向键要走进菜单，而不是在遮罩背后移动列表焦点。
   实机上这是焦点栈的压栈，原型里用同一套 zone 表示。 */
function mainItems() {
  if (state.sheet) return $$('#viewSheet .vs-opt, #viewSheet .vs-dir, #viewSheet .vs-reset');
  return $$('#main .focusable');
}

/* 网格模式下上下键要按「一行 4 个」跳，不是 ±1。
   返回 0 表示当前焦点不在网格里（列表或侧栏）。 */
function gridCols() {
  if (state.focus.zone !== 'main') return 0;
  const el = mainItems()[state.focus.i];
  if (!el) return 0;
  const g = el.closest('.grid');
  return g ? parseInt(g.dataset.cols || '4', 10) : 0;
}

/* 在网格内按「网格自己的顺序」移动。
   不能直接用 moveFocus(±4)：最后一行不足 4 张时，clamp 会把焦点甩出网格
   （例如 8 项里从第 8 项上移 4 会落到列表区第 4 行），跳变毫无规律。
   所以这里按网格内的相对位置算。

   返回值：
     'nogrid' 焦点不在网格里，交给调用方
     'moved'  真的移动了
     'before' 已经在第一张（按左键应该回侧栏）
     'after'  已经在最后一张（按右键到头了，按键吃掉不移动） */
function moveInGrid(step) {
  const items = mainItems();
  const cur = items[state.focus.i];
  if (!cur) return 'nogrid';
  const g = cur.closest('.grid');
  if (!g) return 'nogrid';
  const cards = items.filter((e) => e.closest('.grid') === g);
  const pos = cards.indexOf(cur);
  const next = pos + step;
  if (next < 0) return 'before';
  if (next >= cards.length) return 'after';
  state.focus.i = items.indexOf(cards[next]);
  applyFocus();
  cards[next].scrollIntoView({ block: 'nearest' });
  return 'moved';
}

function applyFocus() {
  const navs = $$('.nav-item');
  const mains = mainItems();
  // 焦点集合会随状态换（列表 ↔ 菜单），旧元素上残留的 focused 必须先清掉，
  // 否则遮罩背后的卡片会一直亮着，屏幕上出现两个焦点框。
  $$('#main .focused, #sheetLayer .focused').forEach((el) => el.classList.remove('focused'));
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
  state.sheet = false;   // 菜单属于媒体库，换页就收起
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

/* OSD 几何 —— 对应实机 src/view/mpv_player.cpp 的 namespace osd。
 *
 * 2026-11 改版：从「三行 + 三条轴」压成「两行 + 两条轴」。
 *   旧：设置行(距底 196, 右对齐) / 主控行(130, 居中) / 进度条(66) / 时间(34)
 *   新：工具条(124, 左端 m ↔ 右端 w−m) / 进度条(58) + 时间内联在两端
 * 底栏高度随之 238 → 168。 */
function osdGeom(w, h) {
  const m = clamp(w * 0.042, 24, 56);
  const barCY = h - 58;    // 进度条中心
  const toolCY = h - 124;  // 工具条中心（唯一一行控件）
  const gap = 62;          // 时间占位：进度条两端各让出这么多
  return {
    m: m,
    barCY: barCY,
    toolCY: toolCY,
    barX: m + gap,
    barW: w - m * 2 - gap * 2,
  };
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
  state.player.subPanel = false;
  state.player.subPicker = false;
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
    setBtns.push(btn(id, cx, g.toolCY, 40, icon, 19, '', name,
      '直径 40 · 间距 12 · 命中区 48'));
    right = cx - 20 - 12;
  };
  place('btnLock', 'lock', '锁定触控');
  place('btnPlaylist', 'playlist', '播放列表');
  place('btnAspect', 'aspect', '画面比例');
  const pillLeft = right - 78;
  const speedLabel = speedText(p.speed);
  const speedPill =
    '<button class="osd-pill" id="btnSpeed" data-name="倍速胶囊" data-spec="78×34 · 间距 12 · 命中区 86×42" ' +
    'style="left:' + pillLeft + 'px;top:' + (g.toolCY - 17) + 'px;width:78px">' +
    ic('speed', 16, 1.8) + '<span id="speedText">' + speedLabel + '</span></button>';
  right = pillLeft - 12;
  place('btnAudio', 'audio', '音轨');
  place('btnSub', p.sub > 0 ? 'subtitles' : 'subsOff', '字幕');

  const fill = clamp(p.pos / p.dur, 0, 1) * 100;

  const html =
    '<div class="player on" id="player">' +
      '<div class="scene ' + state.scene + '" id="scene"><div class="grain"></div></div>' +

      '<div class="osd on fade" id="osd" data-name="播放器 OSD" ' +
        'data-spec="顶栏 112 / 底栏 168 / 进度条 5 / 工具条 64·46·40 / 设置 40" ' +
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
          'data-spec="可视高 5 · 命中高 46 · 两端让出 62 给时间" ' +
          'style="left:' + g.barX + 'px;top:' + (g.barCY - 23) + 'px;width:' + g.barW + 'px">' +
          '<div class="seek-track"><div class="seek-fill" id="seekFill" style="width:' + fill + '%"></div></div>' +
          '<div class="seek-knob" id="seekKnob" style="left:' + fill + '%"></div>' +
          '<div class="seek-bubble" id="seekBubble" style="left:0;display:none">' + fmt(p.pos) + '</div>' +
        '</div>' +

        '<div class="osd-time" id="timeNow" data-name="当前时间" ' +
          'data-spec="14px · 与进度条同一基线 · 左端对齐 m" ' +
          'style="left:' + g.m + 'px;top:' + (g.barCY - 7) + 'px;line-height:14px;width:56px">' + fmt(p.pos) + '</div>' +
        '<div class="osd-time total" id="timeTotal" style="left:' + (w - g.m) + 'px;top:' + (g.barCY - 7) +
          'px;line-height:14px;width:56px;text-align:right;transform:translateX(-100%)">' + fmt(p.dur) + '</div>' +

        /* 主控块左边缘贴 m，块内播放按钮居中 —— 这样它的左缘和进度条的
           起点、右下设置块的右缘构成两条竖向对齐线，不再有第三条轴。 */
        btn('btnPrev', g.m + 20, g.toolCY, 40, 'prev', 20, '', '上一集', '直径 40 · 块内第 1 位') +
        btn('btnBack10', g.m + 75, g.toolCY, 46, 'seekBack', 21, '', '快退 10 秒', '直径 46 · 块内第 2 位') +
        btn('btnPlay', g.m + 142, g.toolCY, 64, p.playing ? 'pause' : 'play', 28, 'play', '播放 / 暂停',
          '直径 64 · 全屏唯一的实心强调色主操作 · 块内居中') +
        btn('btnFwd10', g.m + 209, g.toolCY, 46, 'seekFwd', 21, '', '快进 10 秒', '直径 46 · 块内第 4 位') +
        btn('btnNext', g.m + 264, g.toolCY, 40, 'next', 20, '', '下一集', '直径 40 · 块内末位') +

        setBtns.join('') + speedPill +
      '</div>' +

      /* 字幕面板与文件选择器都挂在 .osd 之外：
         OSD 会在 8 秒后收起，但用户正在挑字幕时不能被收走。 */
      '<div class="sub-panel' + (p.subPanel ? ' on' : '') + '" id="subPanel" ' +
        'style="right:' + g.m + 'px;bottom:186px;width:324px">' + subPanelHTML() + '</div>' +
      '<div class="pk-layer' + (p.subPicker ? ' on' : '') + '" id="subPicker">' +
        '<div class="pk" id="pkBox">' + subPickerHTML() + '</div></div>' +

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
  $('#btnSub').onclick = (e) => { e.stopPropagation(); toggleSubPanel(); };

  bindSubEvents();
  bindSeek();

  if (state.player.locked) {
    $('#lockBadge').style.display = 'grid';
    $('#osd').classList.remove('on');
  }
  syncOSD();
  decorate();
}

/* ── 字幕 ─────────────────────────────────────────────────────
   两条路都要有，缺一条就不可用：
     · 播放器内轨道 —— 内嵌字幕，mpv 的 track-list 直接给
     · 外挂字幕文件 —— 必须能「按文件挑」，而不是手打绝对路径
   实机现状只有后者的一半：promptLoadExternalSubtitle() 弹的是 IME 输入框，
   让用户在掌机上用虚拟键盘敲 /run/media/mmcblk0p1/Videos/... 这不可能用。
   正确做法是把 FolderPickerActivity 泛化成 FilePicker，默认停在当前视频目录。 */

function subTracks() {
  const p = state.player;
  const list = SUBS.slice();
  if (p.loadedSub) {
    list.push({
      title: p.loadedSub.title, sub: '外挂 · ' + p.loadedSub.name,
      icon: 'subtitles', ext: true, fresh: true,
    });
  }
  return list;
}

function subPanelHTML() {
  const p = state.player;
  const tracks = subTracks();
  const rows = tracks.map((s, i) =>
    '<button class="sp-row' + (p.sub === i ? ' on' : '') + '" data-sub="' + i + '">' +
      '<span class="sp-ic">' + ic(s.icon, 18, 1.8) + '</span>' +
      '<span class="sp-tx"><b>' + esc(s.title) + '</b><i>' + esc(s.sub) + '</i></span>' +
      (p.sub === i ? '<span class="sp-ck">' + ic('check', 15, 2.2) + '</span>' : '') +
    '</button>').join('');

  const delay = p.subDelay;
  const delayTxt = (delay > 0 ? '+' : '') + delay.toFixed(1) + 's';

  return '<div class="sp-head"><span>字幕</span>' +
      '<button class="sp-x" id="subClose" title="关闭">' + ic('close', 14, 2.1) + '</button></div>' +
    '<div class="sp-body">' + rows + '</div>' +
    '<button class="sp-add" id="subAdd">' + ic('plus', 15, 2.1) + '<span>从文件加载…</span></button>' +
    '<div class="sp-delay">' +
      '<button data-delay="-0.5">−0.5s</button>' +
      '<span class="sp-dv" id="subDelayVal">延时 ' + delayTxt + '</span>' +
      '<button data-delay="0.5">+0.5s</button>' +
    '</div>' +
    '<div class="sp-note">外挂字幕不会覆盖内嵌轨，会作为新轨道排在后面</div>';
}

function subPickerHTML() {
  const p = state.player;
  const rows = p.subFiles.map((f, i) =>
    '<button class="pk-row' + (f.best ? ' best' : '') + '" data-file="' + i + '">' +
      '<span class="pk-ic">' + ic('file', 18, 1.8) + '</span>' +
      '<span class="pk-tx"><b>' + esc(f.name) + '</b><i>' + esc(f.sub) + '</i></span>' +
      (f.best ? '<span class="pk-tag">同名</span>' : '') +
    '</button>').join('');

  return '<div class="pk-head">' +
      '<div><div class="pk-t">选择字幕文件</div>' +
        '<div class="pk-s">' + SUB_DIR + ' · 只列 .srt / .ass / .vtt / .sup / .ssa</div></div>' +
      '<button class="pk-x" id="pkClose" title="取消">' + ic('close', 15, 2.1) + '</button>' +
    '</div>' +
    '<div class="pk-body">' +
      '<div class="pk-crumb">' + ic('folder', 14, 1.8) + '<span>Videos</span>' +
        '<span class="pk-dot">·</span><span>默认停在当前视频所在目录</span></div>' +
      rows +
    '</div>' +
    '<div class="pk-foot"><span>与片名匹配的文件已排在最前</span>' +
      '<span class="pk-n">' + p.subFiles.length + ' 个</span></div>';
}

function toggleSubPanel(force) {
  const p = state.player;
  p.subPanel = force == null ? !p.subPanel : force;
  p.subPicker = p.subPanel ? p.subPicker : false;
  const panel = $('#subPanel');
  const picker = $('#subPicker');
  if (panel) panel.classList.toggle('on', p.subPanel);
  if (picker) picker.classList.toggle('on', p.subPicker);
  pokeOSD();
}

function renderSubPanel() {
  const panel = $('#subPanel');
  if (panel) panel.innerHTML = subPanelHTML();
  const picker = $('#subPicker');
  if (picker) picker.querySelector('.pk').innerHTML = subPickerHTML();
  pokeOSD();
}

function selectSub(i) {
  const p = state.player;
  p.sub = i;
  const icon = $('#btnSub');
  if (icon) icon.innerHTML = ic(i > 0 ? 'subtitles' : 'subsOff', 19, 1.9);
  const t = subTracks()[i];
  showHUD(i > 0 ? 'subtitles' : 'subtitles_off', i > 0 ? '已选字幕: ' + t.title : '关闭字幕');
  renderSubPanel();
}

function addSubFile(i) {
  const p = state.player;
  const f = p.subFiles[i];
  if (!f) return;
  // 与实机 sub-add 一致：新增一条 external 轨道并立刻选中，不动内嵌轨
  p.loadedSub = { name: f.name, title: f.name.replace(/\.[^.]+$/, '') };
  p.sub = SUBS.length;
  p.subPicker = false;
  const picker = $('#subPicker');
  if (picker) picker.classList.remove('on');
  const icon = $('#btnSub');
  if (icon) icon.innerHTML = ic('subtitles', 19, 1.9);
  showHUD('subtitles', '已加载字幕: ' + f.name);
  renderSubPanel();
}

function bumpSubDelay(d) {
  const p = state.player;
  p.subDelay = Math.round((p.subDelay + d) * 10) / 10;
  const txt = '延时 ' + (p.subDelay > 0 ? '+' : '') + p.subDelay.toFixed(1) + 's';
  const el = $('#subDelayVal');
  if (el) el.textContent = txt;
  showHUD('timer', '字幕' + txt);
  pokeOSD();
}

function bindSubEvents() {
  const panel = $('#subPanel');
  if (panel) {
    panel.onclick = (e) => {
      e.stopPropagation();
      const x = e.target.closest('#subClose');
      if (x) { toggleSubPanel(false); return; }
      const add = e.target.closest('#subAdd');
      if (add) { state.player.subPicker = true; $('#subPicker').classList.add('on'); pokeOSD(); return; }
      const d = e.target.closest('[data-delay]');
      if (d) { bumpSubDelay(parseFloat(d.dataset.delay)); return; }
      const row = e.target.closest('[data-sub]');
      if (row) { selectSub(parseInt(row.dataset.sub, 10)); return; }
    };
  }
  const picker = $('#subPicker');
  if (picker) {
    picker.onclick = (e) => {
      e.stopPropagation();
      if (e.target.closest('#pkClose')) {
        state.player.subPicker = false;
        picker.classList.remove('on');
        pokeOSD();
        return;
      }
      const r = e.target.closest('[data-file]');
      if (r) { addSubFile(parseInt(r.dataset.file, 10)); return; }
    };
  }
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

/* 倍速标签：1.25 不能显示成 1.3×。
   原来用 toFixed(1)，SPEEDS 里的 1.25 就被四舍五入成「1.3×」，
   和实际播放速度不符 —— 用户会以为选的是 1.3 倍。 */
function speedText(v) {
  if (v % 1 === 0) return v.toFixed(0) + '×';
  if (Math.round(v * 100) % 10 === 0) return v.toFixed(1) + '×';
  return v.toFixed(2) + '×';
}

function cycleSpeed() {
  const p = state.player;
  const i = SPEEDS.indexOf(p.speed);
  p.speed = SPEEDS[(i + 1) % SPEEDS.length];
  const label = speedText(p.speed);
  $('#speedText').textContent = label;
  showHUD('speed', '倍速 ' + label, (i + 1) / SPEEDS.length);
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
  if (e.target.closest('.osd-btn, .osd-pill, .seek-hit, .hud, .dialog-layer, .sub-panel, .pk-layer')) return;
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
  if (e.target.closest('.osd-btn, .osd-pill, .seek-hit, .dialog-layer, .sub-panel, .pk-layer')) return;
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
    { k: 'media', label: '媒体库', icon: 'video' },
    { k: 'viewmenu', label: '视图菜单', icon: 'filter' },
    { k: 'files', label: '文件', icon: 'folder' },
    { k: 'history', label: '历史', icon: 'clock' },
    { k: 'settings', label: '设置', icon: 'settings' },
    { k: 'player', label: '播放器 OSD', icon: 'play' },
    { k: 'playlist', label: '播放列表', icon: 'playlist' },
    { k: 'subs', label: '字幕面板', icon: 'subtitles' },
    { k: 'subfile', label: '字幕文件', icon: 'file' },
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
  if (k === 'playlist') {
    if (!state.player.open) openPlayer(state.player.index || 0);
    openDialog('playlist');
    return;
  }
  if (k === 'subs') {
    if (!state.player.open) openPlayer(state.player.index || 0);
    toggleSubPanel(true);
    return;
  }
  if (k === 'subfile') {
    if (!state.player.open) openPlayer(state.player.index || 0);
    toggleSubPanel(true);
    state.player.subPicker = true;
    const el = $('#subPicker');
    if (el) el.classList.add('on');
    return;
  }
  if (k === 'plugins') { openPlugins(); return; }
  closePlugins();
  if (state.player.open) closePlayer();
  if (state.dialog) closeDialog();
  if (k === 'viewmenu') {
    switchTab('media');
    state.group = 'folder';
    state.libFolder = null;
    setSheet(true, true);
    refreshList();
    return;
  }
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
    '- 主题：' + THEME_LABEL[state.theme],
    '- 布局：' + (state.layout === 'grid' ? '网格' : '列表'),
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

/* 三套主题。浅色只作用于浏览界面 —— 播放页 OSD 永远暗色，
   浅底控件压在亮画面上会直接消失（这正是当初把 ghost 按钮从
   「半透明白底 8%」改成「压暗底」的原因）。 */
const THEMES = ['dark', 'oled', 'light'];
const THEME_LABEL = { dark: '深色', oled: 'OLED', light: '浅色' };
const THEME_NOTE = {
  dark: '标准深色主题',
  oled: 'OLED 真黑：背景纯黑，表面只提 2–3 个灰阶',
  light: '浅色主题：暖白底，强调色压深到 #C4650F 以保住对比度',
};

function applyThemeLabel() {
  const t = state.theme;
  $('#themeLabel').textContent = THEME_LABEL[t];
  const dot = $('#themeDot');
  dot.classList.toggle('oled', t === 'oled');
  dot.classList.toggle('light', t === 'light');
}

function setTheme(t) {
  state.theme = t;
  $('#screen').dataset.theme = t;
  applyThemeLabel();
  toast(THEME_NOTE[t]);
}

function applyTheme() {
  $('#screen').dataset.theme = state.theme;
  applyThemeLabel();
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

  // 视图菜单：V 开合，Esc 收起
  if ((k === 'v' || k === 'V') && state.tab === 'media') {
    setSheet(!state.sheet, true);
    e.preventDefault();
    return;
  }
  if (k === 'Escape' && state.sheet) {
    setSheet(false, false);
    e.preventDefault();
    return;
  }
  // 在某个文件夹里按 Esc：退回文件夹总览
  if (k === 'Escape' && state.libFolder) {
    state.libFolder = null;
    state.focus = { zone: 'main', i: 0 };
    refreshView();
    e.preventDefault();
    return;
  }

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
    if (k === 's' || k === 'S') { toggleSubPanel(); return; }
    if (k === 'a' || k === 'A') { openDialog('audio'); return; }
  }

  if (state.pluginPage) {
    if (k === 'ArrowDown' || k === 'ArrowUp') { movePluginFocus(k === 'ArrowDown' ? 1 : -1); e.preventDefault(); }
    if (k === 'Enter') { const f = $('.page .cell.focused'); if (f) f.click(); }
    return;
  }

  const cols = gridCols();
  if (k === 'ArrowDown') {
    e.preventDefault();
    const r = moveInGrid(cols || 1);
    // 'after' = 已经是最后一行，下方没有卡片：停在原地，但按键要吃掉，
    // 否则会滑到「正在播放条」之类不该被方向键碰到的地方。
    if (r === 'moved' || r === 'after') return;
    moveFocus(1);
    return;
  }
  if (k === 'ArrowUp') {
    e.preventDefault();
    const r = moveInGrid(cols ? -cols : -1);
    if (r === 'moved') return;
    // 已经在第一行：上方的分组标题不算焦点，直接落到网格前一个元素
    moveFocus(-1);
    return;
  }
  if (k === 'ArrowRight') {
    e.preventDefault();
    // 菜单里左右与上下同义：一条竖着走的选项带，不做二维网格
    if (state.sheet) { moveFocus(1); return; }
    // 网格里左右就是「换一个卡片」，不离开主区域；到末张就停住
    if (moveInGrid(1) !== 'nogrid') return;
    if (state.focus.zone === 'sidebar') { state.focus = { zone: 'main', i: 0 }; applyFocus(); }
    return;
  }
  if (k === 'ArrowLeft') {
    e.preventDefault();
    if (state.sheet) { moveFocus(-1); return; }
    const r = moveInGrid(-1);
    if (r === 'moved') return;              // 网格内已经左移一格
    // 'before'（网格首张）与 'nogrid'（列表区）都回侧栏
    if (r !== 'nogrid' || state.focus.zone === 'main') {
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

  $('#themeBtn').onclick = () => setTheme(THEMES[(THEMES.indexOf(state.theme) + 1) % THEMES.length]);
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
