# FBNeoRageX v2.3.1

> Fixes a black screen on PCs with older integrated graphics, adds a software renderer, and ships the default opening video inside the executable.
> 구형 내장 그래픽 PC에서 화면이 검게 나오던 문제를 고치고, 소프트웨어 렌더러와 실행 파일에 내장된 기본 오프닝 영상을 추가했습니다.

---

## 🐛 Fixed / 수정

| English | 한국어 |
|---|---|
| **Black screen on some PCs** (older integrated graphics / drivers): the whole window stayed black or turned red once a game was started — even the menu. The OpenGL game view is no longer attached to the window until it is needed, so the menu is always drawn without GPU compositing. | **일부 PC에서 화면이 검게 나오던 문제**(구형 내장 그래픽·드라이버): 게임을 켜면 메뉴까지 창 전체가 검은색(전체화면은 빨간색)으로 나왔습니다. 이제 OpenGL 게임 화면을 필요할 때까지 창에 붙이지 않아서, 메뉴는 항상 GPU 합성 없이 그려집니다. |
| **Automatic fallback (Windows)**: if the game is running but the real window stays black, the program tells you, switches to the software renderer and restarts itself. | **자동 전환(Windows)**: 게임이 돌고 있는데 실제 화면이 계속 검으면 안내창을 띄우고 소프트웨어 렌더러로 바꿔 스스로 다시 시작합니다. |

## ✨ New / 새 기능

| English | 한국어 |
|---|---|
| **Software renderer** — a CPU-drawn game view that works on any PC. Switch it in **VIDEO OPTIONS → RENDERER** (restart required). It supports scale mode, smooth filter, TATE rotation, bezel and the REC marker. | **소프트웨어 렌더러** — 어떤 PC에서도 보이는 CPU 방식 게임 화면. **VIDEO OPTIONS → RENDERER**에서 바꿉니다(다시 시작해야 적용). 화면 비율, 부드럽게, TATE 회전, 베젤, REC 표시를 지원합니다. |
| With the software renderer, options it cannot do (**CRT, FLASH GUARD, SHADER, SHADER PARAMS, VSYNC**) are locked and greyed out. | 소프트웨어 렌더러에서는 쓸 수 없는 항목(**CRT, 플래시 감소, 셰이더, 셰이더 설정, VSYNC**)이 회색으로 잠깁니다. |
| **Default opening video is embedded in the exe.** Put your own `intro.mp4` in `assets/intro/` to replace it; with no video the code-drawn animation is used as before. | **기본 오프닝 영상을 exe에 내장**했습니다. `assets/intro/` 에 직접 만든 `intro.mp4` 를 넣으면 그 영상이 우선하고, 영상이 전혀 없으면 예전처럼 코드로 그린 애니메이션이 나옵니다. |
| Startup log now records the graphics environment (OpenGL version, GPU, monitors, Qt version) in `crash_log.txt` to help diagnose display problems. | 시작 로그(`crash_log.txt`)에 그래픽 환경(OpenGL 버전, GPU, 모니터, Qt 버전)을 기록해 화면 문제를 진단하기 쉬워졌습니다. |

> If the game screen is black on your PC: open **VIDEO OPTIONS → RENDERER**, choose **SOFTWARE**, and restart.
> 게임 화면이 검게 나오면 **VIDEO OPTIONS → RENDERER** 에서 **SOFTWARE** 로 바꾸고 다시 시작하세요.

## ⚠️ Known notes / 알려 둘 점

- The automatic fallback is **Windows only** (screen capture is blocked on Wayland/gamescope). Steam Deck keeps using OpenGL.
  자동 전환은 **Windows 전용**입니다(Wayland/gamescope 는 화면 캡처가 막혀 있음). 스팀덱은 OpenGL 을 계속 씁니다.
- Windows Defender may flag the unsigned executable (`Behavior:Win32/DefenseEvasion.A!ml`, a heuristic detection). Verify the SHA256 below and, if needed, add the folder to the exclusion list or report a false positive to Microsoft.
  서명 없는 실행 파일이라 Windows Defender 가 의심(`Behavior:Win32/DefenseEvasion.A!ml`, 행동 기반 추정 탐지)할 수 있습니다. 아래 SHA256 을 확인하고, 필요하면 해당 폴더를 예외로 지정하거나 Microsoft 에 오탐으로 신고하세요.

## 📦 Downloads / 다운로드

| File / 파일 | Platform |
|---|---|
| `FBNeoRageX.exe` | Windows 10/11 x64 — single portable executable / 단일 포터블 실행 파일 |
| `FBNeoRageX-linux-x86_64.tar.gz` | Steam Deck · Linux x86_64 |

> ⚠️ **The FBNeo core is not included / FBNeo 코어는 포함되어 있지 않습니다.**
> Put `fbneo_libretro.dll` next to the executable (Windows), or `fbneo_libretro.so` in `~/FBNeoRageX/bin/` (Steam Deck).
> 윈도우는 실행 파일 옆에 `fbneo_libretro.dll`, 스팀덱은 `~/FBNeoRageX/bin/` 에 `fbneo_libretro.so` 를 넣어 주세요.

**SHA256 (FBNeoRageX.exe):** `19965ac0d34a299e22d86269a99ef6dac3eb7c8839f3754133872279e1cbb819`
