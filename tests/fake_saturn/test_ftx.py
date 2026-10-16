"""ftx tests against a fake Saturn (fakesat.so, an LD_PRELOAD stand-in for libftdi).

usage: python3 test_ftx.py <ftx binary, dynamically linked to libftdi> <fakesat.so>

Covers the code paths that write or delete files (--get, --sync, the WebDAV server,
interrupted uploads) and the GDB proxy's session handling. Exits non-zero if any check fails.
"""
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

FTX = os.path.abspath(sys.argv[1])
FAKESAT = os.path.abspath(sys.argv[2])
PORT = 18080
NO_RTC = time.mktime((2018, 1, 1, 0, 0, 0, 0, 0, -1))  # SRL_HostIO's fixed file stamp

failures = []
work = tempfile.mkdtemp(prefix="ftx-test-")
sd = os.path.join(work, "sd")
loc = os.path.join(work, "loc")


def check(name, ok, detail=""):
    print("%s  %s%s" % ("PASS" if ok else "FAIL", name, ("  (%s)" % detail) if detail else ""))
    if not ok:
        failures.append(name)


def put(root, name, data, mtime=None):
    path = os.path.join(root, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    if mtime is not None:
        os.utime(path, (mtime, mtime))


def get(root, name):
    try:
        with open(os.path.join(root, name), "rb") as f:
            return f.read()
    except OSError:
        return None


def env(**extra):
    e = dict(os.environ, LD_PRELOAD=FAKESAT, FAKE_SD=sd)
    e.update({k: str(v) for k, v in extra.items()})
    return e


def ftx(*args, **extra):
    r = subprocess.run([FTX] + list(args), env=env(**extra), capture_output=True, text=True, timeout=120)
    return r.returncode, r.stdout + r.stderr


# --- --get: a failed download never damages the existing local file ------------------
put(sd, "big.bin", os.urandom(100000))
put(loc, "keep.txt", b"ORIGINAL")
rc, _ = ftx("--get", "/big.bin", os.path.join(loc, "keep.txt"), FAKE_CUT=5000)
check("--get cut mid-transfer keeps the local file", rc != 0 and get(loc, "keep.txt") == b"ORIGINAL"
      and not os.path.exists(os.path.join(loc, "keep.txt.ftxpart")))
rc, _ = ftx("--get", "/missing.bin", os.path.join(loc, "keep.txt"))
check("--get of a missing file keeps the local file", rc != 0 and get(loc, "keep.txt") == b"ORIGINAL")
rc, _ = ftx("--get", "/big.bin", os.path.join(loc, "keep.txt"))
check("--get replaces the file on success", rc == 0 and get(loc, "keep.txt") == get(sd, "big.bin"))

# --- --sync mode 3 --------------------------------------------------------------------
hour_ago = time.time() - 7200
put(loc, "d/a.txt", b"old", hour_ago)
put(sd, "d/a.txt", b"newer saturn content")
rc, _ = ftx("--sync", os.path.join(loc, "d"), "/d", "3", FAKE_CUT=3)
check("sync: cut download keeps the local file", rc != 0 and get(loc, "d/a.txt") == b"old")
rc, _ = ftx("--sync", os.path.join(loc, "d"), "/d", "3")
check("sync: newer Saturn copy wins, old one kept as .ftxbak",
      rc == 0 and get(loc, "d/a.txt") == b"newer saturn content" and get(loc, "d/a.txt.ftxbak") == b"old")
put(loc, "d/x.bin.ftxpart", b"junk")
rc, out = ftx("--sync", os.path.join(loc, "d"), "/d", "3")
check("sync: .ftxbak/.ftxpart leftovers are never pushed", rc == 0 and "cmd=4" not in out)

put(loc, "e/same.sav", b"SAVE-A"); put(sd, "e/same.sav", b"SAVE-A")
put(loc, "e/edit.sav", b"SAVE-A"); put(sd, "e/edit.sav", b"SAVE-B", NO_RTC)
put(loc, "e/grow.sav", b"old"); put(sd, "e/grow.sav", b"longer", NO_RTC)
put(loc, "e/pc.sav", b"PC-A", hour_ago); put(sd, "e/pc.sav", b"PC-B")
rc, out = ftx("--sync", os.path.join(loc, "e"), "/e", "3")
check("sync: same size and CRC is left alone", get(sd, "e/same.sav") == b"SAVE-A")
check("sync: same-size edit by the Saturn is a conflict",
      "Conflict for edit.sav" in out and get(sd, "e/edit.sav") == b"SAVE-B" and get(loc, "e/edit.sav") == b"SAVE-A")
check("sync: size change by the Saturn is a conflict",
      "Conflict for grow.sav" in out and get(sd, "e/grow.sav") == b"longer")
check("sync: same size, really newer on the Saturn, is pulled",
      get(loc, "e/pc.sav") == b"PC-B" and get(loc, "e/pc.sav.ftxbak") == b"PC-A")

for i in range(40):
    put(sd, "full/SAVE%02d.BIN" % i, b"%d" % i)
    put(loc, "full/SAVE%02d.BIN" % i, b"LOCAL-%d" % i)
rc, _ = ftx("--sync", os.path.join(loc, "full"), "/full", "3")
untouched = all(get(sd, "full/SAVE%02d.BIN" % i) == b"%d" % i for i in range(40))
check("sync: a cut-short Saturn listing stops the sync", rc != 0 and untouched)

# --- interrupted upload ----------------------------------------------------------------
put(loc, "prog.bin", os.urandom(512 * 1024))
p = subprocess.Popen([FTX, "-u", os.path.join(loc, "prog.bin"), "0x6004000"],
                     env=env(FAKE_SINK=1, FAKE_WRITE_DELAY_US=100000),
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.5)
p.send_signal(signal.SIGINT)
check("Ctrl-C during an upload exits with an error", p.wait(30) != 0)


# --- GDB proxy (ftx -g) ----------------------------------------------------------------
GDB_PORT = PORT + 1


def start_proxy(**extra):
    proc = subprocess.Popen([FTX, "-g", str(GDB_PORT)], env=env(**extra),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(100):
        try:
            return proc, socket.create_connection(("127.0.0.1", GDB_PORT), 0.1)
        except OSError:
            time.sleep(0.1)
    proc.kill()
    raise SystemExit("GDB proxy did not start")


def read_for(sock, seconds):
    sock.settimeout(0.1)
    data, end = b"", time.time() + seconds
    while time.time() < end:
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
        except socket.timeout:
            pass
    return data


# The target answers 300 ms late, after client A has already hung up.
proxy, client_a = start_proxy(FAKE_ECHO_DELAY_MS=300)
client_a.sendall(b"$stale#00")
client_a.close()
time.sleep(0.8)
client_b = socket.create_connection(("127.0.0.1", GDB_PORT), 2)
early = read_for(client_b, 0.5)
client_b.sendall(b"$fresh#00")
answer = read_for(client_b, 1.5)
client_b.close()
proxy.send_signal(signal.SIGINT)
proxy.wait(10)
check("GDB proxy: a late reply for the previous client never reaches the next one",
      early == b"" and answer == b"$fresh#00", "got %r then %r" % (early, answer))

proxy, client = start_proxy(FAKE_READ_FAIL=1)
try:
    rc = proxy.wait(5)
except subprocess.TimeoutExpired:
    proxy.kill()
    rc = None
client.close()
check("GDB proxy: exits with an error when the cartridge stops answering", rc == 1, "exit code %r" % rc)


# --- WebDAV ----------------------------------------------------------------------------
def start_server(**extra):
    try:
        socket.create_connection(("127.0.0.1", PORT), 0.2).close()
        raise SystemExit("port %d is already in use (a leftover ftx -wd?)" % PORT)
    except OSError:
        pass
    log = open(os.path.join(work, "server.log"), "a")
    proc = subprocess.Popen([FTX, "-wd", str(PORT)], env=env(**extra), stdout=log, stderr=log)
    for _ in range(100):
        try:
            socket.create_connection(("127.0.0.1", PORT), 0.1).close()
            return proc
        except OSError:
            time.sleep(0.1)
    raise SystemExit("WebDAV server did not start")


def stop_server(proc):
    proc.send_signal(signal.SIGINT)
    try:
        return proc.wait(10)
    except subprocess.TimeoutExpired:
        proc.kill()
        return None


def http(method, target, headers=None, body=b""):
    s = socket.create_connection(("127.0.0.1", PORT), 60)
    head = "%s %s HTTP/1.1\r\nHost: localhost\r\nContent-Length: %d\r\n" % (method, target, len(body))
    for k, v in (headers or {}).items():
        head += "%s: %s\r\n" % (k, v)
    data = b""
    try:
        s.sendall(head.encode() + b"\r\n" + body)
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    except OSError:
        pass  # the server dropped the connection: reported as status 0
    s.close()
    if not data:
        return 0, b""
    head, _, payload = data.partition(b"\r\n\r\n")
    return int(head.split(b" ", 2)[1]), payload


def raw_ops():
    return open(os.path.join(work, "server.log")).read().count("[fakesat] RAW")


servers = []
_start = start_server


def start_server(**extra):
    proc = _start(**extra)
    servers.append(proc)
    return proc


try:
    srv = start_server()
    check("WebDAV: DELETE sdraw:0:64 is refused", http("DELETE", "sdraw:0:64")[0] == 400 and raw_ops() == 0)
    check("WebDAV: PUT sdraw:0 is refused", http("PUT", "sdraw:0", body=b"x" * 512)[0] == 400 and raw_ops() == 0)
    put(sd, "a.sav", b"AAAA")
    st, _ = http("MOVE", "/a.sav", {"Destination": "sdraw:0:1"})
    check("WebDAV: MOVE to sdraw: is refused", st == 400 and get(sd, "a.sav") == b"AAAA" and raw_ops() == 0)
    lan_ip = next((ip for ip in subprocess.run(["hostname", "-I"], capture_output=True, text=True).stdout.split()
                   if not ip.startswith("127.")), None)
    if lan_ip:
        try:
            socket.create_connection((lan_ip, PORT), 1).close()
            reachable = True
        except OSError:
            reachable = False
        check("WebDAV: not reachable from the network", not reachable, lan_ip)

    put(sd, "game.sav", b"SAVEDATA")
    st, _ = http("MOVE", "/game.sav", {"Destination": "http://localhost:%d/GAME.SAV" % PORT})
    check("WebDAV: case-only rename keeps the file", st == 204 and get(sd, "GAME.SAV") == b"SAVEDATA")
    put(sd, "b.sav", b"BBBB")
    check("WebDAV: MOVE onto itself is refused", http("MOVE", "/b.sav", {"Destination": "/b.sav"})[0] == 403
          and get(sd, "b.sav") == b"BBBB")
    check("WebDAV: COPY onto itself is refused", http("COPY", "/b.sav", {"Destination": "/B.SAV"})[0] == 403
          and get(sd, "b.sav") == b"BBBB")
    put(sd, "src.sav", b"SOURCE!!")
    put(sd, "dst.sav", b"DEST")
    st, _ = http("COPY", "/src.sav", {"Destination": "/dst.sav"})
    check("WebDAV: COPY over an existing file", st == 204 and get(sd, "dst.sav") == b"SOURCE!!")

    big = os.urandom(1536 * 1024)
    st, _ = http("PUT", "/BIG.BIN", body=big)
    check("WebDAV: PUT of a 1.5 MB file", st == 201 and get(sd, "BIG.BIN") == big, "status %d" % st)
    st, body = http("GET", "/BIG.BIN")
    check("WebDAV: GET returns the file", st == 200 and body == big)

    st, _ = http("PROPFIND", "/full", {"Depth": "1"})
    check("WebDAV: listing a folder that was cut short fails", st == 500, "status %d" % st)
    results = [http("GET", "/a.sav") for _ in range(3)]
    check("WebDAV: replies stay in step after a cut-short listing",
          all(r == (200, b"AAAA") for r in results), str([r[0] for r in results]))

    idle = socket.create_connection(("127.0.0.1", PORT))  # connects and never sends a request
    t0 = time.time()
    stopped = stop_server(srv)
    check("WebDAV: Ctrl-C works while a client sits idle", stopped is not None and time.time() - t0 < 5)
    idle.close()
    srv = start_server()
    idle = socket.create_connection(("127.0.0.1", PORT))
    t0 = time.time()
    st, _ = http("GET", "/a.sav")
    check("WebDAV: an idle client does not block others for long", st == 200 and time.time() - t0 < 40,
          "%.0f s" % (time.time() - t0))
    idle.close()
    stop_server(srv)

    put(sd, "dst.sav", b"DEST")
    srv = start_server(FAKE_CUT=3)
    st, _ = http("COPY", "/src.sav", {"Destination": "/dst.sav"})
    check("WebDAV: COPY with a failed download keeps the destination", st == 500 and get(sd, "dst.sav") == b"DEST")
    stop_server(srv)
finally:
    for proc in servers:
        if proc.poll() is None:
            proc.kill()

shutil.rmtree(work)
print("\n%d check(s) failed" % len(failures) if failures else "\nall checks passed")
sys.exit(1 if failures else 0)
