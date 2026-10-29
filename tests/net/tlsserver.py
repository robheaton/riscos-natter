#!/usr/bin/env python3
"""tlsserver.py PORT [CERT KEY]: a small server for the tests of net.c.  With CERT and KEY it speaks TLS, without them plain TCP.
On connecting it sends ':srv NOTICE * :hello'.  'PING :x' is answered with ':srv PONG srv :x'.  'BULK n' sends n bytes 'x' and then 'OK'.
'SEND' starts a count of the bytes received up to a line 'END', then it answers 'GOT n'.  'QUIT' closes."""
import socket, ssl, sys, threading

def handle(conn):
    try:
        conn.sendall(b":srv NOTICE * :hello\r\n")
        buf = b""
        counting = False
        count = 0
        while True:
            data = conn.recv(65536)
            if not data:
                return
            if counting:
                buf += data
                while True:
                    i = buf.find(b"\r\nEND\r\n")
                    if i >= 0:
                        count += i
                        conn.sendall(b"GOT %d\r\n" % count)
                        buf = buf[i + 7:]
                        counting = False
                        count = 0
                        break
                    keep = 6                      # what could be the start of the marker stays
                    if len(buf) > keep:
                        count += len(buf) - keep
                        buf = buf[-keep:]
                    break
                continue
            buf += data
            while b"\r\n" in buf:
                line, buf = buf.split(b"\r\n", 1)
                if line.startswith(b"PING "):
                    conn.sendall(b":srv PONG srv " + line[5:] + b"\r\n")
                elif line.startswith(b"BULK "):
                    n = int(line[5:])
                    conn.sendall(b"x" * n + b"\r\nOK\r\n")
                elif line == b"SEND":
                    counting = True
                    count = 0
                    break
                elif line == b"QUIT":
                    return
    except (ssl.SSLError, ConnectionError, OSError):
        pass
    finally:
        try:
            conn.close()
        except OSError:
            pass

def main():
    port = int(sys.argv[1])
    ctx = None
    if len(sys.argv) >= 4:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(sys.argv[2], sys.argv[3])
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port))
    s.listen(8)
    print("listening", port, flush=True)
    while True:
        c, _ = s.accept()
        if ctx:
            try:
                c = ctx.wrap_socket(c, server_side=True)
            except (ssl.SSLError, OSError):
                c.close()
                continue
        threading.Thread(target=handle, args=(c,), daemon=True).start()

main()
