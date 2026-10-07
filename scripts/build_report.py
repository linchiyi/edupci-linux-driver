#!/usr/bin/env python3
"""Build report.pdf from docs/report.md and local image assets."""

from pathlib import Path
import re
from xml.sax.saxutils import escape

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT, TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.platypus import (
    Image,
    ListFlowable,
    ListItem,
    PageBreak,
    Paragraph,
    Preformatted,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs" / "report.md"
OUTPUT = ROOT / "report.pdf"
CONTENT_WIDTH = A4[0] - 1.28 * inch


def inline_markup(text: str) -> str:
    parts = re.split(
        r"(`[^`]+`|\*\*[^*]+\*\*|\*[^*]+\*|\[[^\]]+\]\(https?://[^)]+\))",
        text,
    )
    rendered = []
    for part in parts:
        if not part:
            continue
        if part.startswith("`") and part.endswith("`"):
            rendered.append(f'<font name="Courier">{escape(part[1:-1])}</font>')
        elif part.startswith("["):
            match = re.fullmatch(r"\[([^\]]+)\]\((https?://[^)]+)\)", part)
            if match:
                label, href = match.groups()
                rendered.append(
                    f'<link href="{escape(href, {chr(34): "&quot;"})}" '
                    f'color="#285b72" underline="1">{escape(label)}</link>'
                )
            else:
                rendered.append(escape(part))
        elif part.startswith("**") and part.endswith("**"):
            rendered.append(f"<b>{escape(part[2:-2])}</b>")
        elif part.startswith("*") and part.endswith("*"):
            rendered.append(f"<i>{escape(part[1:-1])}</i>")
        else:
            rendered.append(escape(part))
    return "".join(rendered)


def styles():
    base = getSampleStyleSheet()
    return {
        "title": ParagraphStyle(
            "EduTitle", parent=base["Title"], fontName="Helvetica-Bold",
            fontSize=31, leading=35, textColor=colors.HexColor("#173557"),
            alignment=TA_LEFT, spaceAfter=4,
        ),
        "subtitle": ParagraphStyle(
            "EduSubtitle", parent=base["Heading2"], fontName="Helvetica",
            fontSize=17, leading=21, textColor=colors.HexColor("#42658a"),
            spaceAfter=8,
        ),
        "h2": ParagraphStyle(
            "EduH2", parent=base["Heading1"], fontName="Helvetica-Bold",
            fontSize=20, leading=24, textColor=colors.HexColor("#173557"),
            spaceBefore=1, spaceAfter=11, keepWithNext=True,
        ),
        "h3": ParagraphStyle(
            "EduH3", parent=base["Heading2"], fontName="Helvetica-Bold",
            fontSize=13, leading=16, textColor=colors.HexColor("#285b72"),
            spaceBefore=7, spaceAfter=5, keepWithNext=True,
        ),
        "body": ParagraphStyle(
            "EduBody", parent=base["BodyText"], fontName="Helvetica",
            fontSize=10.2, leading=14.1, textColor=colors.HexColor("#263447"),
            spaceAfter=7,
        ),
        "bullet": ParagraphStyle(
            "EduBullet", parent=base["BodyText"], fontName="Helvetica",
            fontSize=9.8, leading=13.2, textColor=colors.HexColor("#263447"),
            spaceAfter=1,
        ),
        "quote": ParagraphStyle(
            "EduQuote", parent=base["BodyText"], fontName="Helvetica-Bold",
            fontSize=11, leading=15, textColor=colors.HexColor("#174c49"),
            leftIndent=11, borderColor=colors.HexColor("#62a99b"),
            borderWidth=2, borderPadding=7, spaceBefore=3, spaceAfter=10,
        ),
        "cell": ParagraphStyle(
            "EduCell", parent=base["BodyText"], fontName="Helvetica",
            fontSize=8.7, leading=11.2, textColor=colors.HexColor("#263447"),
            spaceAfter=0,
        ),
        "cell_header": ParagraphStyle(
            "EduCellHeader", parent=base["BodyText"], fontName="Helvetica-Bold",
            fontSize=8.8, leading=11.2, textColor=colors.white, spaceAfter=0,
        ),
        "code": ParagraphStyle(
            "EduCode", parent=base["Code"], fontName="Courier",
            fontSize=8.4, leading=11, textColor=colors.HexColor("#24364c"),
            backColor=colors.HexColor("#f1f5f9"), borderPadding=7,
        ),
    }


def parse_table(lines, style):
    rows = []
    for line in lines:
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if cells and all(re.fullmatch(r":?-{3,}:?", cell.replace(" ", "")) for cell in cells):
            continue
        rows.append(cells)
    if not rows:
        return None
    columns = max(len(row) for row in rows)
    data = []
    for row_index, row in enumerate(rows):
        row += [""] * (columns - len(row))
        cell_style = style["cell_header"] if row_index == 0 else style["cell"]
        data.append([Paragraph(inline_markup(cell), cell_style) for cell in row])
    widths = [CONTENT_WIDTH / columns] * columns
    table = Table(data, colWidths=widths, repeatRows=1, hAlign="LEFT")
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#234767")),
        ("GRID", (0, 0), (-1, -1), 0.45, colors.HexColor("#c7d2df")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 7),
        ("RIGHTPADDING", (0, 0), (-1, -1), 7),
        ("TOPPADDING", (0, 0), (-1, -1), 6),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f5f8fb")]),
    ]))
    return table


def parse_markdown(source: str, style):
    lines = source.splitlines()
    story = []
    index = 0
    in_code = False
    code_lines = []
    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        if stripped == "<!-- pagebreak -->":
            story.append(PageBreak())
            index += 1
            continue
        if stripped.startswith("```"):
            if in_code:
                story.append(Preformatted("\n".join(code_lines), style["code"]))
                story.append(Spacer(1, 6))
                code_lines = []
                in_code = False
            else:
                in_code = True
            index += 1
            continue
        if in_code:
            code_lines.append(line)
            index += 1
            continue
        if not stripped:
            index += 1
            continue
        if stripped.startswith("!["):
            match = re.match(r"!\[(.*?)\]\((.*?)\)", stripped)
            if match:
                image_path = (SOURCE.parent / match.group(2)).resolve()
                if not image_path.is_file():
                    raise FileNotFoundError(image_path)
                image = Image(str(image_path))
                scale = min(CONTENT_WIDTH / image.imageWidth, 4.55 * inch / image.imageHeight)
                image.drawWidth = image.imageWidth * scale
                image.drawHeight = image.imageHeight * scale
                image.hAlign = "CENTER"
                story.extend([image, Spacer(1, 8)])
            index += 1
            continue
        if stripped.startswith("|"):
            table_lines = []
            while index < len(lines) and lines[index].strip().startswith("|"):
                table_lines.append(lines[index])
                index += 1
            table = parse_table(table_lines, style)
            if table:
                story.extend([table, Spacer(1, 9)])
            continue
        if stripped.startswith("- "):
            items = []
            while index < len(lines) and lines[index].strip().startswith("- "):
                bullet_text = lines[index].strip()[2:].strip()
                items.append(ListItem(Paragraph(inline_markup(bullet_text), style["bullet"]), leftIndent=9))
                index += 1
            story.append(ListFlowable(items, bulletType="bullet", start="circle",
                                      leftIndent=16, bulletFontSize=6, spaceAfter=7))
            continue
        if stripped.startswith("> "):
            text = stripped[2:]
            story.append(Paragraph(inline_markup(text), style["quote"]))
            index += 1
            continue
        heading = re.match(r"^(#{1,3})\s+(.*)$", stripped)
        if heading:
            level = len(heading.group(1))
            key = "title" if level == 1 else "subtitle" if level == 2 and len(story) < 2 else "h2" if level == 2 else "h3"
            story.append(Paragraph(inline_markup(heading.group(2)), style[key]))
            index += 1
            continue
        paragraph = [stripped]
        index += 1
        while index < len(lines):
            upcoming = lines[index].strip()
            if (not upcoming or upcoming.startswith(("#", "- ", "|", "![", "> ", "```"))
                    or upcoming == "<!-- pagebreak -->"):
                break
            paragraph.append(upcoming)
            index += 1
        story.append(Paragraph(inline_markup(" ".join(paragraph)), style["body"]))
    return story


def footer(canvas, document):
    canvas.saveState()
    page_width, _ = A4
    canvas.setStrokeColor(colors.HexColor("#d7e0ea"))
    canvas.setLineWidth(0.6)
    canvas.line(0.65 * inch, 0.49 * inch, page_width - 0.65 * inch, 0.49 * inch)
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(colors.HexColor("#718096"))
    canvas.drawString(0.66 * inch, 0.31 * inch, "EduPCI · Portfolio Systems Report")
    canvas.drawRightString(page_width - 0.66 * inch, 0.31 * inch,
                           f"Page {document.page}")
    canvas.restoreState()


def main():
    if not SOURCE.is_file():
        raise SystemExit(f"Missing editable report source: {SOURCE}")
    doc = SimpleDocTemplate(
        str(OUTPUT), pagesize=A4, rightMargin=0.64 * inch,
        leftMargin=0.64 * inch, topMargin=0.58 * inch,
        bottomMargin=0.70 * inch, title="EduPCI — Linux PCI Device Driver for QEMU EDU",
        author="EduPCI Project", subject="Portfolio technical report",
    )
    story = parse_markdown(SOURCE.read_text(encoding="utf-8"), styles())
    doc.build(story, onFirstPage=footer, onLaterPages=footer)
    print(f"Built {OUTPUT.relative_to(ROOT)} from {SOURCE.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
