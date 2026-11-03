# WebDAV 视频源插件示例

这是一个**可实际工作**的 DeckPlayer 插件参考实现，展示如何用纯 C ABI
接入一个真实的网络媒体源。

适用服务：Alist、群晖 WebDAV、坚果云、Nextcloud、任意标准 WebDAV 服务器。

---

## 这个示例演示了什么

1. **用纯 C ABI 实现视频源扩展点**——不传任何 C++ 对象
2. **通过宿主 `http_request` 访问网络**——插件自身不链接 libcurl
3. **解析 WebDAV PROPFIND 的 XML 响应**——不引入 XML 库
4. **Basic 鉴权的生成与传递**
5. **把逻辑路径解析为播放直链**（含内嵌凭证，因为 mpv 只接受 URL）
6. **错误处理**：鉴权失败、路径不存在、未配置，各有明确返回码
7. **内存所有权**：谁分配谁释放，配 `free_list`

---

## 编译

```bash
clang -shared -fPIC -I <deck-player>/include/plugin \
      -o webdav.dylib examples/webdav_plugin/webdav_plugin.c
```

Linux 上把输出后缀改为 `.so`。

## 安装

```bash
mkdir -p ~/.config/deck-player/plugins
cp webdav.dylib ~/.config/deck-player/plugins/
```

然后在 DeckPlayer 里进入「存储位置 → 插件管理 → 重新扫描」（或按 Y）。

## 配置

通过环境变量（真实产品中应由宿主通过 `config_json` 传入）：

| 变量 | 说明 | 示例 |
| :--- | :--- | :--- |
| `DECKPLAYER_WEBDAV_URL` | 服务器地址 | `http://192.168.1.10:5244/dav` |
| `DECKPLAYER_WEBDAV_USER` | 用户名（可空） | `alist` |
| `DECKPLAYER_WEBDAV_PASS` | 密码（可空） | `secret` |

启动示例：

```bash
DECKPLAYER_WEBDAV_URL=http://192.168.1.10:5244/dav \
DECKPLAYER_WEBDAV_USER=alist \
DECKPLAYER_WEBDAV_PASS=secret \
./deck-player
```

未设置 `DECKPLAYER_WEBDAV_URL` 时，插件会正常加载但报告「需要配置」，
不会导致主程序出错。

---

## 实测验证

本插件用真实的 WebDAV 服务端验证过完整链路
（自制最小 PROPFIND 服务，目录结构含视频、子目录、非媒体文件）。

```
== 导出与版本 ==
  PASS 导出入口符号
  PASS ABI 版本一致
  PASS 提供视频源扩展点

== 未配置时优雅降级 ==
  [plugin L1] WebDAV: 未设置 DECKPLAYER_WEBDAV_URL
  PASS 无配置时 init 返回非 OK
  PASS needs_config 返回真

== 列出子目录并过滤非媒体 ==
    条目数=3
      [0] a.mp4 type=2      ← 视频
      [1] b.mkv type=2      ← 视频
      [2] sub   type=1      ← 目录
  PASS 非媒体文件 note.txt 被过滤
  PASS 识别出 2 个视频

== 解析播放地址 ==
      http://testuser:testpass@127.0.0.1:18080/movies/a.mp4
  PASS URL 中内嵌了鉴权信息
  PASS URL 包含文件名

== 错误处理 ==
  PASS 错误密码返回 AUTH 错误
  PASS 不存在的路径返回失败

RESULT: PASS WebDAV 插件端到端可用
```

同时用 **AddressSanitizer** 跑过，无内存错误。
在主程序中加载也正常（未配置时给出清晰提示，不影响其他插件）。

---

## 开发中踩到的坑（供插件作者参考）

### XML 必须在「当前 response 块内」查找

最初用全文查找判断 `<D:collection>`，结果**某个目录出现在后面时，
前面的所有文件都被误判成目录**。

原因：`find_ci(body, "<d:collection")` 会一直搜到文档末尾，
匹配到的是**后续条目**的标签。

修复：所有属性判断都必须限定在当前 `<D:response>...</D:response>` 范围内
（见源码中的 `find_ci_range`）。

这是实测才发现的——只看代码会觉得逻辑没问题。

### 播放地址要内嵌鉴权

libmpv 只接受一个 URL 字符串，无法传递自定义 HTTP 头。
所以带鉴权的源必须把凭证写进 URL：

```
http://user:pass@host/path/file.mp4
```

### 命名空间前缀不统一

不同服务器返回的前缀不同：`<D:response>`、`<d:response>`、
`<lp1:response>`，甚至无前缀。查找时应做大小写不敏感匹配并准备多种候选。

### 跳过父目录自身

PROPFIND 的 Depth:1 响应里会包含请求目录本身（href 就是请求路径）。
不处理会多出一个同名条目，用户点击后原地打转。

---

## 与生产实现的差距

这是一个**参考实现**，不是成品。要用于实际产品还需要：

- **配置界面**：目前只能通过环境变量，应改为宿主传入 `config_json`
- **HTTPS 证书校验策略**：自签名证书的 NAS 场景需要处理
- **大目录分页**：条目极多时的内存与渲染考虑
- **凭证安全**：URL 内嵌密码会出现在进程列表与日志里，
  生产环境应改用一次性 token 或代理
- **缓存**：目录列表可以短期缓存，避免每次进目录都发 PROPFIND
