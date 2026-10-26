from build_study_pdfs import *

def evidence():
    return json.loads((ROOT/R2/'spatial-summary.json').read_text(encoding='utf-8'))

def spatial_guide():
    result = evidence()
    rows=[]
    for scenario in ('Uniform','Clustered'):
        for mode in ('Linear','Grid'):
            r=result['benchmark'][scenario][mode]
            rows.append([scenario+' / '+mode, f"{r['total_median_us']/1000:.3f} ms", f"{r['total_p95_us']/1000:.3f} ms", f"{r['candidate_checks']:,}"])
    return [
Page('공통 게임 규칙에 검색 방식만 교체','28 / CSpatialIndex · 2026-10-18 추가',[
P('A/B 비교용 서버를 복제하지 않습니다. CGameLogic이 로그인·이동·전투·리플레이를 계속 담당하고, Snapshot과 범위 스킬의 반경 후보 수집을 CSpatialIndex에 맡깁니다. 같은 exe와 같은 게임 규칙에서 INI 값으로 Linear 또는 Grid를 선택합니다.'),
Flow([('Snapshot','현재 World 참가자와 좌표를 모아 인덱스 한 번 갱신'),('QueryLinear / QueryGrid','전체 좌표 또는 주변 셀을 검사하고 정확한 반경 후보 반환'),('SelectSnapshotPlayers','공통 CanSee → 진영 분리 → 거리/ID 정렬 → 적 32 + 아군 31'),('전송','자신 포함 최대 64명. 기존 Protobuf 조각 생성·Send 경로 공유')]),
Code('[Battle]\nSnapshotRadius=25\nSpatialSearch=Grid\nSpatialCellSize=25\nVerifySpatialSearch=0'),
P('Linear는 전체 좌표를 검사합니다. Grid는 x/cellSize, y/cellSize로 셀을 정하고 실제 유닛이 있는 셀만 unordered_map에 담습니다. 사각형 셀 범위를 조회한 뒤 거리 제곱으로 원의 경계를 다시 검사합니다. 아주 많은 빈 셀을 읽어야 하면 Linear로 대체합니다.'),
Note('갱신과 수명','매 Snapshot마다 재구축해 이동·사망·복귀로 낡은 셀이 남지 않습니다. 좌표는 값으로 보관하고 인덱스는 메인에서 구축하고 계산 장벽 안에서 워커가 읽습니다. DB에는 저장하지 않습니다. INI는 시작 시 읽으므로 변경 후 서버를 재시작합니다.')
],[Ref(S+'SpatialIndex.h','class CSpatialIndex'),Ref(S+'SpatialIndex.cpp','void CSpatialIndex::Rebuild'),Ref(S+'GameLogic.cpp','SelectSnapshotPlayers'),Ref('Server.ini','SpatialSearch=')]),

Page('1,500개 좌표로 검색 비용을 비교하기','29 / 실측 · 검색 전용 마이크로벤치마크',[
P('2026-10-18 x64 Release, Ryzen 7 7700X에서 고정 시드 20261018을 사용했습니다. 분산 배치는 0~120, 밀집 배치는 55~65 범위입니다. 두 방식에 같은 1,500개 좌표를 넣고 모든 점을 반경 25로 조회합니다. 셀 크기는 25입니다.'),
TableBlock(['배치 / 방식','전체 중앙값','전체 p95','거리 검사 횟수'],rows,[1.7,1.1,1.1,1.4]),
P('표의 전체 시간은 좌표 복사·인덱스 구축과 1,500회 조회를 합친 한 회의 시간입니다. 2회 예열 후 실행 순서를 번갈아 20쌍을 측정했습니다. p95는 20개 샘플의 nearest-rank 19번째 값입니다. 검사 횟수에는 자기 자신도 포함합니다.'),
P('분산 배치는 주변 셀 밖 후보를 제외할 수 있지만 밀집 배치는 대부분 같은 셀에 들어가 후보 수가 줄지 않습니다. 격자 구축과 해시 조회 비용도 있으므로 Grid가 모든 배치에서 빠르다고 가정하지 않습니다. 실제 값과 계산 범위로 개선 효과를 설명하세요.'),
Note('이 표가 측정하지 않은 것','CanSee·진영별 정렬·Protobuf·소켓·DB·실제 전투·RTT는 포함하지 않습니다. 10/16의 1,500명 시연 수치와 전후 성능으로 직접 비교할 수 없습니다. 빌드·회귀가 끝난 뒤 이 측정을 별도로 실행했습니다.'),
Small('원본: docs/results/2026-10-18/spatial-benchmark.csv\n명령: MultiThreadServer.exe --spatial-check --csv output.csv\n원시 샘플, 요약 JSON, 실행 파일·소스 SHA-256을 같은 날짜 폴더에 보관합니다.')
],[Ref(R2+'spatial-benchmark.csv'),Ref(R2+'spatial-summary.json'),Ref(S+'SpatialCheck.cpp','Measurement Measure')]),

Page('동일성 검사와 새 지표 읽기','30 / 정확성과 비용을 따로 확인',[
P('독립적인 전체 좌표 검사와 Linear/Grid 결과를 비교합니다. 0·최대 좌표·반경 경계·중복 위치·셀 크기 변경·삭제/빈 전장·잘못된 값과 분산/밀집 9,000개 쿼리를 검사합니다. 런타임 검증은 같은 순간의 최종 목록과 순서를 비교해 다르면 서버 오류로 중단합니다.'),
TableBlock(['실행 중 대조','이번 확인'],[[m, f"{result['live'][m]['verified']:,}개 수신 목록 대조 / 오류 0"] for m in ('Linear','Grid')],[1.2,3.5]),
Small('각 방식 120명(서버별 40명), 전투 35초, 사본 사망 대기 3초. 이동·전투·복귀 중 양쪽 목록을 비교한 기능 시험입니다. 난수 전투 경로가 달라 두 실행의 전투량/시간을 성능 대조로 사용하지 않습니다.'),
TableBlock(['5초 지표','정확한 뜻'],[['spatial_queries / candidates','선택한 방식의 수신자별 검색 수 / 거리 검사 후보 수. 검증용 반대 검색은 제외.'],['spatial_build_us','World 좌표 수집과 인덱스 구축 누계.'],['spatial_select_us','검색 + CanSee + 정렬·최종 선택의 누계. 반대 방식 검증 시간은 제외.'],['snapshot_send_us','Protobuf 구성·직렬화·송신 큐 적재 누계. 실제 소켓 전송 시간은 제외.'],['snapshot_p95_us / max_us','Snapshot 호출 전체 시간. Verify=1이면 추가 대조 시간도 포함.'],['spatial_verified','최종 목록과 순서가 일치한 대조 횟수. 지표 출력 때 초기화.']],[1.65,3.35]),
Note('비교할 때의 설정','성능 측정에서는 VerifySpatialSearch=0으로 둡니다. SnapshotRadius=0은 정원 64 이하의 전체 시야 경로라 검색 모드가 적용되지 않습니다. 전투 틱·기록·명령 큐 지표는 기존대로 함께 확인합니다.')
],[Ref(S+'GameLogic.cpp','void CGameLogic::BroadcastWorldState'),Ref(S+'GameReplay.cpp','void CGameLogic::ReportMetrics'),Ref('test-spatial-live.ps1'),Ref(R2+'spatial-summary.json')]),
]

def spatial_practice():
    return Page('같은 코드로 A/B 전환하고 설명하기','11 / Linear·Grid 실습',[
P('실행할 폴더의 Server.ini를 편집하고 서버를 재시작합니다. root Server.ini는 빌드할 때 Run64/Debug·Release에 복사됩니다. 비교용 A/B는 검색 방식 이름이며 게임 서버 A/B/C의 진영 구분과는 별개입니다.'),
Code('[Battle]\nSpatialSearch=Linear\nSpatialCellSize=25\nVerifySpatialSearch=1'),
P('먼저 Verify=1로 전투를 실행해 spatial_verified가 증가하고 오류가 없는지 봅니다. Grid로 바꾼 뒤 재시작해 같은 검사를 합니다. 순수 검색 성능을 측정할 때는 검증을 끄고 다른 빌드·시험이 끝난 상태에서 실행합니다.'),
H('루트에서 실행하는 재현 명령'),
Code('.\\test-spatial.ps1 -Configuration Release\n.\\test-spatial-live.ps1 -Configuration Release\n.\\Run64\\Release\\MultiThreadServer.exe --spatial-check `\n  --csv .\\spatial-benchmark.csv'),
P('test-spatial-live는 별도 포트·Realm과 설정 사본으로 Linear/Grid 각각 120명의 35초 전투를 실행합니다. 사망 대기를 시험용 3초로 줄이고 실제 전송 대상의 동일성을 확인합니다. 계정·유닛·세션은 로컬 project_db, 결과와 비용은 로그/CSV로 남깁니다.'),
TableBlock(['직접 답할 질문','확인할 근거'],[['게임 규칙을 복제했나?','SelectSnapshotPlayers와 패킷 생성은 공통, Query만 분기.'],['삭제한 유닛이 셀에 남나?','Snapshot마다 현재 좌표로 Rebuild.'],['밀집에서도 같은 이득인가?','Clustered의 후보 수·전체 시간과 최악 O(N²).'],['서버가 몇 배 빨라졌나?','검색 전용 측정만 완료. 서버 전체 개선율은 별도 시험 필요.']],[1.4,3.5]),
Note('발표에 넣을 한 장','공통 흐름 그림 → INI 두 값 → 같은 목록 검증 → 분산/밀집 측정표 → 남은 병목 순서로 보여 주세요. 새 클래스 수보다 선택 이유와 검증 범위가 드러나도록 구성합니다.')
],[Ref('test-spatial.ps1'),Ref('test-spatial-live.ps1'),Ref(R2+'spatial-summary.json')])
