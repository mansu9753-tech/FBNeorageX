# FBNeoRageX

> A modern arcade emulator frontend powered by [FinalBurn Neo](https://github.com/finalburnneo/FBNeo) (libretro core).  
> Built with **C++17 + Qt6**, targeting Windows and Steam Deck.

> FinalBurn Neo libretro 코어 기반의 아케이드 에뮬레이터 프론트엔드입니다.  
> **C++17 + Qt6**로 제작되었으며 Windows와 Steam Deck을 지원합니다.

---

## 📋 Update History / 업데이트 내역

### v1.9 (2026-05-08)

| | English | 한국어 |
|---|---|---|
| ✨ NEW | **gamelist.xml support** — Automatically reads Korean game names from a community-provided `gamelist.xml` (라즈겜동 format). Windows: place next to `.exe`. Steam Deck: place in `~/FBNeoRageX/`. | **gamelist.xml 지원** — 라즈겜동 한글화 파일(`gamelist.xml`) 자동 인식. 파일을 프로그램 폴더에 넣으면 게임 목록에 한글명 자동 적용. |
| 🐛 FIX | **Cheat system (CPS1)** — Cheats now correctly apply for CPS1 games: Cadillacs & Dinosaurs, The Punisher, Warriors of Fate, Saturday Night Slam Masters, King of Dragons, and more. | **CPS1 치트 수정** — 케딜락 앤 다이노소어, 더 퍼니셔, 워리어스 오브 페이트 등 CPS1 게임 치트가 정상 적용되지 않던 버그 수정. |
| 🐛 FIX | **fbneo_libretro.dll** — Rebuilt as a fully standalone DLL with no `libwinpthread-1.dll` dependency. | **fbneo_libretro.dll 단독 실행** — `libwinpthread-1.dll` 없이도 동작하는 단일 파일로 재빌드. |

### v1.8 (2026-04-xx)

| | English | 한국어 |
|---|---|---|
| ✨ NEW | **Service Mode Protection** — Arcade test/service menu blocked by default; intentional access via `` ` `` key. | **서비스 모드 보호** — 기판 테스트 메뉴 기본 차단. `` ` `` 키로 5초간 허용. |
| ✨ NEW | **Preview Factory** — `Ctrl+F9` / `Ctrl+F12` to record/capture preview clips and images. | **프리뷰 팩토리** — `Ctrl+F9` / `Ctrl+F12`로 프리뷰 영상·이미지 촬영. |
| 🐛 FIX | **Audio improvements** — DRC-based buffer stabilization, reduced stuttering. | **오디오 개선** — DRC 기반 버퍼 안정화, 끊김 감소. |
| 🐛 FIX | **Netplay stability** — Rollback sync improvements for Windows ↔ Steam Deck cross-play. | **넷플레이 안정성** — Windows ↔ Steam Deck 롤백 동기화 개선. |

---

## ✨ Features / 주요 기능

- 🎮 **Full libretro support** — uses `fbneo_libretro.dll/.so` for accurate arcade emulation  
  **libretro 코어 지원** — `fbneo_libretro.dll/.so`로 정확한 아케이드 에뮬레이션
- 🖥️ **4-panel UI** — Game List / Options / Preview / Events log  
  **4분할 UI** — 게임 목록 / 옵션 / 프리뷰 / 이벤트 로그
- 🔍 **Game list** — ROM scan, search filter, favorites (★)  
  **게임 목록** — ROM 자동 검색, 필터, 즐겨찾기 (★)
- 🌏 **Korean game names** — Supports `gamelist.xml` (라즈겜동 format) for Korean titles  
  **한글 게임명** — `gamelist.xml` (라즈겜동 형식) 자동 인식으로 한글 게임명 표시
- 🖼️ **Preview** — PNG preview image → auto-plays MP4 after 3 seconds  
  **프리뷰** — PNG 미리보기 → 3초 후 MP4 자동 재생
- 💾 **Save States** — 8 slots (F1–F8 load / Shift+F1–F8 save)  
  **세이브스테이트** — 8슬롯 (F1–F8 로드 / Shift+F1–F8 저장)
- ⏩ **Fast Forward** — uncapped speed (F11)  
  **패스트포워드** — 속도 제한 해제 (F11)
- 📹 **Recording** — MP4 capture (F9), preview clip (Ctrl+F9)  
  **녹화** — MP4 캡처 (F9), 프리뷰 영상 (Ctrl+F9)
- 📸 **Screenshots** — F12 / Ctrl+F12 (preview image)  
  **스크린샷** — F12 / Ctrl+F12 (프리뷰 이미지)
- 🔄 **1P↔2P Swap** — practice mode (F10)  
  **1P↔2P 스왑** — 혼자 연습 모드 (F10)
- 🎛️ **DIP Switches** — per-game Machine Settings  
  **DIP 스위치** — 게임별 기판 설정 (지역, 난이도, 목숨 수, NeoGeo 모드 등)
- 🃏 **Cheat system** — INI-based cheat parser  
  **치트 시스템** — INI 형식 치트 파일 자동 로드
- 🌐 **Rollback Netplay** — cross-platform Windows ↔ Steam Deck  
  **롤백 넷플레이** — Windows ↔ Steam Deck 크로스플레이
- 🎮 **Controller support** — XInput / WinMM (arcade sticks) / keyboard  
  **컨트롤러 지원** — XInput / WinMM (아케이드 스틱) / 키보드
- 🕹️ **Turbo buttons** — per-button toggle  
  **터보 버튼** — 버튼별 독립 터보 설정
- 📺 **CRT shader** — scanlines, RGB mask, vignette, bloom  
  **CRT 쉐이더** — 스캔라인, RGB 마스크, 비네트, 블룸
- 🔒 **Service mode protection** — arcade test menu blocked by default  
  **서비스 모드 보호** — 아케이드 테스트 메뉴 기본 차단

---

## 📋 Requirements / 요구사항

### Windows
| Component / 항목 | Requirement / 요구사항 |
|-----------|-------------|
| OS | Windows 10 / 11 (64-bit) |
| Core / 코어 | `fbneo_libretro.dll` (place next to `.exe` / `.exe` 옆에 배치) |
| ROMs | FinalBurn Neo compatible ROM set |

### Steam Deck / Linux
| Component / 항목 | Requirement / 요구사항 |
|-----------|-------------|
| OS | SteamOS 3.x / Ubuntu 22.04+ |
| Core / 코어 | `fbneo_libretro.so` (place in `~/FBNeoRageX/bin/`) |
| ROMs | FinalBurn Neo compatible ROM set |

---

## 🚀 Installation / 설치 방법

### Windows

1. Download the latest release ZIP from the [Releases](../../releases) page.  
   [Releases](../../releases) 페이지에서 최신 ZIP을 다운로드합니다.
2. Extract to any folder (e.g. `C:\FBNeoRageX\`).  
   원하는 폴더에 압축을 풉니다.
3. Obtain `fbneo_libretro.dll` and place it in the **same folder** as `FBNeoRageX.exe`.  
   `fbneo_libretro.dll`을 `FBNeoRageX.exe`와 **같은 폴더**에 넣습니다.
4. Launch `FBNeoRageX.exe`.  
   `FBNeoRageX.exe`를 실행합니다.
5. Go to **Options → Directories** and set your **ROMs folder**.  
   **Options → Directories**에서 ROM 폴더를 설정합니다.
6. *(Optional)* Place `gamelist.xml` next to `FBNeoRageX.exe` for Korean game names.  
   *(선택)* 한글 게임명을 사용하려면 `gamelist.xml`을 `FBNeoRageX.exe` 옆에 넣습니다.

### Steam Deck

1. Download `FBNeoRageX-linux-x86_64.tar.gz` from the [Releases](../../releases) page.  
   [Releases](../../releases) 페이지에서 `FBNeoRageX-linux-x86_64.tar.gz`를 다운로드합니다.
2. Copy the archive to your Steam Deck (USB drive or network share).  
   Steam Deck으로 파일을 복사합니다 (USB 또는 네트워크).
3. Open **Konsole** (Desktop Mode) and run:  
   **Konsole** (데스크톱 모드)에서 실행:
   ```bash
   tar -xzf FBNeoRageX-linux-x86_64.tar.gz -C ~/
   ~/FBNeoRageX/FBNeoRageX.sh
   ```
4. Obtain `fbneo_libretro.so` and copy it to `~/FBNeoRageX/bin/`.  
   `fbneo_libretro.so`를 `~/FBNeoRageX/bin/`에 복사합니다.
5. **Add to Steam** (optional — enables Gaming Mode):  
   **Steam에 추가** (선택 — 게이밍 모드 실행):
   - Steam → **Add a Game** → **Add a Non-Steam Game**
   - Browse and select `~/FBNeoRageX/FBNeoRageX.sh`
6. *(Optional)* Place `gamelist.xml` in `~/FBNeoRageX/` for Korean game names.  
   *(선택)* 한글 게임명을 사용하려면 `gamelist.xml`을 `~/FBNeoRageX/` 폴더에 넣습니다.

> **gamelist.xml** — Korean game name database in 라즈겜동 (ES-DE) format.  
> Not included with this app. Obtain from the community and place in the app folder.  
> 한글 게임명 데이터베이스 (라즈겜동 ES-DE 형식). 앱에 포함되지 않으며 직접 구해서 프로그램 폴더에 넣으시면 됩니다.

---

## 🎮 Controls / 조작

### Keyboard / 키보드 (default mapping / 기본 설정)

| Keyboard Key | Libretro Button | NeoGeo |
|:---:|:---:|:---:|
| `Z` | B (JOYPAD_B) | A Button / A 버튼 |
| `X` | A (JOYPAD_A) | B Button / B 버튼 |
| `A` | Y (JOYPAD_Y) | C Button / C 버튼 |
| `S` | X (JOYPAD_X) | D Button / D 버튼 |
| `D` | L (JOYPAD_L) | — |
| `C` | R (JOYPAD_R) | — |
| `Enter` | START | START |
| `Space` | SELECT | SELECT |
| `↑ ↓ ← →` | D-Pad / 방향키 | D-Pad |

> All mappings can be changed in **Options → Controls → KEYBOARD**.  
> 모든 키는 **Options → Controls → KEYBOARD**에서 변경 가능합니다.

---

### Xbox / XInput Controller / 엑스박스 컨트롤러

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
| D-Pad / 방향패드 | D-Pad | D-Pad |

---

### Arcade Stick (WinMM) / 아케이드 스틱

| Button # / 버튼 번호 | Libretro Button | NeoGeo |
|:---:|:---:|:---:|
| Button 1 / 버튼 1 | B (JOYPAD_B) | A Button |
| Button 2 / 버튼 2 | A (JOYPAD_A) | B Button |
| Button 3 / 버튼 3 | Y (JOYPAD_Y) | C Button |
| Button 4 / 버튼 4 | X (JOYPAD_X) | D Button |
| Button 5 / 버튼 5 | L | — |
| Button 6 / 버튼 6 | R | — |
| Button 7 / 버튼 7 | L2 | — |
| Button 8 / 버튼 8 | R2 | — |
| Button 9 / 버튼 9 | SELECT | SELECT |
| Button 10 / 버튼 10 | START | START |
| Stick / 스틱 | D-Pad / 방향 | D-Pad |

> Mappings can be changed in **Options → Controls → ARCADE STICK**.  
> **Options → Controls → ARCADE STICK**에서 변경 가능합니다.

---

## ⌨️ Keyboard Shortcuts / 키보드 단축키

### System / 시스템

| Key / 키 | Action / 기능 |
|-----|--------|
| `Tab` | Pause / Resume / 일시정지·재개 |
| `ESC` | Stop game → main screen / 게임 종료 → 메인 화면 |
| `Alt + Enter` | Toggle fullscreen / 전체화면 토글 |
| `` ` `` (backtick) | Toggle **Service Mode** 5 sec / **서비스 모드** 5초 허용 |

### Save States / 세이브스테이트

| Key / 키 | Action / 기능 |
|-----|--------|
| `F1` – `F8` | **Load** save state (slot 1–8) / 세이브스테이트 **로드** |
| `Shift + F1` – `Shift + F8` | **Save** save state (slot 1–8) / 세이브스테이트 **저장** |

### Emulation & Recording / 에뮬레이션·녹화

| Key / 키 | Action / 기능 |
|-----|--------|
| `F9` | Start/stop recording / 녹화 시작·중지 → `recordings/{rom}_{timestamp}.mp4` |
| `Ctrl + F9` | Record **preview clip** / **프리뷰 영상** 녹화 → `previews/{rom}.mp4` |
| `F10` | **1P↔2P port swap** / **1P↔2P 포트 스왑** |
| `F11` | Fast Forward / 패스트포워드 |
| `F12` | Screenshot / 스크린샷 → `screenshots/{rom}_{timestamp}.png` |
| `Ctrl + F12` | Save **preview image** / **프리뷰 이미지** 저장 → `previews/{rom}.png` |

### Gamepad / 게임패드

| Combo / 조합 | Action / 기능 |
|-------|--------|
| `SELECT + START` (hold 2 sec / 2초 유지) | Pause / return to GUI / 일시정지·GUI 복귀 |

---

## 🕹️ Steam Deck — Recommended Button Layout / 권장 버튼 레이아웃

Steam Deck has no F-keys — map them via **Steam Input**.  
Steam Deck에는 F키가 없으므로 **Steam Input**으로 매핑합니다.

### Recommended Back Button Mapping / 권장 뒷면 버튼 매핑

| Button / 버튼 | Map to / 매핑 키 | Function / 기능 |
|--------|-----------|----------|
| R4 | `F10` | 1P↔2P swap / 포트 스왑 |
| R5 | `F11` | Fast Forward / 패스트포워드 |
| L4 | `F12` | Screenshot / 스크린샷 |
| L5 | `Tab` | Pause / 일시정지 |

### How to set Steam Input / Steam Input 설정 방법
1. **Gaming Mode**: Steam button → Controller icon → **Edit Layout** → **Back Buttons**  
   **게이밍 모드**: Steam 버튼 → 컨트롤러 아이콘 → **Edit Layout** → **Back Buttons**
2. **Desktop Mode**: Steam → Library → right-click FBNeoRageX → Properties → Controller → Edit Custom Configuration  
   **데스크톱 모드**: Steam → 라이브러리 → FBNeoRageX 우클릭 → 속성 → 컨트롤러 → 커스텀 설정 편집
3. Assign each button → **Keyboard Key** → select the key.  
   각 버튼 → **Keyboard Key** → 해당 키 선택.

---

## 🎛️ Options Panel Guide / 옵션 패널 안내

| Tab / 탭 | Description / 설명 |
|-----|-------------|
| **Controls** | Remap keyboard, XInput, arcade stick / 키보드·XInput·아케이드 스틱 키 재설정 |
| **Directories** | Set ROM, save, screenshot, recording paths / ROM·저장·스크린샷·녹화 경로 설정 |
| **Video** | Scale mode, CRT shader, frameskip, V-Sync / 화면 배율, CRT 쉐이더, 프레임스킵, V싱크 |
| **Audio** | Volume, sample rate, buffer / 볼륨, 샘플레이트, 버퍼 |
| **Machine** | DIP switches — region, difficulty, lives, Neo-Geo mode / 기판 설정 — 지역·난이도·목숨·NeoGeo 모드 |
| **Shots** | Preview image/video factory / 프리뷰 이미지·영상 촬영 (Ctrl+F12 / Ctrl+F9) |
| **Cheats** | Enable/disable per-game cheats / 게임별 치트 ON/OFF |
| **Netplay** | Host or join rollback netplay / 넷플레이 호스트·참가 |

---

## 🔒 Arcade Service Menu / 아케이드 서비스 메뉴

Arcade boards have an internal service/test menu.  
아케이드 기판에는 내부 서비스 메뉴가 있습니다.

| System / 기판 | Service Trigger / 서비스 트리거 | Protection / 보호 |
|--------|----------------|------------|
| CPS1 / CPS2 | L2 button | **Blocked by default / 기본 차단** |
| NeoGeo MVS | START button hold | **Use AES mode / AES 모드 사용** |

**Intentional access / 의도적 접근:**  
Press `` ` `` while a game is running → Service Mode activates for **5 seconds**.  
게임 실행 중 `` ` `` 키 → **5초간** 서비스 모드 허용 (LT/L2 차단 해제).

**NeoGeo fix / NeoGeo 해결:**  
**Options → Machine → Neo-Geo Mode → `aes`**  
AES 모드는 오퍼레이터 테스트 메뉴가 없으며 보스 선택 커맨드는 정상 동작합니다.

---

## 🌐 Netplay / 넷플레이

FBNeoRageX supports **rollback-based netplay** between Windows and Steam Deck.  
FBNeoRageX는 Windows ↔ Steam Deck **롤백 넷플레이**를 지원합니다.

1. Both players must use the **same ROM** and **same core version**.  
   양쪽 모두 **같은 ROM**과 **같은 버전의 코어**를 사용해야 합니다.
2. **Host / 호스트**: Options → Netplay → set port → **HOST GAME**
3. **Guest / 게스트**: Options → Netplay → enter host IP and port → **CONNECT**

> Port forwarding may be required on the host side for internet play.  
> 인터넷 대전 시 호스트 측 포트포워딩이 필요할 수 있습니다.

---

## 🔧 Building from Source / 소스 빌드

### Requirements / 요구사항
- CMake 3.20+
- Qt6 (Core, Widgets, OpenGL, OpenGLWidgets, Multimedia, Network)
- C++17 compiler — MSVC 2022 or GCC/Clang

### Windows (automated / 자동)
```bat
build_windows.bat
```
Output / 출력: `build_win\FBNeoRageX.exe`

### Windows (manual / 수동)
```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

### Steam Deck / Linux (via WSL2)
```bat
build_steamdeck_wsl.bat
```
Output / 출력: `build_linux/FBNeoRageX-linux-x86_64.tar.gz`

### Steam Deck (native / 네이티브)
```bash
chmod +x build_steamdeck.sh
./build_steamdeck.sh
```

---

## 📁 Directory Structure / 폴더 구조

```
FBNeoRageX/
├── FBNeoRageX.exe          ← Main executable (Windows) / 실행 파일
├── fbneo_libretro.dll      ← FBNeo libretro core (required) / 코어 (필수, 미포함)
├── gamelist.xml            ← Korean game names (optional) / 한글 게임명 (선택, 미포함)
├── assets/                 ← UI assets (fonts, shaders) / UI 에셋
├── roms/                   ← Place your ROM ZIPs here / ROM 파일 폴더
├── saves/                  ← Save states (auto-created) / 세이브스테이트
├── screenshots/            ← Screenshots (auto-created) / 스크린샷
├── recordings/             ← Video recordings (auto-created) / 녹화 영상
├── previews/               ← Preview images/videos / 프리뷰 이미지·영상
└── cheats/                 ← Cheat files (.ini format) / 치트 파일
```

**Steam Deck:**
```
~/FBNeoRageX/
├── FBNeoRageX.sh           ← Launcher / 런처
├── gamelist.xml            ← Korean game names (optional) / 한글 게임명 (선택)
├── bin/
│   ├── FBNeoRageX          ← Binary / 바이너리
│   └── fbneo_libretro.so   ← Core / 코어
├── lib/                    ← Qt libraries / Qt 라이브러리
└── plugins/                ← Qt plugins / Qt 플러그인
```

---

## ❓ FAQ

**Q: Where do I get `fbneo_libretro.dll` / `fbneo_libretro.so`?**  
**A:** Build from the [FinalBurn Neo source](https://github.com/finalburnneo/FBNeo) or download from [Libretro Buildbot](https://buildbot.libretro.com/nightly/).  
> **Q: `fbneo_libretro.dll` / `.so`는 어디서 구하나요?**  
> **A:** [FinalBurn Neo 소스](https://github.com/finalburnneo/FBNeo)에서 직접 빌드하거나 [Libretro Buildbot](https://buildbot.libretro.com/nightly/)에서 다운로드하세요.

**Q: My game is not in the list.**  
**A:** Make sure your ROM ZIPs are compatible with the **FinalBurn Neo** ROM set (not MAME).  
> **Q: 게임 목록에 게임이 없어요.**  
> **A:** ROM이 **FinalBurn Neo** 형식인지 확인하세요 (MAME ROM과 다를 수 있습니다).

**Q: The NeoGeo test menu keeps appearing.**  
**A:** Go to **Options → Machine → Neo-Geo Mode** and set it to **`aes`**.  
> **Q: NeoGeo 테스트 메뉴가 계속 떠요.**  
> **A:** **Options → Machine → Neo-Geo Mode**를 **`aes`**로 변경하세요.

**Q: Audio is stuttering.**  
**A:** Try increasing **Options → Audio → Buffer** (recommended: 80–120 ms).  
> **Q: 오디오가 끊겨요.**  
> **A:** **Options → Audio → Buffer**를 늘려보세요 (권장: 80–120 ms). 블루투스 헤드셋은 150+ ms.

**Q: How do I use cheats?**  
**A:** Place `.ini` cheat files in the `cheats/` folder. Enable them in **Options → Cheats**.  
> **Q: 치트는 어떻게 사용하나요?**  
> **A:** `.ini` 치트 파일을 `cheats/` 폴더에 넣고 **Options → Cheats**에서 활성화하세요.

**Q: How do I use Korean game names?**  
**A:** Place `gamelist.xml` (라즈겜동 ES-DE format) next to `FBNeoRageX.exe` (Windows) or in `~/FBNeoRageX/` (Steam Deck). The app loads it automatically.  
> **Q: 한글 게임명은 어떻게 사용하나요?**  
> **A:** `gamelist.xml` (라즈겜동 ES-DE 형식)을 Windows는 `FBNeoRageX.exe` 옆에, Steam Deck은 `~/FBNeoRageX/`에 넣으면 자동으로 적용됩니다.

---

## 📜 License / 라이선스

This project is released under the **MIT License**.  
FinalBurn Neo core is subject to its own license — see [FBNeo License](https://github.com/finalburnneo/FBNeo/blob/master/LICENSE).

이 프로젝트는 **MIT 라이선스**로 배포됩니다.  
FinalBurn Neo 코어는 별도의 라이선스를 따릅니다.

---

## 🙏 Credits / 크레딧

- [FinalBurn Neo](https://github.com/finalburnneo/FBNeo) — arcade emulation core / 아케이드 에뮬레이션 코어
- [libretro](https://www.libretro.com/) — core/frontend interface standard / 코어·프론트엔드 인터페이스 표준
- [Qt6](https://www.qt.io/) — UI framework / UI 프레임워크
- [라즈겜동](https://cafe.naver.com/razberry) — Korean game name database / 한글 게임명 데이터베이스
