# tool_automouse v1.0.0

Windows 10/11용 MFC 시스템 트레이 오토클릭 앱의 첫 정식 릴리스입니다.

- **Alt+C를 누른 채 P**: 현재 포인터 좌표에서 반복 왼쪽 클릭 시작
- **Alt+C를 누른 채 S**: 자동 클릭 정지
- 트레이 우클릭 메뉴의 **설정**, **정지**, **종료**
- 밀리초 또는 초 단위 클릭 간격 설정과 사용자별 설정 저장
- 다중 모니터 좌표 지원, 잠금·절전·세션 연결 해제 시 정지

64비트 Windows에는 `tool_automouse-v1.0.0-windows-x64.zip`, 32비트 Windows에는 `tool_automouse-v1.0.0-windows-x86.zip`을 사용하세요. 압축을 풀고 `AutoClicker.exe`를 실행하면 됩니다. 사용 안내와 버전 정보도 ZIP에 포함되어 있습니다.

`SHA256SUMS.txt`에는 ZIP 파일의 SHA-256 체크섬이 포함됩니다.

Windows x64·Win32 Release 빌드 및 핵심 로직 테스트를 거쳐 배포합니다. 실제 Windows GUI·전역 훅·입력 전달은 수동 확인이 필요합니다. Alt+C 접두 키는 활성 프로그램에도 전달될 수 있습니다.
