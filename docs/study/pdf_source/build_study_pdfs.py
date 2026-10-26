'현재 소스에 근거한 한국어 학습 PDF 3종. ReportLab + 맑은 고딕을 사용한다.'
from pathlib import Path
from dataclasses import dataclass, field
import hashlib, json, html, re
from reportlab.pdfgen import canvas
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import Paragraph, Table, TableStyle, Preformatted, Flowable

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'output/pdf'
DATE = '2026-10-21'
S = 'MultiThreadServer/MultiThreadServer/src/'
D = 'DummyClient/src/'
R = 'docs/results/2026-10-16/'
R2 = 'docs/results/2026-10-18/'
RF = 'docs/results/2026-10-19/redis-final/'
NAVY = colors.HexColor('#142B40')
TEAL = colors.HexColor('#087E8B')
MUTED = colors.HexColor('#526575')
LIGHT = colors.HexColor('#EFF5F7')
GOLD = colors.HexColor('#E9AD35')
WIDTH, HEIGHT, MARGIN = 595.276, 841.89, 44
CW = WIDTH - 2 * MARGIN

pdfmetrics.registerFont(TTFont('Korean', 'C:/Windows/Fonts/malgun.ttf'))
pdfmetrics.registerFont(TTFont('KoreanBold', 'C:/Windows/Fonts/malgunbd.ttf'))
pdfmetrics.registerFont(TTFont('Code', 'C:/Windows/Fonts/consola.ttf'))
pdfmetrics.registerFontFamily('Korean', normal='Korean', bold='KoreanBold', italic='Korean', boldItalic='KoreanBold')
STYLES = {
    'p': ParagraphStyle('body', fontName='Korean', fontSize=10, leading=16.2, textColor=NAVY, wordWrap='CJK', spaceAfter=9),
    'small': ParagraphStyle('small', fontName='Korean', fontSize=8.6, leading=13.2, textColor=MUTED, wordWrap='CJK', spaceAfter=7),
    'h': ParagraphStyle('h', fontName='KoreanBold', fontSize=12.4, leading=18, textColor=TEAL, spaceBefore=6, spaceAfter=7, wordWrap='CJK'),
    'table': ParagraphStyle('cell', fontName='Korean', fontSize=9.1, leading=14, textColor=NAVY, wordWrap='CJK'),
    'th': ParagraphStyle('th', fontName='KoreanBold', fontSize=9.1, leading=14, textColor=colors.white, wordWrap='CJK'),
    'code': ParagraphStyle('code', fontName='Code', fontSize=8.5, leading=12, textColor=NAVY),
    'title': ParagraphStyle('title', fontName='KoreanBold', fontSize=22, leading=30, textColor=NAVY, wordWrap='CJK'),
    'answer': ParagraphStyle('answer', fontName='Korean', fontSize=11, leading=18.5, textColor=NAVY, wordWrap='CJK', spaceAfter=10),
    'follow': ParagraphStyle('follow', fontName='Korean', fontSize=9.5, leading=15, textColor=MUTED, wordWrap='CJK', spaceAfter=13),
}

def esc(s):
    return html.escape(str(s)).replace('\n', '<br/>')

def P(s): return ('p', s)
def H(s): return ('h', s)
def Small(s): return ('small', s)
def Code(s): return ('code', s)
def TableBlock(head, rows, widths=None): return ('table', (head, rows, widths))
def Note(title, text): return ('note', (title, text))
def Flow(items): return ('flow', items)
def Picture(path, caption, height=220): return ('picture', (path, caption, height))
def Ref(path, needle=None): return (path, needle)

@dataclass
class Page:
    title: str
    label: str
    blocks: list
    refs: list = field(default_factory=list)

class Sequence(Flowable):
    def __init__(self, items):
        super().__init__(); self.items=items; self.width=CW
        self.height=len(items)*45-8
    def draw(self):
        c=self.canv
        for i, (title, detail) in enumerate(self.items):
            y=self.height-(i+1)*45+8
            c.setFillColor(LIGHT); c.roundRect(0,y,CW,37,5,fill=1,stroke=0)
            c.setFillColor(TEAL); c.roundRect(0,y,29,37,5,fill=1,stroke=0)
            c.setFillColor(colors.white); c.setFont('KoreanBold',10); c.drawCentredString(14.5,y+13,str(i+1))
            p=Paragraph('<b>'+esc(title)+'</b>  '+esc(detail),STYLES['table'])
            _,h=p.wrap(CW-48,32)
            if h>31: raise ValueError('flow overflow '+title)
            p.drawOn(c,39,y+(37-h)/2)
            if i < len(self.items)-1:
                c.setStrokeColor(TEAL); c.line(14.5,y,14.5,y-8)

class AnswerMemo(Flowable):
    def __init__(self):
        super().__init__(); self.width=CW; self.height=116
    def draw(self):
        c=self.canv
        c.setFont('KoreanBold',9); c.setFillColor(TEAL)
        c.drawString(0,102,'내 코드로 확인하고 다시 답하기')
        for i,label in enumerate(['직접 확인한 함수 / 상태','재현하거나 읽은 검증 근거','내 말로 설명할 핵심']):
            y=77-i*31
            c.setFont('Korean',8.6); c.setFillColor(MUTED); c.drawString(0,y,label)
            c.setStrokeColor(colors.HexColor('#D7E2E8')); c.line(0,y-12,CW,y-12)

USED = {}
def resolve_ref(ref):
    path, needle = ref
    source = ROOT/path
    if not source.exists(): raise FileNotFoundError(source)
    raw=source.read_bytes(); USED[path]=hashlib.sha256(raw).hexdigest()
    line=None
    if needle:
        lines=raw.decode('utf-8-sig').splitlines()
        hits=[i+1 for i,l in enumerate(lines) if needle in l]
        if not hits: raise ValueError(f'Source marker missing: {path}: {needle}')
        line=hits[0]
    display=path.replace(S,'S/').replace(D,'D/').replace(R,'R/')
    return display+(f':{line}' if line else '')

def flowable(block):
    kind,value=block
    if kind in ('p','h','small','answer','follow'):
        return Paragraph(esc(value), STYLES[kind])
    if kind=='code':
        for line in value.splitlines():
            if pdfmetrics.stringWidth(line,'Code',8.5)>CW-24: raise ValueError('long code '+line)
        pf=Preformatted(value,STYLES['code'])
        t=Table([[pf]],colWidths=[CW]); t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1),LIGHT),('BOX',(0,0),(-1,-1),.5,colors.HexColor('#D7E2E8')),('LEFTPADDING',(0,0),(-1,-1),12),('RIGHTPADDING',(0,0),(-1,-1),12),('TOPPADDING',(0,0),(-1,-1),10),('BOTTOMPADDING',(0,0),(-1,-1),10)])); t.spaceAfter=10; return t
    if kind=='table':
        head, rows, widths=value
        widths=[CW*w/sum(widths) for w in widths] if widths else [CW/len(head)]*len(head)
        data=[[Paragraph(esc(x),STYLES['th']) for x in head]]+[[Paragraph(esc(x),STYLES['table']) for x in row] for row in rows]
        t=Table(data,colWidths=widths,hAlign='LEFT')
        t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),NAVY),('ROWBACKGROUNDS',(0,1),(-1,-1),[LIGHT,colors.white]),('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),9),('RIGHTPADDING',(0,0),(-1,-1),9),('TOPPADDING',(0,0),(-1,-1),7),('BOTTOMPADDING',(0,0),(-1,-1),7),('LINEBELOW',(0,-1),(-1,-1),.5,colors.HexColor('#D7E2E8'))]))
        t.spaceAfter=11; return t
    if kind=='note':
        title,text=value
        t=Table([[Paragraph('<b>'+esc(title)+'</b><br/>'+esc(text), STYLES['p'])]],colWidths=[CW])
        t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1),colors.HexColor('#FFF7E6')),('LINEBEFORE',(0,0),(0,-1),3,GOLD),('LEFTPADDING',(0,0),(-1,-1),12),('TOPPADDING',(0,0),(-1,-1),9),('BOTTOMPADDING',(0,0),(-1,-1),9)])); t.spaceAfter=11; return t
    if kind=='flow':
        v=Sequence(value); v.spaceAfter=12; return v
    if kind=='memo': return AnswerMemo()
    raise ValueError(kind)

def chrome(c, book, n, total):
    c.setFillColor(TEAL); c.rect(0,HEIGHT-7,WIDTH,7,fill=1,stroke=0)
    c.setFont('Korean',8); c.setFillColor(MUTED)
    c.drawString(MARGIN,HEIGHT-30,'STARCRAFT SERVER  /  '+book)
    c.setStrokeColor(colors.HexColor('#D7E2E8')); c.line(MARGIN,40,WIDTH-MARGIN,40)
    c.drawString(MARGIN,25,'현재 작업 트리 기준 · '+DATE+'  |  S/D/R 경로 약어는 안내 페이지 참조')
    c.setFont('KoreanBold',9); c.drawRightString(WIDTH-MARGIN,25,f'{n:02d} / {total:02d}')

def draw_cover(c, title, subtitle, number, desc, total):
    c.setFillColor(NAVY); c.rect(0,0,WIDTH,HEIGHT,fill=1,stroke=0)
    c.setFillColor(TEAL); c.rect(0,0,18,HEIGHT,fill=1,stroke=0)
    c.setFillColor(GOLD); c.setFont('KoreanBold',12); c.drawString(53,HEIGHT-85,'CODE READING SERIES  /  '+number)
    style=ParagraphStyle('cover',parent=STYLES['title'],fontSize=35,leading=49,textColor=colors.white)
    p=Paragraph(esc(title),style); _,h=p.wrap(CW-15,300); p.drawOn(c,53,HEIGHT-175-h)
    p=Paragraph(esc(subtitle),ParagraphStyle('sub',parent=STYLES['p'],fontSize=14,leading=23,textColor=colors.HexColor('#BDE4E6')))
    _,h=p.wrap(CW-25,200); p.drawOn(c,53,HEIGHT-350-h)
    p=Paragraph(esc(desc),ParagraphStyle('desc',parent=STYLES['p'],fontSize=11,leading=19,textColor=colors.HexColor('#D8E2E8')))
    _,h=p.wrap(CW-35,200); p.drawOn(c,53,205-h)
    c.setFillColor(GOLD); c.rect(53,275,65,4,fill=1,stroke=0)
    c.setFont('Korean',9); c.setFillColor(colors.white); c.drawString(53,55,DATE+'  /  C++20 · x64 · Protobuf · MSSQL · Redis')
    c.drawRightString(WIDTH-44,55,f'01 / {total:02d}')

def build(filename, number, title, subtitle, desc, pages):
    OUT.mkdir(parents=True,exist_ok=True)
    path=OUT/filename
    c=canvas.Canvas(str(path), pagesize=(WIDTH,HEIGHT), pageCompression=1)
    c.setTitle(title.replace('\n',' ')); c.setAuthor('StarCraft 서버 프로젝트 학습 자료'); c.setSubject('현재 소스, 기존 검증 기록에 근거한 코드 학습 및 면접 준비')
    c.showOutline()
    total=len(pages)+1; occupancy=[]
    c.bookmarkPage('cover'); c.addOutlineEntry(title.replace('\n',' '),'cover',0)
    draw_cover(c,title,subtitle,number,desc,total); c.showPage()
    for index,page in enumerate(pages,2):
        chrome(c,title.replace('\n',' '),index,total)
        c.bookmarkPage(f'p{index}'); c.addOutlineEntry(page.title,f'p{index}',1)
        c.setFillColor(TEAL); c.setFont('KoreanBold',9); c.drawString(MARGIN,HEIGHT-61,page.label)
        p=Paragraph(esc(page.title),STYLES['title']); _,h=p.wrap(CW,90); p.drawOn(c,MARGIN,HEIGHT-76-h)
        y=HEIGHT-76-h-20
        refs='코드·근거  '+ '  /  '.join(resolve_ref(r) for r in page.refs)
        refp=Paragraph(esc(refs),STYLES['small']) if page.refs else None
        refh=refp.wrap(CW,120)[1] if refp else 0
        bottom=57+refh+(12 if refp else 0)
        for block in page.blocks:
            if block[0]=='picture':
                rel,caption,ih=block[1]; source=ROOT/rel
                USED[rel]=hashlib.sha256(source.read_bytes()).hexdigest()
                c.drawImage(str(source),MARGIN,y-ih,width=CW,height=ih,preserveAspectRatio=True,anchor='c',mask='auto')
                y-=ih+7
                f=Paragraph(esc(caption),STYLES['small'])
            else: f=flowable(block)
            y-=f.getSpaceBefore() if hasattr(f,'getSpaceBefore') else 0
            _,fh=f.wrap(CW,max(0,y-bottom))
            if y-fh < bottom: raise ValueError(f'{filename} p{index} "{page.title}": overflow {bottom-(y-fh):.1f}pt at {block[0]}')
            f.drawOn(c,MARGIN,y-fh); y-=fh+(f.getSpaceAfter() if hasattr(f,'getSpaceAfter') else 9)
        if refp: refp.drawOn(c,MARGIN,57)
        occupancy.append({'page':index,'title':page.title,'remaining_pt':round(y-bottom,1)})
        c.showPage()
    c.save()
    return {'file':filename,'pages':total,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'layout':occupancy}

def main():
    from convention_content import convention_guide, convention_practice
    from guide_content import guide
    from qa_content import questions
    from practice_content import practice
    from engine_content import current_pages, engine_guide, engine_questions, engine_practice
    from ini_content import manual
    result=[]
    result.append(build('01_Code_Study_Guide.pdf','01','코드 구조와 실행 흐름\n학습 가이드','IOCP · 전투 계산 병렬화 · I/O 메모리 풀',
        '현재 구현을 기준으로 작성한 개인 학습용 설명서입니다. 클래스·함수·소유권을 소스 위치와 연결하고, 정상 경로와 실패 경로를 함께 설명합니다.',current_pages(guide())+engine_guide()+convention_guide()))
    result.append(build('02_Interview_Questions_Answers.pdf','02','예상 면접 질문과\n답변 연습','현재 구현 기준 58문항 · 수명과 병렬 계산',
        '답변은 연습용 초안입니다. 코드를 직접 따라가며 자신의 설명으로 바꾸세요. 구현된 사실과 앞으로 할 개선을 구분해 답할 수 있도록 구성했습니다.',current_pages(questions())+engine_questions()))
    result.append(build('03_Practice_and_Portfolio_Workbook.pdf','03','코드 실습과\n포트폴리오 준비','최종 코드 실행 · 설정 비교 · 측정 근거',
        '로그인부터 전투·리플레이·순위까지 확인하는 실습 자료입니다. 1,500명 시연과 별도 96명 워커 비교의 실행·CSV·그래프를 연결하고 측정 범위를 구분합니다.',current_pages(practice())+engine_practice()+convention_practice()))
    result.append(build('04_Server_Ini_Manual.pdf','04','Server.ini 설정\n사용 설명서','값별 동작 · 비교 A-E · 실행과 검증','같은 서버 코드에서 IOCP·WSAPoll, 메모리 풀, 전투 쓰레드(워커)를 바꾸는 방법과 각 설정의 적용 범위·기본값·허용값을 설명합니다.',manual()))
    manifest={'date':DATE,'root':str(ROOT),'basis':'현재 작업 트리(미커밋 포함). 10/16 과거 RTT, 10/18 공간 검색·장애, 10/19 Redis 활성 1,500명 시연과 이후 더미 계측을 보완한 96명 워커 18회 비교를 구분함. 이전 성능 장은 8바이트 헤더 이력이며, CURRENT/MEASUREMENT 새 장은 4바이트 헤더·IOCP/병렬 계산/풀 최종 빌드의 반복 비교와 1500명 시연이다.','pdfs':result,'sources_sha256':USED}
    (OUT/'build-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps([{'file':x['file'],'pages':x['pages'],'tightest_space':min(p['remaining_pt'] for p in x['layout'])} for x in result],ensure_ascii=False))

if __name__=='__main__': main()
