import base64, re, subprocess, sys, time, urllib.request

PORT = "/dev/cu.usbmodem2101"
W, H = 400, 300
PLANE = W // 8 * H


def fetch(host, page=0):
    url = "http://%s/panel.bin?page=%d" % (host, page)
    with urllib.request.urlopen(url, timeout=20) as answer:
        raw = answer.read()
    if len(raw) != PLANE * 2:
        raise SystemExit("expected %d bytes, got %d" % (PLANE * 2, len(raw)))
    return raw[:PLANE], raw[PLANE:]


def capture(commands, settle=4.0, boot_wait=0.0):
    import serial

    link = serial.Serial(PORT, 115200, timeout=1)
    deadline = time.time() + boot_wait
    while time.time() < deadline:
        link.read(4096)
    trigger = commands.pop() if commands and commands[-1] in "dsh" else "d"
    for command in commands:
        link.write(command.encode())
        link.flush()
        time.sleep(settle)
        link.read(262144)
    link.write(trigger.encode())
    link.flush()
    buf = b""
    deadline = time.time() + 25
    while time.time() < deadline:
        buf += link.read(8192)
        if b"--DUMP-END--" in buf:
            break
    link.close()
    text = re.sub(r"\x1b\[[0-9;]*m", "", buf.decode("utf-8", "replace"))
    if "--DUMP-BEGIN--" not in text:
        raise SystemExit("no dump captured:\n" + text[-1200:])
    body = text.split("--DUMP-BEGIN--")[1].split("--DUMP-END--")[0]
    planes = []
    for part in body.split("--PLANE-")[1:]:
        payload = part.split("\n", 1)[1]
        lines = [l.strip() for l in payload.splitlines() if re.fullmatch(r"[A-Za-z0-9+/=]{4,}", l.strip())]
        planes.append(b"".join(base64.b64decode(l) for l in lines))
    return planes


PAPER = (255, 255, 255)
INK = (26, 26, 26)
ACCENT = (198, 40, 40)


def write_png(planes, path, scale=2):
    black, red = planes[0], planes[1]
    ppm = path.replace(".png", ".ppm")
    with open(ppm, "wb") as handle:
        handle.write(b"P6\n%d %d\n255\n" % (W * scale, H * scale))
        for y in range(H):
            row = bytearray()
            for x in range(W):
                at, bit = y * (W // 8) + (x >> 3), 7 - (x & 7)
                if (red[at] >> bit) & 1:
                    pixel = ACCENT
                elif (black[at] >> bit) & 1:
                    pixel = INK
                else:
                    pixel = PAPER
                row.extend(bytes(pixel) * scale)
            for _ in range(scale):
                handle.write(bytes(row))
    subprocess.run(["sips", "-s", "format", "png", ppm, "--out", path],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["rm", ppm], check=False)
    print("wrote", path)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "preview/overview.png"
    second = sys.argv[2] if len(sys.argv) > 2 else ""
    if second.endswith(".bin"):
        with open(second, "rb") as handle:
            raw = handle.read()
        write_png((raw[:PLANE], raw[PLANE:]), out)
    elif second.startswith("http") or "." in second:
        host = second.removeprefix("http://").rstrip("/")
        page = int(sys.argv[3]) if len(sys.argv) > 3 else 0
        write_png(fetch(host, page), out)
    else:
        wait = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
        write_png(capture(list(second), boot_wait=wait), out)
