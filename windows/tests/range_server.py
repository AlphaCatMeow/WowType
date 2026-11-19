"""下载续传测试用的 HTTP 服务器（CI 在 Windows 上起，配 wowtype-windows-download-test）。

    python range_server.py <端口> <把内容也写到这个文件>

内容是固定的 3 MB。几个路径：
  /flaky   认 Range；每次最多给 700 KB 就掐断连接（一次下载里要自动接好几次）
  /norange 不认 Range（总是 200 从头给）；第一次给 1 MB 就掐断，之后给全
  /broken  认 Range；每次只给 300 KB 就掐断（一次调用接 6 次也下不完，留着 .part，下一次调用接着下）
"""

import socket
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SIZE = 3_000_000
DATA = bytes((i * 7 + 3) % 256 for i in range(SIZE))
LIMITS = {"/flaky": 700_000, "/broken": 300_000}
served = {"/norange": 0}
lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("range-server: %s %s\n" % (self.path, self.headers.get("Range", "-")))

    def cut(self, body: bytes, limit: int) -> None:
        self.wfile.write(body[:limit])
        self.wfile.flush()
        if limit < len(body):
            # 掐断：客户端收到的比 Content-Length 少
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self.close_connection = True

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/norange":
            with lock:
                first = served["/norange"] == 0
                served["/norange"] += 1
            self.send_response(200)
            self.send_header("Content-Length", str(SIZE))
            self.end_headers()
            self.cut(DATA, 1_000_000 if first else SIZE)
            return
        if path not in LIMITS:
            self.send_error(404)
            return
        start = 0
        header = self.headers.get("Range")
        if header and header.startswith("bytes=") and header.endswith("-"):
            start = int(header[len("bytes="):-1])
            if start >= SIZE:
                self.send_response(416)
                self.send_header("Content-Range", "bytes */%d" % SIZE)
                self.end_headers()
                return
            self.send_response(206)
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, SIZE - 1, SIZE))
        else:
            self.send_response(200)
        body = DATA[start:]
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.cut(body, LIMITS[path])


def main() -> None:
    port = int(sys.argv[1])
    with open(sys.argv[2], "wb") as f:
        f.write(DATA)
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
