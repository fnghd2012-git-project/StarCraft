"""원본 로그와 대조 결과에서 문서용 수치를 만든다. 구간 p95를 전체 p95로 합치지 않는다."""
from pathlib import Path
import csv
import hashlib
import json
import math
import re

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]


def main():
    result = {k: int(v) for k, v in re.findall(r'^(\w+)=(\d+)$', (OUT/'result.txt').read_text(), re.M)}
    assert result['Ready'] == result['PeakWorld'] == result['LoggedOut'] == 1500
    assert result['Errors'] == 0 and result['Deaths'] > 0 and result['DeathReentries'] > 0
    metrics = []
    for line in (OUT/'server-3.out.txt').read_text(encoding='utf-8', errors='replace').splitlines():
        if line.startswith('[METRICS]'):
            metrics.append({k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)})
    with (OUT/'world-metrics.csv').open('w', newline='', encoding='utf-8') as f:
        writer=csv.DictWriter(f, fieldnames=metrics[0]); writer.writeheader(); writer.writerows(metrics)
    errors=[]
    for path in OUT.glob('server-*.err.txt'):
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            if re.search(r'\[(?:REDIS|REPLAY|WORKER|MATCH DB).*FAIL|\[FAIL\]', line):
                errors.append(line)
    assert not errors, errors
    assert not (OUT/'dummy.err.txt').read_text().strip()
    verification=json.loads((OUT/'redis-xml-validation.json').read_text())
    with (OUT/'sql-match-stats.csv').open(encoding='utf-8-sig', newline='') as f:
        sql=list(csv.DictReader(f))
    assert len(sql) == 1500
    assert sum(int(row['Deaths']) for row in sql) == result['Deaths']
    assert sum(int(row['Entries']) for row in sql) == result['Entered']
    replay_text=(OUT/'replay-verification.txt').read_text(encoding='utf-8-sig')
    replay_info=json.loads((OUT/'replay-file.json').read_text(encoding='utf-8-sig'))
    replay=dict(frames=int(re.search(r'frames=(\d+)',replay_text)[1]),
        snapshots=int(re.search(r'snapshots=(\d+)',replay_text)[1]),
        duration_seconds=float(re.search(r'duration=([\d.]+)',replay_text)[1]),
        min_reentry_tick_ms=int(re.search(r'min_reentry_ms=(\d+)',replay_text)[1]),
        bytes=Path(replay_info['Path']).stat().st_size,sha256=replay_info['Hash'])
    actions={k:int(v) for k,v in re.findall(r'\[ACTION\] (\w+)=(\d+)',replay_text)}
    assert actions['Enter']==result['Entered'] and actions['Return']==result['Deaths']
    ranking_log=(OUT/'ranking-release-test.txt').read_text(encoding='utf-8-sig')
    assert '[OK] Release ranking, Redis outage/recovery, final SQL transaction/idempotence.' in ranking_log
    with (OUT/'redis-samples.csv').open(newline='') as f:
        samples=list(csv.DictReader(f))
    def percentile(values, p):
        return sorted(values)[max(0, math.ceil(len(values)*p)-1)]
    query={}
    for metric in ('kills','deaths','entries'):
        values=[float(s['query_ms']) for s in samples if s['metric']==metric and int(s['players'])==1500]
        query[metric]=dict(samples=len(values),p50_ms=percentile(values,.5),p95_ms=percentile(values,.95),max_ms=max(values))
    summary=dict(date='2026-10-19',build='x64 Release',per_server=500,participants=1500,battle_seconds=240,
        death_cooldown_seconds=100,network_workers=4,db_workers=6,redis_workers=1,replay_workers=1,
        tick_hz=20,replay_hz=5,building_hp=750000,spatial='Grid',cell_size=25,redis_flush_ms=1000,
        result=result,match=verification,sql_rows=len(sql),redis_xml_sql_equal=True,server_failure_lines=errors,
        metrics=dict(samples=len(metrics),max_interval_tick_p95_us=max(m['tick_p95_us'] for m in metrics),
            max_tick_us=max(m['tick_max_us'] for m in metrics),skipped_ticks=sum(m['skipped_ticks'] for m in metrics),
            intervals_over_50ms=sum(m['tick_max_us']>50000 for m in metrics),
            max_snapshot_us=max(m['snapshot_max_us'] for m in metrics),
            max_interval_snapshot_p95_us=max(m['snapshot_p95_us'] for m in metrics),
            max_queue_peak=max(m['queue_peak'] for m in metrics),
            max_replay_queue_bytes=max(m['replay_queue_bytes'] for m in metrics),
            max_db_outstanding=max(m['db_outstanding'] for m in metrics),
            replay_failed=max(m['replay_failed'] for m in metrics)),
        redis_queries=query,replay=replay,ranking_regression='passed',
        limits=['One local run, not sustained 1500 alive players or Internet capacity',
            'Top-10 ZREVRANGE direct RESP round-trip sampled every 5s; not game Rank packet RTT',
            'World metrics include setup and cleanup; p95 values are per 5-second bucket',
            'Time-limited demonstration ends by server_shutdown, not a round victory',
            'No Redis OFF baseline; cannot attribute differences from old run to Redis or Grid alone',
            'Replay time is tick-based with 200ms frame batching; reentry 99600ms is not wall-clock cooldown measurement'])
    (OUT/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    names=['Server.ini','Launch/StartBattle.ps1','Protocol/Game.proto','Database/002_rankings.sql','test-ranking.ps1']
    names += [p.relative_to(ROOT).as_posix() for base in ('MultiThreadServer/MultiThreadServer/src','DummyClient/src')
              for p in (ROOT/base).glob('*') if p.suffix in ('.cpp','.h')]
    manifest=dict(date='2026-10-19',run_directory=(OUT/'run-directory.txt').read_text().strip(),
        binaries=json.loads((OUT/'simulation-binaries.json').read_text()),
        source_sha256={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in sorted(names)})
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
