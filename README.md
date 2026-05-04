# FBNeoRageX

> A modern arcade emulator frontend powered by [FinalBurn Neo](https://github.com/finalburnneo/FBNeo) (libretro core).  
> Built with **C++17 + Qt6**, targeting Windows and Steam Deck.

---

## ✨ Features

- 🎮 **Full libretro support** — uses `fbneo_libretro.dll/.so` for accurate arcade emulation
- 🖥️ **4-panel UI** — Game List / Options / Preview / Events log
- 🔍 **Game list** — ROM scan, search filter, favorites (★)
- 🖼️ **Preview** — PNG preview image → auto-plays MP4 after 3 seconds
- 💾 **Save States** — 8 slots (F1–F8 load / Shift+F1–F8 save)
- ⏩ **Fast Forward** — uncapped speed (F11)
- 📹 **Recording** — MP4 capture via libav* (F9), preview clip (Ctrl+F9)
- 📸 **Screenshots** — F12 / Ctrl+F12 (preview image)
- 🔄 **1P↔2P Swap** — practice mode (F10)
- 🎛️ **DIP Switches** — per-game Machine Settings (region, lives, difficulty, Neo-Geo mode…)
- 🃏 **Cheat system** — INI-based cheat parser with per-game support
- 🌐 **Rollback Netplay** — cross-platform Windows ↔ Steam Deck
- 🎮 **Controller support** — XInput / WinMM (arcade sticks) / keyboard
- 🕹️ **Turbo buttons** — per-button toggle with adjustable period
- 📺 **CRT shader** — scanlines, RGB mask, vignette, bloom
- 🔒 **Service mode protection** — arcade test/service menu blocked by default; intentional access via `` ` `` key

---

## 📋 Requirements

### Windows
| Component | Requirement |
|-----------|-------------|
| OS | Windows 10 / 11 (64-bit) |
| Runtime | Visual C++ Redistributable 2022 (x64) |
| Core | `fbneo_libretro.dll` (place next to the `.exe`) |
| ROMs | FinalBurn Neo compatible ROM set |

### Steam Deck / Linux
| Component | Requirement |
|-----------|-------------|
| OS | SteamOS 3.x / Ubuntu 22.04+ |
| Core | `fbneo_libretro.so` (place in `~/FBNeoRageX/bin/`) |
| ROMs | FinalBurn Neo compatible ROM set |

---

## 🚀 Installation

### Windows

1. Download the latest release ZIP from the [Releases](../../releases) page.
2. Extract to any folder (e.g. `C:\FBNeoRageX\`).
3. Obtain `fbneo_libretro.dll` and place it in the **same folder** as `FBNeoRageX.exe`.
4. Launch `FBNeoRageX.exe`.
5. Go to **Options → Directories** and set your **ROMs folder**.

### Steam Deck

1. Download `FBNeoRageX-linux-x86_64.tar.gz` from the [Releases](../../releases) page.
2. Copy the archive to your Steam Deck (USB drive or network share).
3. Open **Konsole** (Desktop Mode) and run:
   ```bash
   tar -xzf FBNeoRageX-linux-x86_64.tar.gz -C ~/
   ~/FBNeoRageX/FBNeoRageX.sh
   ```
4. Obtain `fbneo_libretro.so` and copy it to `~/FBNeoRageX/bin/`.
5. **Add to Steam** (optional — enables Gaming Mode):
   - Steam → **Add a Game** → **Add a Non-Steam Game**
   - Browse and select `~/FBNeoRageX/FBNeoRageX.sh`

---

## 🎮 Controls

### Keyboard (default mapping)

| Keyboard Key | Libretro Button | NeoGeo |
|:---:|:---:|:---:|
| `Z` | B (JOYPAD_B) | A Button |
| `X` | A (JOYPAD_A) | B Button |
| `A` | Y (JOYPAD_Y) | C Button |
| `S` | X (JOYPAD_X) | D Button |
| `D` | L (JOYPAD_L) | — |
| `C` | R (JOYPAD_R) | — |
| `Enter` | START | START |
| `Space` | SELECT | SELECT |
| `↑ ↓ ← →` | D-Pad | D-Pad |

> All mappings can be changed in **Options → Controls → KEYBOARD**.

---

### Xbox / XInput Controller

| Gamepad Button | Libretro Button | NeoGeo |
|:---:|:---:|:---:|
| A | B (JOYPAD_B) | A Button |
| B | A (JOYPAD_A) | B Button |
| X | Y (JOYPAD_Y) | C Button |
| Y | X (JOYPAD_X) | D Button |
| LB | L (JOYPAD_L) | — |
| RB | R (JOYPAD_R) | — |
| LT | L2 | — |
| RT | R2 | — |
| L3 (stick click) | L3 | — |
| R3 (stick click) | R3 | — |
| Back / Select | SELECT | SELECT |
| Start / Menu | START | START |
| D-Pad | D-Pad | D-Pad |

---

### Arcade Stick (WinMM)

| Button # | Libretro Button | NeoGeo |
|:---:|:---:|:---:|
| Button 1 | B (JOYPAD_B) | A Button |
| Button 2 | A (JOYPAD_A) | B Button |
| Button 3 | Y (JOYPAD_Y) | C Button |
| Button 4 | X (JOYPAD_X) | D Button |
| Button 5 | L | — |
| Button 6 | R | — |
| Button 7 | L2 | — |
| Button 8 | R2 | — |
| Button 9 | SELECT | SELECT |
| Button 10 | START | START |
| Stick | D-Pad | D-Pad |

> Mappings can be changed in **Options → Controls → ARCADE STICK**.

---

## ⌨️ Keyboard Shortcuts

### System / Emulator

| Key | Action |
|-----|--------|
| `Tab` | Pause / Resume (toggle between game and GUI) |
| `ESC` | Stop game → return to main screen |
| `Alt + Enter` | Toggle fullscreen |
| `` ` `` (backtick) | Toggle **Service Mode** for 5 seconds (enables L2/Test button) |

### Save States

| Key | Action |
|-----|--------|
| `F1` – `F8` | **Load** save state (slot 1–8) |
| `Shift + F1` – `Shift + F8` | **Save** save state (slot 1–8) |

### Emulation & Recording

| Key | Action |
|-----|--------|
| `F9` | Start / stop recording → `recordings/{rom}_{timestamp}.mp4` |
| `Ctrl + F9` | Record **preview clip** → `previews/{rom}.mp4` (overwrites) |
| `F10` | **1P↔2P port swap** (single-player practice mode) |
| `F11` | Fast Forward toggle |
| `F12` | Screenshot → `screenshots/{rom}_{timestamp}.png` |
| `Ctrl + F12` | Save **preview image** → `previews/{rom}.png` (overwrites) |

### Gamepad Shortcut (all platforms)

| Combo | Action |
|-------|--------|
| `SELECT + START` (hold 2 sec) | Pause / return to GUI |

---

## 🕹️ Steam Deck — Recommended Button Layout

Steam Deck has no F-keys, so map them via **Steam Input**.

### Recommended Back Button Mapping

| Button | Map to key | Function |
|--------|-----------|----------|
| R4 | `F10` | 1P↔2P swap |
| R5 | `F11` | Fast Forward |
| L4 | `F12` | Screenshot |
| L5 | `Tab` | Pause |

### How to set Steam Input
1. **Gaming Mode**: Steam button → Controller icon → **Edit Layout** → **Back Buttons**
2. **Desktop Mode**: Steam → Library → right-click FBNeoRageX → Properties → Controller → Edit Custom Configuration
3. Assign each button → **Keyboard Key** → select the corresponding key.

---

## 🎛️ Options Panel Guide

| Tab | Description |
|-----|-------------|
| **Controls** | Remap keyboard, XInput, and arcade stick buttons |
| **Directories** | Set ROM path, save path, screenshot path, recording path |
| **Video** | Scale mode (Fit / Stretch / Integer), CRT shader, frameskip, V-Sync |
| **Audio** | Volume, sample rate, buffer size |
| **Machine** | DIP switches — region, difficulty, lives, Neo-Geo mode (MVS/AES), etc. |
| **Shots** | Preview image/video factory (Ctrl+F12 / Ctrl+F9) |
| **Cheats** | Enable/disable per-game cheats loaded from `cheats/` folder |
| **Netplay** | Host or join a rollback netplay session |

---

## 🔒 Arcade Service Menu (Test Mode)

Arcade boards have an internal **service/test menu** that can be accidentally triggered during gameplay.

### How it's handled

| System | Service Trigger | Protection |
|--------|----------------|------------|
| CPS1 / CPS2 | L2 button (JOYPAD_L2, index 12) | **Blocked by default** |
| NeoGeo MVS | START button hold (internal BIOS logic) | **Use AES mode** (see below) |

### Intentional service menu access
Press `` ` `` (backtick) while a game is running → **Service Mode activates for 5 seconds**.  
During this window, L2 (Xbox LT) is unblocked → press it to enter the service menu.

### NeoGeo service menu — permanent fix
The NeoGeo MVS BIOS has a built-in operator test menu triggered by holding START.  
This **cannot** be blocked from the frontend because it's indistinguishable from legitimate game commands (e.g., KOF boss select requires holding START for up to 30 seconds while pressing button combos).

**Solution**: In **Options → Machine**, set `Neo-Geo Mode` to `aes`.  
The AES (home console) BIOS has no operator test menu — boss select codes still work normally.

---

## 🌐 Netplay

FBNeoRageX supports **rollback-based netplay** between Windows and Steam Deck.

1. Both players must use the **same ROM** and the **same version** of `fbneo_libretro.dll/.so`.
2. **Host**: Options → Netplay → set port → **HOST GAME**
3. **Guest**: Options → Netplay → enter host IP and port → **CONNECT**
4. The host's player 1 and guest's player 2 are automatically assigned.

> Port forwarding may be required on the host side for internet play.

---

## 🔧 Building from Source

### Requirements
- CMake 3.20+
- Qt6 (Core, Widgets, OpenGL, OpenGLWidgets, Multimedia, Network)
- C++17 compiler (MSVC 2022 or GCC/Clang)

### Windows (automated)
```bat
build_windows.bat
```
Automatically finds Qt6 (MSYS2 or official installer), configures CMake, builds, and runs `windeployqt`.  
Output: `build_win\FBNeoRageX.exe`

### Windows (manual)
```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

### Steam Deck / Linux (via WSL2)
```bat
build_steamdeck_wsl.bat
```
Runs the Linux build inside WSL2 Ubuntu, packages all Qt libraries with `linuxdeploy`, and produces:  
`build_linux/FBNeoRageX-linux-x86_64.tar.gz`

### Steam Deck (native)
```bash
chmod +x build_steamdeck.sh
./build_steamdeck.sh
```

---

## 📁 Directory Structure

After setup, your folder should look like this:

```
FBNeoRageX/
├── FBNeoRageX.exe          ← Main executable (Windows)
├── fbneo_libretro.dll      ← FBNeo libretro core (required, not included)
├── assets/                 ← UI assets (fonts, shaders)
├── roms/                   ← Place your ROM ZIPs here
├── saves/                  ← Save states (auto-created)
├── screenshots/            ← Screenshots (auto-created)
├── recordings/             ← Video recordings (auto-created)
├── previews/               ← Preview images/videos
└── cheats/                 ← Cheat files (.ini format)
```

---

## ❓ FAQ

**Q: Where do I get `fbneo_libretro.dll` / `fbneo_libretro.so`?**  
A: Build it from the [FinalBurn Neo source](https://github.com/finalburnneo/FBNeo) or download a pre-built core from [Libretro Buildbot](https://buildbot.libretro.com/nightly/).

**Q: My game is not in the list.**  
A: Make sure your ROM ZIPs are compatible with the **FinalBurn Neo** ROM set (not MAME). Check the FBNeo compatibility list.

**Q: The NeoGeo test menu keeps appearing.**  
A: Go to **Options → Machine → Neo-Geo Mode** and set it to **`aes`**.

**Q: Audio is stuttering.**  
A: Try increasing **Options → Audio → Buffer** (recommended: 80–120 ms). If using a Bluetooth headset, increase to 150+ ms.

**Q: How do I use cheats?**  
A: Place `.ini` cheat files in the `cheats/` folder. Enable individual cheats in **Options → Cheats** while a game is loaded.

**Q: Fast Forward is too fast.**  
A: Fast Forward removes the frame limiter entirely. Use **Options → Video → Frameskip** to limit speed, or disable V-Sync.

---

## 📜 License

This project is released under the **MIT License**.  
FinalBurn Neo core is subject to its own license — see [FBNeo License](https://github.com/finalburnneo/FBNeo/blob/master/LICENSE).

---

## 🙏 Credits

- [FinalBurn Neo](https://github.com/finalburnneo/FBNeo) — arcade emulation core
- [libretro](https://www.libretro.com/) — core/frontend interface standard
- [Qt6](https://www.qt.io/) — UI framework

---

<details>
<summary>🇰🇷 한국어 설명 (Korean)</summary>

## 소개

FBNeoRageX는 **FinalBurn Neo libretro 코어** 기반의 아케이드 에뮬레이터 프론트엔드입니다.  
C++17 + Qt6로 제작되었으며 Windows와 Steam Deck을 지원합니다.

## 설치 방법

### Windows
1. [Releases](../../releases)에서 ZIP 파일을 다운로드합니다.
2. 원하는 폴더에 압축을 풉니다.
3. `fbneo_libretro.dll`을 `FBNeoRageX.exe`와 **같은 폴더**에 넣습니다.
4. `FBNeoRageX.exe`를 실행합니다.
5. **Options → Directories**에서 ROM 폴더를 설정합니다.

### Steam Deck
1. [Releases](../../releases)에서 `FBNeoRageX-linux-x86_64.tar.gz`를 다운로드합니다.
2. Steam Deck으로 파일을 복사합니다 (USB 또는 네트워크).
3. **Konsole** (데스크톱 모드)에서 실행:
   ```bash
   tar -xzf FBNeoRageX-linux-x86_64.tar.gz -C ~/
   ~/FBNeoRageX/FBNeoRageX.sh
   ```
4. `fbneo_libretro.so`를 `~/FBNeoRageX/bin/`에 복사합니다.

## 키보드 단축키

| 키 | 기능 |
|----|------|
| `Tab` | 일시정지 / 재개 |
| `ESC` | 게임 종료 → 메인 화면 |
| `Alt + Enter` | 전체화면 토글 |
| `` ` `` | 서비스 모드 토글 (5초간 L2 차단 해제) |
| `F1–F8` | 세이브스테이트 로드 (슬롯 1–8) |
| `Shift+F1–F8` | 세이브스테이트 저장 (슬롯 1–8) |
| `F9` | 녹화 시작/중지 |
| `Ctrl+F9` | 프리뷰 영상 녹화 |
| `F10` | 1P↔2P 포트 스왑 |
| `F11` | 패스트포워드 |
| `F12` | 스크린샷 |
| `Ctrl+F12` | 프리뷰 이미지 저장 |

## 서비스 모드 (기판 테스트 메뉴)

- **CPS1/CPS2**: L2 버튼이 기본 차단됨. 의도적 접근 시 `` ` `` 키 → 5초간 L2 허용.
- **NeoGeo**: Options → Machine → `Neo-Geo Mode`를 **`aes`**로 변경하면 오퍼레이터 테스트 메뉴가 사라집니다. (KOF 보스 선택 커맨드는 정상 동작)

## 넷플레이

- Options → Netplay에서 **HOST GAME** 또는 **CONNECT** 선택
- Windows ↔ Steam Deck 크로스플레이 지원
- 같은 ROM과 같은 버전의 코어 DLL/SO 사용 필수

</details>
