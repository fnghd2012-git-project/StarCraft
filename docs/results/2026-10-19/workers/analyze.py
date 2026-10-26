"""반복별 원본 표본을 검증하고 중앙값/범위 표와 정적 그래프를 만든다."""
from pathlib import Path
import csv
import gzip
import hashlib
import json
import math
import statistics

OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[3]
MATRIX=OUT/'matrix'


def rows(path):
    if path.exists():
        with path.open(encoding='utf-8-sig',newline='') as file: return list(csv.DictReader(file))
    with gzip.open(str(path)+'.gz','rt',encoding='utf-8',newline='') as file: return list(csv.DictReader(file))


def percentile(values,p):
    ordered=sorted(values)
    return ordered[max(0,math.ceil(len(ordered)*p)-1)] if ordered else 0


def save_csv(path,data):
    with path.open('w',encoding='utf-8-sig',newline='') as file:
        writer=csv.DictWriter(file,fieldnames=list(data[0])); writer.writeheader(); writer.writerows(data)


def verify_and_summarize():
    plan=json.loads((MATRIX/'plan.json').read_text())
    complete=json.loads((MATRIX/'completed.json').read_text())
    assert len(complete)==len(plan['schedule'])==18
    trial_rows=[]; resource_rows=[]; errors=0; completed_sessions=0; total_requests=0
    data_hashes=[]; normal_configs=[]
    import configparser
    for n,d,repeat in plan['schedule']:
        directory=MATRIX/f'r{repeat}-n{n}-d{d}'
        run=json.loads((directory/'run.json').read_text())
        assert run['network']==n and run['db']==d and run['healthy_exit']
        config=configparser.ConfigParser(interpolation=None); config.optionxform=str
        config.read(directory/'Server.ini',encoding='utf-8')
        assert config['Network']['WorkerCount']==str(n) and config['Database']['WorkerCount']==str(d)
        for section,key in [('Network','WorkerCount'),('Database','WorkerCount'),('Database','Realm'),('World','Port')]:
            config.remove_option(section,key)
        normal_configs.append({section:dict(config[section]) for section in config.sections()})
        data_hashes.append({file.name:hashlib.sha256(file.read_bytes()).hexdigest() for file in (directory/'Data').iterdir() if file.name!='GameServer.csv'})
        session=rows(directory/'session.csv'); assert len(session)==96 and all(x['Ok']=='1' for x in session)
        completed_sessions+=len(session)
        session_output=(directory/'session.out.txt').read_text()
        import re
        duration=float(re.search(r' seconds=([\d.]+)',session_output)[1])
        base=dict(network=n,db=d,repeat=repeat,phase='session',samples=len(session),errors=0,
            accepted=len(session),rejected=0,p50_ms=percentile([float(x['LoginMs']) for x in session],.50),
            p95_ms=percentile([float(x['LoginMs']) for x in session],.95),
            p99_ms=percentile([float(x['LoginMs']) for x in session],.99),
            max_ms=max(float(x['LoginMs']) for x in session),throughput=len(session)/duration,
            transfer_p95_ms=percentile([float(x['TransferMs']) for x in session],.95),
            logout_p95_ms=percentile([float(x['LogoutMs']) for x in session],.95),
            cycle_p95_ms=percentile([float(x['TotalMs']) for x in session],.95))
        trial_rows.append(base)
        for phase in ('ping','fixed-combat'):
            aggregate=rows(directory/f'{phase}.csv')[0]; raw=rows(directory/f'{phase}-samples.csv')
            count=int(aggregate['Requests']); accepted=sum(int(x['Accepted']) for x in raw)
            assert count==len(raw)>0 and accepted==int(aggregate['Accepted'])
            assert int(aggregate['Errors'])==0
            assert all(0<=float(x['OffsetMs'])<20000 and float(x['LatencyMs'])>0 for x in raw)
            if phase=='fixed-combat':
                assert all(x['Accepted']=='1' for x in raw if int(x['Sequence'])%2==1), 'Move request rejected'
            clients={int(x['Client']) for x in raw}; assert clients==set(range(96))
            last_sequence={}
            for sample in raw:
                client=int(sample['Client']); sequence=int(sample['Sequence'])
                assert sequence>last_sequence.get(client,-1)
                last_sequence[client]=sequence
            latency=[float(x['LatencyMs']) for x in raw]
            total_requests+=count
            trial_rows.append(dict(network=n,db=d,repeat=repeat,phase=phase,samples=count,errors=0,
                accepted=accepted,rejected=count-accepted,p50_ms=percentile(latency,.5),p95_ms=percentile(latency,.95),
                p99_ms=percentile(latency,.99),max_ms=max(latency),throughput=float(aggregate['RequestsPerSecond']),
                transfer_p95_ms=0,logout_p95_ms=0,cycle_p95_ms=0))
        metric_rows=rows(directory/'metrics.csv')
        for phase in ('session','ping','fixed-combat'):
            resources=run['measurements'][phase]['resources']
            world=[x for x in metric_rows if x['phase']==phase and x['source']=='World']
            game=[x for x in metric_rows if x['phase']==phase and x['source']=='A']
            resource_rows.append(dict(network=n,db=d,repeat=repeat,phase=phase,
                server_cpu_one_core_pct=sum(resources[name]['cpu_one_core_percent'] for name in ('A','B','C','World')),
                world_cpu_one_core_pct=resources['World']['cpu_one_core_percent'],
                game_a_cpu_one_core_pct=resources['A']['cpu_one_core_percent'],
                dummy_cpu_one_core_pct=resources['Dummy']['cpu_one_core_percent'],
                world_sampled_peak_mb=resources['World']['sampled_peak_working_set_mb'],
                dummy_sampled_peak_mb=resources['Dummy']['sampled_peak_working_set_mb'],
                world_metric_intervals=len(world),
                tick_bucket_p95_max_ms=max([int(x['tick_p95_us'])/1000 for x in world],default=0),
                tick_max_ms=max([int(x['tick_max_us'])/1000 for x in world],default=0),
                skipped_ticks=sum(int(x['skipped_ticks']) for x in world),
                command_queue_observed_max=max([int(x['queue_peak']) for x in world],default=0),
                game_db_outstanding_sampled_max=max([int(x['db_outstanding']) for x in game],default=0),
                world_db_outstanding_sampled_max=max([int(x['db_outstanding']) for x in world],default=0),
                replay_failed=max([int(x['replay_failed']) for x in world],default=0)))
    assert all(value==data_hashes[0] for value in data_hashes)
    assert all(value==normal_configs[0] for value in normal_configs)
    save_csv(OUT/'trials.csv',trial_rows); save_csv(OUT/'resources-summary.csv',resource_rows)
    def stats(values): return dict(median=statistics.median(values),minimum=min(values),maximum=max(values),runs=values)
    configurations=[(1,1),(2,2),(4,6),(1,2),(4,2),(4,1)]
    summaries=[]
    for n,d in configurations:
        summary=dict(network=n,db=d)
        for phase in ('session','ping','fixed-combat'):
            selected=sorted([x for x in trial_rows if (x['network'],x['db'],x['phase'])==(n,d,phase)],key=lambda x:x['repeat'])
            res=sorted([x for x in resource_rows if (x['network'],x['db'],x['phase'])==(n,d,phase)],key=lambda x:x['repeat'])
            assert len(selected)==len(res)==3
            summary[phase]={key:stats([x[key] for x in selected]) for key in ('p50_ms','p95_ms','p99_ms','max_ms','throughput','transfer_p95_ms','cycle_p95_ms','accepted','rejected')}
            summary[phase]['resources']={key:stats([x[key] for x in res]) for key in res[0] if key not in ('network','db','repeat','phase')}
        summaries.append(summary)
    result=dict(date=plan['date'],trials=18,sessions=completed_sessions,requests=total_requests,errors=errors,
        samples_per_session_run=96,latency_definition='nearest-rank on raw samples; table = median of three run quantiles, range = minimum..maximum',
        configurations=summaries,identical_non_worker_config=True,identical_unit_skill_map_data=True,
        fixed_fixture_sha256=data_hashes[0],source_and_binary_hashes=plan['binaries'])
    (OUT/'summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    flat=[]
    for item in summaries:
        for phase in ('session','ping','fixed-combat'):
            row=dict(network=item['network'],db=item['db'],phase=phase)
            for metric in ('throughput','p95_ms','p99_ms'):
                for key in ('median','minimum','maximum'): row[f'{metric}_{key}']=item[phase][metric][key]
            flat.append(row)
    save_csv(OUT/'comparison.csv',flat)
    return result


def charts(result):
    import math, subprocess, sys, shutil
    from reportlab.graphics.shapes import Drawing, String, Line, Circle
    from reportlab.graphics.charts.barcharts import VerticalBarChart
    from reportlab.graphics import renderPDF
    from reportlab.lib.colors import HexColor
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.ttfonts import TTFont
    font='WorkerFont'; pdfmetrics.registerFont(TTFont(font,'C:/Windows/Fonts/malgun.ttf'))
    ink=HexColor('#142b40')
    scratch=OUT/'chart-source'; scratch.mkdir(exist_ok=True)
    poppler=Path(sys.executable).parent.parent/'native/poppler/Library/bin/pdftoppm.exe'
    if not poppler.is_file():
        found=shutil.which('pdftoppm')
        if not found: raise RuntimeError('Chart rendering requires Poppler pdftoppm.')
        poppler=Path(found)
    def lookup(n,d): return next(x for x in result['configurations'] if (x['network'],x['db'])==(n,d))
    def panel(drawing,offset,groups,phase,metric,title,ylabel,color):
        values=[x[phase][metric] for x in groups]
        upper=max(x['maximum'] for x in values)*1.3
        power=10**math.floor(math.log10(upper)); upper=math.ceil(upper/power*4)/4*power
        chart=VerticalBarChart(); chart.x=offset+45; chart.y=58; chart.width=238; chart.height=157
        chart.data=[[x['median'] for x in values]]
        chart.categoryAxis.categoryNames=[f"{x['network']}/{x['db']}" for x in groups]
        chart.categoryAxis.labels.fontName=font; chart.categoryAxis.labels.fontSize=11
        chart.categoryAxis.strokeColor=HexColor('#b5c7d2')
        chart.valueAxis.valueMin=0; chart.valueAxis.valueMax=upper; chart.valueAxis.valueStep=upper/4
        chart.valueAxis.labels.fontName=font; chart.valueAxis.labels.fontSize=9
        chart.valueAxis.labelTextFormat='%.2f' if upper<5 else '%.1f'
        chart.valueAxis.strokeColor=HexColor('#b5c7d2')
        chart.valueAxis.visibleGrid=True; chart.valueAxis.gridStrokeColor=HexColor('#e2eaf0')
        chart.valueAxis.gridStart=chart.x; chart.valueAxis.gridEnd=chart.x+chart.width
        chart.bars[0].fillColor=HexColor(color); chart.bars[0].strokeColor=None
        chart.barWidth=18; chart.groupSpacing=17
        drawing.add(chart)
        drawing.add(String(offset+12,262,title,fontName=font,fontSize=12.5,fillColor=ink))
        drawing.add(String(offset+45,235,ylabel,fontName=font,fontSize=9.5,fillColor=ink))
        for index,value in enumerate(values):
            center=chart.x+(index+.5)*chart.width/len(groups)
            def ypos(v): return chart.y+v/upper*chart.height
            lo,hi=ypos(value['minimum']),ypos(value['maximum'])
            drawing.add(Line(center,lo,center,hi,strokeColor=ink,strokeWidth=1))
            for y in (lo,hi): drawing.add(Line(center-4,y,center+4,y,strokeColor=ink,strokeWidth=1))
            for dx,v in zip((-5,0,5),value['runs']):
                drawing.add(Circle(center+dx,ypos(v),1.8,fillColor=ink,strokeColor=None))
            drawing.add(String(center,hi+9,f'{value["median"]:.2f}',fontName=font,fontSize=10,textAnchor='middle',fillColor=ink))
        drawing.add(String(offset+164,28,'네트워크 / DB 워커',fontName=font,fontSize=10,textAnchor='middle',fillColor=ink))
    def finish(fig,path):
        fig.add(String(300,7,'막대: 3회 중앙값 · 검은 점: 개별 실행 · 오차 막대: 최소~최대 / 단일 PC·loopback',
            fontName=font,fontSize=9,textAnchor='middle',fillColor=HexColor('#526575')))
        source=scratch/Path(path).with_suffix('.pdf'); renderPDF.drawToFile(fig,str(source))
        subprocess.run([str(poppler),'-r','180','-singlefile','-png',str(source),str((OUT/path).with_suffix(''))],check=True,capture_output=True)
    fig=Drawing(600,280)
    groups=[lookup(4,db) for db in (1,2,6)]
    panel(fig,0,groups,'session','throughput','DB 워커와 세션 처리량','완료 흐름 / 초','#087e8b')
    panel(fig,300,groups,'session','p95_ms','DB 워커와 로그인 지연','로그인 p95 (ms)','#37658d')
    finish(fig,'db-workers.png')
    fig=Drawing(600,280)
    groups=[lookup(n,2) for n in (1,2,4)]
    panel(fig,0,groups,'ping','p95_ms','네트워크 워커와 Ping RTT','p95 (ms)','#087e8b')
    panel(fig,300,groups,'fixed-combat','p95_ms','네트워크 워커와 이동·공격 RTT','p95 (ms)','#37658d')
    finish(fig,'network-workers.png')
    fig=Drawing(600,280)
    groups=[lookup(n,d) for n,d in ((1,1),(2,2),(4,6))]
    panel(fig,0,groups,'session','throughput','전체 구성: 세션 처리량','완료 흐름 / 초','#087e8b')
    panel(fig,300,groups,'fixed-combat','p99_ms','전체 구성: 이동·공격 p99','p99 (ms)','#37658d')
    finish(fig,'overall-workers.png')


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(); parser.add_argument('--matrix',type=Path); parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    if args.matrix: MATRIX=args.matrix.resolve()
    if args.report: OUT=args.report.resolve(); OUT.mkdir(parents=True,exist_ok=True)
    result=verify_and_summarize(); charts(result)
    print(json.dumps({k:v for k,v in result.items() if k not in ('configurations','fixed_fixture_sha256')},indent=2))
