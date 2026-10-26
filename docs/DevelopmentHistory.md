# 개발 과정과 이전 검증 기록

이 문서는 README 개편 전의 누적 기록입니다. 현재 진입점과 실행 순서는 [README](../README.md)를 확인하세요. 날짜별로 서로 다른 빌드·부하 조건을 사용했으며, 과거 수치를 현재 코드의 재측정값으로 해석하지 않습니다. 로컬 output/PDF와 Run64 파일은 저장소 배포 항목이 아닙니다.

# StarCraft

C++ 멀티스레드 게임 서버와 C++ 더미 클라이언트를 먼저 개발하고, 최종적으로 언리얼 클라이언트를 연결하는 개인 포트폴리오 프로젝트입니다.
테란·프로토스·저그 게임 서버와 공용 전장, 마지막 건물 생존에 따른 승패, 전투 리플레이를 목표로 합니다.

## 현재 구현 범위

로컬 MSSQL 계정 저장, 설정/데이터 로딩, Protobuf 통신과 게임 서버 로그인 → World 입장 → 이동·공격·스킬 → 원래 서버 복귀, 전투 기록과 더미 리플레이를 구현했습니다.

- x64 Debug/Release 빌드와 C++20 사용
- Winsock 초기화 및 정리
- ODBC 환경·연결·명령 핸들의 RAII 관리
- Windows 인증으로 MSSQL 접속, 로그인/조회 제한 시간 5초
- DB 이름, 정수 1, 한글 문자열 조회 결과 확인
- ODBC SQLSTATE·서버 오류 번호·진단 메시지 출력
- std::jthread 생성, 합류, 결과 확인
- 명령줄 기본 점검과 오류 종료 코드
- `GameServer.csv`의 서버 목록·종족·포트, `Server.ini`의 World·워커 수·접속/송신 버퍼 상한 적용
- `Unit.csv`, `Skill.csv`, `StartPoint.csv`, `Map.ini` 읽기와 기본 유효성 검사
- `Network.Mode=IOCP/WSAPoll` 선택, 공통 프레임 처리와 게임 명령 큐
- IOCP AcceptEx·WSARecv·WSASend, 세션별 완료 직렬화, 취소 완료 후 버퍼·세션 회수
- `Memory.UsePool=0/1`로 I/O 작업 객체와 4096바이트 버퍼 재사용 비교
- `Battle.WorkerCount=1/2/4`로 한 전장의 이동·Storm 피해·시야/직렬화 계산 분배
- 메인에서 순서대로 HP·죽음·킬·승패 확정, `VerifyParallel=1` 직렬 계산 결과 대조
- 길이 2바이트·종류 2바이트 헤더, 분할/병합 수신, 부분 송신, 비정상 패킷 연결 종료
- 서버/더미가 공유하는 `Game.proto`, ServerVersionCheck/Ping/Pong 직렬화·역직렬화 및 필드 검증
- C++ 더미 클라이언트의 동시 접속, 서버/맵 정보 확인, 한글·바이너리·최대 크기 Ping/Pong 검증
- 맵 X/Y 최소 0, 시작 좌표와 건물 범위 검사
- 접속 서버 종족의 랜덤 유닛 배정, 서명된 일회용 입장권/복귀권, 출발 서버 로그아웃과 도착 서버 로그인
- World의 서버 판정 이동·건물 충돌·기본 공격·보호막/HP·MP 회복, 종족별 아군 판정
- 사망/자발적 복귀 후 100초 대기, 건물 파괴 시 진영 퇴장, 마지막 생존 진영 승리, 300초 후 경기 재시작
- 콘솔 더미 서버 선택, World 입장 시 적 건물 자동 공략, 수동 이동/공격, 상태 표시와 자동 복귀 로그인
- GameServer의 다섯 명령 분리, 게임 서버 일반/월드 채팅, World 전장 내부 일반 채팅
- ID·비밀번호 로그인, 첫 ID 자동 등록, 계정의 고정 ID와 게임 서버별 유닛 저장/복구
- ODBC DB 쓰레드(워커)와 제한된 작업 큐, DB 커밋 후 응답, 중복 로그인 차단과 서버 이동/재시작 복구
- Skill1~5의 현재 10종 스킬, 은신 탐지·상태 이상·범위 효과, 피해 타입/유닛 크기별 보정
- World 틱별 상태 변화/전투 명령과 사용 데이터를 기록하는 비동기 리플레이 파일
- 더미의 오프라인 재생·일시정지·배속·시점 이동·무결성 검사와 실제 수신 상태 대조
- 더미의 Ping/이동/혼합 전투 부하, 응답 지연 CSV와 서버 틱·DB/기록 큐 지표
- World 경기별 Redis 킬·죽음·진입 순위, 별도 I/O 워커, 장애 후 현재 경기 총계 복구
- 종료 경기의 MSSQL 결과 저장과 XML 원본 재적재, 더미의 상위 10명·내 순위 조회

언리얼 연동은 아직 구현하지 않았습니다. 원작 전체 유닛/전투 공식의 완전 복제도 현재 범위가 아닙니다.
같은 계정은 어느 게임 서버에서든 같은 플레이어 ID를 사용하며 유닛은 서버별로 처음 한 번 랜덤 배정해 저장합니다.
사망·자발적 복귀·World 로그아웃의 재입장 대기는 계정에 저장하므로 재접속하거나 다른 게임 서버를 선택해도 유지됩니다.
기존 RTT 측정은 World 64명이었고, 이후 96명 고정 부하로 워커 6개 구성×3회 비교를 수행했습니다. 별도로 같은 PC에서 1,500명 입장·전투·사망 재입장을 시연했습니다. 서로 다른 부하이며 인터넷 환경이나 대규모 서비스 수용량을 의미하지 않습니다.

게임 규칙과 데이터 컬럼 설명은 [설계 문서](../docs/Design.md)에 정리했습니다.

## Redis 경기별 랭킹

현재 PC의 Redis 3.2.100은 `127.0.0.1:6379`에서 실행 중입니다. 기존 서비스를 바꾸지 않고 프로젝트 전용 `portfolio:<Realm>:<MatchId>:...` 키를 사용합니다. C++ 연결 라이브러리는 hiredis 1.3.0이며 `build.ps1`이 `setup-redis.ps1`을 통해 `.deps` 안에 준비합니다. Debug/Release 라이브러리를 각각 만들고 정적 링크하므로 실행 폴더에 hiredis DLL을 복사할 필요가 없습니다.

```powershell
& 'C:\Program Files\Redis\redis-cli.exe' PING
.\init-database.ps1
.\build.ps1 -Configuration Debug
```

`StartServers.cmd`로 서버를 켜고 `StartDummy.cmd`에서 로그인 → `world`로 들어간 뒤 다음 명령을 1초 이상 간격으로 입력합니다. 자동 전투를 멈추고 보려면 `auto off`를 입력합니다.

```text
rank kills
rank deaths
rank entries
```

상위 10명과 `[MY]` 내 순위·킬·죽음·진입을 표시합니다. 같은 점수는 PlayerId 역순으로 개별 순위를 매깁니다. Game 서버에서는 World에 입장한 뒤 조회합니다. Redis 장애는 `unavailable`로 표시하며 게임 전투는 계속 진행합니다.

- 입장 성공마다 진입 +1. 실패·대기 중 요청은 제외합니다.
- 적 유닛의 마지막 타격자에게 킬 +1, 죽은 유닛 계정에 죽음 +1. 건물 처치는 제외합니다.
- 스톰 시전자가 복귀한 뒤에도 처치 ID를 유지합니다. 아군·자기 자신 처치는 킬을 주지 않습니다.
- 사망·자발적 복귀·접속 종료 후에도 같은 경기의 계정 총계는 유지합니다. `back`, 연결 종료, 본진 파괴, 경기 종료는 죽음이 아닙니다.
- 새 경기 ID에서 총계를 초기화합니다. `MaxMatchParticipants`는 한 경기의 누적 참가 계정 수를 제한합니다.

`Server.ini`의 `[Redis]`에서 사용 여부·로컬 포트·키 접두사·I/O 시간 제한·갱신 주기·보관 시간·조회 큐 크기를 설정합니다. 기본 갱신 1초, 타임아웃 200ms, TTL 7일입니다. 설정 변경 후 World를 재시작합니다. 변화가 없어도 30초마다 현재 총계를 갱신합니다.

World 메인 스레드는 집계만 하고 `CRedisWorker` 한 스레드가 hiredis 연결을 독점합니다. 총계 복사본은 최신 하나로 합치고 `MULTI/EXEC` 파이프라인으로 `ZADD` 세 개와 `HSET`을 수행합니다. 재전송은 증가 연산이 아닌 총계 덮어쓰기여서 중복 가산되지 않습니다. 조회는 `ZREVRANGE`, `ZREVRANK`, `HGET`을 사용합니다. 패킷 번호는 `CM_Rank=31`, `SM_RankReply=32`, 본문은 `Protocol/Game.proto`에 있습니다.

경기 종료 시 메모리의 최종 결과를 `project_db.dbo.PortfolioMatch`와 `PortfolioMatchStats`에 한 트랜잭션으로 저장합니다. DB 쓰레드(워커)가 먼저 실행 설정 폴더의 `MatchResults/<MatchId>.xml`을 남깁니다. DB 장애 후 이 파일이 정상 보관되어 있으면 다음 명령으로 중복 없이 재적재할 수 있습니다.

```powershell
.\restore-match-result.ps1 -Path .\Run64\Debug\MatchResults\<MatchId>.xml
.\test-ranking.ps1 -Configuration Debug
.\test-ranking.ps1 -Configuration Release
```

시험은 별도 Redis 프로세스·포트·DB Realm을 사용하고 기존 6379 서비스를 중단하지 않습니다. 일반 처치·스톰 아군 피해·복귀한 시전자·재입장·동점·잘못된 요청·Redis 중단 중 전투·빈 Redis 재시작 후 총계 복구·새 경기 초기화·SQL 결과 및 재적재 멱등성을 검사합니다. 테스트 결과는 `docs/results/2026-10-18/redis/`에 보관합니다.

현재 범위는 개인 PC의 단일 Redis/단일 World입니다. Redis 인증·TLS·Cluster·Sentinel과 다중 World 공동 집계는 포함하지 않습니다. World 강제 종료 시 진행 경기 전체를 복원하는 WAL은 없으며, DB 큐/디스크 실패·종료 제한에 따른 최종 결과 유실 가능성은 오류 로그로 알립니다. Redis 장애가 경기 교체까지 이어지면 이전 경기 Redis 기록은 불완전할 수 있으므로 종료 결과는 SQL/XML로 확인합니다. 10/16의 과거 시연과 아래 10/19 Redis 활성 최종 시뮬레이션을 구분합니다.

### 2026-10-19 Redis 포함 최종 시뮬레이션

Release를 다시 빌드하고 로컬 `project_db` 연결을 확인한 뒤 A/B/C 각 500명이 World에 입장한 상태에서 240초 전투를 실행했습니다. 서버별 네트워크 4/DB 6, World Redis 1/기록 1, 더미 8 워커, Grid, 20Hz, Redis 갱신 1초입니다. 사망 대기는 100초이고 시연 사본의 본진 HP만 750,000으로 높였습니다.

| 결과 | 관측값 |
| --- | --- |
| 준비 / 최대 World / 로그아웃 | 1,500 / 1,500 / 1,500 |
| 사망 / 사망 후 재입장 | 3,075 / 1,692 |
| 더미 오류 / 거부 응답 | 0 / 104,301 (거부 이유별 수량 미분류) |
| Redis·XML·MSSQL 전원 비교 | 1,500명 모두 일치 |
| 킬 / 죽음 / 진입 총계 | 2,677 / 3,075 / 3,192 |
| 5초 구간 tick p95 중 최대 / 개별 Tick 최대 | 81.509ms / 86.731ms |
| 생략 틱 | 15 |

정해진 시간 후 정상 종료한 `server_shutdown` 기록이며 승패가 끝난 경기나 1,500명이 계속 살아 있는 부하가 아닙니다. 50ms 틱 예산 초과가 있으므로 실서비스 수용량 보증으로 사용하지 않습니다. 별도 소규모 Release 시험으로 승패 종료·Redis 중단 중 전투·빈 Redis 복구·새 경기 초기화·SQL 재적재 멱등성도 다시 확인했습니다.

Redis 상위 10명 직접 조회는 지표별 49개 표본이며 킬 순위 p95는 0.2060ms입니다. 기존 TCP 연결의 `ZREVRANGE` 왕복만 재어 게임 Rank 패킷·큐·Hash 조회를 합친 응답 시간과 다릅니다. Redis OFF 대조군이나 반복 측정이 없어 Redis 비용·Grid 개선율은 계산하지 않습니다.

리플레이의 최소 재입장 간격 99.6초는 **틱 기반 기록 시간**입니다. 생략 틱과 200ms 기록 묶음의 영향을 받으며 실제 100초 제한은 `steady_clock`으로 검사합니다. 실제 대기 시간을 정밀 검증하려면 별도 단조 시계 이벤트 시각을 기록해야 합니다.

실행 로그·설정 사본·전원 대조·지표·리플레이 이미지·해시는 [최종 검증 기록](../docs/results/2026-10-19/redis-final/validation.md)에 정리했습니다. 최신 학습 PDF 5종은 `output/pdf/`에 있으며 10/18 이전판은 `output/pdf/archive/2026-10-18/`에 보관합니다.

## 2026-10-19 워커 수 비교 최종 측정

아래 성능 수치는 4바이트 헤더로 변경하기 전의 8바이트 헤더 실행 결과입니다. 원본 측정값을 유지하며, 현재 코드의 재측정 결과나 헤더 축소에 따른 성능 개선으로 해석하지 않습니다.

공통 게임 로직·같은 서버 실행 파일·Redis/리플레이 활성·Grid를 유지하고 `Server.ini`의 `[Network] WorkerCount`와 `[Database] WorkerCount`를 바꿨습니다. 전체 비교 1/1·2/2·4/6에 원인 분리용 1/2·4/2·4/1을 더해 6개 구성, 각 3회씩 총 18회를 실행했습니다. 아래는 **실행별 지표 3개의 중앙값**입니다.

| 네트워크 / DB | 세션 완료/초 | 로그인 p95 (ms) | 이동·공격 p99 (ms) |
| --- | ---: | ---: | ---: |
| 1 / 1 | 10.20 | 2,845.54 | 42.03 |
| 2 / 2 | 16.47 | 1,748.22 | 38.63 |
| 4 / 6 | 42.12 | 600.83 | 37.39 |

- 세션은 기존 계정 96개, 동시 작업 32개로 Game A 로그인→World 이동→로그아웃을 완료한 흐름입니다. 네트워크=4 고정에서 DB 1→6의 중앙값은 **10.39→42.12건/초, 4.06배**입니다. 비밀번호 PBKDF2와 ODBC를 함께 실행하는 워커의 효과이며 순수 SQL 성능이 아닙니다.
- DB=2 고정, 네트워크 1→2→4에서 Ping p95는 **1.261→0.710→0.383ms**입니다. 이동·공격 p95는 **33.52→33.69→31.08ms**이며 반복 범위가 겹칩니다. 동일 비율의 전투 성능 향상을 주장하지 않습니다.
- Ping/전투는 96연결, 각각 연결당 목표 상한 100/20회/초, 워밍업 3초 후 20초 측정입니다. 응답을 기다리는 closed-loop이며 실제 전송률도 기록했습니다. 인원 유지를 위해 시험 사본만 HP=100000·종족당 한 유닛·스킬 없음으로 고정했습니다.
- 세션 1,728건·응답 3,907,163개, 더미 오류 0. 공격 거부 303,539건은 별도 집계이며 전투 RTT에 포함합니다. Redis/XML/MSSQL 참가자 3,672명 점수 대조, 리플레이 18개 검증을 통과했습니다.
- 1/1도 메인·기록·Redis 스레드가 따로 있습니다. Grid는 검색 방식이며 전장 스레드 분할 옵션이 아닙니다. 이번 측정은 단일 PC에서 워커 수를 비교한 결과입니다.

```powershell
.\build.ps1 -Configuration Release -RunDbCheck
.\test-workers.ps1 -Pilot
.\test-workers.ps1
```

기존 `Server.ini`는 유지하며 별도 실행 사본과 격리 DB Realm을 사용합니다. [조건·전체 수치·반복 범위·한계](../docs/results/2026-10-19/workers/validation.md), [CSV](../docs/results/2026-10-19/workers/comparison.csv), [그래프](../docs/results/2026-10-19/workers/overall-workers.png)를 보세요. 이전 측정은 코드 가이드 39~43쪽에 보존했고, 이번 IOCP·전투 계산 병렬화·풀 구현과 최종 측정은 뒤의 CURRENT 장에 추가했습니다. 설정 설명서는 별도 04권입니다.

## 각 게임 서버 10명으로 한 경기 진행

`Run64/Release/StartMatch10.cmd`를 더블클릭합니다. 게임 서버/더미를 따로 실행하거나 계정 30개를 직접 입력할 필요가 없습니다. 로컬 project_db 스키마는 설치되어 있어야 합니다.

1. 현재 GameServer.csv의 서버마다 시연 계정 10개로 로그인합니다. 기본 A/B/C 구성은 총 30명이며 유닛은 기존 서버 로직으로 랜덤 배정합니다.
2. 전원이 World에 들어온 것을 확인한 뒤 자동 건물 공략을 시작합니다. 콘솔에서 경과 시간과 건물 HP를 확인할 수 있습니다.
3. 최종 건물 하나가 남아 경기가 끝나면 전원의 원래 게임 서버 복귀/로그아웃과 완료된 리플레이를 검증합니다.
4. 2D 리플레이 창으로 전환됩니다. **Play**를 누르면 기본 1배속으로 계속 재생하고, 필요할 때 배속을 바꿉니다.

유닛/스킬/맵의 수치를 변경하지 않습니다. 이 기존 한 경기 실행기는 auto buildings로 건물만 공략합니다. 유닛 전투·자동 스킬·재입장은 아래 StartBattle1500.cmd를 사용합니다. 기본 공격이 없는 메딕/하이 템플러/디파일러는 건물 앞에서 대기합니다.
별도 로컬 포트와 `demo_*` DB Realm을 사용하고 이 실행이 만든 서버/더미만 정리합니다. 새 계정 행은 기록 조사용으로 DB에 남습니다.
게임 서버는 CSV 수만큼, World는 하나, 더미는 참가 인원만큼 백그라운드 프로세스로 실행하며 화면은 진행 창 하나로 모읍니다. 원래 서버 설정과 개발 계정은 유지합니다.
경기당 설정/유닛 목록/로그/원본 기록은 `Run64/Release/MatchRuns/<실행 시각-ID>`에, 완료된 리플레이 사본은 `Run64/Release/Replays/Match-10x3-<매치ID>.replay`에 저장합니다. 나중에는 `StartReplay.cmd`에서 해당 파일을 선택하면 됩니다.

출력 폴더에서 인원과 제한 시간을 바꾸거나 재생 없이 한 경기만 실행할 수도 있습니다. World 최대 인원은 64명이며 기본 제한 시간은 600초입니다. 제한 시간 안에 경기가 끝나지 않으면 실패를 알리고 로그를 남깁니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\StartMatch.ps1 -PerServer 10
powershell -NoProfile -ExecutionPolicy Bypass -File .\StartMatch.ps1 -PerServer 10 -NoReplay
```

Debug 출력 폴더에도 같은 스크립트가 있습니다. 서버별 인원을 늘려 현재 정원을 넘으면 시연용 설정의 정원만 필요한 만큼 늘립니다.
2026-10-16 원본 수치로 A/B/C 각각 10명의 한 경기를 실행해 A(저그) 우승과 30명 전원의 복귀/로그아웃을 확인했습니다. 기록은 84.350초, 1,687프레임이며 [검증 결과](../docs/results/2026-10-16/match-10x3.txt)와 [참가 유닛 목록](../docs/results/2026-10-16/match-10x3-participants.csv)을 남겼습니다.

## 더미로 리플레이 보기

World는 시작부터 틱별 기록을 남깁니다. 승패가 결정되거나 서버를 **Ctrl+C로 정상 종료**하면 `Run64/Debug/Replays` 또는 `Run64/Release/Replays`에 `<매치ID>.replay`가 생깁니다.
같은 출력 폴더의 **StartReplay.cmd**를 더블클릭하고 파일 번호를 선택합니다. 서버와 DB를 켜지 않아도 됩니다.
현재 PC의 Debug/Release 출력 폴더에는 `Sample-Gameplay-*.replay`도 준비했습니다. HP/속도/대기 시간을 줄인 통합 테스트 기록이며 원본 밸런스의 경기는 아닙니다. 이 샘플은 Git에 포함하지 않습니다.
기본 실행은 **C++ Win32/GDI+ 2D 뷰어**입니다. 맵에 9종 유닛의 이미지 아이콘, 이동, 공격선, 스킬 영역, 본진 HP와 승자를 표시합니다. 유닛을 클릭하면 좌표·HP·보호막·MP·상태 이상을 확인합니다. 화면의 유닛 아이콘이 겹칠 수 있으며, 실제 게임의 걷기/공격 애니메이션은 아직 없습니다.

- **Play**만 누르면 기본 **1배속**으로 계속 재생됩니다. **Pause** 후 다시 Play해도 선택한 배속을 유지합니다.
- 배속 목록에서 고르거나 숫자(0.25~16)를 입력하고 **배속 적용** 또는 Enter를 누르면 재생 중에도 바로 변경됩니다.
- 아래 시간 막대로 원하는 시점으로 이동합니다. **첫 전투로**는 첫 공격 1초 전으로 이동합니다.
- Space는 재생/정지, 좌우 방향키는 5초 이동입니다. 기록 시작 부분은 로그인/입장 대기 시간이므로 유닛이 없을 수 있습니다.
- JPG/GIF 이미지는 `Assets/Replay`에 보관하고 빌드 때 exe 옆에 복사합니다. 실행 중 다운로드는 하지 않으며, 이미지가 빠지면 종족색 원으로 표시합니다. 원본 출처/저작권은 `Assets/Replay/SOURCES.txt`에 기록했습니다.

텍스트 출력과 아래 명령은 `--console` 옵션으로 계속 사용할 수 있습니다.

| 입력 | 동작 |
| --- | --- |
| `play` / `pause` | 재생 / 일시정지 |
| `speed 2` | 2배속, 허용 범위 0.25~16 |
| `seek 10` | 기록 시작 10초 시점으로 이동 |
| `step 1` | 한 틱씩 진행하며 상태 확인 |
| `status` / `events` | 현재 틱의 전체 상태 / 명령·입퇴장 이벤트 |
| `quit` | 종료 |

명령줄 예시는 `Run64/Debug` 안에서 실행합니다.

```powershell
.\DummyClient.exe --replay-list
.\DummyClient.exe --replay .\Replays\<매치ID>.replay --verify
.\DummyClient.exe --replay .\Replays\<매치ID>.replay
.\DummyClient.exe --replay .\Replays\<매치ID>.replay --console
.\DummyClient.exe --replay .\Replays\<매치ID>.replay --image .\battle.png --at 40
.\DummyClient.exe --replay .\Replays\<매치ID>.replay.part --verify --allow-partial
```

`--image`는 해당 시점의 전장과 유닛 정보를 PNG로 저장합니다. 실시간 창과 같은 그리기 함수를 사용하며 재생 버튼 등 Windows 기본 컨트롤은 포함하지 않습니다.
그래픽 조작 검사는 `test-replay-window.ps1 -Configuration Release -ReplayFile <15초 이상 기록>`으로 기본 1배속, 배속 변경/유지, 일시정지, 시간 이동, 끝에서 재시작과 화면 출력을 점검합니다.

`.part`는 강제 종료나 쓰기 실패로 끝맺지 못한 파일입니다. 기본 모드는 거부하고, `--allow-partial`을 명시한 경우에만 정상 체크섬을 가진 앞부분을 검사합니다. 중간 손상은 이 옵션으로도 허용하지 않습니다.
`complete=1`은 파일이 정상 마감됐다는 뜻입니다. 실제 경기 종료는 `reason=round_finished`, 진행 중 서버 정상 종료는 `reason=server_shutdown`으로 구분합니다.
현재 방식은 **서버가 확정한 상태를 재생**합니다. 입력만으로 전투 공식을 다시 실행하는 결정적 시뮬레이션이나 중단된 World 경기의 재개 기능은 아닙니다.

`Server.ini`의 `[Replay]`에서 사용 여부, 경로, 큐 8MiB, 파일 128MiB, 전체 스냅샷 간격 1초를 설정합니다. 나머지 틱은 바뀐 유닛/퇴장만 기록하고 건물·영역 상태는 매 틱 남깁니다.
큐/파일 상한 또는 디스크 오류에서는 `[REPLAY FAIL]`을 출력하고 해당 기록만 중단합니다. 파일 삭제/자동 보관 정리는 아직 없습니다.
계정 ID·전투 상태는 포함하지만 비밀번호·입장권·채팅·Cluster 키는 기록하지 않습니다. 관전자용 전체 상태이므로 은신 유닛도 기록에 포함됩니다.

## 1,500명 자동 전투와 서버 정원

빌드 후 `Run64/Release/StartBattle1500.cmd`를 실행합니다. 게임 서버 A/B/C와 World, 대규모 더미 프로세스 하나를 실행하고 준비가 끝나면 240초 동안 자동 전투합니다. 완료 후 저장한 리플레이 창을 엽니다. `첫 전투` 버튼으로 접속 준비 구간을 건너뛴 뒤 Play를 누릅니다.

- A/B/C에서 500명씩 로그인하고 원래 서버에서 로그아웃한 뒤 World에 로그인합니다.
- 1,500명 입장 완료 후 중앙으로 이동하며 주변 적 유닛을 우선 공격합니다. 사망 시 원래 게임 서버로 복귀하고 서버가 알려준 100초 대기가 끝나면 같은 유닛으로 재입장합니다.
- World의 건물 파괴/경기 재시작 규칙을 그대로 적용합니다. 입장이 거절되면 게임 서버로 돌아와 기다립니다.
- 실행별 `BattleRuns/<시각-ID>`에 별도 Realm/포트/데이터를 만듭니다. 시연용 건물 HP만 750,000으로 높여 재입장 시간을 확보하고 원본 Unit.csv/Map.ini는 바꾸지 않습니다. 240초에 서버를 정상 종료한 기록은 승패가 끝난 경기가 아니라 `server_shutdown` 시연 기록입니다.
- 서버 틱은 20Hz, 대규모 시연의 상태 기록은 5Hz입니다. 기록 사이 명령은 순서를 유지해 다음 프레임에 모으므로 개별 명령 시각은 최대 200ms 단위로 표시됩니다. 메모리 큐 32MiB/파일 1GiB 상한을 사용합니다.
- 더미는 기본 8개 WSAPoll 워커로 소켓을 나눕니다. 유닛마다 스레드나 exe를 만들지 않습니다. 자동 요청은 250ms 간격이며 사거리·쿨다운 등 정상 거부도 통계에 포함합니다.
- 유닛 충돌/밀어내기는 없으므로 같은 좌표에 여러 유닛이 겹칠 수 있습니다. 뷰어는 100명 초과 시 화면상 같은 구역의 같은 종족을 대표 이미지와 `×인원`으로 표시합니다. 실제 좌표와 인원은 기록 그대로입니다.

```powershell
# Run64/Release 안에서 인원/실행 시간을 바꾸는 예
.\StartBattle.ps1 -PerServer 500 -Seconds 240
# 30명 동작 점검: 테스트 사본에만 3초 사망 대기를 적용
.\StartBattle.ps1 -PerServer 10 -Seconds 55 -TestCooldownSeconds 3 -NoReplay
```

프로젝트 루트 `Server.ini`에서 정원을 수정한 뒤 빌드하면 Run64에 복사됩니다. 시작 시 읽는 설정이므로 실행 중인 서버는 재시작해야 합니다.

```ini
[Battle]
MaxGamePlayers=500
MaxWorldPlayers=1500
SnapshotRadius=25

[Server.A]
MaxPlayers=500
[Server.B]
MaxPlayers=500
[Server.C]
MaxPlayers=500
```

`MaxGamePlayers`는 게임 서버 공통 기본값이고 `[Server.<CSV Index>] MaxPlayers`가 있으면 해당 서버에 우선 적용합니다. 예를 들어 CSV의 Index가 `2`이면 `[Server.2]`로 설정합니다. World는 `MaxWorldPlayers`를 사용합니다. 각 값은 1~2048이며, 최대 설정값 자체가 성능 보증은 아닙니다.

게임 정원은 로그인된 인원과 DB 로그인 처리 중인 예약 인원을 포함합니다. ServerVersionCheck/채팅 중계 전용 연결은 제외하고 World 이동 로그아웃 완료 시 슬롯을 반환합니다. 복귀 로그인에도 정원 제한을 적용합니다. 네트워크 연결 상한은 별도 `WorkerCount × MaxClientsPerWorker`이며 사용자 수와 중계/전환 중 연결을 수용할 여유가 필요합니다.

`test-capacity.ps1`은 A별도값=1/B기본값=2로 8개 동시 로그인을 검사하고, World 정원=1 초과 거부·복귀권과 잘못된 값 0 거부를 확인합니다. 실행 결과는 `result.txt`, 서버 지표는 `server-*.out.txt`, 전체 전투는 `Replays/*.replay`에 남습니다.

2026-10-16 Release 시연에서 A/B/C 각 500명·최대 동시 1500명, 사망 2954회·사망 후 재입장 1551회(최소 100.4초), 오류 0·전원 로그아웃을 확인했습니다. 리플레이 1470프레임/약 62.16MiB를 검증했습니다. 일부 틱은 50ms 예산을 넘었고(최대 104.199ms), 이는 동작 검증이며 정밀 성능 벤치마크가 아닙니다. [설정·결과·한계](../docs/results/2026-10-16/battle-1500-validation.txt)를 함께 확인하세요.


## 공통 게임 로직과 Linear/Grid 비교

게임 규칙은 `CGameLogic` 하나를 사용하고 Snapshot과 Plague/Storm의 반경 후보 검색을 `CSpatialIndex`로 선택합니다. `Server.ini`의 `[Battle]`에서 설정하며 **서버 재시작 후 적용**됩니다. A/B 비교의 A와 B는 게임 서버 이름 A/B/C와 무관합니다.

```ini
[Battle]
SnapshotRadius=25
SpatialSearch=Grid
SpatialCellSize=25
VerifySpatialSearch=0
```

- `Linear`: 기존 방식처럼 전체 좌표를 검색합니다. 옛 INI에 새 항목이 없으면 이 방식이 기본입니다.
- `Grid`: 매 Snapshot에 격자를 한 번 만들고 주변 셀에서 정확한 반경 후보를 찾습니다. 현재 배포 설정입니다.
- 거리·은신·진영별 정렬·적 32/아군 31 제한·패킷 생성은 공통입니다. 공격·스킬 검색과 게임 상태의 스레드 소유권은 그대로입니다.
- `SpatialCellSize`는 1~1,000,000입니다. 밀집 배치에서는 후보가 줄지 않을 수 있습니다. 조회할 셀이 전체 좌표 수보다 많으면 Linear 검색으로 대체합니다.
- `VerifySpatialSearch=1`은 같은 순간 두 방식의 최종 수신 목록과 순서를 대조합니다. 불일치는 오류로 중단합니다. 비용 측정은 `0`으로 합니다.
- `SnapshotRadius=0`은 기존 전체 시야 경로이며 정원 64 이하에서만 허용합니다. 이 경로에서는 공간 검색을 하지 않습니다.

루트 설정은 빌드 시 `Run64/Debug`, `Run64/Release`에 복사됩니다. 실행 중 사용할 설정은 exe 옆 파일입니다.

```powershell
.\test-spatial.ps1 -Configuration Release
.\test-spatial-live.ps1 -Configuration Release
.\Run64\Release\MultiThreadServer.exe --spatial-check --csv .\spatial-benchmark.csv
```

`test-spatial-live`는 Linear/Grid 각각 서버별 40명, 총 120명의 35초 전투를 실행합니다. 사본의 사망 대기는 3초이며 원본 설정은 수정하지 않습니다. 계정·유닛·세션은 기존 로컬 `127.0.0.1/project_db`에 기록하고 임시 Realm으로 구분합니다. 공간 인덱스는 메모리, 비교 결과는 로그/CSV로 관리하므로 추가 SQL 스키마는 없습니다.

`spatial_build_us`는 좌표 수집·인덱스 구축, `spatial_select_us`는 검색·시야·정렬, `snapshot_send_us`는 Protobuf 구성·직렬화의 워커별 시간 합계입니다. 실제 소켓 전송 시간은 포함하지 않습니다. `snapshot_p95_us/max_us`는 Snapshot 전체 시간이며 검증을 켜면 반대 검색 비용도 포함합니다. `spatial_candidates`는 선택한 방식의 거리 검사 수, `spatial_verified`는 성공한 목록 대조 횟수입니다.

이번 검증과 1,500개 고정 좌표의 검색 전용 측정은 [비교 결과](../docs/results/2026-10-18/spatial-comparison.md)에 정리합니다. 검색 함수의 결과를 서버 전체 RTT 개선율로 해석하지 않습니다.

## 부하 측정과 운영 지표

별도 테스트 Realm/포트/데이터에서 실행하는 자동 검사는 저장소 루트에서 실행합니다. 테스트 계정 행과 로그는 조사용으로 남깁니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-replay.ps1 -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-load.ps1 -Configuration Debug -Quick
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-load.ps1 -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-load.ps1 -Configuration Release -ReplayFailure Directory
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-load.ps1 -Configuration Release -ReplayFailure FileLimit
```

Release 기본 측정은 Ping 64명, 이동 16/32/64명, 혼합 전투 64명 60초입니다. 요청 빈도는 각 10회/초이고 이전 응답을 받은 뒤 다음 요청을 보내는 방식입니다.
자동 검사만 HP 100000, MP/초기 MP 1000, 이동 속도 100으로 바꿔 사망으로 인원이 줄지 않게 합니다. 원본 유닛 수치는 바꾸지 않습니다.
`build/load-*`에 `latency.csv`, `resources.csv`, 서버/더미 로그와 검증 가능한 리플레이가 남습니다. 측정 스크립트가 만든 프로세스만 마지막에 종료합니다.

실행 중인 개인 테스트 서버에 직접 부하를 주려면 해당 설정의 World 정원 이하로 지정합니다. 이 명령도 테스트 계정을 생성합니다.

```powershell
.\DummyClient.exe --load --config Server.ini --mode world --clients 16 --seconds 15 --rate 10 --csv load.csv
```

`ping`은 통신 워커 응답, `world`는 이동 처리, `combat`은 이동/기본 공격/CSV 스킬을 섞습니다.
지연은 요청부터 응답까지의 실측 RTT이며 p50/p95/p99/최댓값을 기록합니다. 정상적인 쿨다운·MP·대상 거부는 Rejected, 연결/응답 실패는 Errors로 나눕니다.
SetupP95Ms는 접속·비밀번호 인증·World 이동·초기 이동까지의 준비 시간이며 전투 지연에 섞지 않습니다. 요청 측정 전 3초를 기다립니다.
resources.csv의 CPU는 준비/정리까지 포함한 전체 단계에서의 **World 프로세스, 논리 코어 한 개=100%** 기준이며 메모리는 1초 간격으로 관측한 Working Set 최댓값입니다.

`[METRICS]`는 기본 5초마다 명령 수/거부 수, 명령 큐 관측 최대치, World 틱 p95/최대 비용(마이크로초), 밀려 생략한 틱, 미완료 DB 작업 수와 리플레이 큐 바이트/실패 상태를 출력합니다.
틱 비용에는 전투·기록 직렬화·주기적 상태 송신 큐 삽입이 포함됩니다. 요청 대기 시간이나 DB 지연 그 자체를 나타내지는 않습니다.
WSAPoll 대기는 `Network.PollTimeoutMs=5`로 조절합니다. 작은 응답에는 TCP_NODELAY를 사용하며 이미 게임 응답이 있는 소켓은 바로 송신 가능 여부를 확인합니다.

2026-10-16, Ryzen 7 7700X(8코어/16스레드), Windows 빌드 26200, x64 Release, 네트워크 쓰레드(워커) 2개·DB 쓰레드(워커) 2개·20Hz·기록 활성화 조건의 실측입니다. 서버 4개/더미/로컬 MSSQL을 같은 PC에서 실행했습니다.

| 모드 | 인원 | 측정 시간 | 요청 | 정상 허용 / 정상 거부 | 오류 | p95 / p99 RTT | 처리량 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Ping | 64 | 8초 | 5,120 | 5,120 / 0 | 0 | 0.480 / 0.591ms | 638.9/s |
| 이동 | 16 | 8초 | 1,280 | 1,280 / 0 | 0 | 30.950 / 31.158ms | 160.0/s |
| 이동 | 32 | 8초 | 2,560 | 2,560 / 0 | 0 | 31.066 / 31.633ms | 319.6/s |
| 이동 | 64 | 10초 | 6,400 | 6,400 / 0 | 0 | 31.150 / 31.582ms | 639.5/s |
| 혼합 전투 | 64 | 60초 | 38,400 | 22,428 / 15,972 | 0 | 31.428 / 31.871ms | 639.8/s |

혼합 전투 단계의 World CPU는 코어 한 개 기준 평균 6.089%, 관측 메모리 최대 24.020MiB였습니다. 이 CPU 구간에는 접속 준비/로그아웃도 포함됩니다.
전투의 5초 지표 구간에서 틱 p95 최고 2.823ms, 개별 틱 최고 4.164ms, 생략한 틱 0을 관측했습니다. 요청 RTT와 틱 실행 비용의 차이에 큐·스케줄링·송수신 대기가 포함됩니다.
리플레이 2,661프레임/전체 스냅샷 134개, 약 10.81MiB를 강제 종료 전 정상 프레임까지 검증했습니다. 현재 수치는 60초 구간 측정이며 장시간 안정성/최대 수용량을 확정한 결과는 아닙니다.
원본 [지연 CSV](../docs/results/2026-10-16/latency.csv), [리소스 CSV](../docs/results/2026-10-16/resources.csv), [서버 지표](../docs/results/2026-10-16/world-metrics.txt), [측정 당시 실행 파일 해시](../docs/results/2026-10-16/benchmark-binaries.txt)를 함께 보관합니다.

기록 경로 생성 실패와 파일 1MiB 상한 초과를 각각 유도했을 때도 64명 이동 요청 9,600건씩이 성공했습니다. 상한 초과 파일은 `.part`로 남아 마지막 정상 프레임까지만 검증했습니다.
기능 전체 회귀는 `powershell -NoProfile -ExecutionPolicy Bypass -File .\test-all.ps1 -Configuration Debug`로 실행하며 Release도 같은 방식입니다. 성능 측정과 동시에 실행하지 않습니다.
2026-10-16 최종 Debug/Release 모두 네트워크, 게임 흐름/리플레이, 계정/10종 스킬, 자동 건물 공략, 중계 장애, 서버 재시작/DB 저장 실패 검사를 통과했습니다. [검증 로그 요약](../docs/results/2026-10-16/validation.txt)에 각 실행의 상세 로그 폴더를 남겼습니다.

## 더블클릭으로 전장 테스트

처음 한 번 저장소 루트에서 `powershell -NoProfile -ExecutionPolicy Bypass -File .\init-database.ps1`로 로컬 `project_db`에 계정 테이블과 프로시저를 설치합니다. 설치는 반복 실행 가능하며 기존 데이터를 삭제하지 않습니다.
빌드 후 `Run64/Debug` 또는 `Run64/Release`에서 진행합니다.

1. `StartServers.cmd` 실행: CSV의 게임 서버 전부와 World를 각각 별도 프로세스로 실행합니다. 이미 실행한 서버는 먼저 종료합니다.
2. `StartDummy.cmd` 실행: 서버 Index → 계정 ID → 비밀번호 순으로 입력합니다. 처음 사용한 ID는 등록하고, 기존 ID는 비밀번호를 검사해 저장된 유닛을 불러옵니다.
3. `world` 입력: 게임 서버 로그아웃 후 World에 로그인합니다. 종족의 시작 위치에 같은 유닛으로 생성됩니다.
4. 입장하면 별도 명령 없이 가까운 적 건물로 이동해 기본 공격합니다. 건물이 파괴되면 다음 적 건물을 선택하며, 최종 승패가 나면 원래 게임 서버로 돌아옵니다.
5. `status`로 유닛/건물 상태와 자동 공략 여부를 확인합니다. `auto off`로 중지, `auto on`으로 재개하며 `auto 30`은 30초만 실행합니다. `move 60 60` 또는 `attack <player_id>`를 입력하면 자동 공략을 끄고 수동 명령을 수행합니다.
6. `back`은 World 로그아웃 후 원래 서버로 복귀 로그인하며 100초 재입장 대기가 적용됩니다. 사망도 동일합니다.
7. `server`로 서버 선택 화면, `quit`으로 종료합니다. 서버 창은 Ctrl+C로 종료합니다.

자동 공략 중에도 `status`, 채팅과 `back`을 사용할 수 있습니다. 사망/경기 종료 통지를 받으면 공략을 멈추고 원래 서버에 자동 로그인합니다.
자동 전투는 주변 적 유닛을 먼저 공격하고 적이 없으면 건물을 공략합니다. 메딕은 부상 아군 치료, 하이 템플러는 사이오닉 스톰, 디파일러는 플레이그를 사용합니다. 사거리·MP·쿨다운·피해량은 서버가 판정하며, 나머지 스킬은 직접 입력합니다.
실제 전투는 사용자가 이동·공격·스킬을 조작하는 방식이며 일반 길찾기는 구현 범위에서 제외합니다. 더미 자동 시연에만 기존의 직선 이동/맵 중앙 경유를 사용합니다. 이동 선분이 건물을 통과하면 서버가 멈추며, 더미에서 경로를 찾지 못하면 `move`로 직접 이동할 수 있습니다.

GameServer에서는 아래 다섯 명령만 사용할 수 있습니다. 명령 이름은 대소문자를 구분하지 않습니다.

| 명령 | 동작 |
| --- | --- |
| `world` | 현재 GameServer 로그아웃 후 World 로그인 |
| `status` | 현재 서버와 배정받은 유닛 상태 확인 |
| `server` | DB 로그아웃 완료 후 다른 서버 선택. 계정/비밀번호 입력 후 그 서버의 저장 유닛 복구 |
| `quit` | 더미 종료 |
| `chat 일반 내용` / `chat 월드 내용` | 현재 게임 서버 일반 채팅 / 모든 게임 서버 월드 채팅 |

`move`, `attack`, `skills`, `skill`, `auto`, `back`은 World에서 사용합니다. GameServer는 직접 받은 전투 패킷도 거부합니다.
`skills`는 보유 스킬과 입력 방법을 표시합니다. `skill 1`은 자기 대상, `skill 1 대상ID`는 유닛 대상, `skill 1 60 60`은 좌표 대상 예시입니다. 실제 슬롯은 Unit.csv에 따릅니다. `skill stop`은 치료/잠복 전환을 취소하며 스킬 입력도 자동 공략을 끕니다.
계정 ID는 대소문자를 구분하는 영문·숫자·밑줄·하이픈 1~32자, 비밀번호는 UTF-8 8~128바이트입니다. 콘솔 비밀번호 입력은 화면에 표시하지 않습니다. 서버 이동에는 입장권을 사용하므로 비밀번호를 다시 보내지 않습니다.
채팅 채널은 `일반`/`local`, `월드`/`world`로 선택합니다. `chat 내용`은 기존처럼 일반 채팅입니다.

| 접속 위치 / 채널 | 전달 범위 | 표시 예 |
| --- | --- | --- |
| GameServer 일반 | 같은 서버에 로그인한 사용자 | `[일반 A] 이름: 내용` |
| GameServer 월드 | 모든 게임 서버의 로그인 사용자 | `[월드 A] 이름: 내용` |
| World 일반 | 전장 안에 있는 사용자 | `[일반 World] 이름: 내용` |
| World 월드 | 사용 불가 | 거부 응답 |

같은 종족이라도 서버 ID가 다르면 일반 채팅은 공유하지 않습니다. 게임 서버 채팅과 전장 채팅은 서로 섞이지 않습니다.
일반 채팅은 같은 서버의 더미 두 개, 월드 채팅은 서로 다른 게임 서버의 더미로 확인합니다. 입력 대기 중에도 수신 내용을 표시합니다.
본문은 UTF-8 최대 256바이트, 전송 간격은 500ms입니다. 빈 내용·공백만 있는 내용·제어 문자는 거부하며 DB에는 저장하지 않습니다.
World 프로세스가 게임 서버 간 월드 채팅을 중계합니다. 추가 EXE 없이 게임 서버마다 중계 연결 하나를 유지하며 끊기면 재연결합니다.
World가 꺼져 있거나 중계 연결이 준비되지 않았으면 월드 채팅은 실패를 알리고, 게임 서버 일반 채팅은 계속 사용할 수 있습니다.
중계 확인 응답 이후 성공 처리하며, 응답 유실/연결 종료 시에는 전달을 확인하지 못했다고 알립니다. 자동 재전송·오프라인 저장은 하지 않습니다.

## 서버 목록을 CSV로 관리

`Data/GameServer.csv`의 컬럼은 `Index,Name,Race,Address,Port`입니다. 현재 A=저그, B=프로토스, C=테란이며 World는 `Server.ini`에서 설정합니다.
Index는 서버의 고유 ID로 숫자 또는 영문을 사용할 수 있고, 이름과 종족은 별도입니다. 종족은 저그/Zerg, 프로토스/Protoss, 테란/Terran을 받습니다.

게임 서버를 네 개로 늘리는 예:

```csv
Index,Name,Race,Address,Port
1,게임 서버 1,저그,127.0.0.1,7001
2,게임 서버 2,저그,127.0.0.1,7002
3,게임 서버 3,프로토스,127.0.0.1,7003
4,게임 서버 4,테란,127.0.0.1,7004
```

이 경우 게임 서버 4개와 World 1개, 총 5개 프로세스가 실행됩니다. 더미 목록도 CSV를 읽어 자동으로 바뀝니다.
1번과 2번은 모두 저그 유닛/시작점/하이브를 사용하며 서로 아군입니다. 퇴장하면 각자 원래 1번/2번 서버로 돌아갑니다.
CSV를 수정한 뒤 빌드하고 서버/더미를 다시 실행합니다. 서로 다른 PC에 배치할 때도 모든 서버가 같은 목록을 읽어야 합니다.

## 개발 환경

- Visual Studio 2019, C++ 데스크톱 개발 도구, v142 도구 집합
- Windows SDK 10.0.19041.0
- C++20, x64, Unicode, UTF-8 소스/실행 문자 집합
- 경고 수준 /W4, 표준 준수 모드
- Debug /MDd, Release /MD
- Debug/Release 모두 PDB 생성
- 링크 라이브러리: Ws2_32.lib, odbc32.lib, bcrypt.lib
- Protobuf v33.6의 C++ lite 런타임과 protoc, Abseil 20250512.1 (프로젝트 내부 정적 링크)

실제 MSSQL 접속에는 x64용 Microsoft ODBC Driver 17 또는 18과 개인 테스트 DB가 필요합니다.
현재 PC에서는 SQL Server 2022 Developer Edition과 x64 ODBC Driver 17을 확인했습니다.
ODBC 환경 핸들 생성 성공만으로 SQL Server 드라이버 설치나 DB 접속을 확인할 수는 없습니다.

## Visual Studio에서 실행

1. 아래 명령줄 빌드로 사용할 구성(Debug/Release)의 Protobuf 의존성을 한 번 준비합니다.
2. MultiThreadServer/MultiThreadServer/MultiThreadServer.sln을 엽니다. 이미 열려 있다면 다시 로드합니다.
3. 상단 구성을 Debug, 플랫폼을 x64로 선택합니다.
4. Ctrl+Shift+B로 빌드하고 F5로 실행합니다. `.proto` 변경 시 C++ 코드는 자동 생성됩니다.
5. `[OK] MSSQL ODBC 연결 및 조회`와 `DB: project_db`를 확인한 뒤 Enter로 종료합니다.

기본 설정으로 현재 Windows 계정을 사용해 `127.0.0.1:1433`의 `project_db`에 연결합니다.
이 계정에 SQL Server 로그인과 해당 DB 접근 권한이 있어야 합니다.

## 명령줄 빌드

저장소 루트에서 아래 명령을 실행합니다. ExecutionPolicy Bypass는 해당 프로세스에만 적용합니다.
첫 실행은 공식 GitHub 릴리스에서 고정 버전의 소스와 protoc를 받아 SHA-256을 확인하고, `.deps` 안에서 런타임을 빌드합니다.
인터넷 연결과 Visual Studio의 CMake 구성 요소가 필요하며 최초 빌드에는 추가 시간이 걸립니다.
전역 PATH는 바꾸지 않습니다. 이후 같은 구성의 빌드는 로컬 결과를 재사용합니다.

    powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Debug -RunChecks
    powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release -RunChecks

솔루션에 `MultiThreadServer`와 `DummyClient` 프로젝트가 함께 포함되어 있습니다.
통합 네트워크 테스트는 `-RunNetworkChecks`를 추가합니다. 테스트는 별도 임시 설정과 로컬 포트로 CSV의 게임 서버와 World를 실행하고 종료합니다.

    powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Debug -RunChecks -RunNetworkChecks
    powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release -RunChecks -RunNetworkChecks

서버 이동/기본 전투 통합 테스트는 `-RunGameplayChecks`를 추가합니다. 네 게임 서버(저그 두 개 포함)와 World를 실행합니다.

    powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Debug -RunGameplayChecks

실제 DB 연결까지 확인하려면 `-RunDbCheck`를 사용합니다. 기본 환경 점검도 함께 실행합니다.

    powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Debug -RunDbCheck
    powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release -RunDbCheck

실행 파일과 PDB는 프로젝트 루트의 `Run64/Debug` 또는 `Run64/Release`에 생성됩니다.
서버를 빌드하면 같은 폴더에 실행 스크립트, `Server.ini`, `Data`의 CSV/맵 설정, `Cluster.local.ini`도 복사하므로 해당 폴더에서 바로 실행할 수 있습니다.
설정과 데이터는 프로젝트 루트의 원본을 수정합니다. `Run64` 안의 복사본은 다음 빌드에서 원본으로 갱신됩니다.
Visual Studio 디버깅 작업 폴더도 해당 `Run64` 출력 폴더를 사용합니다. 중간 빌드 파일은 기존 `build` 폴더에 둡니다.
의존성만 준비하려면 `setup-protobuf.ps1 -Configuration Debug` 또는 `Release`를 실행합니다.

실행 파일 인수:

- 인수 없음: 기본 환경과 DB 연결 점검 후 Enter를 기다립니다.
- --self-check: DB에 접속하지 않고 기본 환경만 점검한 뒤 종료합니다.
- --db-check: 기본 환경과 DB 연결 점검 후 즉시 종료합니다.
- --help: 사용법을 출력합니다.
- --config-check A: 설정·맵·CSV만 검증합니다. A 대신 CSV의 Index 또는 World도 가능합니다.
- --server A: 지정한 서버 프로필로 TCP 서버를 실행합니다. Ctrl+C로 종료합니다.
- --config 경로: 사용할 Server.ini를 지정합니다. 기본값은 작업 폴더의 Server.ini입니다.
- --run-seconds N: TCP 서버를 N초 후 종료합니다. 자동 테스트에 사용합니다.
- 종료 코드 0: 성공, 1: 초기화/점검 실패, 2: 잘못된 인수.

신규 서버 모드의 설정/인수 오류와 더미 클라이언트 오류는 종료 코드 1을 반환합니다.

## 서버와 더미 클라이언트 실행

프로젝트 루트에서 PowerShell 두 개를 열어 각각 실행합니다.

```powershell
# 첫 번째 창: A 서버 (7001번 포트)
.\Run64\Debug\MultiThreadServer.exe --server A

# 두 번째 창: 더미 8개 접속, 각각 20회 왕복 검증 후 종료
.\Run64\Debug\DummyClient.exe --server A --clients 8 --rounds 20
```

B는 7002, C는 7003, World는 7100 포트입니다. 게임 서버 값은 `GameServer.csv`, World 값은 `Server.ini`에서 읽습니다.
다른 폴더에서 실행할 때는 `--config`로 파일 경로를 전달합니다. 데이터 파일의 상대 경로는 INI 위치를 기준으로 해석합니다.
ServerVersionCheck는 접속 직후 프로토콜 버전을 한 번 확인합니다. 응답은 서버 버전과 종류, 종족, 맵 외곽, 진영별 시작 좌표를 알려줍니다. 게임 흐름은 `--play` 모드의 별도 로그인/이동 패킷을 사용합니다.

접속 수락은 메인 스레드, 송수신은 `Network.WorkerCount`개 워커가 담당합니다.
한 연결의 버퍼는 배정된 워커만 수정하며, 새 연결 인계 시에만 mutex를 사용합니다.
느린 클라이언트의 송신 대기는 `MaxPendingSendBytes`로 제한합니다. 접속 수 상한은 `WorkerCount × MaxClientsPerWorker`입니다.
게임 요청은 길이가 제한된 큐를 통해 메인 스레드에 전달합니다. 메인 스레드가 접속 수락과 게임 상태/전장 틱을 소유하고, 응답은 연결별 우편함을 통해 해당 네트워크 쓰레드(워커)로 전달합니다.
World 상태는 약 5회/초 수신자별 스냅샷으로 보내며 4유닛씩 나눠 패킷 크기 상한을 지킵니다. 탐지되지 않은 적 은신 유닛은 보내지 않습니다. `MaxWorldPlayers`는 기본 1500, 설정 상한 2048입니다. `SnapshotRadius=25`에서는 자신과 반경 안의 가까운 적 32명/아군 31명까지 전송합니다. 0은 전체 시야이며 정원 64명 이하에서만 허용합니다. 리플레이는 이 제한과 별개로 전체 유닛을 기록합니다.
DB 작업은 `Database.WorkerCount`개 스레드가 각자 ODBC 연결 하나로 처리합니다. 메인 스레드는 DB 작업/결과 큐만 사용하며 SQL 완료를 기다리지 않습니다.

## Protobuf 패킷

공유 명세는 `Protocol/Game.proto`입니다. 애플리케이션의 4바이트 헤더 뒤에 Protobuf 본문을 붙입니다.

패킷 번호는 `src/Packet.h`의 `Protocol::Type`에서 관리합니다. 시스템은 1~4를 사용하고 5~9는 비워 둡니다. 게임 패킷은 `CM_JoinGame = 10`만 지정하고 나머지는 선언 순서대로 자동 증가합니다. 현재 마지막은 `SM_RankReply`의 32번입니다.
`SYS_`는 시스템 확인, `CM_`은 클라이언트→서버, `SM_`은 서버→클라이언트, `SS_`는 게임 서버↔World 중계입니다. 이 접두어는 패킷 종류에 붙이며 Protobuf 본문 타입은 `JoinGameRequest`, `JoinGameReply`처럼 유지합니다.

```cpp
CM_JoinGame = 10,
SM_JoinGameReply,  // 11
CM_RequestTicket, // 12
SM_WorldTicket,   // 13
```

새 게임 패킷은 목록 끝에 추가합니다. 중간 삽입·순서 변경으로 기존 번호가 바뀌면 통신 버전도 갱신합니다. 이번 번호 변경으로 `Protocol::Version`은 5이며 서버·더미를 함께 교체해야 합니다.

```text
[전체 길이 u16][패킷 종류 u16][Protobuf 본문]
```

길이는 헤더를 포함한 4~4096바이트입니다. 두 필드는 big-endian으로 직접 읽고 쓰며 헤더에는 패딩과 버전 필드가 없습니다. 본문은 헤더 다음(오프셋 4)에서 시작합니다. 서버와 더미를 함께 빌드해 사용하며 이전 8바이트 실행 파일과 혼용하지 않습니다.
접속 직후 `SYS_ServerVersionCheck=1`로 `client_name`과 `protocol_version`을 보냅니다. 서버는 `Protocol::Version=5`와 같을 때만 `SYS_ServerVersionCheckReply=2`로 서버 버전·맵 정보를 돌려주고 다음 요청을 허용합니다. 버전 누락·불일치·중복 확인 요청은 연결을 종료합니다. 같은 값은 리플레이 메타데이터에도 기록합니다.
버전은 첫 요청/응답의 Protobuf 본문에만 포함합니다. 이후 매 패킷에는 반복하지 않습니다. Ping/Pong은 순번·한글 문자열·바이너리 왕복을 검증합니다.
Protobuf 파싱에 성공해도 이름 길이와 필수 값 등 서버 규칙을 추가로 검사합니다.
CM_JoinGame=10의 본문은 `JoinGameRequest { string name=1; string password=2; }`입니다. 패킷 번호는 `src/Packet.h`, 각 필드와 형식은 `Protocol/Game.proto`에서 확인합니다. CM_Logout=15, CM_Skill=24을 추가했습니다.

입장권에는 플레이어 ID·유닛·원래 서버·목적지·만료 시각·재입장 시각·nonce를 넣고 HMAC-SHA256으로 서명합니다.
World는 서명, 목적지, 서버-종족-유닛 조합, 30초 만료, 중복 ID와 사용한 nonce를 검사합니다. 복귀권도 원래 게임 서버에서 같은 방식으로 검증합니다.
게임 서버는 입장권을 발급하면 로그인 상태를 지우고 송신을 마친 뒤 연결을 닫습니다. World도 복귀권을 보낸 뒤 로그아웃합니다.
World가 정원/진영 탈락/경기 대기 때문에 정상 입장권을 거절하면 새 복귀권을 발급해 원래 서버에 다시 로그인하게 합니다.
공유 키는 최초 빌드에서 무작위 생성한 `Cluster.local.ini`에 저장하며 Git에서 제외합니다. 실제 클라이언트 배포에는 이 서버용 키를 포함하면 안 됩니다.
입장권 nonce와 세션 소유권은 DB에도 저장해 서버 재시작 후 재사용을 막습니다. 이동 응답이 유실되면 최대 30초 후 계정으로 다시 로그인할 수 있습니다.
게임 TCP 구간 TLS는 아직 구현하지 않았습니다. 현재 비밀번호 로그인은 로컬 테스트용이며 외부 서비스 전에 TLS와 계정 운영 기능이 필요합니다.

`Protocol.props`가 같은 `.proto`에서 서버와 더미의 C++ 파일을 생성합니다.
생성 파일은 `Protocol/Generated`에 두며 직접 수정하지 않습니다. 표준 라이브러리 공통 헤더/using 규칙은 직접 작성한 코드에 적용합니다.
protoc와 C++ 런타임은 v33.6로 맞추고, Debug/Release 런타임도 각각 /MDd, /MD로 맞춥니다.
VS2019(v142) 환경에 맞춰 33.x 유지보수 계열을 사용합니다. 의존성 갱신 시 생성 코드와 런타임을 함께 갱신하고 두 구성의 통신 테스트를 다시 실행합니다.
최종 언리얼 클라이언트에서도 이 명세를 사용하고 Unreal 모듈에서 런타임 링크와 소켓 처리를 추가할 예정입니다.
언리얼 연동은 아직 구현하지 않았습니다.

현재 맵은 X/Y 모두 0~120, 중심 (60,60)입니다. 테란 (60,104), 프로토스 (104,60), 저그 (60,16)으로 배치합니다.
`StartPoint.csv`의 음수 좌표는 거부하고 0은 허용합니다.

## DB 연결 설정

`src/Database.h`의 `stDatabaseConfig`에 로컬 기본값을 두고, `LoadDatabaseConfig()`에서 환경 변수로 덮어씁니다.
SQL Server 접속 자체는 Windows 인증을 사용합니다. 게임 계정 ID/비밀번호와 별개이며 아래 환경 변수는 DB 점검과 DB 쓰레드(워커)에 공통 적용합니다.
`Server.ini`의 `[Database]`에는 Realm, WorkerCount, QueueCapacity, SessionLeaseSeconds를 둡니다. 기본값은 local, 2, 256, 15초입니다. 같은 서버 묶음은 같은 DB와 Realm을 사용해야 합니다.

| 환경 변수 | 기본값 | 용도 |
| --- | --- | --- |
| `MTS_DB_DRIVER` | `ODBC Driver 17 for SQL Server` | 설치된 x64 드라이버 이름 |
| `MTS_DB_SERVER` | `tcp:127.0.0.1,1433` | 서버 주소와 TCP 포트 |
| `MTS_DB_NAME` | `project_db` | 접속할 DB 이름 |
| `MTS_DB_TRUST_SERVER_CERTIFICATE` | 기본 로컬 주소에서 `yes`, 다른 주소에서 `no` | 인증서 검증 생략 여부. `yes`/`no`만 허용 |

기본값으로 실행할 때는 환경 변수를 지정할 필요가 없습니다. 다른 테스트 설정을 쓸 때만 현재 PowerShell에서 지정합니다.

```powershell
$env:MTS_DB_SERVER = 'tcp:127.0.0.1,1433'
$env:MTS_DB_NAME = 'project_db'
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Debug -RunDbCheck
```

Visual Studio에서는 프로젝트 속성의 **디버깅 → 환경**에 같은 변수를 지정할 수 있습니다.
연결에는 `Encrypt=yes`를 명시합니다. 로컬 테스트에서는 자체 서명 인증서를 허용하며, 다른 서버에서는 유효한 인증서를 사용하는 설정이 기본입니다.
관련 옵션은 [Microsoft ODBC 연결 문자열 문서](https://learn.microsoft.com/en-us/sql/connect/odbc/dsn-connection-string-attribute?view=sql-server-ver17)를 참고합니다.

`CDatabaseConnection`은 환경 핸들을 소유하고, 소멸 시 연결 해제 후 핸들을 반납합니다.
조회용 명령 핸들은 함수 안에서만 유지합니다. 연결을 여러 스레드에서 공유하는 기능은 아직 없으며 현재 점검은 메인 스레드에서 실행합니다.
`SQL_SUCCESS_WITH_INFO`도 진단 메시지를 출력합니다. 예를 들어 서버의 DB 전환 안내(5701)와 언어 설정 안내(5703)는 연결 실패가 아닙니다.

## 로컬 검증 결과

2026-10-13, `tcp:127.0.0.1,1433` / `project_db`에서 확인했습니다.

- SQL Server 2022 Developer Edition (64-bit), 버전 `16.0.1200.5`
- x64 Debug/Release 빌드 및 `--db-check` 성공, 종료 코드 0
- TCP / Windows 인증(NTLM), 요청한 DB 이름 일치 확인
- 정수 `1`과 `N'한글 연결 확인'`의 조회 결과 일치 확인
- 없는 드라이버: `IM002`, 없는 DB: 서버 오류 `4060`, 각각 종료 코드 1
- 접속할 수 없는 로컬 포트: 로그인 제한 시간 후 `HYT00`, 종료 코드 1
- 잘못된 인증서 설정: 오류 메시지와 종료 코드 1
- 도움말과 기본 환경 점검: 종료 코드 0, 잘못된 인수: 종료 코드 2
- 드라이버 설정이 잘못되어도 `--self-check`는 DB 접속 없이 성공

`--db-check`는 읽기 전용 SELECT입니다. 실제 게임 로그인은 `Database/001_accounts.sql`의 PortfolioAccount/PortfolioUnit/PortfolioSession과 프로시저를 사용해 계정·유닛·세션을 저장합니다.

2026-10-16 통신 검증 항목:

- x64 Debug/Release의 서버 및 더미 클라이언트 빌드와 기본 점검
- A/B/C/World 각각 더미 8개, A 서버 추가 16개 접속의 ServerVersionCheck/Ping/Pong 검증
- Protobuf 서버/맵 필드 일치, 한글 문자열과 0/0xff 바이너리, 4096바이트 최대 패킷
- 분할 헤더와 이어 붙인 패킷 복원
- 짧거나 과도한 길이, 잘못된 버전/종류, ServerVersionCheck 이전 Ping, 중복 ServerVersionCheck 거부
- 잘린 Protobuf 문자열, 잘못된 UTF-8/와이어 타입, 누락된 이름/순번 거부
- 초기 MP 초과, 없는 스킬 참조, 스킬 번호 중복, 시작 좌표 중복/범위 오류, 워커 수 0 거부
- 시작 좌표/맵 외곽의 음수 X/Y 거부, 시작 좌표 (0,0) 허용
- 정해진 시간 후 서버 종료, 워커 합류, 소켓 정리

Debug와 Release 각각 총 48개의 더미 접속 시나리오에서 Ping/Pong 응답 960개를 확인했습니다.
최대 동시 더미 수는 한 서버에 16개이며, 잘못된 설정과 패킷의 거부도 확인했습니다. 현재 스크립트에는 서버 ID의 대소문자 중복/예약 이름 검사도 포함합니다.
빌드 중 외부 Protobuf/Abseil 헤더에서 발생하는 MSVC 경고는 의존성 갱신 때 별도로 확인합니다.

테스트 스크립트는 `test-network.ps1`이며, 로그는 무시되는 `build/network-check-*` 폴더에 남깁니다.
이 네트워크 검증은 Ping/Pong과 잘못된 입력 처리에 대한 결과입니다. 일반 채팅은 아래 게임 통합 테스트에서 검사합니다.

`test-gameplay.ps1`은 별도 CSV/포트에서 1=저그, 2=저그, 3=프로토스, 4=테란의 5개 프로세스를 실행합니다.
서명 변조·재사용 거부, 원래 유닛 유지, 종족별 시작점, 같은 종족의 다른 서버 아군 판정, 이동/공격/MP/사망, 원래 서버 복귀 로그인, 재입장 제한, 건물 파괴·경기 종료·재시작을 검사합니다.
Debug/Release 모두 위 검증과 실제 대화형 메뉴의 `2번 선택 → world → back → 2번 자동 복귀 로그인`을 통과했습니다.
추가 검증은 같은 게임 서버 간 채팅, 다른 서버/로그인 전 연결/로그아웃 후 수신 제외, 한글·길이·빈 내용·전송 간격 검사입니다.
네 게임 서버의 월드 채팅 수신/중복 방지, 전장 내부 일반 채팅 분리, 잘못된 채널 및 미인증/반사 서명 중계 접속 거부도 검사합니다.
대화형 메뉴에서는 GameServer 전투 명령 차단과 채팅, World 복귀 후 채팅, 다른 게임 서버 선택을 확인합니다.
전투 테스트는 원본을 수정하지 않고 임시 유닛 HP/속도/피해량과 2초 사망 대기·4초 재시작을 사용합니다. 실제 설정은 100초/300초입니다.

중계 연결 장애 검증은 `test-chat-relay.ps1 -Configuration Debug` 또는 `Release`로 실행합니다.
전용 테스트 프로세스에서 World를 늦게 시작하고 종료/재시작하며, 일반 채팅 유지·월드 채팅 실패 응답·자동 재연결 후 전달을 확인합니다.

자동 건물 공략은 다음 명령으로 별도 검증합니다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-auto-battle.ps1 -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-auto-battle.ps1 -Configuration Release
```

실제 대화형 더미에서 World 입장만으로 두 적 건물 파괴 → 자기 종족 승리 → 원래 서버 복귀를 세 종족 각각 검증합니다.
중지/재개와 상태 조회·채팅, 직선 경로의 아군 건물 회피, 비공격 유닛의 접근 후 대기도 확인합니다.
테스트 복사본에서 이동 속도 80, 사거리 1, 공격력 10, 건물 HP 40과 재시작 1초를 사용합니다. 비공격 시나리오는 공격 수치를 0으로 바꿉니다. 실제 Unit.csv·Map.ini·Server.ini 수치는 변경하지 않습니다.

## 계정·스킬·복구 검증

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-account-skills.ps1 -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File .\test-recovery.ps1 -Configuration Debug
```

Release도 같은 명령의 Configuration만 바꿔 실행합니다. 로컬 project_db의 스키마 설치가 필요합니다.
각 실행은 무작위 `test_*` Realm과 임시 포트/설정을 사용합니다. 개발 계정을 건드리지 않으며 테스트 행은 결과 조사용으로 DB에 남깁니다.

- 계정: 비밀번호 검증, 잘못된 비밀번호 거부, 다른 서버와 동시 로그인 시 한 세션만 허용, 고정 ID와 서버별 유닛 복구, 재접속/서버 변경 후 대기 유지.
- 스킬: 슬롯·범위 거부, Stim 비용/쿨다운, Heal, Cloak 유지 비용/MP 회복 중단/탐지, Lockdown, Blind, Burrow, Storm/중첩, DarkSwarm, Plague, Restoration, 스킬 사망 후 저장된 복귀권.
- 복구: Game/World 프로세스 강제 종료와 재시작, lease 만료 후 복구, World 대기 유지, 이동권 유실 후 30초 만료 복구와 이전 티켓 거부.
- 저장 실패: 테스트 계정 하나에만 SQL 잠금을 걸어 실패를 유도하고, 그동안 일반 채팅 처리 유지·실패 응답·잠금 해제 후 재로그인을 확인.

스킬 테스트는 임시 데이터에서 HP/MP·이동 속도와 일부 지속 시간을 조정합니다. 적 기계 유닛 Lockdown 검사를 위해 테스트 질럿만 IsMechanical=1로 두며 원본 Unit.csv는 변경하지 않습니다.
2026-10-16 로컬 project_db에서 Debug/Release 계정·스킬·복구 테스트를 통과했습니다. 기존 네트워크·게임 흐름·자동 건물 공략·채팅 중계 재연결도 두 구성에서 확인했습니다.
세션/큐/재시도와 장애 시 보수적인 재입장 대기 계산은 [설계 문서](../docs/Design.md#계정-저장과-장애-복구)에 설명합니다.

## 이후 작업

1. 언리얼 클라이언트의 로그인·유닛 표시·수동 조작과 리플레이 화면 연결.
2. 실제 화면으로 플레이하며 임시 이동 속도·사거리·스킬 효과량 조정.
3. 코드/실행 증거에 언리얼 시연 화면을 더해 최종 포트폴리오 제작.

게임 처리 시간과 DB 저장 완료 시간은 구분해서 측정합니다. 길찾기 추가는 현재 범위에서 제외합니다.

## 저장소 관리

.gitignore에 빌드 결과, Protobuf 의존성/생성 코드, VS 개인 설정, 덤프, 개인 접속 설정을 제외했습니다.
개인 접속 설정을 파일로 보관할 경우 *.local.json 또는 *.local.ini처럼 제외되는 이름을 사용합니다.
DB 점검은 환경 변수, TCP 서버와 게임 데이터는 Server.ini에서 설정을 읽습니다.
원격 저장소는 [fnghd2012-git-project/StarCraft](https://github.com/fnghd2012-git-project/StarCraft)이며 기본 작업 브랜치는 `main`입니다.
현재 프로젝트 폴더에서 Git으로 변경 이력을 관리합니다. GitHub 인증 정보는 소스 파일에 넣지 않고 로컬 자격 증명 저장소에서 관리합니다.

다른 PC에서는 저장소를 복제한 뒤 위의 `build.ps1`을 실행하면 Protobuf 의존성을 다시 준비할 수 있습니다.

```powershell
git clone https://github.com/fnghd2012-git-project/StarCraft.git
cd StarCraft
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Debug -RunChecks
```

개발 시작 전에 `git pull --ff-only`, 변경 확인은 `git status`와 `git diff`를 사용합니다.
저장할 변경만 `git add`로 선택한 뒤 `git commit`과 `git push`로 기록합니다.
`Run64`, `.deps`, `Protocol/Generated`, `build` 폴더의 파일은 올리지 않으며 `.proto` 원본과 의존성 준비 스크립트를 함께 관리합니다.

## IOCP · 전투 계산 병렬화 · I/O 풀 (2026-10-19 최종 코드)

`Server.ini`에서 아래 값을 수정한 뒤 서버를 다시 시작합니다. 전체 INI를 이 예제로 덮어쓰지 말고 해당 항목만 바꿉니다.

```ini
[Network]
Mode=IOCP
WorkerCount=2
[Memory]
UsePool=1
PoolCapacity=8192
[Battle]
WorkerCount=4
VerifyParallel=0
```

- `Mode=WSAPoll`: 기존 비교 경로. `PollTimeoutMs`는 이 경로에서만 적용합니다.
- `UsePool=0`: 같은 IOCP 작업 경로에서 new/delete. `1`은 완료된 작업 블록을 재사용합니다. Player와 Protobuf 전체를 풀링하지 않습니다.
- `Battle.WorkerCount=1`: 같은 계산을 메인에서 실행합니다. `2~16`은 World 전투 계산 워커를 사용합니다. 네트워크/DB 쓰레드(워커) 수와는 별개입니다.
- `VerifyParallel=1`: 이동·MP·Storm 피해·스냅샷 바이트를 직렬 계산과 대조합니다. 성능 측정은0으로 둡니다.

같은 전장 안에서 이동/충돌/MP, 같은 Storm의 대상별 피해, 수신자별 시야/직렬화를 분배합니다. 메인은 계산이 끝날 때까지 기다린 뒤 상태를 순서대로 반영합니다. 기본 공격, HP 확정, 사망·킬·승패는 메인에서 처리합니다. 기본 공격·단일 스킬·Heal은 ID 인덱스, 범위 스킬은 공간 인덱스로 대상을 찾습니다.

IOCP의 각 작업은 OVERLAPPED·4096바이트 버퍼·세션 참조를 완료 통지까지 유지합니다. 연결 종료 후에도 취소 완료를 회수하고 `pending_io=0`, `pool_in_use=0`이 된 뒤 워커를 종료합니다. 풀 고갈 시 해당 연결을 정리하며 Accept는 여유가 생기면 다시 보충합니다.

```powershell
.\test-parallel.ps1 -Configuration Release
.\test-iocp.ps1 -Configuration Release
.\test-network.ps1 -Configuration Release -NetworkMode WSAPoll
```

현재 비교 자료는 `docs/results/2026-10-19/iocp/`에 보관합니다. 과거 8바이트 헤더의 측정값과 이번 4바이트 헤더·IOCP/병렬 계산/풀 결과를 섞지 않습니다. 자세한 설정별 허용값·적용 범위는 `output/pdf/04_Server_Ini_Manual.pdf`를 확인하세요.

최종 Release에서 96명 고정 부하로 5개 조건을 각각 3회 측정했습니다. 아래는 반복별 값의 중앙값입니다.

| 조건 | 전투 응답 p95(ms) | Snapshot 구간 p95 최대(ms) |
|---|---:|---:|
| WSAPoll / 전투 1 | 32.208 | 4.093 |
| IOCP / 전투 1 / 풀 OFF | 16.485 | 4.259 |
| IOCP / 전투 1 / 풀 ON | 16.413 | 3.895 |
| IOCP / 전투 2 / 풀 ON | 16.576 | 2.981 |
| IOCP / 전투 4 / 풀 ON | 16.520 | 1.867 |

Snapshot 값은 측정 구간 안에 포함된 1초 로그들의 p95 중 최대값이며 전체 표본의 p95가 아닙니다. 네트워크/DB 쓰레드(워커)는 2/2로 고정했습니다. CPU·메모리·반복 범위는 `comparison.csv`, 개별 실행은 `comparison-raw.csv`에 있습니다. 전투 쓰레드(워커) 증가에 따른 Snapshot 비용 감소를 서버 전체 처리량 배수로 해석하지 않습니다.

별도 최종 1,500명 시연은 사망 2,668회, 사망 후 재입장 2,138회, 연결·응답 오류 0, 최대 Tick 39.143ms, 생략 틱 0이었습니다. Redis·XML·MSSQL의 1,500명 점수를 전수 대조했습니다. 전원 준비 후 240초로 종료한 로컬 시연이며 승패 완료 경기나 장시간 수용량 시험은 아닙니다. 실행 사본은 네트워크/DB 4/6, 전투 4와 원본 유닛 데이터를 사용하므로 96명 비교와 조건이 다릅니다.

최신 학습 PDF는 `output/pdf/`의 5권(코드 51쪽, 면접 32쪽·58문항, 실습 20쪽, 설정 12쪽, 핵심 함수 18쪽)입니다. `FINAL_README.txt`에 권장 순서와 이전 측정 이력의 구분을 남겼습니다.


## 코드 읽기 순서 (2026-10-20 가독성 정리)

클래스의 멤버와 함수 선언은 헤더에서, 함수 구현은 같은 이름의 `.cpp`에서 확인합니다.
`CIocpServer::Impl`을 제거하고 `Run`, `Accepted`, `Receive`, `Send`를 `CIocpServer`의 private 함수로 옮겼습니다.

| 먼저 열 헤더 | 구현 파일에서 따라갈 함수 |
| --- | --- |
| `IocpServer.h` | `IocpServer.cpp`: 생성자 → PostAccept → Run → Accepted → Receive/Send → Stop |
| `IocpSession.h` | 연결 하나의 소켓·버퍼·stGameChannel·잠금 |
| `NetworkWorker.h` | `NetworkWorker.cpp`: Add → Run → Process (WSAPoll 비교 경로) |
| `IoBufferPool.h` | `IoBufferPool.cpp`: Acquire → Release |
| `GameLogic.h` | `GameLogic.cpp`: Submit → Update → Handle/Tick |
| `BattleWorker.h` | `BattleWorker.cpp`: 더미 여러 연결의 로그인·전투·복귀 |
| `ReplayWindow.h` | `ReplayWindow.cpp`: Run → Update/Seek → Draw |

축약한 조건문·반복문을 펼치고 `.clang-format`으로 들여쓰기와 중괄호를 통일했습니다.
학습은 `output/pdf/05_Network_Thread_Core_Guide.pdf`부터 시작하면 됩니다.
Debug/Release 빌드와 양쪽 네트워크, 병렬 전투, 채팅·계정 복구·랭킹·리플레이, IOCP 수명·풀 고갈 회귀를 통과했습니다.
[이번 검증 기록](../docs/results/2026-10-20/readability/validation.md)을 참고하세요.
기존 성능 표는 **10/19 측정 빌드의 기록**이며 이번 구조 정리의 성능 향상 수치로 해석하지 않습니다.


## 코드 읽기와 명명 규칙

클래스는 `CGameLogic`, 구조체는 `stPlayer`, 클래스 멤버는 `m_players`처럼 구분합니다. 자세한 기준은 [코드 작성 규칙](../docs/CodeConventions.md)을 참고하세요.

패킷 수신은 `CIocpServer::Run → HandleSessionCompletion → HandleClientPackets → CGameLogic::Submit`, 게임 처리는 `Update → HandlePacket → HandleGamePacket/HandleWorldPacket`, 이동 목표 설정은 `HandleMove`에서 확인합니다. 응답은 `SendMessage → SendPacket → sendQueue → NotifySend → PostSend` 순서입니다.
