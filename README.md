# StarCraft

C++ 멀티스레드 게임 서버와 C++ 더미 클라이언트를 먼저 개발하고, 최종적으로 언리얼 클라이언트를 연결하는 개인 포트폴리오 프로젝트입니다.
테란·프로토스·저그 게임 서버와 공용 전장, 마지막 건물 생존에 따른 승패, 전투 리플레이를 목표로 합니다.

## 현재 구현 범위

현재는 로컬 MSSQL 연결 점검과 설정/데이터 로딩, Protobuf를 사용하는 TCP 서버·더미 클라이언트 통신까지 구현했습니다.

- x64 Debug/Release 빌드와 C++20 사용
- Winsock 초기화 및 정리
- ODBC 환경·연결·명령 핸들의 RAII 관리
- Windows 인증으로 MSSQL 접속, 로그인/조회 제한 시간 5초
- DB 이름, 정수 1, 한글 문자열 조회 결과 확인
- ODBC SQLSTATE·서버 오류 번호·진단 메시지 출력
- std::jthread 생성, 합류, 결과 확인
- 명령줄 기본 점검과 오류 종료 코드
- `Server.ini`의 A/B/C/World 프로필, 네트워크 워커 수, 접속/송신 버퍼 상한 적용
- `Unit.csv`, `Skill.csv`, `StartPoint.csv`, `Map.ini` 읽기와 기본 유효성 검사
- 워커별로 연결을 소유하는 nonblocking TCP / WSAPoll 송수신
- 길이·종류·버전 헤더, 분할/병합 수신, 부분 송신, 비정상 패킷 연결 종료
- 서버/더미가 공유하는 `Game.proto`, Hello/Ping/Pong 직렬화·역직렬화 및 필드 검증
- C++ 더미 클라이언트의 동시 접속, 서버/맵 정보 확인, 한글·바이너리·최대 크기 Ping/Pong 검증
- 맵 X/Y 최소 0, 시작 좌표와 건물 범위 검사

로그인, 채팅, 유닛 선택/DB 저장, 서버 간 이동, 전투, 리플레이는 아직 구현하지 않았습니다.
CSV 스킬 데이터와 전투 설정을 읽는 것과 해당 기능의 실행은 구분합니다.
현재 더미 접속 검증은 기능 점검이며 서버 성능이나 대규모 부하를 검증한 결과는 아닙니다.

게임 규칙과 데이터 컬럼 설명은 [설계 문서](docs/Design.md)에 정리했습니다.

## 개발 환경

- Visual Studio 2019, C++ 데스크톱 개발 도구, v142 도구 집합
- Windows SDK 10.0.19041.0
- C++20, x64, Unicode, UTF-8 소스/실행 문자 집합
- 경고 수준 /W4, 표준 준수 모드
- Debug /MDd, Release /MD
- Debug/Release 모두 PDB 생성
- 링크 라이브러리: Ws2_32.lib, odbc32.lib
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
통합 네트워크 테스트는 `-RunNetworkChecks`를 추가합니다. 테스트는 별도 임시 설정과 로컬 포트로 서버 4개를 실행하고 종료합니다.

    powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Debug -RunChecks -RunNetworkChecks
    powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release -RunChecks -RunNetworkChecks

실제 DB 연결까지 확인하려면 `-RunDbCheck`를 사용합니다. 기본 환경 점검도 함께 실행합니다.

    powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Debug -RunDbCheck
    powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release -RunDbCheck

실행 파일과 PDB는 MultiThreadServer/MultiThreadServer/build/x64/{Debug 또는 Release}/bin에 생성됩니다.
의존성만 준비하려면 `setup-protobuf.ps1 -Configuration Debug` 또는 `Release`를 실행합니다.

실행 파일 인수:

- 인수 없음: 기본 환경과 DB 연결 점검 후 Enter를 기다립니다.
- --self-check: DB에 접속하지 않고 기본 환경만 점검한 뒤 종료합니다.
- --db-check: 기본 환경과 DB 연결 점검 후 즉시 종료합니다.
- --help: 사용법을 출력합니다.
- --config-check A: 설정·맵·CSV만 검증합니다. A 대신 B/C/World도 가능합니다.
- --server A: 지정한 서버 프로필로 TCP 서버를 실행합니다. Ctrl+C로 종료합니다.
- --config 경로: 사용할 Server.ini를 지정합니다. 기본값은 작업 폴더의 Server.ini입니다.
- --run-seconds N: TCP 서버를 N초 후 종료합니다. 자동 테스트에 사용합니다.
- 종료 코드 0: 성공, 1: 초기화/점검 실패, 2: 잘못된 인수.

신규 서버 모드의 설정/인수 오류와 더미 클라이언트 오류는 종료 코드 1을 반환합니다.

## 서버와 더미 클라이언트 실행

프로젝트 루트에서 PowerShell 두 개를 열어 각각 실행합니다.

```powershell
# 첫 번째 창: A 서버 (7001번 포트)
.\MultiThreadServer\MultiThreadServer\build\x64\Debug\bin\MultiThreadServer.exe --server A

# 두 번째 창: 더미 8개 접속, 각각 20회 왕복 검증 후 종료
.\MultiThreadServer\MultiThreadServer\build\x64\Debug\bin\DummyClient.exe --server A --clients 8 --rounds 20
```

B는 7002, C는 7003, World는 7100 포트입니다. 실제 값은 `Server.ini`에서 읽습니다.
다른 폴더에서 실행할 때는 `--config`로 파일 경로를 전달합니다. 데이터 파일의 상대 경로는 INI 위치를 기준으로 해석합니다.
현재 Hello 응답은 서버 종류, 종족, 맵 외곽, 진영별 시작 좌표를 알려주는 통신 점검입니다. 전장 입장이나 유닛 생성은 아직 수행하지 않습니다.

접속 수락은 메인 스레드, 송수신은 `Network.WorkerCount`개 워커가 담당합니다.
한 연결의 버퍼는 배정된 워커만 수정하며, 새 연결 인계 시에만 mutex를 사용합니다.
느린 클라이언트의 송신 대기는 `MaxPendingSendBytes`로 제한합니다. 접속 수 상한은 `WorkerCount × MaxClientsPerWorker`입니다.
DB/전투 로직 워커는 아직 생성하지 않으며, 구현 단계에서 해당 설정도 추가합니다.

## Protobuf 패킷

공유 명세는 `Protocol/Game.proto`입니다. TCP의 8바이트 헤더 뒤에 Protobuf 본문을 붙입니다.

```text
[전체 길이 u32][패킷 종류 u16][버전 u16][Protobuf 본문]
```

헤더는 네트워크 바이트 순서, 버전은 2이며 전체 길이는 최대 4096바이트입니다.
HelloRequest/HelloReply는 접속 점검과 서버·맵 정보를, Ping/Pong은 순번·한글 문자열·바이너리 왕복을 검증합니다.
Protobuf 파싱에 성공해도 이름 길이와 필수 값 등 서버 규칙을 추가로 검사합니다.
계정 로그인은 이후 별도 메시지로 구현합니다.

`Protocol.props`가 같은 `.proto`에서 서버와 더미의 C++ 파일을 생성합니다.
생성 파일은 `Protocol/Generated`에 두며 직접 수정하지 않습니다. 표준 라이브러리 공통 헤더/using 규칙은 직접 작성한 코드에 적용합니다.
protoc와 C++ 런타임은 v33.6로 맞추고, Debug/Release 런타임도 각각 /MDd, /MD로 맞춥니다.
VS2019(v142) 환경에 맞춰 33.x 유지보수 계열을 사용합니다. 의존성 갱신 시 생성 코드와 런타임을 함께 갱신하고 두 구성의 통신 테스트를 다시 실행합니다.
최종 언리얼 클라이언트에서도 이 명세를 사용하고 Unreal 모듈에서 런타임 링크와 소켓 처리를 추가할 예정입니다.
언리얼 연동은 아직 구현하지 않았습니다.

현재 맵은 X/Y 모두 0~120, 중심 (60,60)입니다. 테란 (60,104), 프로토스 (104,60), 저그 (60,16)으로 배치합니다.
`StartPoint.csv`의 음수 좌표는 거부하고 0은 허용합니다.

## DB 연결 설정

`src/Database.h`의 `DatabaseConfig`에 로컬 기본값을 두고, `LoadDatabaseConfig()`에서 환경 변수로 덮어씁니다.
현재는 Windows 인증만 사용하므로 ID와 비밀번호 설정은 없습니다. DB 점검은 기존 환경 변수를 사용합니다.
`Server.ini`는 신규 TCP 서버와 게임 데이터 설정용이며 DB 연결 풀은 아직 연결하지 않았습니다.

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

`DatabaseConnection`은 환경 핸들을 소유하고, 소멸 시 연결 해제 후 핸들을 반납합니다.
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

DB 연결 테스트는 읽기 전용 SELECT만 실행합니다. 이후 유닛 선택 저장과 전투/리플레이 저장에 필요한 테이블을 추가할 계획입니다.

2026-10-16 통신 검증 항목:

- x64 Debug/Release의 서버 및 더미 클라이언트 빌드와 기본 점검
- A/B/C/World 각각 더미 8개, A 서버 추가 16개 접속의 Hello/Ping/Pong 검증
- Protobuf 서버/맵 필드 일치, 한글 문자열과 0/0xff 바이너리, 4096바이트 최대 패킷
- 분할 헤더와 이어 붙인 패킷 복원
- 짧거나 과도한 길이, 잘못된 버전/종류, Hello 이전 Ping, 중복 Hello 거부
- 잘린 Protobuf 문자열, 잘못된 UTF-8/와이어 타입, 누락된 이름/순번 거부
- 초기 MP 초과, 없는 스킬 참조, 스킬 번호 중복, 시작 좌표 중복/범위 오류, 워커 수 0 거부
- 시작 좌표/맵 외곽의 음수 X/Y 거부, 시작 좌표 (0,0) 허용
- 정해진 시간 후 서버 종료, 워커 합류, 소켓 정리

Debug와 Release 각각 총 48개의 더미 접속 시나리오에서 Ping/Pong 응답 960개를 확인했습니다.
최대 동시 더미 수는 한 서버에 16개이며, 설정 오류 10건과 잘못된 패킷 11건의 거부도 확인했습니다.
빌드 중 외부 Protobuf/Abseil 헤더에서 발생하는 MSVC 경고는 의존성 갱신 때 별도로 확인합니다.

테스트 스크립트는 `test-network.ps1`이며, 로그는 무시되는 `build/network-check-*` 폴더에 남깁니다.
채팅, 이동, 스킬, 전장 승패는 아직 테스트 대상이 아닙니다.

## 다음 구현 순서

1. 게임 서버 내 일반 채팅, A/B/C 공통 월드 채팅, 유닛 선택과 저장.
2. 전장 진입/복귀, 단일 전장 로직 소유 스레드, 서버 판정 이동과 기본 공격.
3. MP/스킬/사망 100초 대기, 건물 파괴와 마지막 생존 진영 승리, 300초 재시작.
4. 전투 틱·이벤트 순서·데이터 버전과 스냅샷을 기록하고 더미에서 리플레이 검증.
5. C++ 더미 부하/장애 검증 후 언리얼 클라이언트의 조작·표시·리플레이 화면 연결.

DB 작업 큐 도입 시 종료·재시도·중복·프로세스 중단 시 유실 정책을 먼저 정합니다.
게임 응답 완료와 DB 저장 완료는 구분해서 측정합니다.

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
`.deps`, `Protocol/Generated`, `build` 폴더의 파일은 올리지 않으며 `.proto` 원본과 의존성 준비 스크립트를 함께 관리합니다.
