'현재 소스에서 줄 번호와 발췌를 가져오는 네트워크/스레드 핵심 학습서.'
from pathlib import Path
import hashlib
import html
import json

from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.colors import HexColor, white
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import Paragraph, Table, TableStyle

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / 'MultiThreadServer/MultiThreadServer/src'
OUT = ROOT / 'output/pdf'
W, H, M = 960, 680, 42
INK, MUTED, BORDER = '#162D42', '#566A7B', '#D7E2EA'
PALE, BLUE, GOLD, GREEN, PURPLE = '#F0F5F8', '#1667A6', '#9C660E', '#22785C', '#77539C'
COLORS = {'NETWORK': BLUE, 'MAIN': GOLD, 'BATTLE': GREEN, 'DATABASE': PURPLE, 'CORE': BLUE}
FONT = 'C:/Windows/Fonts/'
pdfmetrics.registerFont(TTFont('CoreKR', FONT + 'malgun.ttf'))
pdfmetrics.registerFont(TTFont('CoreBold', FONT + 'malgunbd.ttf'))
pdfmetrics.registerFont(TTFont('CoreCode', FONT + 'consola.ttf'))
pdfmetrics.registerFontFamily('CoreKR', normal='CoreKR', bold='CoreBold')
SOURCES, EXCERPTS, LAYOUT = {}, [], []


def source(name):
    path = SRC / name
    raw = path.read_bytes()
    SOURCES[str(path.relative_to(ROOT)).replace('\\', '/')] = hashlib.sha256(raw).hexdigest()
    return raw.decode('utf-8-sig').splitlines()


def at(name, marker):
    hits = [i + 1 for i, line in enumerate(source(name)) if marker in line]
    if len(hits) != 1:
        raise ValueError(f"Unique source anchor required: {name} {marker}: {hits}")
    return f"{name}:{hits[0]}"


def para(c, value, x, top, width, size=12, leading=19, color=INK, bold=False):
    style = ParagraphStyle('core', fontName='CoreBold' if bold else 'CoreKR',
                           fontSize=size, leading=leading, textColor=HexColor(color),
                           wordWrap='CJK')
    p = Paragraph(html.escape(value).replace('\n', '<br/>'), style)
    _, height = p.wrap(width, H)
    p.drawOn(c, x, top-height)
    return top-height


def header(c, number, page, total):
    accent = COLORS[page.get('role', 'CORE')]
    c.setFillColor(HexColor(accent))
    c.rect(0, H-7, W, 7, fill=1, stroke=0)
    para(c, '05 / NETWORK & THREAD CORE GUIDE', M, H-28, 600, 9, 13, MUTED)
    para(c, page['tag'], W-300, H-28, 258, 9, 13, accent, True)
    para(c, page['title'], M, H-62, W-2*M, 26, 34, INK, True)
    para(c, page['intro'], M, H-106, W-2*M, 12, 19, MUTED)
    c.setStrokeColor(HexColor(BORDER))
    c.line(M, 45, W-M, 45)
    para(c, '현재 작업 트리 기준 · 2026-10-21   |   S = MultiThreadServer/MultiThreadServer/src', M, 33, 800, 8, 12, MUTED)
    para(c, f"{number:02d} / {total:02d}", W-96, 34, 60, 10, 12, INK, True)
    c.bookmarkPage(f"p{number}")
    c.addOutlineEntry(f"{number:02d}  {page['title']}", f"p{number}", 0)


def notes(c, items, x, top, width):
    for title, body in items:
        top = para(c, title, x, top, width, 12, 18, BLUE, True)-5
        top = para(c, body, x, top, width, 11.3, 18)-17
    return top


def snippet(c, name, spans, x, top, width, label=None, size=9.5):
    '원문 줄 번호를 유지한다. 긴 원문은 줄바꿈만 하고 공통 들여쓰기만 줄인다.'
    lines = source(name)
    selected = []
    for lo, hi in spans:
        if not (1 <= lo <= hi <= len(lines)):
            raise ValueError((name, lo, hi))
        if selected:
            selected.append((None, '...'))
        segment = [(i, lines[i-1]) for i in range(lo, hi+1)]
        nonempty = [len(s)-len(s.lstrip()) for _, s in segment if s.strip()]
        indent = min(nonempty) if nonempty else 0
        selected += [(i, s[indent:]) for i, s in segment]
    rendered = []
    available = width-62
    for n, line in selected:
        value = line
        # 한글 주석은 포함된 줄 전체를 한글 글꼴로 그린다.
        font = 'CoreKR' if any(ord(ch) > 127 for ch in value) else 'CoreCode'
        chunks = []
        while value:
            cut = len(value)
            while cut > 1 and pdfmetrics.stringWidth(value[:cut], font, size) > available:
                cut -= 1
            if cut < len(value) and font == 'CoreCode':
                boundary = value.rfind(' ', 0, cut)
                if boundary > cut//2:
                    cut = boundary
            chunks.append(value[:cut].rstrip())
            value = value[cut:].lstrip()
        chunks = chunks or ['']
        for j, chunk in enumerate(chunks):
            rendered.append((str(n) if n and j == 0 else '', chunk, font))
    height = 49+len(rendered)*13.7
    bottom = top-height
    if bottom < 104:
        raise ValueError(f"Code overflow {name}: {bottom}")
    c.setFillColor(HexColor(PALE))
    c.roundRect(x, bottom, width, height, 7, fill=1, stroke=0)
    para(c, label or f"{name} / 원문 발췌", x+15, top-12, width-30, 10, 14, BLUE, True)
    y = top-43
    for number, line, font in rendered:
        c.setFillColor(HexColor(MUTED)); c.setFont('CoreCode', 8)
        c.drawRightString(x+37, y, number)
        c.setFillColor(HexColor(INK)); c.setFont(font, size)
        c.drawString(x+47, y, line)
        y -= 13.7
    EXCERPTS.append({'file':name, 'ranges':spans, 'wrapped_lines':len(rendered)})
    return bottom


def table(c, headers, rows, widths, x=M, top=507, links=None, size=11):
    total = sum(widths)
    data = []
    for index, row in enumerate([headers]+rows):
        style = ParagraphStyle('cell', fontName='CoreBold' if index == 0 else 'CoreKR',
                               fontSize=size, leading=size+5, wordWrap='CJK',
                               textColor=white if index == 0 else HexColor(INK))
        data.append([Paragraph(html.escape(str(v)).replace('\n','<br/>'), style) for v in row])
    t = Table(data, colWidths=widths)
    t.setStyle(TableStyle([
        ('BACKGROUND',(0,0),(-1,0),HexColor(INK)),
        ('ROWBACKGROUNDS',(0,1),(-1,-1),[HexColor(PALE),white]),
        ('VALIGN',(0,0),(-1,-1),'TOP'),
        ('LEFTPADDING',(0,0),(-1,-1),12),
        ('RIGHTPADDING',(0,0),(-1,-1),12),
        ('TOPPADDING',(0,0),(-1,-1),7),
        ('BOTTOMPADDING',(0,0),(-1,-1),7),
        ('LINEBELOW',(0,-1),(-1,-1),.5,HexColor(BORDER)),
    ]))
    _, height = t.wrap(total, H)
    if top-height < 105:
        raise ValueError(f"Table overflow: {top-height} {headers}")
    t.drawOn(c,x,top-height)
    if links:
        y = top-t._rowHeights[0]
        for rh, target in zip(t._rowHeights[1:], links):
            c.linkRect('', f"p{target}", (x,y-rh,x+total,y), relative=0, thickness=0)
            y -= rh
    return top-height


def takeaway(c, text):
    c.setFillColor(HexColor('#FFF5DF'))
    c.roundRect(M, 58, W-2*M, 36, 5, fill=1, stroke=0)
    y = para(c, text, M+12, 84, W-2*M-24, 10.8, 15.5)
    if y < 60:
        raise ValueError('Takeaway overflow: '+text)


def box(c,x,y,w,h,title,detail,role):
    color = COLORS[role]
    c.setFillColor(HexColor(PALE)); c.roundRect(x,y,w,h,6,fill=1,stroke=0)
    c.setFillColor(HexColor(color)); c.rect(x,y,4,h,fill=1,stroke=0)
    para(c,title,x+14,y+h-12,w-25,13,20,color,True)
    bottom = para(c,detail,x+14,y+h-43,w-25,11.5,18)
    if bottom < y+8: raise ValueError('Diagram box overflow '+title)


def arrow(c,x1,y1,x2,y2):
    c.setStrokeColor(HexColor(MUTED)); c.setLineWidth(1.2); c.line(x1,y1,x2,y2)
    if x1 == x2:
        sign=1 if y2<y1 else -1
        c.line(x2,y2,x2-4,y2+sign*6); c.line(x2,y2,x2+4,y2+sign*6)
    else:
        sign=1 if x2<x1 else -1
        c.line(x2,y2,x2+sign*6,y2-4); c.line(x2,y2,x2+sign*6,y2+4)


def diagram(c, mode):
    if mode == 'architecture':
        box(c,42,383,265,122,'네트워크 쓰레드(워커)','CIocpServer::Run()\n수신 완료 / 패킷 분리\nWSASend / 송신 완료','NETWORK')
        box(c,348,383,265,122,'메인 스레드','CGameLogic::Update() / Tick()\n명령 실행 / 게임 상태 확정\nHP·사망·킬·승패 반영','MAIN')
        box(c,654,383,264,122,'전투 계산 워커','CBattleWorkers::Work()\n이동·MP / 스톰 피해 계산\n수신자별 시야·직렬화','BATTLE')
        arrow(c,307,463,348,463); arrow(c,348,414,307,414)
        arrow(c,613,463,654,463); arrow(c,654,414,613,414)
        para(c,'명령',312,491,34,9,13)
        para(c,'응답',312,402,34,9,13)
        para(c,'계산',617,491,34,9,13)
        para(c,'결과',617,402,34,9,13)
        box(c,348,185,265,118,'DB 쓰레드(워커)','CDatabaseWorker::Run()\n워커별 DB 연결 / SQL 실행\n결과 큐 → 메인 적용','DATABASE')
        arrow(c,447,383,447,303); arrow(c,514,303,514,383)
        para(c,'작업 큐',386,349,90,10,15,PURPLE)
        para(c,'결과 큐',529,349,80,10,15,PURPLE)
        notes(c,[('색깔은 실행 위치','네트워크 역할도 여러 스레드에서 실행된다. ‘네트워크 파일’과 ‘스레드 파일’은 서로 겹친다.'),
                 ('게임 상태의 최종 작성자','네트워크는 명령 전달, 전투 쓰레드(워커)는 계산 결과 작성. stPlayer·건물·랭킹 반영은 메인이 맡는다.')],42,336,265)
        notes(c,[('보조 워커는 이후에 읽기','CRedisWorker::Run(): Redis 통신.\nReplay::CRecorder::Run(): 기록 파일 저장. 핵심 큐 구조를 먼저 이해한 뒤 읽는다.'),
                 ('그림의 기준','서버 프로세스 하나의 내부 구조다. Game A/B/C와 World는 별도 프로세스이며 전투 계산은 World에 적용된다.')],654,336,264)
    elif mode == 'move':
        rows = [
            ('NETWORK','1  수신 완료','Run → HandleSessionCompletion','recvBuffer에 바이트를 누적하고 HandleClientPackets 호출'),
            ('NETWORK','2  프레임 → 명령','HandleClientPackets → CGameLogic::Submit','완성된 CM_Move의 본문을 복사해 m_commands에 보관'),
            ('MAIN','3  명령 처리','Update → HandlePacket → HandleMove','목적지 유효성 검사 후 goalX / goalY / moving 설정'),
            ('MAIN','4  접수 응답','SendActionReply → SendMessage → SendPacket','SM_ActionReply를 sendQueue에 넣고 IOCP 쓰레드(워커) 깨우기'),
            ('BATTLE','5  다음 전장 틱','Tick → MovePlayers → CBattleWorkers::Run','워커 계산 완료를 기다린 뒤 메인에서 실제 x / y 반영'),
            ('MAIN','6  변경 상태 전송','BroadcastWorldState → SendPacket → PostSend','현재 좌표를 SM_WorldState로 전달하고 WSASend 등록'),
        ]
        top=510
        for i,(role,title,func,detail) in enumerate(rows):
            y=top-59
            c.setFillColor(HexColor(PALE));c.roundRect(M,y,W-2*M,53,5,fill=1,stroke=0)
            para(c,title,M+12,top-13,159,11.5,17,COLORS[role],True)
            para(c,func,M+181,top-11,660,11.5,17,INK,True)
            para(c,detail,M+181,top-31,660,10.8,16,MUTED)
            if i<5:arrow(c,M+86,y,M+86,y-10)
            top-=65
        para(c,'접수 성공과 이동 완료는 다르다. 4번 응답은 명령 접수 결과이며, 실제 위치 변화는 틱과 상태 패킷으로 확인한다.',M,116,W-2*M,10.5,16)
    else:
        raise ValueError(mode)


def build():
    from core_guide_content import pages
    all_pages = pages(at)
    for name in ('IocpSession.h', 'NetworkWorker.h', 'NetworkWorker.cpp', 'IoBufferPool.cpp', 'GameLogic.h', 'GameSkills.cpp', 'GameSnapshot.cpp', 'RedisWorker.cpp',
                 'Replay.cpp', 'IocpServer.h'):
        source(name)
    for name in ('Server.ini', 'Protocol/Game.proto'):
        SOURCES[name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
    OUT.mkdir(parents=True,exist_ok=True)
    dest = OUT/'05_Network_Thread_Core_Guide.pdf'
    c = canvas.Canvas(str(dest), pagesize=(W,H), pageCompression=1)
    c.setTitle('네트워크와 멀티스레드 핵심 함수 읽기')
    c.setAuthor('민수홍 · 개인 C++ 서버 학습 자료')
    c.setSubject('클래스·함수·스레드·큐·수명 중심의 현재 코드 읽기')
    for i,page in enumerate(all_pages,1):
        print(f"[PAGE {i}] {page['title']}", flush=True)
        header(c,i,page,len(all_pages))
        kind=page.get('kind','code')
        if kind=='code':
            left=M; top=507; width=510
            for block in page['snippets']:
                top=snippet(c,block[0],block[1],left,top,width,
                            block[2] if len(block)>2 else None)-12
            right=notes(c,page['notes'],578,507,340)
            if min(top+12,right+17)<105:
                raise ValueError(f"Page {i} content overflow {top}, {right}")
            LAYOUT.append({'page':i,'lowest_content':min(top+12,right+17)})
        elif kind=='table':
            end=table(c,page['headers'],page['rows'],page['widths'],
                      links=page.get('links'),size=page.get('size',11))
            if page.get('after'):
                end=para(c,page['after'],M,end-17,W-2*M,11,18)
            if end<105:raise ValueError(f"Page {i} after table overflow: {end}")
            LAYOUT.append({'page':i,'lowest_content':end})
        elif kind=='diagram':
            diagram(c,page['diagram'])
        elif kind=='columns':
            a=notes(c,page['left'],M,507,415)
            b=notes(c,page['right'],503,507,415)
            if min(a,b)+17<105:raise ValueError(f"Page {i} notes overflow")
        takeaway(c,page['takeaway'])
        c.showPage()
    c.save()
    manifest={'date':'2026-10-21','file':str(dest),'pages':len(all_pages),
              'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),
              'sources_sha256':SOURCES,'excerpts':EXCERPTS,'layout':LAYOUT}
    (OUT/'05_Core_Guide_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'file':str(dest),'pages':len(all_pages),'lowest_content':min(p['lowest_content'] for p in LAYOUT)},ensure_ascii=False))


if __name__=='__main__':
    build()
