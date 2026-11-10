# 핵심 로직 테스트

Linux에서 C++17 지원 g++와 pthread로 실행합니다. 테스트 실행 파일은 임시 폴더에 만들고 자동으로 삭제합니다.

```sh
bash tests/run_portable.sh
```

- `chord_tests.cpp`: 눌림·해제 순서, 키 반복, 정지 우선, 추가 modifier 제외, 키 입력 전달/차단 상태
- `interval_tests.cpp`: 간격 범위, 초 소수 변환, 단위 왕복, 오버플로·잘못된 입력
- `engine_tests.cpp`: 실제 `ClickEngine.cpp`를 모의 Win32 API와 연결하여 긴 대기 중 정지, 정지 후 입력 없음, 시작 키 해제 대기, modifier·물리 버튼 보호, 음수 화면 좌표, 부분 입력 실패 복구 확인

`win32_stub/`은 테스트에서만 사용합니다. 앱 프로젝트에는 포함하지 않습니다. 모의 API 테스트는 Windows ABI, 실제 키보드 훅, 시스템 트레이, UIPI 또는 실제 클릭 전달을 검증하지 않습니다. Windows에서는 프로젝트 루트 README의 수동 검증 절차를 수행해야 합니다.
