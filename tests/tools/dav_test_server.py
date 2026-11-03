#!/usr/bin/env python3
"""最小 WebDAV 测试服务端。

只实现 PROPFIND（Depth: 1），用于验证 DeckPlayer 的 WebDAV 插件。
不实现 PROPPATCH / PUT / DELETE 等写操作——本插件只需要读。

用法：
    python3 tests/tools/dav_test_server.py <根目录> [端口] [用户名] [密码]

示例：
    python3 tests/tools/dav_test_server.py /tmp/davroot 18080 testuser testpass
"""

import base64
import http.server
import os
import sys
from urllib.parse import unquote


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class DavHandler(http.server.BaseHTTPRequestHandler):
    root = "/tmp/davroot"
    user = "testuser"
    password = "testpass"

    def log_message(self, *args):  # 静默，避免污染测试输出
        pass

    def _authorized(self) -> bool:
        auth = self.headers.get("Authorization", "")
        expect = "Basic " + base64.b64encode(
            f"{self.user}:{self.password}".encode()
        ).decode()
        return auth == expect

    def do_PROPFIND(self):  # noqa: N802 (HTTP 方法名)
        if not self._authorized():
            self.send_response(401)
            self.send_header("WWW-Authenticate", 'Basic realm="dav"')
            self.end_headers()
            return

        path = unquote(self.path.split("?")[0])
        fpath = os.path.join(self.root, path.lstrip("/"))

        if not os.path.exists(fpath):
            self.send_response(404)
            self.end_headers()
            return
        if not os.path.isdir(fpath):
            self.send_response(405)
            self.end_headers()
            return

        # Depth:1 响应包含自身与直接子项
        entries = [("/", "/")]
        try:
            names = sorted(os.listdir(fpath))
        except OSError:
            names = []
        for name in names:
            full = os.path.join(fpath, name)
            href = path.rstrip("/") + "/" + name
            if os.path.isdir(full):
                href += "/"
            entries.append((href, name))

        body = [
            '<?xml version="1.0" encoding="utf-8"?>',
            '<D:multistatus xmlns:D="DAV:">',
        ]
        for href, name in entries:
            body.append("<D:response>")
            body.append(f"<D:href>{esc(href)}</D:href>")
            body.append(
                "<D:propstat><D:prop>"
                f"<D:displayname>{esc(name)}</D:displayname>"
            )
            if href.endswith("/"):
                body.append("<D:resourcetype><D:collection/></D:resourcetype>")
            else:
                body.append("<D:resourcetype/>")
            body.append(
                "</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat>"
            )
            body.append("</D:response>")
        body.append("</D:multistatus>")

        data = "\n".join(body).encode("utf-8")
        self.send_response(207)  # Multi-Status
        self.send_header("Content-Type", "application/xml; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    DavHandler.root = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 18080
    if len(sys.argv) > 3:
        DavHandler.user = sys.argv[3]
    if len(sys.argv) > 4:
        DavHandler.password = sys.argv[4]

    if not os.path.isdir(DavHandler.root):
        print(f"根目录不存在: {DavHandler.root}", file=sys.stderr)
        sys.exit(1)

    srv = http.server.HTTPServer(("127.0.0.1", port), DavHandler)
    print(f"WebDAV 测试服务: http://127.0.0.1:{port} 根={DavHandler.root}")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
