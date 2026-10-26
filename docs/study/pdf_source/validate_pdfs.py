'PDF 텍스트/북마크와 렌더된 전체 페이지를 검사하고 시각 검수용 축소판을 만든다.'
from pathlib import Path
import json, re
from pypdf import PdfReader
import pdfplumber
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'output/pdf'
REVIEW=ROOT/'tmp/pdfs/iocp-review'
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',17)
def count_outline(items):
    return sum(count_outline(item) if isinstance(item,list) else 1 for item in items)
result={'date':'2026-10-19','visual_review':'pending','files':[]}
manifest=json.loads((OUT/'build-manifest.json').read_text('utf-8'))
for item in manifest['pdfs']:
    filename,expected=item['file'],item['pages']
    path=OUT/filename
    reader=PdfReader(path)
    assert len(reader.pages)==expected
    assert count_outline(reader.outline)==expected
    texts=[p.extract_text() for p in reader.pages]
    assert all(len(t)>100 and '�' not in t and '■' not in t for t in texts)
    if filename.startswith('02'):
        ids=re.findall('Q(\\d\\d)\\.', '\n'.join(texts))
        assert ids==[f'{i:02d}' for i in range(1,59)], ids
    with pdfplumber.open(path) as pdf:
        for n,page in enumerate(pdf.pages,1):
            bad=[c for c in page.chars if c['x0'] < 0 or c['x1'] > page.width or c['top']<0 or c['bottom']>page.height]
            assert not bad, (filename,n,bad[:2])
    images=sorted(REVIEW.glob(path.stem+'-*.png'))
    assert len(images)==expected
    for start in range(0,len(images),12):
        sheet=Image.new('RGB',(1080,2152),'#dce2e8')
        draw=ImageDraw.Draw(sheet)
        for offset,png in enumerate(images[start:start+12]):
            img=Image.open(png).convert('RGB')
            img.thumbnail((348,502))
            x=(offset%3)*360+6; y=(offset//3)*538+24
            draw.text((x,y-22),f'{filename[:2]} / page {start+offset+1:02}',font=font,fill='#142b40')
            sheet.paste(img,(x,y))
        sheet.save(REVIEW/f'contact-{filename[:2]}-{start//12+1}.jpg',quality=90)
    result['files'].append({'file':filename,'pages':expected,'bookmarks':count_outline(reader.outline),'text_and_page_bounds':'passed','rendered_pages':len(images)})
(OUT/'validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))
