# Pintos 개발 환경 설정

이 프로젝트는 Ubuntu 22.04 기반 VS Code DevContainer에서 빌드하고 실행합니다. macOS와 Windows에서도 동일한 x86-64 Linux 개발 환경을 사용할 수 있도록 Docker와 QEMU를 포함한 설정을 제공합니다.

## 준비 사항

- Docker Desktop
- Visual Studio Code
- VS Code Dev Containers 확장

## 저장소 받기

```bash
git clone https://github.com/5giran/pintos.git
cd pintos
```

## DevContainer 열기

1. VS Code에서 저장소 디렉터리를 엽니다.
2. 명령 팔레트를 엽니다.
3. `Dev Containers: Reopen in Container`를 실행합니다.
4. 최초 container image 빌드가 끝날 때까지 기다립니다.

컨테이너 terminal이 열리면 Pintos 환경을 활성화합니다.

```bash
cd /workspaces/pintos/pintos
source ./activate
```

저장소 이름에 따라 `/workspaces/pintos` 부분은 달라질 수 있습니다. 현재 저장소 안의 `pintos` 디렉터리로 이동하면 됩니다.

## 빌드

User Programs를 빌드하려면 다음 명령을 사용합니다.

```bash
cd pintos/userprog
make
```

Virtual Memory 구성을 빌드하려면 다음 명령을 사용합니다.

```bash
cd pintos/vm
make
```

## 테스트 결과 확인

전체 테스트 실행 후 요약은 각 build 디렉터리의 `results`에서 확인할 수 있습니다.

```bash
cat build/results
```

개별 테스트는 다음 파일을 확인합니다.

```text
build/tests/.../<test-name>.result
build/tests/.../<test-name>.output
build/tests/.../<test-name>.errors
```

호스트 macOS에서 직접 빌드하면 ARM clang과 Pintos의 x86 옵션이 충돌할 수 있습니다. 빌드와 테스트는 DevContainer 안에서 수행하는 것을 권장합니다.
