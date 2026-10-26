# Linear / Grid 공간 검색 비교 - 2026-10-18

게임 규칙은 공통으로 유지하고 Snapshot의 후보 검색만 선택하도록 변경했다.
Server.ini의 [Battle] SpatialSearch=Linear 또는 Grid. 기본 배포 설정은 Grid다.
시작 시 읽으므로 재시작이 필요하며, 예전 INI에 옵션이 없으면 Linear다.

## 정확성 검증

- Debug/Release 빌드 및 공간 검색·설정 검사 통과. 경계/중복/삭제/빈 전장과 고정 배치 9,000개 쿼리를 독립 전체 검사에 대조했다.
- Debug/Release의 네트워크·전투·리플레이·정원·계정/스킬·자동 전투·채팅/복구 회귀 통과.
- Debug 첫 실행은 Windows PowerShell 5.1에서 예상 실패 stderr를 예외로 처리한 기존 capacity 테스트에서 중단했다. 테스트를 종료 코드 판정으로 수정한 뒤 해당 검사부터 재개했고 통과했다. debug-regression.txt / debug-regression-continued.txt / debug-final-checks.txt에 남겼다.
- Release 전체 실행은 release-regression.txt에 기록했다.
- 실제 전투 대조는 각 방식별 120명, 35초, 시험용 사망 대기 3초다. VerifySpatialSearch=1에서 같은 순간의 두 최종 수신 목록과 순서를 비교했다.
- Linear: 20,336개 목록 대조, 사망 99, 재입장 83, 오류 0.
- Grid: 20,288개 목록 대조, 사망 107, 재입장 91, 오류 0.
- 위 대조 횟수는 지표가 출력된 구간의 합이며 종료 전 미출력 구간을 포함하지 않는다. 실행마다 난수 배정과 전투 경로가 달라 두 전투 실행을 성능 비교로 쓰지 않는다.
- 계정·유닛·세션은 로컬 127.0.0.1/project_db의 기존 테이블에 저장한다. 공간 인덱스는 메모리, 비용은 CSV/로그에 남겨 신규 스키마는 추가하지 않았다.

## 검색 전용 비용

x64 Release, 고정 시드 20261018, 1,500개 점과 동일한 1,500개 질의 중심, 반경/셀 크기 25.
Uniform은 X/Y 0~120, Clustered는 55~65다. 2회 예열 후 20쌍, 매 회 실행 순서를 교대했다.
좌표 복사·검증·인덱스 구축 + 정확한 반경 조회 + 동일한 결과 합계 계산을 측정했다.
구축/조회 원본은 spatial-benchmark.csv. p95는 20개 시간의 nearest-rank 19번째 값이다.
빌드와 회귀·전투 대조가 끝난 뒤 별도 실행했다. OS와 다른 일반 앱의 모든 부하는 통제한 실험실 환경이 아니다.

| 배치 | 방식 | 구축+조회 중앙값 | p95 | 거리 검사 횟수 |
| --- | --- | ---: | ---: | ---: |
| Uniform | Linear | 3.795 ms | 3.887 ms | 2,250,000 |
| Uniform | Grid | 1.873 ms | 1.916 ms | 671,262 |
| Clustered | Linear | 2.755 ms | 3.014 ms | 2,250,000 |
| Clustered | Grid | 2.555 ms | 2.609 ms | 2,250,000 |

분산 배치에서 후보를 줄일 수 있지만 밀집 배치는 후보 수가 줄지 않는다. Grid는 밀집 시 최악 O(N²)가 남으며 구축/해시 비용이 추가된다.
이 측정에는 CanSee·진영 정렬·Protobuf·소켓·DB·실제 전투가 없다. 서버 전체 개선율이나 RTT 개선이라고 소개하지 않는다.
10/16의 1,500명 시연·64명 RTT를 이번 표와 개선 전후로 직접 비교하지 않는다.

## 지표와 재현

- spatial_build_us: 좌표 수집 및 인덱스 구축의 구간 누계.
- spatial_select_us: 선택한 방식의 검색·시야·진영 정렬 누계. 검증용 반대 검색 제외.
- snapshot_send_us: 메시지 구성·직렬화·송신 큐 적재 누계. 소켓 전송 제외.
- snapshot_p95_us/max_us: Snapshot 호출 전체. 검증 옵션이 켜지면 추가 대조 비용 포함.
- spatial_candidates: 선택한 방식의 거리 검사 횟수. 자기 자신 포함.
- spatial_verified: 성공한 최종 목록 대조 횟수. 성능 측정 시 VerifySpatialSearch=0.

```powershell
.\test-spatial.ps1 -Configuration Release
.\test-spatial-live.ps1 -Configuration Release
.\Run64\Release\MultiThreadServer.exe --spatial-check --csv .\spatial-benchmark.csv
```

다음 측정은 동일 설정·데이터에서 검증 옵션을 끄고 서버 전체 RTT·틱·큐·CPU·메모리를 반복 비교하는 것이다.
공격/스킬의 대상 검색, CanSee의 탐지 순회와 전투 스레드 소유권은 이번 변경에서 유지했다.
