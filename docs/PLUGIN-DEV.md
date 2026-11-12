# Muvik 插件开发指南 (Plugin Developer Guide)

Muvik 提供了基于 **QuickJS** 的轻量级 JavaScript 插件沙盒系统。无需配置复杂的编译工具链，开发者只需使用简单的 JavaScript 即可编写**自定义视频源**与**影视刮削器（Scraper）**。

---

## 目录

- [一、插件基本结构](#一插件基本结构)
- [二、元数据声明：manifest.json](#二元数据声明manifestjson)
- [三、刮削器插件开发 (Scraper)](#三刮削器插件开发-scraper)
- [四、视频源插件开发 (Video Source)](#四视频源插件开发-video-source)
- [五、宿主内置 API (Host APIs)](#五宿主内置-api-host-apis)
- [六、本地调试、打包与安装](#六本地调试打包与安装)

---

## 一、插件基本结构

一个标准的 Muvik JS 插件为一个独立文件夹（或 ZIP 压缩包），其根目录包含两个核心文件：

```text
my_awesome_plugin/
├── manifest.json   # 插件元数据清单与配置声明
└── main.js          # 插件核心业务逻辑入口
```

---

## 二、元数据声明：manifest.json

`manifest.json` 描述了插件的身份、能力与用户可配置项：

```json
{
  "id": "com.example.tmdb-scraper",
  "name": "TMDB 影视刮削器",
  "version": "1.0.0",
  "description": "基于 TheMovieDb API 的影视海报与剧情简介在线刮削插件",
  "author": "YourName",
  "capabilities": ["scraper"],
  "main": "main.js",
  "configSchema": [
    {
      "key": "api_key",
      "title": "TMDB API Key",
      "type": "string",
      "default": "",
      "description": "填写您在 TMDB 申请的个人 API Key"
    },
    {
      "key": "language",
      "title": "元数据语言",
      "type": "string",
      "default": "zh-CN",
      "description": "如 zh-CN, en-US"
    }
  ]
}
```

### 字段说明
* `id` (string, 必须): 插件唯一包名（推荐反向域名格式，如 `com.author.plugin_name`）。
* `name` (string, 必须): 界面展示的插件名称。
* `version` (string, 必须): 语义化版本号，如 `1.0.0`。
* `capabilities` (array, 必须): 声明插件能力，可包含：
  * `"scraper"`: 刮削器插件（提供在线影视信息与海报拉取）；
  * `"video_source"`: 视频源插件（提供目录树浏览与视频流播放）。
* `main` (string, 可选): 入口 JS 文件，默认为 `"main.js"`。
* `configSchema` (array, 可选): 声明插件的自定义设置项。Muvik 会在“设置 -> 插件管理”中自动生成对应的可视化配置界面。

---

## 三、刮削器插件开发 (Scraper)

声明了 `"capabilities": ["scraper"]` 的插件，需在 `main.js` 中实现以下两个核心导出函数：

### 1. `scrapeSearch(query, year)`

当用户触发刮削或进入未匹配影片时，Muvik 会清洗文件名（去除 `1080p`、`x264` 等压制信息），并将搜索词传入该函数。

* **参数**：
  * `query` (string): 搜索关键词（如 `"奥本海默"`）。
  * `year` (number): 从文件名提取出的年份（若未能识别则为 `0`）。
* **返回值**：`Promise<Array<{ id: string, title: string, year: number }>>`
  * 必须返回候选列表数组，每个条目包含唯一的 `id`、片名 `title` 及上映年份 `year`。

```javascript
async function scrapeSearch(query, year) {
    const apiKey = host.getConfig("api_key");
    const resp = host.http({
        url: `https://api.themoviedb.org/3/search/movie?api_key=${apiKey}&query=${encodeURIComponent(query)}`,
        method: "GET"
    });
    if (resp.status !== 200) return [];
    
    const data = JSON.parse(resp.body);
    return (data.results || []).map(item => ({
        id: String(item.id),
        title: item.title,
        year: item.release_date ? parseInt(item.release_date.slice(0, 4)) : 0
    }));
}
```

### 2. `scrapeDetail(id)`

根据用户选择（或搜索命中的首选条目）的 `id`，获取该条目的完整海报与元数据详情。

* **参数**：
  * `id` (string): `scrapeSearch` 返回的条目唯一标识。
* **返回值**：`Promise<Object>` 详情对象：
  * `title` (string): 正式影片名称；
  * `overview` (string): 剧情简介；
  * `poster_url` (string, 可选): 竖版海报图片直链；
  * `backdrop_url` (string, 可选): 横版背景剧照直链；
  * `release_date` (string, 可选): 上映日期，如 `"2023-07-21"`；
  * `runtime_minutes` (number, 可选): 片长（分钟）；
  * `rating` (number, 可选): 评分（0.0 ~ 10.0）；
  * `genres` (string, 可选): 类型标签，如 `"剧情, 历史"`。

```javascript
async function scrapeDetail(id) {
    const apiKey = host.getConfig("api_key");
    const resp = host.http({
        url: `https://api.themoviedb.org/3/movie/${id}?api_key=${apiKey}&language=zh-CN`,
        method: "GET"
    });
    if (resp.status !== 200) return null;
    
    const data = JSON.parse(resp.body);
    return {
        title: data.title,
        overview: data.overview,
        release_date: data.release_date,
        runtime_minutes: data.runtime || 0,
        rating: data.vote_average || 0.0,
        genres: (data.genres || []).map(g => g.name).join(", "),
        poster_url: data.poster_path ? `https://image.tmdb.org/t/p/w500${data.poster_path}` : ""
    };
}
```

---

## 四、视频源插件开发 (Video Source)

声明了 `"capabilities": ["video_source"]` 的插件，可以向 Muvik 的“文件 -> 插件视频源”提供远程目录浏览或流媒体解析：

### 1. `listDirectory(path)`
* 浏览远程目录树，返回文件夹和文件列表：
```javascript
async function listDirectory(path) {
    return {
        directories: [
            { name: "动作电影", path: "/movies/action" }
        ],
        files: [
            { name: "示例短片.mp4", path: "/movies/sample.mp4", size: 10485760, type: "video" }
        ]
    };
}
```

### 2. `resolvePlayUrl(path)`
* 当用户点击文件时解析为真实播放地址（由底层 libmpv 直接播放）：
```javascript
async function resolvePlayUrl(path) {
    return {
        url: "https://example.com/stream.m3u8",
        headers: { "Referer": "https://example.com" }
    };
}
```

---

## 五、宿主内置 API (Host APIs)

Muvik 为沙盒环境注入了 `host` 全局对象，无需引入任何第三方 npm 包即可直接使用底层 C++ 高性能服务：

### 1. `host.http(options)` —— 异步 HTTP 请求
```javascript
const resp = host.http({
    url: "https://api.example.com/data",
    method: "GET",                 // GET, POST, HEAD, PUT 等
    headers: {
        "Authorization": "Bearer token",
        "User-Agent": "MyPlugin/1.0"
    },
    body: JSON.stringify({ key: "val" }), // POST 请求体
    timeout: 10                           // 超时时间（秒，默认 15，最大 120）
});

console.log(resp.status);     // HTTP 状态码，如 200
console.log(resp.statusText); // "OK" 或 "Error"
console.log(resp.body);       // 响应体文本字符串
console.log(resp.headers);    // 响应头小写字典
```

### 2. `host.getConfig(key)` —— 读取插件用户设置
获取用户在界面中配置的参数（对应 `manifest.json` 中的 `configSchema`）：
```javascript
const userKey = host.getConfig("api_key");
```

### 3. `host.storage` —— 插件独立持久化存储
每个插件独享隔离的键值对持久化空间，存取 Token 或临时缓存：
```javascript
host.storage.set("auth_token", "abc123xyz");
const token = host.storage.get("auth_token");
host.storage.remove("auth_token");
```

### 4. `host.crypto` —— 加密与哈希工具
方便与各类签名 API 对接：
```javascript
host.crypto.md5("string");
host.crypto.sha256("string");
host.crypto.hmacSha256("data", "secret_key");
host.crypto.base64Encode("text");
host.crypto.base64Decode("base64_str");
```

### 5. `console` —— 日志输出
在控制台及 Muvik 日志输出调试信息：
```javascript
console.log("调试信息:", value);
console.error("错误记录:", err);
```

---

## 六、本地调试、打包与安装

### 1. 本地免安装调试
开发时无需打包 ZIP，直接将开发文件夹放置在 Muvik 的本地插件目录即可自动识别并热加载：

* **Linux / Steam Deck**: `~/.config/muvik/plugins/<插件名>/`
* **macOS**: `~/.config/muvik/plugins/<插件名>/`
* **Windows**: `%APPDATA%/muvik/plugins/<插件名>/`

### 2. 打包分发
插件开发完成后，将 `manifest.json` 与 `main.js` 打包为标准 ZIP 文件（注意：`manifest.json` 必须位于 ZIP 包根目录）：
```bash
zip -r my-scraper-v1.0.0.zip manifest.json main.js
```

### 3. 用户安装方式
用户打开 Muvik -> **设置** -> **插件管理** -> **从 ZIP 包安装插件**，选中制作好的 `.zip` 文件即可一键安装并启用。
