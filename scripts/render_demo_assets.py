#!/usr/bin/env python3
"""Render portfolio images from the captured EduPCI demo log.

The text drawn in demo screenshots is read verbatim from
evidence/public_demo_output.txt. This script only adds a terminal-style frame;
it does not synthesize or edit command output.
"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
LOG = ROOT / "evidence" / "public_demo_output.txt"
DEMO_DIR = ROOT / "assets" / "demo"
ARCH_DIR = ROOT / "assets" / "architecture"

BG = "#0b1020"
PANEL = "#111827"
TEXT = "#dbe4f0"
MUTED = "#8b9bb4"
BLUE = "#73c7ff"
GREEN = "#8fe3a1"
ORANGE = "#ffbf69"


def font(name: str, size: int) -> ImageFont.FreeTypeFont:
    paths = {
        "mono": "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "sans": "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "bold": "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    }
    return ImageFont.truetype(paths[name], size)


def log_sections(lines: list[str]) -> list[list[str]]:
    sections: list[list[str]] = []
    current: list[str] = []
    for line in lines:
        if line.startswith("[") and "/6]" in line:
            if current:
                sections.append(current)
            current = [line]
        elif current:
            current.append(line)
    if current:
        sections.append(current)
    return sections


def make_terminal(name: str, lines: list[str]) -> None:
    mono = font("mono", 22)
    width = max(1000, int(max((mono.getlength(line) for line in lines), default=0) + 72))
    line_height = 36
    top = 56
    bottom = 28
    height = top + max(1, len(lines)) * line_height + bottom
    image = Image.new("RGB", (width, height), BG)
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((12, 12, width - 12, height - 12), radius=18,
                           fill=PANEL, outline="#25314a", width=2)
    draw.line((13, 48, width - 13, 48), fill="#26334b", width=2)
    for x, color in ((35, "#ff6b6b"), (61, "#ffd166"), (87, "#5bd28b")):
        draw.ellipse((x - 7, 24, x + 7, 38), fill=color)
    for index, line in enumerate(lines):
        color = BLUE if line.startswith("[") and "/6]" in line else TEXT
        if line.endswith("PASS") or ": PASS" in line:
            color = GREEN
        draw.text((34, top + index * line_height), line, font=mono, fill=color)
    image.save(DEMO_DIR / name, optimize=True)


def rounded(draw: ImageDraw.ImageDraw, box, fill: str, outline: str = "#d2dce9",
            radius: int = 20, width: int = 3) -> None:
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline,
                           width=width)


def arrow(draw: ImageDraw.ImageDraw, start, end, color: str, width: int = 6,
          head: int = 18) -> None:
    draw.line((start, end), fill=color, width=width)
    x1, y1 = start
    x2, y2 = end
    if abs(x2 - x1) >= abs(y2 - y1):
        sign = 1 if x2 >= x1 else -1
        points = [(x2, y2), (x2 - sign * head, y2 - head // 2),
                  (x2 - sign * head, y2 + head // 2)]
    else:
        sign = 1 if y2 >= y1 else -1
        points = [(x2, y2), (x2 - head // 2, y2 - sign * head),
                  (x2 + head // 2, y2 - sign * head)]
    draw.polygon(points, fill=color)


def make_architecture() -> None:
    width, height = 1800, 1180
    image = Image.new("RGB", (width, height), "#f4f7fb")
    draw = ImageDraw.Draw(image)
    title = font("bold", 44)
    section = font("bold", 34)
    body = font("sans", 28)
    small = font("sans", 24)
    mono = font("mono", 23)

    draw.text((95, 45), "Execution environment and device path", font=title,
              fill="#16243a")

    rounded(draw, (280, 120, 1520, 260), "#e8eef8", outline="#90a4bf")
    draw.text((330, 145), "WSL2 host", font=section, fill="#1b3556")
    draw.text((330, 193), "Runs QEMU/KVM; the host kernel is not modified.",
              font=body, fill="#354a65")
    arrow(draw, (900, 263), (900, 325), "#607d9e", 6, 19)
    draw.text((955, 279), "QEMU/KVM launches Ubuntu guest", font=small,
              fill="#455d78")

    rounded(draw, (70, 330, 1730, 1110), "#ffffff", outline="#9aacc2", radius=28, width=4)
    draw.rounded_rectangle((72, 332, 1728, 402), radius=26, fill="#dceafa")
    draw.rectangle((72, 372, 1728, 402), fill="#dceafa")
    draw.text((110, 350), "QEMU/KVM Ubuntu guest — Linux driver runs here",
              font=section, fill="#173a5e")

    # User-space command and character device.
    rounded(draw, (120, 500, 445, 640), "#eef6ff", outline="#83a9d4")
    draw.text((150, 525), "User-space CLI", font=section, fill="#173a5e")
    draw.text((150, 572), "edu_cli", font=mono, fill="#35577b")
    rounded(draw, (120, 770, 445, 905), "#eef6ff", outline="#83a9d4")
    draw.text((150, 798), "/dev/edupci", font=section, fill="#173a5e")
    draw.text((150, 847), "misc char device", font=body, fill="#35577b")
    arrow(draw, (282, 642), (282, 765), "#4182b8", 6)
    draw.text((300, 680), "ioctl / read / poll", font=small, fill="#355d80")

    # Driver block.
    rounded(draw, (570, 470, 1160, 950), "#e9f7f4", outline="#62a99b", radius=26, width=4)
    draw.text((620, 505), "Linux PCI character driver", font=section,
              fill="#174c49")
    driver_lines = [
        "PCI probe / remove",
        "BAR0 MMIO  ·  readl / writel",
        "MSI IRQ  ·  completion",
        "64-event ring  ·  wait queue  ·  poll",
        "coherent DMA",
    ]
    for i, line in enumerate(driver_lines):
        draw.rounded_rectangle((620, 575 + i * 65, 1110, 625 + i * 65),
                               radius=12, fill="#ffffff", outline="#c8dedb", width=2)
        draw.text((642, 586 + i * 65), line, font=small, fill="#265b57")

    # Virtual PCI device.
    rounded(draw, (1320, 555, 1640, 960), "#fff4e8", outline="#e2a45b", radius=24, width=4)
    draw.text((1370, 592), "QEMU EDU", font=section, fill="#744315")
    draw.text((1360, 655), "virtual PCI device", font=body, fill="#89591f")
    draw.text((1390, 710), "1234:11e8", font=mono, fill="#89591f")
    draw.text((1381, 765), "BAR0 · IRQ · DMA", font=small, fill="#89591f")

    # Driver/device paths and labels.
    arrow(draw, (1165, 575), (1315, 575), "#b8752e", 6)
    draw.text((1169, 525), "BAR0 MMIO", font=small, fill="#80521d")
    arrow(draw, (1320, 700), (1168, 700), "#5486b2", 6)
    draw.text((1169, 650), "MSI IRQ", font=small, fill="#355d80")
    arrow(draw, (1165, 875), (1315, 875), "#47a88e", 6)
    arrow(draw, (1320, 925), (1168, 925), "#47a88e", 6)
    draw.text((1220, 965), "bidirectional coherent DMA", font=small,
              fill="#247a67")

    # File operation boundary.
    arrow(draw, (450, 835), (565, 835), "#4182b8", 6)
    draw.text((457, 745), "VFS ops", font=small, fill="#355d80")
    draw.text((112, 1035), "User space", font=small, fill="#47617c")
    draw.text((575, 1035), "Kernel space", font=small, fill="#47617c")
    draw.text((1385, 1035), "QEMU device model", font=small, fill="#47617c")

    image.save(ARCH_DIR / "edupci_architecture.png", optimize=True)


def main() -> None:
    if not LOG.exists():
        raise SystemExit(f"Missing fresh captured demo log: {LOG}")
    DEMO_DIR.mkdir(parents=True, exist_ok=True)
    ARCH_DIR.mkdir(parents=True, exist_ok=True)
    raw = LOG.read_text(encoding="utf-8")
    lines = raw.rstrip("\n").splitlines()
    sections = log_sections(lines)
    if len(sections) != 6 or not lines[-1].endswith("EduPCI demo: PASS"):
        raise SystemExit("Captured demo log does not have the expected six sections and final PASS.")
    make_terminal("01-device-driver.png", sections[0] + [""] + sections[1])
    make_terminal("02-mmio-irq.png", sections[2] + [""] + sections[3])
    make_terminal("03-ring-buffer.png", sections[4])
    make_terminal("04-dma.png", sections[5])
    make_terminal("05-full-demo.png", lines)
    make_architecture()
    print("Rendered 5 demo images from evidence/public_demo_output.txt and the architecture figure.")


if __name__ == "__main__":
    main()
