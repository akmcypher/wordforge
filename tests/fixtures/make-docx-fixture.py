"""Recreate the independent DOCX fixture with python-docx 1.1.0 (not app code)."""
from pathlib import Path
from docx import Document
from docx.shared import Inches, Pt

document = Document()
document.core_properties.author = ""
document.core_properties.last_modified_by = ""
section = document.sections[0]
section.page_width, section.page_height = Inches(8.5), Inches(11)
section.top_margin = section.bottom_margin = section.left_margin = section.right_margin = Inches(0.5)
normal = document.styles["Normal"]
normal.font.name, normal.font.size = "DejaVu Serif", Pt(12)
paragraph = document.add_paragraph()
run = paragraph.add_run("Independent नमस्ते ")
run.bold = True
paragraph.add_run("مرحبا").italic = True
document.add_paragraph("numbered item", style="List Number")
document.save(Path(__file__).with_name("python-docx-basic.docx"))
