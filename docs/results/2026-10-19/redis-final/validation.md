# Redis 포함 최종 시뮬레이션 — 2026-10-19

## 코드와 실행 환경

- 현재 작업 트리의 C++20 x64 Release를 `build.ps1 -Configuration Release -RunDbCheck`로 빌드하고 `127.0.0.1 project_db` 연결을 확인했다. 제품 C++ 코드는 이번 검증 중 변경하지 않았다.
- Ryzen 7 7700X, 8코어/16스레드, Windows 11 Pro 빌드 26200, 약 64GB. Game 3개·World 1개·대규모 더미 1개·MSSQL·Redis가 같은 PC에서 실행됐다.
- 서버별 네트워크 4/DB 6, World 기록 1/Redis 1, 더미 WSAPoll 8 워커. 전투 20Hz, 기록 5Hz, Grid 셀 25/반경 25, 검증 옵션 0. Redis 갱신 주기 1초.
- A/B/C 각 500명 준비 완료 후 전투 240초. 원본 Unit/Skill을 유지하고 실행 사본의 본진 HP만 750000으로 높였다. 사망 재입장 대기 100초, 경기 재시작 대기 300초를 유지했다.
- 대규모 실행 중 다른 빌드·통합 시험을 동시에 돌리지 않았다. 가벼운 문서 작업과 5초 간격의 읽기 전용 Redis 표본 수집을 수행했다.
- 기존 Redis 3.2.100의 6379 서비스와 별도 battle_ Realm 키를 사용했다. 서비스 중단이나 기존 키 초기화는 하지 않았다.

## 전투 결과

| 항목 | 결과 |
| --- | ---: |
| 요청 / 준비 / 최대 World / 로그아웃 | 1500 / 1500 / 1500 / 1500 |
| 입장 총횟수 | 3192 |
| 사망 / 사망 후 재입장 | 3075 / 1692 |
| 일반 유닛 공격 / 건물 공격 / 스킬 성공 | 8226 / 340 / 2054 |
| 정상 거부 응답 / 더미 오류 | 104301 / 0 |

거부 응답은 실패 연결·처리 오류와 별도 집계다. 거부 이유별 건수는 분류하지 않았다. 사망 대기자는 전장에서 빠지므로 1500명이 계속 살아 있는 부하가 아니다. 시간 제한 후 정상 종료했으며 `winner=0`, SQL `Complete=0`, `EndReason=server_shutdown`이다. 승자가 나온 한 경기라고 소개하지 않는다.

Realm: `battle_57761a86e07b4d7fabe1f0799e8656f6`  
MatchId: `8002d6d1d6db6c3a9c0b9a6e2bc013c9`

## Redis · XML · MSSQL 전원 대조

`redis-xml-validation.json`은 세 Sorted Set의 1500명 전원 점수를 XML과 비교한다. `validate_sql.ps1`은 해당 Realm/MatchId의 SQL 결과를 읽고 XML의 ID·계정·서버·킬·죽음·진입·승리/완료/종료 사유를 전원 대조한다. 각 서버 500명, 총 1500행이 일치했다. Hash 항목 수도 1500개다.

| 총계 | Redis = XML = MSSQL |
| --- | ---: |
| 킬 | 2677 |
| 죽음 | 3075 |
| 진입 | 3192 |

아군 범위 피해 등 죽음만 올라가는 경로가 있으므로 킬과 죽음의 합계는 같아야 하는 값이 아니다. 최종 집계 일치는 개별 이벤트의 영구 무손실을 의미하지 않는다.

## 지연 측정과 한계

- 준비·정리 포함 58개 로그 구간에서 **5초 구간 tick p95의 최댓값 81.509ms**, 개별 Tick 최대 **86.731ms**, 생략 틱 **15개**. 4개 구간에서 최대 틱 비용이 50ms를 넘었다.
- 개별 Snapshot 최대 82.176ms. 서버 전체 p95가 아니라 구간 통계/최댓값이다.
- 누적 참가자 1500명 이후 지표별 49회, 5초마다 기존 TCP 연결로 `ZREVRANGE 0 9 WITHSCORES`를 조회했다. p95는 nearest-rank 방식이다.

| 직접 Redis 조회 | p95 ms | 최대 ms |
| --- | ---: | ---: |
| kills | 0.2060 | 0.2455 |
| deaths | 0.1016 | 0.1690 |
| entries | 0.0622 | 0.0688 |

이 값은 Game Rank 패킷의 메인 큐·Redis 워커 큐·HGET·송신까지 합친 RTT가 아니다. CPU/메모리의 시간별 사용률, 고정 인원 전체 RTT, Redis OFF 대조군, 반복·장시간 안정성은 이번 측정에 없다. 이전 시연과 설정/작업 조건이 달라 개선율을 계산하지 않는다.

## 리플레이 검증

- Footer와 상태 재적용 검증 통과. 1457프레임, 전체 스냅샷 292개, 틱 기반 기록 길이 291.300초.
- `complete=1`은 파일 정상 마감을 뜻한다. SQL의 경기 완료 여부와 다른 값이다.
- Enter=3192, Return=3075, Attack=8566, Skill=2054. 전원 ID별 참가 서버도 확인했다.
- 최소 재입장 간격 99600ms는 리플레이 프레임의 **틱 기반 시간**이다. `Replay::Recorder::Capture`는 `(tick-startTick)*1000/TickRate`로 시간을 만들고, `GameLogic::Update`의 생략 틱은 tick_을 증가시키지 않는다. 200ms 프레임에 행동이 묶이는 영향도 있다.
- 실제 `GameLogic::Return`은 `steady_clock::now()+seconds(100)`을 저장하고 Enter에서 마감 시각을 검사한다. 이번 리플레이 시간 차이를 실제 대기가 99.6초였다는 근거로 쓰지 않는다. 실시간 대기를 정밀 검증하려면 별도 단조 시계 이벤트 시각이 필요하다.
- `battle-1500.png`는 기록 70초, 생존 1185명(테란 390·프로토스 454·저그 341)을 C++ 뷰어 그리기 함수로 내보낸 이미지다. 실제 Windows 창 캡처가 아니다.
- 원본 실행 폴더는 `run-directory.txt`, 파일 해시는 `replay-file.json`, 판독 로그는 `replay-verification.txt`를 참고한다. Run64/Release/Replays에는 `Battle-1500-8002d6d1d6db6c3a9c0b9a6e2bc013c9.replay`가 남아 있다.

## 별도 소규모 랭킹 회귀 시험

대규모 전투가 끝난 뒤 `test-ranking.ps1 -Configuration Release`를 다시 실행해 통과했다. 이 시험은 별도 4계정·임시 Redis 프로세스/포트·rank_ Realm·짧은 재입장/강한 스킬 등 시험용 수치를 사용한다.

검사: 실제 순위 패킷, 일반 처치, 스톰 아군 피해와 복귀한 시전자, 자발적 복귀, 재입장, 동점, 잘못된 metric, Redis 중단 중 전투, 빈 Redis 재시작 후 총계 복구, 승패 종료, 새 경기 초기화, SQL 트랜잭션과 XML 중복 재적재 멱등성.

기존 6379 서비스는 중단하지 않았다. 1500명이 동시에 순위를 요청한 부하 시험과도 구분한다. 로그는 `ranking-release-test.txt`에 있다. Debug 격리 시험은 10/18 결과이며 이번 최종 재실행은 Release다.

## 재현 및 학습 자료

- `run_simulation.py`: StartBattle 실행과 읽기 전용 Redis 표본·전원 비교.
- `validate_sql.ps1`: 해당 실행의 SQL 전원 대조와 공개 설정 사본/로그 보관.
- `summarize.py`: 원본 로그에서 summary.json·world-metrics.csv·manifest.json 생성.
- `manifest.json`: 사용한 실행 파일과 소스 SHA-256. 비밀번호와 Cluster.local.ini는 보관하지 않는다.
- 최종 PDF: 코드 가이드 38쪽, 면접 48문항 27쪽, 실습 워크북 17쪽. 저장 위치는 루트 `output/pdf/`, 생성 소스는 `docs/study/pdf_source/`다.
- Redis Insight 3.4.2의 `Portfolio Redis` 연결에서 `portfolio:*:kills` 등을 검색한다. Realm과 MatchId가 이번 실행인지 확인한다. TTL 기본 7일 이후에는 SQL/XML·CSV·리플레이가 학습 근거로 남는다.
