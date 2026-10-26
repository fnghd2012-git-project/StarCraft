"""반복별 원시 RTT와 로그에서 표·그래프를 만든다. 표본 구간을 합쳐 전체 p95로 부르지 않는다."""
from pathlib import Path
import configparser,csv,hashlib,importlib.util,json,math,re,socket,statistics,subprocess,xml.etree.ElementTree as ET
from reportlab.graphics.shapes import Drawing, String, Circle
from reportlab.graphics.charts.barcharts import VerticalBarChart
from reportlab.graphics import renderPDF
from reportlab.lib.colors import HexColor
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[3]
spec=importlib.util.spec_from_file_location('artifact_check',HERE/'validate_artifacts.py')
check=importlib.util.module_from_spec(spec);spec.loader.exec_module(check)

def rows(path):
    with path.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
def percentile(values,p):return sorted(values)[max(0,math.ceil(len(values)*p)-1)]
def write_csv(path,data):
    with path.open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)

def stress():
    folder=HERE/'stress';runtime=Path((folder/'run-directory.txt').read_text('utf-8-sig').strip())
    result={k:int(v) for k,v in re.findall(r'^(\w+)=(\d+)$',(folder/'result.txt').read_text(),re.M)}
    assert result['Ready']==result['PeakWorld']==result['LoggedOut']==1500
    assert result['Errors']==0 and result['Deaths']>0 and result['DeathReentries']>0
    config=configparser.ConfigParser();config.read(folder/'Server.ini',encoding='utf-8-sig')
    assert config['Network']['Mode']=='IOCP' and config['Memory']['UsePool']=='1' and config['Battle']['WorkerCount']=='4'
    assert config['Battle']['DeathReentrySeconds']=='100'
    xmlfiles=list(folder.glob('*.xml'));assert len(xmlfiles)==1
    match=ET.parse(xmlfiles[0]).getroot();players=match.findall('player');assert len(players)==1500
    prefix=f"{config['Redis']['KeyPrefix']}:{match.attrib['realm']}:{match.attrib['id']}"
    with socket.create_connection((config['Redis']['Host'],int(config['Redis']['Port'])),5) as sock:
        stream=sock.makefile('rb')
        def command(*args):
            parts=[str(x).encode() for x in args];sock.sendall(b'*'+str(len(parts)).encode()+b'\r\n'+b''.join(b'$'+str(len(p)).encode()+b'\r\n'+p+b'\r\n' for p in parts));return check.reply(stream)
        for metric in ('kills','deaths','entries'):
            data=command('ZRANGE',prefix+':'+metric,0,-1,'WITHSCORES')
            actual={data[i].decode():int(data[i+1]) for i in range(0,len(data),2)}
            assert actual=={p.attrib['id']:int(p.attrib[metric]) for p in players}
        assert {x.decode() for x in command('HKEYS',prefix+':players')}=={p.attrib['id'] for p in players}
    sql=rows(folder/'sql-match-stats.csv');assert len(sql)==1500
    assert sum(int(r['Deaths']) for r in sql)==result['Deaths']
    assert sum(int(r['Entries']) for r in sql)==result['Entered']
    metrics=[]
    for p in folder.glob('server-*.out.txt'):
        text=p.read_text('utf-8',errors='replace')
        stop=next(line for line in text.splitlines() if '[IOCP STOP]' in line)
        d={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',stop)}
        assert d['pending_io']==d['pool_in_use']==d['pool_exhausted']==0
        if p.name=='server-3.out.txt':
            for line in text.splitlines():
                if line.startswith('[METRICS]'):metrics.append({k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)})
    for p in folder.glob('server-*.err.txt'):
        assert not re.search(r'\[(?:REDIS|REPLAY|WORKER|MATCH DB).*FAIL|\[FAIL\]',p.read_text('utf-8',errors='replace'))
    write_csv(folder/'world-metrics.csv',metrics)
    replay=next((runtime/'Replays').glob('*.replay'))
    test=subprocess.run([ROOT/'Run64/Release/DummyClient.exe','--replay',replay,'--verify','--require','Attack'],capture_output=True,text=True,encoding='utf-8',creationflags=subprocess.CREATE_NO_WINDOW,timeout=90)
    (folder/'replay-verification.txt').write_text(test.stdout+test.stderr,encoding='utf-8');assert test.returncode==0
    assert 'complete=1 reason=server_shutdown' in test.stdout and 'match='+match.attrib['id'] in test.stdout
    actions={k:int(v) for k,v in re.findall(r'\[ACTION\] (\w+)=(\d+)',test.stdout)}
    assert actions['Enter']==result['Entered'] and actions['Return']==result['Deaths']
    ticks=max(m['tick_max_us'] for m in metrics)/1000;skipped=sum(m['skipped_ticks'] for m in metrics)
    val=dict(runtime=str(runtime),result=result,match=match.attrib,redis_xml_sql_equal=True,rows=1500,metrics=dict(max_tick_ms=ticks,skipped_ticks=skipped,max_interval_p95_us=max(m['tick_p95_us'] for m in metrics)),
        replay=dict(path=str(replay),sha256=hashlib.sha256(replay.read_bytes()).hexdigest(),bytes=replay.stat().st_size,verified=True,actions=actions),
        display={'참가 / 최대 동시 World':f"1500 / {result['PeakWorld']}",'사망 / 사망 후 재입장':f"{result['Deaths']} / {result['DeathReentries']}",'유닛 공격 / 스킬 성공':f"{result['UnitAttacks']} / {result['Skills']}",'연결·응답 오류':result['Errors'],'개별 Tick 최대 / 생략 틱':f'{ticks:.3f}ms / {skipped}','Redis·XML·MSSQL 대조':'1500명 전 항목 일치','종료 I/O / 사용 중 블록':'4개 서버 모두0','리플레이 종료 사유':'server_shutdown / winner=0'},
        note=f'240초 제한 시연을 정상 종료한 기록이며 승자가 난 경기는 아닙니다. 개별 Tick 최대 {ticks:.3f}ms, 생략 틱 {skipped}개입니다. 같은 PC의 단회 시연이므로 장시간 안정성이나1500명 인터넷 수용량을 입증하지 않습니다.')
    (HERE/'stress-validation.json').write_text(json.dumps(val,ensure_ascii=False,indent=2),encoding='utf-8');return val

def main():
    runs=json.loads((HERE/'matrix/completed.json').read_text());assert len(runs)==15
    numeric=[]
    for run in runs:
        folder=HERE/'matrix'/f"r{run['repeat']}-{run['label']}"
        item=dict(label=run['label'],repeat=run['repeat'])
        for phase,name in [('ping','ping'),('fixed-combat','combat')]:
            r=rows(folder/f'{phase}.csv')[0];samples=rows(folder/f'{phase}-samples.csv');latency=[float(s['LatencyMs']) for s in samples]
            assert int(r['Errors'])==0 and len(latency)==int(r['Requests']) and int(r['Requests'])==int(r['Accepted'])+int(r['Rejected'])
            item[name+'_p95']=percentile(latency,.95);item[name+'_p99']=percentile(latency,.99);item[name+'_rps']=float(r['RequestsPerSecond'])
        metrics=[m for m in rows(folder/'metrics.csv') if m['phase']=='fixed-combat' and m['role']=='World'];assert metrics
        item['snapshot_p95']=max(int(m['snapshot_p95_us']) for m in metrics)/1000
        item['tick_max']=max(int(m['tick_max_us']) for m in metrics)/1000
        item['skipped_ticks']=sum(int(m['skipped_ticks']) for m in metrics)
        item['cpu']=run['measurements']['fixed-combat']['World']['cpu_one_core_percent']
        item['memory']=run['measurements']['fixed-combat']['World']['peak_mb']
        io=rows(folder/'io.csv') if (folder/'io.csv').exists() else []
        world=next((x for x in io if x['role']=='World'),None)
        item['allocated']=int(world['pool_allocations']) if world else 0
        item['reused']=int(world['pool_reused']) if world else 0
        numeric.append(item)
    write_csv(HERE/'comparison-raw.csv',numeric)
    labels=['poll-1','iocp-1-heap','iocp-1-pool','iocp-2-pool','iocp-4-pool'];summary=[]
    for label in labels:
        data=[r for r in numeric if r['label']==label];assert len(data)==3
        row=dict(label=label)
        for key in ('ping_p95','combat_p95','snapshot_p95','cpu','memory'):
            row[key]=f"{statistics.median(r[key] for r in data):.3f}"
        row['allocations']='-' if label=='poll-1' else f"{int(statistics.median(r['allocated'] for r in data)):,} / {int(statistics.median(r['reused'] for r in data)):,}"
        summary.append(row)
    write_csv(HERE/'comparison.csv',summary)
    drawing=Drawing(1050,350)
    drawing.add(String(525,330,'Final build | 96 clients | 12 s x 3 trials | Network/DB = 2/2',fontSize=14,textAnchor='middle'))
    for column,(key,title) in enumerate(zip(['ping_p95','combat_p95','snapshot_p95'],['Ping RTT p95 (ms)','Combat RTT p95 (ms)','Max interval Snapshot p95 (ms)'])):
        chart=VerticalBarChart();chart.x=column*350+45;chart.y=55;chart.width=285;chart.height=225
        values=[[r[key] for r in numeric if r['label']==label] for label in labels]
        chart.data=[[statistics.median(v) for v in values]]
        chart.valueAxis.valueMin=0;chart.valueAxis.valueMax=max(max(v) for v in values)*1.15
        chart.valueAxis.valueStep=chart.valueAxis.valueMax/5
        chart.valueAxis.labelTextFormat='%.2f';chart.valueAxis.labels.fontSize=8
        chart.categoryAxis.categoryNames=['Poll W1','Heap W1','Pool W1','Pool W2','Pool W4']
        chart.categoryAxis.labels.fontSize=8
        chart.bars[0].fillColor=HexColor('#75B7BB');chart.bars.strokeColor=None
        drawing.add(chart);drawing.add(String(column*350+187,302,title,fontSize=12,textAnchor='middle'))
        for i,vals in enumerate(values):
            for j,value in enumerate(vals):
                x=chart.x+(i+.5)*chart.width/5+(j-1)*5
                y=chart.y+value/chart.valueAxis.valueMax*chart.height
                drawing.add(Circle(x,y,2.5,fillColor=HexColor('#142B40'),strokeColor=None))
    drawing.add(String(525,15,'Bars: median of 3 trials | Points: each trial | Same machine and loopback',fontSize=11,textAnchor='middle'))
    temporary=ROOT/'tmp/pdfs/comparison-chart.pdf';temporary.parent.mkdir(parents=True,exist_ok=True)
    renderPDF.drawToFile(drawing,str(temporary))
    poppler=Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/native/poppler/Library/bin/pdftoppm.exe'
    subprocess.run([poppler,'-png','-singlefile','-r','130',temporary,HERE/'comparison'],check=True,creationflags=subprocess.CREATE_NO_WINDOW)
    one=float(summary[2]['snapshot_p95']);four=float(summary[4]['snapshot_p95'])
    interpretation=f'전투 워커1→4의 Snapshot 구간p95 최대 중앙값은 {one:.3f}→{four:.3f}ms입니다. 이 값은 측정 구간에 완전히 포함된1초 로그의 p95 중 최대값이며 전체 Snapshot 표본의 p95가 아닙니다. 개선 여부는 반복 범위와 함께 보며 기본 공격 등 직렬 구간의 처리량 배수로 해석하지 않습니다.'
    result=dict(comparison=summary,interpretation=interpretation,stress=stress(),raw=numeric)
    (HERE/'summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
