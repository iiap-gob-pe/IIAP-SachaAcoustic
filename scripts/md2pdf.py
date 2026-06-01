# -*- coding: utf-8 -*-
"""Mini conversor Markdown -> PDF con reportlab (sin LaTeX).
Soporta: encabezados, parrafos, **negrita**, `codigo`, bloques ```, tablas |..|,
listas '-', e imagenes ![alt](ruta). Uso: python md2pdf.py entrada.md salida.pdf"""
import sys, os, re, html
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import cm
from reportlab.lib import colors
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Table,
                                TableStyle, Preformatted, Image, ListFlowable, ListItem)

def inline(t):
    t = html.escape(t)
    t = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', t)
    t = re.sub(r'\*(.+?)\*', r'<i>\1</i>', t)          # *italica* (despues de **negrita**)
    t = re.sub(r'`(.+?)`', r'<font face="Courier" backColor="#eeeeee">\1</font>', t)
    t = re.sub(r'\[(.+?)\]\((.+?)\)', r'<u>\1</u>', t)  # enlaces -> subrayado
    return t

def main():
    src, out = sys.argv[1], sys.argv[2]
    base = os.path.dirname(os.path.abspath(src))
    lines = open(src, encoding="utf-8").read().splitlines()
    ss = getSampleStyleSheet()
    H1 = ParagraphStyle('H1', parent=ss['Heading1'], fontSize=20, spaceAfter=10, textColor=colors.HexColor('#5a2a82'))
    H2 = ParagraphStyle('H2', parent=ss['Heading2'], fontSize=14, spaceBefore=10, spaceAfter=6, textColor=colors.HexColor('#7a3aa0'))
    H3 = ParagraphStyle('H3', parent=ss['Heading3'], fontSize=11.5, spaceBefore=6, textColor=colors.HexColor('#444'))
    BODY = ParagraphStyle('Body', parent=ss['BodyText'], fontSize=9.5, leading=13)
    story = []
    i = 0
    # frontmatter
    if lines and lines[0].strip() == '---':
        j = 1
        title = None
        while j < len(lines) and lines[j].strip() != '---':
            m = re.match(r'(\w+):\s*"?(.*?)"?$', lines[j])
            if m and m.group(1) == 'title': title = m.group(2)
            j += 1
        i = j + 1
        if title:
            story.append(Paragraph(title, H1)); story.append(Spacer(1, 6))
    while i < len(lines):
        ln = lines[i]
        if ln.strip().startswith('```'):
            buf = []; i += 1
            while i < len(lines) and not lines[i].strip().startswith('```'):
                buf.append(lines[i]); i += 1
            story.append(Preformatted('\n'.join(buf), ParagraphStyle('code', fontName='Courier', fontSize=8, leading=9.5, backColor=colors.HexColor('#f3f3f3'), borderPadding=4)))
            story.append(Spacer(1, 4)); i += 1; continue
        if ln.startswith('|') and i + 1 < len(lines) and re.match(r'^\|[\s:\-\|]+\|', lines[i+1]):
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                if re.match(r'^\|[\s:\-\|]+\|$', lines[i]): i += 1; continue
                cells = [c.strip() for c in lines[i].strip().strip('|').split('|')]
                rows.append([Paragraph(inline(c), BODY) for c in cells]); i += 1
            t = Table(rows, hAlign='LEFT')
            t.setStyle(TableStyle([('GRID',(0,0),(-1,-1),0.4,colors.HexColor('#bbb')),
                ('BACKGROUND',(0,0),(-1,0),colors.HexColor('#ede3f5')),
                ('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),4),('RIGHTPADDING',(0,0),(-1,-1),4),
                ('TOPPADDING',(0,0),(-1,-1),2),('BOTTOMPADDING',(0,0),(-1,-1),2)]))
            story.append(t); story.append(Spacer(1, 6)); continue
        m = re.match(r'^(#{1,3})\s+(.*)$', ln)
        if m:
            lvl = len(m.group(1)); txt = inline(m.group(2))
            story.append(Paragraph(txt, [H1, H2, H3][lvl-1])); i += 1; continue
        mimg = re.match(r'^!\[(.*?)\]\((.+?)\)', ln)
        if mimg:
            alt = mimg.group(1); rel = mimg.group(2); p = os.path.join(base, rel)
            if os.path.exists(p):
                try:
                    from reportlab.lib.utils import ImageReader
                    iw, ih = ImageReader(p).getSize()
                    w = (2.2 if 'icon' in rel else 15.5)*cm; h = w*ih/iw
                    if h > 11*cm: h = 11*cm; w = h*iw/ih
                    story.append(Image(p, width=w, height=h))
                    if alt and 'icon' not in rel:
                        story.append(Paragraph('<i>'+inline(alt)+'</i>',
                            ParagraphStyle('cap', parent=BODY, fontSize=8, textColor=colors.HexColor('#666'), spaceAfter=8)))
                except Exception: pass
            i += 1; continue
        if ln.strip() == '---': story.append(Spacer(1, 6)); i += 1; continue
        if ln.strip().startswith('- '):
            items = []
            while i < len(lines) and lines[i].strip().startswith('- '):
                items.append(ListItem(Paragraph(inline(lines[i].strip()[2:]), BODY))); i += 1
            story.append(ListFlowable(items, bulletType='bullet', start='•')); story.append(Spacer(1,4)); continue
        if ln.strip() == '': story.append(Spacer(1, 5)); i += 1; continue
        story.append(Paragraph(inline(ln), BODY)); i += 1
    SimpleDocTemplate(out, pagesize=A4, leftMargin=2*cm, rightMargin=2*cm,
                      topMargin=1.8*cm, bottomMargin=1.8*cm).build(story)
    print("PDF:", out)

if __name__ == "__main__":
    main()
