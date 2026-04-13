# FBNeoRageX

**NeoGeo / Arcade Emulator Frontend — NeoRAGEx 클래식 스타일**

> FBNeo libretro 코어 기반의 아케이드 에뮬레이터 프론트엔드.  
> 1999년 NeoRAGEx UI 감성을 현대적으로 재현하였습니다.

---

## 🖥️ 스크린샷 / Screenshot

*(추후 추가 예정)*

---

## 📋 목차 / Table of Contents

- [한국어 설명](#한국어)
- [English Description](#english)

---

<a name="한국어"></a>
# 🇰🇷 한국어

## 소개

FBNeoRageX는 [FinalBurn Neo](https://github.com/finalburnneo/FBNeo) libretro 코어를 사용하는  
NeoGeo / 아케이드 에뮬레이터 프론트엔드입니다.  
Python + PySide6(Qt6) + OpenGL로 제작되었으며 Windows와 Steam Deck(Linux)을 지원합니다.

## 주요 기능

| 기능 | 설명 |
|---|---|
| 🎮 게임 목록 | ROM 폴더 자동 스캔, 한글/영문 게임명 표시, 즐겨찾기(★) |
| 🖼️ 프리뷰 | 게임 선택 시 스크린샷/동영상 미리보기 자동 표시 |
| 💾 세이브 스테이트 | F5 저장 / F7 불러오기, 슬롯 10개 |
| ⏩ 빠른 진행 | F6 고속 진행 (Fast Forward) |
| 🎬 녹화 | F9 게임 플레이 영상 녹화 (JPEG+오디오 → MP4) |
| 📸 스크린샷 | F8 즉시 캡처 |
| 🕹️ 게임패드 | XInput(Windows), /dev/input/js0(Linux/Steam Deck) 지원 |
| ⌨️ 키 리매핑 | 모든 버튼 사용자 지정 가능 |
| 🔄 터보 버튼 | 버튼별 터보 ON/OFF, 주기 조절 |
| 🎨 GLSL 쉐이더 | RetroArch 표준 .glsl 쉐이더 적용 (crt-pi 등) |
| 📺 CRT 스캔라인 | 내장 CRT 효과, 강도 조절 |
| 🔧 DIP 스위치 | 게임별 난이도/지역/설정 조절 |
| 🃏 치트 코드 | 치트 파일 로드 및 실시간 적용 |
| 🔊 오디오 | DRC(Dynamic Rate Control)로 끊김 없는 오디오 |

## 시스템 요구사항

### Windows

| 항목 | 요구사항 |
|---|---|
| OS | Windows 10 / 11 (64bit) |
| Python | **3.10 이상** 필요 |
| 필수 패키지 | `PySide6` `PyOpenGL` `numpy` `Pillow` |
| 선택 패키지 | `sounddevice` (오디오), `av` (MP4 녹화) |
| 코어 파일 | `fbneo_libretro.dll` (별도 다운로드 필요) |

#### Windows 실행 방법

```bash
# 1. 패키지 설치
pip install PySide6 PyOpenGL PyOpenGL_accelerate numpy Pillow sounddevice

# 2. fbneo_libretro.dll 을 같은 폴더에 배치

# 3. 실행
python FBNeoRageX_v1.7.py
```

> ⚠️ **주의**: 현재 단독 실행 파일(`.exe`) 버전은 일부 환경에서 문제가 발생할 수 있습니다.  
> **Python 스크립트로 직접 실행**하는 방법을 권장합니다.

### Steam Deck / Linux

| 항목 | 요구사항 |
|---|---|
| OS | SteamOS 3 / Ubuntu 기반 Linux |
| Python | 시스템 Python 3.10+ (기본 내장) |
| 코어 파일 | `fbneo_libretro.so` (별도 다운로드 필요) |
| 의존성 | **첫 실행 시 자동 설치** (venv 생성, 약 5분 소요) |

#### Steam Deck 실행 방법

```bash
# 1. 파일을 ~/FBNeoRageX/ 에 복사 후
chmod +x ~/FBNeoRageX/run.sh

# 2. 터미널에서 실행 또는 Steam 비-Steam 게임으로 등록
~/FBNeoRageX/run.sh
```

> ⚠️ **주의**: 첫 실행 시 PySide6 등 패키지를 자동으로 설치합니다.  
> 인터넷 연결 상태에서 실행하세요. 이후 실행은 즉시 시작됩니다.

## ROM / 코어 파일 준비

- **ROM**: `.zip` 형식의 FBNeo 호환 ROM을 `roms/` 폴더에 배치
- **libretro 코어**: [FBNeo 공식 빌드](https://github.com/finalburnneo/FBNeo) 또는 RetroArch 코어에서 획득
  - Windows: `fbneo_libretro.dll`
  - Linux/Steam Deck: `fbneo_libretro.so`

## 폴더 구조

```
FBNeoRageX/
├── FBNeoRageX_v1.7.py     # 메인 실행 파일
├── fbneo_libretro.dll      # Windows 코어 (별도 획득)
├── fbneo_libretro.so       # Linux 코어 (별도 획득)
├── config.json             # 설정 저장
├── game_names_db.json      # 게임명 DB
├── run.sh                  # Steam Deck 런처
├── roms/                   # ROM 파일 (.zip)
├── previews/               # 프리뷰 이미지/동영상
├── screenshots/            # 캡처 이미지
├── cheats/                 # 치트 파일 (.cht / .ini)
└── assets/                 # UI 리소스
```

## 기본 조작키

| 키 | 기능 |
|---|---|
| F5 | 세이브 스테이트 저장 |
| F7 | 세이브 스테이트 불러오기 |
| F6 | 빠른 진행 (누르는 동안) |
| F8 | 스크린샷 |
| F9 | 녹화 시작/중지 |
| F2 | 슬롯 변경 |
| ESC | 게임 종료 → 메인 화면 |
| 방향키 | 이동 |
| A / S / D / F | 버튼 A / B / C / D |
| 1 | START |
| 5 | COIN (동전 투입) |

## 업데이트 내역

### v1.7 (현재)
- ✅ GLSL 쉐이더 적용 수정 — vec2/vec4 uniform 타입 자동 감지 (crt-pi 등 정상 작동)
- ✅ 게임 목록 즐겨찾기 이름순 정렬
- ✅ GL 진단 로그 이벤트 패널 표시
- ✅ Steam Deck 패키지 지원
- ✅ NeoRAGEx 스타일 UI (픽셀 폰트, 파란 테두리, 애니메이션)
- ✅ DRC 오디오 엔진 (끊김 방지)
- ✅ 게임플레이 녹화 (MP4)
- ✅ XInput / Linux 게임패드 지원
- ✅ 치트 코드 시스템
- ✅ 세이브 스테이트 (슬롯 10개)

## 알려진 문제

- 단독 실행 파일(`.exe`) 빌드 시 일부 환경에서 오류 발생 가능 → Python 직접 실행 권장
- Steam Deck 첫 실행 시 패키지 설치로 약 5분 소요
- MP4 녹화 기능은 `av` 패키지 설치 필요

## 라이선스

- **FBNeoRageX 프론트엔드 코드**: MIT License
- **FBNeo 코어**: [FBNeo 라이선스](https://github.com/finalburnneo/FBNeo/blob/master/LICENSE) 참고
- ROM 파일은 포함되지 않으며 사용자가 직접 합법적으로 획득해야 합니다

---

<a name="english"></a>
# 🇺🇸 English

## Overview

FBNeoRageX is an arcade emulator frontend based on the [FinalBurn Neo](https://github.com/finalburnneo/FBNeo) libretro core.  
Built with Python + PySide6 (Qt6) + OpenGL, it supports both **Windows** and **Steam Deck (Linux)**.  
The UI is inspired by the classic NeoRAGEx (1999) interface.

## Features

| Feature | Description |
|---|---|
| 🎮 Game List | Auto-scan ROM folder, Korean/English game names, Favorites (★) |
| 🖼️ Preview | Auto-display screenshot/video preview on game selection |
| 💾 Save States | F5 Save / F7 Load, 10 slots |
| ⏩ Fast Forward | F6 hold for fast forward |
| 🎬 Recording | F9 gameplay recording (JPEG + audio → MP4) |
| 📸 Screenshot | F8 instant capture |
| 🕹️ Gamepad | XInput (Windows), /dev/input/js0 (Linux/Steam Deck) |
| ⌨️ Key Remapping | Fully customizable button layout |
| 🔄 Turbo | Per-button turbo with adjustable period |
| 🎨 GLSL Shaders | RetroArch-compatible .glsl shaders (crt-pi, etc.) |
| 📺 CRT Scanlines | Built-in CRT effect with intensity control |
| 🔧 DIP Switches | Per-game difficulty / region / config |
| 🃏 Cheat Codes | Load cheat files and apply in real-time |
| 🔊 Audio | DRC (Dynamic Rate Control) for smooth audio |

## System Requirements

### Windows

| Item | Requirement |
|---|---|
| OS | Windows 10 / 11 (64-bit) |
| Python | **3.10 or higher** |
| Required packages | `PySide6` `PyOpenGL` `numpy` `Pillow` |
| Optional packages | `sounddevice` (audio), `av` (MP4 recording) |
| Core file | `fbneo_libretro.dll` (download separately) |

#### Running on Windows

```bash
# 1. Install packages
pip install PySide6 PyOpenGL PyOpenGL_accelerate numpy Pillow sounddevice

# 2. Place fbneo_libretro.dll in the same folder

# 3. Run
python FBNeoRageX_v1.7.py
```

> ⚠️ **Note**: The standalone `.exe` build may have issues on some systems.  
> Running the **Python script directly** is recommended.

### Steam Deck / Linux

| Item | Requirement |
|---|---|
| OS | SteamOS 3 / Ubuntu-based Linux |
| Python | System Python 3.10+ (pre-installed) |
| Core file | `fbneo_libretro.so` (download separately) |
| Dependencies | **Auto-installed on first run** (venv, ~5 min) |

#### Running on Steam Deck

```bash
# 1. Copy files to ~/FBNeoRageX/ then:
chmod +x ~/FBNeoRageX/run.sh

# 2. Run from terminal or add as Non-Steam Game in Steam
~/FBNeoRageX/run.sh
```

> ⚠️ **Note**: On first launch, PySide6 and other packages are installed automatically.  
> Make sure you have an internet connection. Subsequent launches start instantly.

## ROM / Core File Setup

- **ROMs**: Place FBNeo-compatible `.zip` ROMs in the `roms/` folder
- **libretro core**: Obtain from [FBNeo official builds](https://github.com/finalburnneo/FBNeo) or RetroArch core downloader
  - Windows: `fbneo_libretro.dll`
  - Linux/Steam Deck: `fbneo_libretro.so`

## Directory Structure

```
FBNeoRageX/
├── FBNeoRageX_v1.7.py     # Main script
├── fbneo_libretro.dll      # Windows core (obtain separately)
├── fbneo_libretro.so       # Linux core (obtain separately)
├── config.json             # Settings
├── game_names_db.json      # Game name database
├── run.sh                  # Steam Deck launcher
├── roms/                   # ROM files (.zip)
├── previews/               # Preview images/videos
├── screenshots/            # Captured screenshots
├── cheats/                 # Cheat files (.cht / .ini)
└── assets/                 # UI resources
```

## Default Controls

| Key | Action |
|---|---|
| F5 | Save state |
| F7 | Load state |
| F6 | Fast forward (hold) |
| F8 | Screenshot |
| F9 | Record toggle |
| F2 | Change save slot |
| ESC | Quit game → Main menu |
| Arrow keys | Directional input |
| A / S / D / F | Button A / B / C / D |
| 1 | START |
| 5 | COIN (Insert coin) |

## Changelog

### v1.7 (Current)
- ✅ Fixed GLSL shader rendering — auto-detect vec2/vec4 uniform types (crt-pi and others now work correctly)
- ✅ Favorites list sorted alphabetically by name
- ✅ GL diagnostic log shown in event panel
- ✅ Steam Deck package support
- ✅ NeoRAGEx-style UI (pixel font, blue border, animations)
- ✅ DRC audio engine (no stuttering)
- ✅ Gameplay recording (MP4)
- ✅ XInput / Linux gamepad support
- ✅ Cheat code system
- ✅ Save states (10 slots)

## Known Issues

- Standalone `.exe` build may fail on some systems → use Python script directly
- Steam Deck first launch takes ~5 minutes for package installation
- MP4 recording requires the `av` package

## License

- **FBNeoRageX frontend code**: MIT License
- **FBNeo core**: See [FBNeo License](https://github.com/finalburnneo/FBNeo/blob/master/LICENSE)
- ROM files are not included. Users must obtain ROMs legally.
