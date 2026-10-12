"""移植笔记 .md -> .pdf
用 python-markdown 转 HTML, 再用 headless Chrome 打印成 PDF (无外部依赖).
用法: python _md2pdf.py
"""

import os
import subprocess
import tempfile

import markdown

HERE = os.path.dirname(os.path.abspath(__file__))
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

CSS = """
@page { size: A4; margin: 16mm 13mm; }
body { font-family: "Microsoft YaHei", "Segoe UI", "Segoe UI Emoji", sans-serif;
       font-size: 10.5pt; line-height: 1.65; color: #1a1a1a; }
h1 { font-size: 19pt; border-bottom: 2px solid #333; padding-bottom: 5px; margin-top: 0; }
h2 { font-size: 15pt; border-bottom: 1px solid #bbb; padding-bottom: 3px; margin-top: 22px; }
h3 { font-size: 12.5pt; margin-top: 18px; }
h4 { font-size: 11pt; margin-top: 14px; }
code { font-family: Consolas, "Courier New", monospace; font-size: 9.5pt;
       background: #f2f2f2; padding: 1px 3px; border-radius: 2px; }
pre { background: #f7f7f7; border: 1px solid #ddd; border-left: 3px solid #888;
      padding: 7px 9px; font-size: 9pt; line-height: 1.45;
      white-space: pre-wrap; word-break: break-all; page-break-inside: avoid; }
pre code { background: none; padding: 0; font-size: 9pt; }
table { border-collapse: collapse; width: 100%; margin: 9px 0;
        font-size: 9.5pt; page-break-inside: avoid; }
th, td { border: 1px solid #bbb; padding: 4px 6px; text-align: left; vertical-align: top; }
th { background: #ececec; font-weight: 600; }
blockquote { margin: 9px 0; padding: 5px 11px; border-left: 3px solid #bbb;
             background: #fafafa; color: #333; }
blockquote p { margin: 3px 0; }
hr { border: none; border-top: 1px solid #ccc; margin: 18px 0; }
a { color: #0b5ed7; text-decoration: none; }
ul, ol { padding-left: 22px; }
li { margin: 2px 0; }
.doc { page-break-before: always; }
.doc:first-child { page-break-before: avoid; }
"""

TMPL = """<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8"><title>{title}</title>
<style>{css}</style></head><body>{body}</body></html>
"""


def to_html_body(md_text):
    return markdown.markdown(
        md_text,
        extensions=["tables", "fenced_code", "sane_lists", "attr_list"],
    )


def write_pdf(html, pdf_path):
    tmp = os.path.join(tempfile.gettempdir(), "md2pdf_tmp.html")
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(html)
    url = "file:///" + tmp.replace("\\", "/")
    subprocess.run(
        [CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
         "--virtual-time-budget=4000", "--print-to-pdf=" + pdf_path, url],
        check=True, capture_output=True,
    )


def main():
    md_files = sorted(f for f in os.listdir(HERE) if f.endswith(".md"))

    docs = []
    for name in md_files:
        with open(os.path.join(HERE, name), encoding="utf-8") as f:
            docs.append((name, to_html_body(f.read())))

    # 每个文件一个 PDF
    for name, body in docs:
        pdf = os.path.join(HERE, name[:-3] + ".pdf")
        write_pdf(TMPL.format(title=name[:-3], css=CSS, body=body), pdf)
        print("OK", os.path.basename(pdf))

    # 合并成一个
    joined = "\n".join('<div class="doc">%s</div>' % b for _, b in docs)
    pdf = os.path.join(HERE, "移植笔记合集.pdf")
    write_pdf(TMPL.format(title="移植笔记", css=CSS, body=joined), pdf)
    print("OK", os.path.basename(pdf))


if __name__ == "__main__":
    main()
