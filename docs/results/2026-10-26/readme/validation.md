# GitHub 게시 전 검증 — 2026-10-26

현재 작업 트리의 코드와 README를 StarCraft 저장소에 반영하기 위한 점검이다. 성능 부하를 새로 측정한 작업이 아니다.

## 변경

- 루트 README를 소개·구현 범위·구조·쓰레드 소유권·패킷 흐름·실행 화면·빌드·테스트·측정 결과·코드 읽기 순서로 재구성했다.
- 구성·기능·실행 예·이미지 배치는 사용자 지정 참고 저장소 https://github.com/jej10e5/NetCore 의 README를 확인했다. 기능 설명과 수치는 이 프로젝트의 코드·검증 기록을 사용했다.
- 누적 README는 docs/DevelopmentHistory.md로 옮겨 과거 조건·측정 이력을 보존했다.
- 현재 Release 뷰어의 동일 Draw 경로로 2026-10-19 실제 1,500명 기록의 60초·210초 이미지를 출력하고 확인했다. 원본 파일 해시는 기존 iocp/stress-validation.json, 출력 이미지 해시는 screenshots.json에 있다.
- Windows PowerShell 5.1의 한글 스크립트 구문 오류를 해결하기 위해 한글을 포함한 PowerShell 파일 16개에 UTF-8 BOM을 추가했다. 스크립트 본문 로직은 바꾸지 않았다.
- tmp/output/의 로컬 편집 사본·PDF, 빌드 산출물·개인 설정·서명키·리플레이 원본·대용량 개별 RTT 표본은 Git에서 제외한다. 요약·반복 실행 CSV와 검증 코드는 보존한다.

## 현재 코드 검사

| 항목 | 결과 | 기록 |
|---|---|---|
| Release 빌드·self-check·MSSQL 연결 | 통과 | build-release.txt |
| Debug 빌드·self-check | 통과 | build-debug.txt |
| 전투 계산 1/2/4 쓰레드, 1500개 입력·200회 결과 대조 및 예외 복구 | 두 빌드 통과 | build-*.txt |
| 공간 검색 동일성·설정 검증 | 두 빌드 통과 | build-*.txt |
| IOCP/WSAPoll 통신·분할/병합·잘못된 패킷 | 통과 | network-iocp.txt / network-wsapoll.txt |
| 로그인·채팅·전장 이동·전투·복귀·승패·재시작 | 통과 | replay.txt의 gameplay 검사 |
| 리플레이 실제 상태 대조·손상/잘림·오프라인 제어 | 통과 | replay.txt |
| Redis 랭킹·장애/복구·SQL 최종 결과·재적재 중복 방지 | 통과 | ranking.txt |
| IOCP 연결 중단·느린 수신·풀 OFF/ON/고갈·종료 완료 회수 | 통과 | iocp-lifetime.txt / iocp-lifetime.json |

IOCP 수명 테스트의 기존 실행기는 2026-10-19/iocp/fault-release.json을 덮어쓰므로, 해당 파일은 이번 실행 결과로 갱신됐으며 이 폴더에 사본을 남겼다. 기존 부하 비교 CSV와 1500명 시연 수치는 재측정하지 않았다.

README는 과거 96명 비교, 1500명 단회 시연, 현재 회귀 검사를 구분한다. 프레임 출력 이미지는 실제 UI의 그리기 경로를 사용하며 별도로 합성한 실행 화면이 아니다. 새 서버 수용량·성능 향상 주장은 추가하지 않았다.

## 저장소 점검

README의 로컬 링크·실제 서명키 미포함·빌드/개인 파일 제외를 확인했다. 코드·문서의 git diff --check는 통과했다. docs/results의 원본 로그·측정 설정에는 출력 당시 공백과 마지막 빈 줄이 있으며 증거 보존을 위해 정리하지 않았다.
