"""원본 36쪽 그림·코드 근거·발표 문장을 연결하는 상세 해설 PDF 생성기."""
from pathlib import Path
import argparse,html,hashlib,json,re,subprocess,sys
from collections import Counter
from PIL import Image as PILImage
from pypdf import PdfReader
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.colors import HexColor
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import Paragraph,Table,TableStyle,Image

from content import SECTIONS
from appendices import APPENDICES

W,H=595.276,841.89
M=44;CW=W-2*M;TOP=H-119;BOTTOM=57
INK='#192126';GOLD='#9C7938';PAPER='#F7F5EE';MUTED='#566167';LINE='#D9D9CE';SOFT='#EEE9DA'
S='MultiThreadServer/MultiThreadServer/src/'
FONT='GBody';BOLD='GBold';DISPLAY='GDisplay';CODE='GCode'
USED={};EXCERPTS=[];LAYOUT=[]

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def clean(text):return str(text).replace('\u2011','-').replace('\u2013','-').replace('\u2014','-')
def esc(text):return html.escape(clean(text)).replace('\n','<br/>')
def style(size=10.2,bold=False,color=INK,leading=None):
    return ParagraphStyle('guide',fontName=BOLD if bold else FONT,fontSize=size,
                          leading=leading or size*1.52,textColor=HexColor(color),wordWrap='CJK',splitLongWords=True)
def p(text,size=10.2,bold=False,color=INK):return Paragraph(esc(text),style(size,bold,color))
def heading(text):return p(text,11.2,True,GOLD)
def unit(title,text,small=False):return [heading(title),p(text,9.8 if small else 10.2)]
def box(title,text):
    table=Table([[Paragraph('<b>'+esc(title)+'</b><br/>'+esc(text),style(10.3))]],colWidths=[CW])
    table.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1),HexColor(SOFT)),('LINEBEFORE',(0,0),(-1,-1),3,HexColor(GOLD)),('LEFTPADDING',(0,0),(-1,-1),13),('RIGHTPADDING',(0,0),(-1,-1),12),('TOPPADDING',(0,0),(-1,-1),10),('BOTTOMPADDING',(0,0),(-1,-1),10)]))
    return [table]
def table(rows,first=125):
    data=[[p(a,9.8,True),p(b,9.8)] for a,b in rows]
    t=Table(data,colWidths=[first,CW-first])
    t.setStyle(TableStyle([('VALIGN',(0,0),(-1,-1),'TOP'),('ROWBACKGROUNDS',(0,0),(-1,-1),[HexColor('#EEEDE5'),HexColor('#FFFFFF')]),('LEFTPADDING',(0,0),(-1,-1),9),('RIGHTPADDING',(0,0),(-1,-1),9),('TOPPADDING',(0,0),(-1,-1),8),('BOTTOMPADDING',(0,0),(-1,-1),8),('LINEBELOW',(0,-1),(-1,-1),.5,HexColor(LINE))]))
    return [t]

def source(root,relative):
    path=root/relative;assert path.is_file(),path
    USED[relative]=sha(path)
    return path.read_text('utf-8-sig').splitlines()
def reference(root,ref):
    path,needle=ref;lines=source(root,path)
    line=None
    if needle:
        hits=[i+1 for i,v in enumerate(lines) if needle in v]
        if not hits:raise ValueError(f'Missing reference: {path}: {needle}')
        line=hits[0]
    short=path.replace(S,'S/').replace('DummyClient/src/','D/')
    return short+(f':{line}' if line else '')
def excerpt(root,spec):
    path,scope,needle,count=spec;lines=source(root,path)
    scopes=[i for i,l in enumerate(lines) if scope in l]
    assert scopes,(path,scope)
    start=next((i for i in range(scopes[0],len(lines)) if needle in lines[i]),None)
    assert start is not None,(path,scope,needle)
    selected=lines[start:start+count]
    EXCERPTS.append(dict(file=path,scope=scope,start=start+1,end=start+len(selected),sha256=USED[path]))
    leading=min([len(line)-len(line.lstrip()) for line in selected if line.strip()] or [0])
    body=[]
    for index,line in enumerate(selected,start+1):
        line=line[leading:].split('//')[0].rstrip()
        if not line.strip():continue
        prefix=f'{index:4}  '
        # 실제 문장을 보존하며 긴 행만 이어 쓴다. 한글은 본문 글꼴로 렌더링한다.
        remaining=prefix+line
        while pdfmetrics.stringWidth(remaining,FONT,8.5)>CW-24:
            end=len(remaining)
            while end>8 and pdfmetrics.stringWidth(remaining[:end],FONT,8.5)>CW-24:end-=1
            body.append(remaining[:end]);remaining='      '+remaining[end:]
        body.append(remaining)
    text='<br/>'.join(esc(line).replace(' ','&#160;') for line in body)
    block=Table([[Paragraph(text,ParagraphStyle('source',fontName=FONT,fontSize=8.5,leading=12.5,textColor=HexColor(INK),wordWrap='CJK'))]],colWidths=[CW])
    block.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1),HexColor('#E9EDF0')),('LEFTPADDING',(0,0),(-1,-1),10),('RIGHTPADDING',(0,0),(-1,-1),10),('TOPPADDING',(0,0),(-1,-1),8),('BOTTOMPADDING',(0,0),(-1,-1),8)]))
    label=f"실제 소스 발췌 · {path.replace(S,'S/').replace('DummyClient/src/','D/')}:{start+1}"
    return [p(label,8.5,True,MUTED),block]

def height(group):return sum(item.wrap(CW,2000)[1]+5 for item in group)+6
def pack(title,tag,groups,original=0,dest=None):
    result=[];current=[];used=0;part=1
    for group in groups:
        h=height(group)
        if h>TOP-BOTTOM:raise ValueError(f'Block too tall: {title}: {h}')
        if current and used+h>TOP-BOTTOM:
            result.append(dict(title=title,tag=tag,groups=current,original=original,dest=dest if part==1 else None,part=part))
            current=[];used=0;part+=1
        current.append(group);used+=h
    result.append(dict(title=title,tag=tag,groups=current,original=original,dest=dest if part==1 else None,part=part))
    return result

def header(c,page,n,total):
    c.setFillColor(HexColor(PAPER));c.rect(0,0,W,H,fill=1,stroke=0)
    c.setFillColor(HexColor(INK));c.rect(0,H-42,W,42,fill=1,stroke=0)
    c.setFillColor(HexColor('#D9BE83'));c.setFont(FONT,8.5);c.drawString(M,H-26,clean(page['tag']))
    title=page['title']+(' · 계속' if page.get('part',1)>1 else '')
    para=Paragraph(esc(title),ParagraphStyle('title',fontName=DISPLAY,fontSize=17.4,leading=25,textColor=HexColor(INK),wordWrap='CJK'))
    _,h=para.wrap(CW,100);assert h<=52,(title,h)
    para.drawOn(c,M,H-57-h)
    c.setStrokeColor(HexColor(LINE));c.line(M,42,W-M,42)
    c.setFont(FONT,8);c.setFillColor(HexColor(MUTED));c.drawString(M,26,'민수홍 · 포트폴리오 상세 해설')
    c.setFont(BOLD,8.5);c.drawRightString(W-M,26,f'{n:03d} / {total:03d}')
    if page.get('dest'):
        c.bookmarkPage(page['dest']);c.addOutlineEntry(clean(page['title']),page['dest'],0,False)
    c.linkRect('', 'contents', (W-116,H-38,W-M,H-8),relative=0,thickness=0)
    c.setFillColor(HexColor('#D9BE83'));c.setFont(FONT,8);c.drawRightString(W-M,H-26,'목차로 이동')

def draw_page(c,page,n,total):
    header(c,page,n,total);y=TOP
    for group in page['groups']:
        for item in group:
            _,h=item.wrap(CW,2000)
            assert y-h>=BOTTOM-1,(page['title'],y,h)
            item.drawOn(c,M,y-h);y-=h+5
        y-=6
    LAYOUT.append(dict(page=n,original=page['original'],title=page['title'],remaining_pt=round(y-BOTTOM,2)))
    c.showPage()

def cover(c,total):
    c.setFillColor(HexColor(INK));c.rect(0,0,W,H,fill=1,stroke=0)
    c.setFillColor(HexColor('#D9BE83'));c.rect(0,0,11,H,fill=1,stroke=0)
    c.setFont(FONT,12);c.drawString(52,H-89,'민수홍')
    title=Paragraph('멀티스레드 포트폴리오<br/>페이지별 상세 해설',ParagraphStyle('cover',fontName=DISPLAY,fontSize=31,leading=47,textColor=HexColor('#F4EFDF'),wordWrap='CJK'))
    _,h=title.wrap(CW-10,250);title.drawOn(c,50,H-176-h)
    subtitle=p('왜 이렇게 그렸는가\n상자·화살표를 어떤 순서로 설명하는가\n실제 코드는 어디로 이어지는가',14,False,'#D9BE83');_,h=subtitle.wrap(CW-15,250);subtitle.drawOn(c,52,H-400-h)
    desc=p('원본 36쪽 전체를 한 장씩 해설합니다.\n그림 읽기 · 발표 예시 · 예상 질문과 답변 · 실제 코드 발췌\n객체 소유권과 쓰레드 경계, 측정 수치의 뜻까지 연결합니다.',11,False,'#E7E5DC');_,h=desc.wrap(CW-15,200);desc.drawOn(c,52,230-h)
    c.setFont(FONT,9);c.setFillColor(HexColor('#D9BE83'));c.drawString(52,57,'2026.10.21 · 현재 소스 / 과거 실행 근거를 구분한 학습용 해설')
    c.drawRightString(W-M,26,f'001 / {total:03d}')
    c.bookmarkPage('cover');c.addOutlineEntry('표지','cover',0,False);c.showPage()

def toc(c,entries,n,total,part):
    page=dict(title='원본 페이지별 해설 찾기'+(' · 2' if part==2 else ''),tag='원본 쪽수와 해설 쪽수는 다릅니다',groups=[],original=0)
    header(c,page,n,total)
    if part==1:
        c.bookmarkPage('contents');c.addOutlineEntry('페이지별 목차','contents',0,False)
    y=TOP
    for original,title,first,last,dest in entries:
        label=f'원본 {original:02d}' if isinstance(original,int) else str(original)
        para=p(label+'  '+title,9.1)
        _,h=para.wrap(CW-80,100);rh=max(26,h+9)
        c.setFillColor(HexColor('#EEEDE5'));c.rect(M,y-rh,CW,rh,fill=1,stroke=0)
        para.drawOn(c,M+8,y-5-h)
        c.setFillColor(HexColor(GOLD));c.setFont(BOLD,9);c.drawRightString(W-M-8,y-15,f'{first}-{last}' if first!=last else str(first))
        c.linkRect('',dest,(M,y-rh,W-M,y),relative=0,thickness=0)
        y-=rh+3
    assert y>=BOTTOM,(part,y)
    c.showPage()

def main():
    parser=argparse.ArgumentParser()
    for arg in ('root','portfolio','content','theme','output','assets'):
        parser.add_argument('--'+arg,type=Path,required=True)
    args=parser.parse_args();root=args.root.resolve()
    args.output.parent.mkdir(parents=True,exist_ok=True);args.assets.mkdir(parents=True,exist_ok=True)
    theme=json.loads(args.theme.read_text('utf-8-sig'))
    for name,src in [('GBody','Body'),('GBold','Bold'),('GDisplay','Display')]:
        pdfmetrics.registerFont(TTFont(name,str(args.theme.parent/theme['fonts'][src])))
    pdfmetrics.registerFontFamily(FONT,normal=FONT,bold=BOLD,italic=FONT,boldItalic=BOLD)
    original=PdfReader(args.portfolio);assert len(original.pages)==36
    portfolio=next(d for d in json.loads(args.content.read_text('utf-8-sig'))['documents'] if d['id']=='portfolio')
    assert len(portfolio['pages'])==36
    poppler=Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/native/poppler/Library/bin/pdftoppm.exe'
    images=args.assets/'original';images.mkdir(exist_ok=True)
    subprocess.run([str(poppler),'-r','110','-png',str(args.portfolio),str(images/'page')],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
    pngs=sorted(images.glob('page-*.png'));assert len(pngs)==36
    plans=[];spans=[]
    for item in SECTIONS:
        n=item['n'];title=portfolio['pages'][n-1]['title']
        refs=[reference(root,r) for r in item['refs']]
        first_groups=[
            [Image(str(pngs[n-1]),width=CW,height=CW*540/960),p(f'원본 {n:02d}/36 · 전체 배치를 찾는 축소 그림. 아래 해설과 다음 쪽의 코드·발표 예시를 함께 읽습니다.',8.5,False,MUTED)],
            unit('왜 이 배치로 그렸는가',item['why']),
            table(item['elements'],135),
        ]
        before=len(plans)
        plans.extend(pack(title,f'원본 {n:02d}/36 · 그림 읽기',first_groups,n,f'original-{n}'))
        second_groups=[box('이렇게 설명해 보세요 · 발표 문장 예시',item['talk'])]
        for heading_text,body in item['details']:second_groups.append(unit(heading_text,body))
        if item.get('excerpt'):second_groups.append(excerpt(root,item['excerpt']))
        for q,a in item['qa']:second_groups.append(unit('질문 · '+q,a))
        second_groups.append(box('설명할 때 주의할 점',item['caution']))
        second_groups.append([p('코드·검증 근거  '+' · '.join(refs),8.6,False,MUTED)])
        plans.extend(pack(title,f'원본 {n:02d}/36 · 발표와 코드 연결',second_groups,n))
        spans.append(dict(original=n,title=title,start=before,end=len(plans)-1,dest=f'original-{n}'))
    for i,item in enumerate(APPENDICES,1):
        start=len(plans)
        groups=[unit('원본과 연결',f"관련 원본: {item['original']}쪽. "+item['intro']),table(item['rows'],119),box('직접 말해 보기',item['closing'])]
        plans.extend(pack(item['title'],f'보충 해설 {i:02d}',groups,0,f'appendix-{i}'))
        spans.append(dict(original=f'보충 {i:02d}',title=item['title'],start=start,end=len(plans)-1,dest=f'appendix-{i}'))
    # 표지 1 + 사용법 1 + 목차 3 = 본문 시작은 6쪽. 목차 3쪽은 원본 36개와 보충 9개를 수록한다.
    prefix=5;total=prefix+len(plans)
    for span in spans:span.update(first=span['start']+prefix+1,last=span['end']+prefix+1)
    c=canvas.Canvas(str(args.output),pagesize=(W,H),pageCompression=1)
    c.setTitle('민수홍 멀티스레드 포트폴리오 페이지별 상세 해설');c.setAuthor('민수홍')
    c.setSubject('원본 36쪽 그림의 구성 이유·발표·코드·질의응답 상세 해설')
    c.setCreator('민수홍 / ReportLab');c.showOutline()
    cover(c,total)
    usage=pack('이 해설을 읽는 방법','민수홍 · 페이지별 상세 해설',[
        unit('한 장을 두 관점으로 읽습니다','먼저 원본 그림과 구성 이유·요소별 뜻을 읽고, 이어지는 발표 예시·코드·예상 질문을 봅니다. 자세한 내용이 길면 계속 쪽이 추가됩니다. “원본 04”는 포트폴리오 4쪽이며 이 해설의 4쪽이라는 뜻이 아닙니다.'),
        unit('화살표는 그림 종류에 따라 의미가 다릅니다','프로세스도는 통신·저장 연결, 객체도는 수명 관리·상태 보유·공유 참조, 시퀀스도는 시간 순서, 쓰레드도는 큐·작업 전달입니다. 도형 모양만 보고 상속·함수 호출·실행 시간을 추정하지 않습니다.'),
        unit('축소 그림과 코드 발췌','전체 그림은 페이지 위치를 찾는 용도입니다. 작은 원본 코드 글씨를 억지로 읽기보다 해설의 실제 소스 발췌와 S/D 경로를 사용하세요. 발췌는 중간 문장을 생략할 수 있고 긴 줄은 이어 썼으며 의미를 바꾸지 않았습니다.'),
        unit('쓰레드 명칭','메인은 게임 로직 Update/Tick을 실행합니다. IOCP 쓰레드(워커)는 통신, 전투 쓰레드(워커)는 독립 계산, DB 쓰레드(워커)는 SQL 작업을 맡습니다. 객체와 쓰레드를 일대일로 읽지 마세요.'),
        unit('자료 시점','코드는 2026-10-21 현재 작업 트리, 성능 숫자는 2026-10-19 비교·시연 기록을 기준으로 설명합니다. 이후 가독성 변경 회귀와 과거 성능 측정을 섞지 않았고 이 PDF를 만들면서 새 부하 시험을 실행하지 않았습니다.'),
        unit('경로 약어와 탐색','S/ = MultiThreadServer/MultiThreadServer/src/, D/ = DummyClient/src/. 나머지는 프로젝트 루트 D:/work/Josun_Server/git_ProJect 기준입니다. 목차 행을 누르면 해당 해설로, 상단 “목차로 이동”을 누르면 목차로 돌아갑니다.'),
        box('발표 문장은 외우기보다 바꿔 말하세요','“무엇을 해결했는가 → 그림 순서 → 누가 데이터를 바꾸는가 → 어느 코드에서 확인했는가 → 어디까지 검증했는가” 순서로 본인의 말로 연습합니다.')
    ],0,'usage')
    assert len(usage)==1
    draw_page(c,usage[0],2,total)
    entries=[(s['original'],s['title'],s['first'],s['last'],s['dest']) for s in spans]
    for i,(a,b) in enumerate(((0,18),(18,36),(36,len(entries))),3):toc(c,entries[a:b],i,total,i-2)
    for i,page in enumerate(plans,prefix+1):draw_page(c,page,i,total)
    c.save()
    manifest=dict(date='2026-10-21',output=str(args.output),pages=total,original_pages=36,
                  original_sha256=sha(args.portfolio),original_working_path=str(args.portfolio),
                  original_content_sha256=sha(args.content),sources_sha256=USED,excerpts=EXCERPTS,
                  guide_sources_sha256={p.name:sha(p) for p in Path(__file__).parent.glob('*.py')},
                  page_map=spans,layout=LAYOUT,sha256=sha(args.output))
    args.output.with_suffix('.manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),'utf-8')
    print(f'[OK] Detailed guide: {total} pages / {len(EXCERPTS)} real code excerpts / {len(USED)} source files',flush=True)

if __name__=='__main__':main()
