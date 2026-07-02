# FBNeoRageX

> A modern arcade emulator frontend powered by [FinalBurn Neo](https://github.com/finalburnneo/FBNeo) (libretro core).  
> Built with **C++17 + Qt6**, targeting Windows and Steam Deck.

> FinalBurn Neo libretro 코어 기반의 아케이드 에뮬레이터 프론트엔드입니다.  
> **C++17 + Qt6**로 제작되었으며 Windows와 Steam Deck을 지원합니다.

---

## 📋 Update History / 업데이트 내역

### v2.0 (2026-05)

| | English | 한국어 |
|---|---|---|
| ✨ NEW | **Pure GGPO Netplay** — Rebuilt netcode: input-only exchange + local rollback + checksum-based desync detection (full-state resync only on mismatch). Native framerate preserved, no frame-skip. | **순수 GGPO 넷플레이** — 넷코드 재설계: 입력만 교환 + 로컬 롤백 + 체크섬 desync 감지(불일치 시에만 풀스테이트 재동기). 게임 네이티브 프레임 유지, 프레임 스킵 없음. |
| ✨ NEW | **STUN + Durable Objects matchmaking** — 6-char room token (no IP exposed), STUN external-address discovery, UDP hole-punching. Cloudflare Durable Objects worker (strong consistency) replaces KV. | **STUN + Durable Objects 매치메이킹** — 6자리 룸 토큰(IP 비노출), STUN 외부주소 발견, UDP 홀펀칭. Cloudflare Durable Objects 워커(강한 일관성)로 KV 교체. |
| ✨ NEW | **Flash Reduction (eye protection)** — Detects full-screen white flashes (counter hits, muzzle flashes) and inverts them to dark. Only triggers when most of the screen flashes — never touches in-game UI. | **플래시 감소 (눈 보호)** — 전체 화면 흰색 번쩍임(카운터·총구 화염)을 감지해 검게 반전. 화면 대부분이 번쩍일 때만 작동 — 인게임 UI는 건드리지 않음. |
| ✨ NEW | **Per-platform / per-game settings** — Controller mappings and Machine (DIP) settings can be saved globally, per-platform (NeoGeo/CPS/Irem…), or per-game. Priority: game > platform > global. | **기종별/게임별 설정** — 컨트롤러 매핑·머신세팅을 전역/기종별(NeoGeo/CPS/아이렘…)/게임별로 저장. 우선순위: 게임별 > 기종별 > 전역. |
| ✨ NEW | **Configurable hotkeys** — Remap all hotkeys (with modifiers), Reset-to-default and Save buttons. Save-state slots F1–F8 stay fixed for safety. | **핫키 설정** — 모든 핫키 재배정(모디파이어 포함), 기본값 복원·저장 버튼. 세이브스테이트 F1–F8은 안전을 위해 고정. |
| 🐛 FIX | **Cheat system (Irem / shooters)** — Fixed cheats for Irem, Cave, Toaplan, Psikyo, etc. RAM offset now derived by size-masking + endian-aware byte-swap instead of hardcoded NeoGeo/CPS bases. | **치트 수정 (아이렘/슈팅)** — 아이렘·Cave·Toaplan·Psikyo 등 치트 정상 작동. RAM 오프셋을 NeoGeo/CPS 하드코딩 대신 크기 마스킹 + 엔디언별 바이트스왑으로 계산. |
| 🐛 FIX | **JOIN-side crashes** — Fixed dangling `QJsonValueRef`, Windows UDP `WSAECONNRESET`, and duplicate peer-handling crashes during connection. | **JOIN 측 크래시** — 연결 중 발생하던 댕글링 `QJsonValueRef`, Windows UDP `WSAECONNRESET`, 피어 이중처리 크래시 수정. |
| 🐛 FIX | **GUI background** — Fixed panels covering the background image (viewport auto-fill + BorderPanel drop-shadow overlay). | **GUI 배경** — 패널이 배경 이미지를 가리던 문제 수정 (viewport 자동채움 + BorderPanel 드롭섀도우). |

### v1.9 (2026-05-13)

| | English | 한국어 |
|---|---|---|
| ✨ NEW | **Fightcade-style Netplay** — Room Code system: host generates an 11-character code (e.g. `A3F7K-9X2-M4P`); guest just enters the code to connect (IP + port + delay encoded automatically). Public IP auto-detected. | **파이트케이드 스타일 넷플레이** — 룸 코드 시스템: 호스트가 11자리 코드 생성, 참가자는 코드 입력만으로 연결 (IP·포트·딜레이 자동 설정). 공개 IP 자동 조회. |
| ✨ NEW | **Input Delay control** — Configurable per-session delay frames (0–8f). Recommended: domestic 0–1f / Asia 2–3f / international 4–6f. RTT displayed live during play. | **입력 딜레이 조절** — 세션별 딜레이 프레임 설정 (0–8f). 국내 0–1f / 아시아 2–3f / 해외 4–6f 권장. 게임 중 RTT 실시간 표시. |
| ✨ NEW | **Tate Mode** — Vertical arcade shooters (1942, DonPachi, Raiden, etc.) auto-rotate on load. Manual toggle with `F8` or the TATE button: AUTO → 90°CCW → 90°CW → OFF. | **Tate 모드** — 세로형 슈팅게임(1942, DonPachi, Raiden 등) 자동 회전. `F8` 또는 TATE 버튼으로 수동 전환: AUTO → 90°CCW → 90°CW → OFF. |
| ✨ NEW | **gamelist.xml support** — Automatically reads Korean game names from a community-provided `gamelist.xml` (라즈겜동 format). | **gamelist.xml 지원** — 라즈겜동 한글화 파일 자동 인식. 프로그램 폴더에 넣으면 한글명 자동 적용. |
| 🐛 FIX | **Steam Deck audio** — Eliminated "warbly" pitch oscillation and slow-sounding audio (DRC maxAdj reduced from ±0.5% to ±0.1%). VSync disabled on GameScope (prevents AFL timer jitter). | **Steam Deck 오디오 수정** — 울렁거리는 음정 진동 및 느린 사운드 제거 (DRC maxAdj ±0.5%→±0.1%). GameScope에서 VSync 비활성화로 AFL 타이머 지터 방지. |
| 🐛 FIX | **Cheat system (CPS1)** — Cheats now correctly apply for CPS1 games. | **CPS1 치트 수정** — CPS1 게임 치트 정상 적용. |
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
- 📺 **Tate Mode** — Auto-rotate vertical shooters; manual toggle F8 (AUTO / 90°CCW / 90°CW / OFF)  
  **Tate 모드** — 세로형 슈팅게임 자동 회전, F8 수동 전환 (AUTO / 90°CCW / 90°CW / OFF)
- 🎛️ **DIP Switches** — per-game Machine Settings  
  **DIP 스위치** — 게임별 기판 설정 (지역, 난이도, 목숨 수, NeoGeo 모드 등)
- 🃏 **Cheat system** — INI-based cheat parser  
  **치트 시스템** — INI 형식 치트 파일 자동 로드
- 🌐 **Pure GGPO Netplay** — 6-char room token, STUN + Cloudflare Durable Objects matchmaking, UDP hole-punching, input-only rollback with checksum desync detection  
  **순수 GGPO 넷플레이** — 6자리 룸 토큰, STUN + Cloudflare Durable Objects 매치메이킹, UDP 홀펀칭, 입력 기반 롤백 + 체크섬 desync 감지
- 🎮 **Controller support** — XInput / WinMM (arcade sticks) / keyboard, savable global / per-platform / per-game  
  **컨트롤러 지원** — XInput / WinMM (아케이드 스틱) / 키보드, 전역/기종별/게임별 저장
- ⌨️ **Configurable hotkeys** — remap with modifiers, reset-to-default & save  
  **핫키 설정** — 모디파이어 포함 재배정, 기본값 복원·저장
- 👁️ **Flash reduction** — inverts full-screen white flashes to dark (eye protection)  
  **플래시 감소** — 전체 화면 흰 번쩍임을 검게 반전 (눈 보호)
- 🕹️ **Turbo buttons** — per-button toggle  
  **터보 버튼** — 버튼별 독립 터보 설정
- 📺 **CRT shader** — scanlines, RGB mask, vignette, bloom  
  **CRT 쉐이더** — 스캔라인, RGB 마스크, 비네트, 블룸
- 🎛️ **Machine settings** — DIP switches, savable per-platform / per-game  
  **머신 세팅** — DIP 스위치, 기종별/게임별 저장
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
| `F8` | **Tate rotation** cycle: AUTO → 90°CCW → 90°CW → OFF / **Tate 회전** 순환: AUTO → 90°CCW → 90°CW → OFF |
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
| L4 | `F8` | Tate rotation / Tate 회전 |
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

FBNeoRageX uses a **Fightcade-style Room Code** system combined with **rollback netcode** (up to 30 frames).  
FBNeoRageX는 **파이트케이드 스타일 룸 코드** 시스템과 **롤백 넷코드** (최대 30프레임)를 결합해 사용합니다.

> **Prerequisites / 사전 조건**  
> Both players must have the **same ROM file** and the **same version** of `fbneo_libretro`.  
> 양쪽 모두 **동일한 ROM 파일**과 **동일한 버전**의 `fbneo_libretro`가 필요합니다.

---

### 📡 HOST — Hosting a Game / 방장으로 게임 열기

| Step | English | 한국어 |
|:---:|---|---|
| 1 | Go to **Options → Netplay** tab | **Options → Netplay** 탭 이동 |
| 2 | Set **Port** (default: `7845`). Change only if the default is blocked. | **Port** 설정 (기본값: `7845`). 막혀 있을 때만 변경. |
| 3 | Set **Delay** frames based on distance (see table below) | 거리 기준으로 **Delay** 프레임 설정 (아래 표 참고) |
| 4 | Click **📡 HOST GAME** | **📡 HOST GAME** 클릭 |
| 5 | A **Room Code** appears (e.g. `A3F7K-9X2-M4P`). Share it with your opponent. | **룸 코드**가 생성됩니다 (예: `A3F7K-9X2-M4P`). 상대방에게 공유하세요. |
| 6 | Go back to the main screen and **select a game** from the list | 메인 화면으로 돌아가 게임 목록에서 **게임 선택** |
| 7 | Click **▶ START GAME (HOST)** | **▶ START GAME (HOST)** 클릭 |
| 8 | Wait for both sides to load — game starts automatically when both are ready | 양쪽 로딩 완료 시 자동으로 게임 시작 |

---

### 🔌 JOIN — Joining a Game / 참가자로 참여하기

| Step | English | 한국어 |
|:---:|---|---|
| 1 | Go to **Options → Netplay** tab | **Options → Netplay** 탭 이동 |
| 2 | Enter the **Room Code** received from the host (e.g. `A3F7K-9X2-M4P`) | 호스트에게 받은 **룸 코드** 입력 (예: `A3F7K-9X2-M4P`) |
| 3 | Click **🔌 JOIN GAME** | **🔌 JOIN GAME** 클릭 |
| 4 | The game downloads automatically, loading begins | 게임이 자동으로 선택되고 로딩 시작 |
| 5 | Wait — game starts simultaneously when host sends START | 대기 — 호스트가 START를 보내면 동시에 시작 |

> **No Room Code?** Enter the host's IP address directly in the `or IP:` field and click JOIN GAME.  
> **룸 코드가 없다면?** `or IP:` 칸에 호스트 IP를 직접 입력하고 JOIN GAME을 클릭하세요.

---

### ⏱️ Input Delay Guide / 입력 딜레이 설정 기준

The host sets the delay — it applies to **both players**.  
딜레이는 호스트가 설정하며 **양쪽 모두에 동일하게 적용**됩니다.

| Connection / 연결 지역 | RTT estimate / RTT 예상 | Recommended Delay / 권장 딜레이 |
|---|:---:|:---:|
| Same city / 같은 도시 (LAN) | < 10 ms | **0–1f** |
| Domestic (Korea) / 국내 | 10–40 ms | **1–2f** |
| Asia region / 아시아권 | 40–80 ms | **2–3f** |
| Cross-continent / 대륙 간 | 80–150 ms | **4–5f** |
| Intercontinental / 해외 원거리 | 150 ms+ | **5–6f** |

> **Formula / 계산식:** Delay(f) ≥ RTT(ms) ÷ 33  
> Example: RTT 100 ms → 100 ÷ 33 ≈ 3f minimum / 예시: RTT 100 ms → 최소 3f  
> RTT is shown live on the status bar after connection / RTT는 연결 후 상태 표시줄에 실시간 표시됩니다.

---

### 🔑 Room Code System / 룸 코드 시스템

The Room Code encodes **IP address + Port + Input Delay** into an **11-character base-36 string**.  
룸 코드는 **IP 주소 + 포트 + 입력 딜레이**를 **11자리 base-36 문자열**로 인코딩합니다.

```
Example: A3F7K-9X2-M4P
         └──── encodes: IP 123.45.67.89  Port 7845  Delay 2f ────┘
```

- Host's **public IP** is auto-detected from `api.ipify.org`  
  호스트의 **공개 IP**는 `api.ipify.org`에서 자동으로 조회됩니다.
- Guest just enters the code — **no manual IP/port/delay entry needed**  
  참가자는 코드만 입력하면 됩니다 — **IP·포트·딜레이를 따로 입력할 필요 없음**
- Copy button (⎘ COPY) copies the code to clipboard  
  복사 버튼(⎘ COPY)으로 클립보드에 복사 가능

---

### 🔧 Port Forwarding / 포트 포워딩

Internet play requires the **host** to open port `7845` (UDP) on their router.  
인터넷 대전을 위해 **호스트** 측 공유기에서 `7845`번 포트(UDP)를 열어야 합니다.

| Step | English | 한국어 |
|:---:|---|---|
| 1 | Find your router's admin page (usually `192.168.0.1` or `192.168.1.1`) | 공유기 관리 페이지 접속 (보통 `192.168.0.1` 또는 `192.168.1.1`) |
| 2 | Go to **Port Forwarding** (or NAT) section | **포트 포워딩** (또는 NAT) 메뉴 이동 |
| 3 | Add rule: **External port** `7845` → **Internal IP** (your PC's local IP) → **Internal port** `7845` → **Protocol** `UDP` | 규칙 추가: **외부 포트** `7845` → **내부 IP** (PC 로컬 IP) → **내부 포트** `7845` → **프로토콜** `UDP` |
| 4 | Save and restart router if needed | 저장 후 공유기 재시작 (필요 시) |

> **Your PC's local IP:** Run `ipconfig` in Command Prompt → look for `IPv4 Address`  
> **PC 로컬 IP 확인:** 명령 프롬프트에서 `ipconfig` 실행 → `IPv4 주소` 확인

> **Tip:** If port forwarding is difficult, try a VPN tunnel service like **ZeroTier** or **Hamachi** — both players join the same virtual LAN, no port forwarding needed.  
> **팁:** 포트 포워딩이 어렵다면 **ZeroTier** 또는 **Hamachi** 같은 VPN 터널 서비스를 사용하세요 — 같은 가상 LAN에 접속하면 포트 포워딩 없이 가능합니다.

---

### ⚙️ Netplay Technical Details / 넷플레이 기술 사양

| Feature / 항목 | Detail / 내용 |
|---|---|
| Netcode type / 넷코드 방식 | Rollback + Delay-based hybrid / 롤백 + 딜레이 하이브리드 |
| Max rollback window / 최대 롤백 창 | 30 frames / 30프레임 |
| Input redundancy / 입력 중복 전송 | 8 packets per input / 입력당 8패킷 |
| Sync interval / 동기화 주기 | Every 4 frames (host → guest snapshot) / 4프레임마다 스냅샷 전송 |
| Protocol / 프로토콜 | UDP |
| Default port / 기본 포트 | 7845 |
| Cross-platform / 크로스플랫폼 | Windows ↔ Steam Deck ✔ |
| Player roles / 플레이어 역할 | Host = P1, Guest = P2 |

---

### ❗ Netplay Troubleshooting / 넷플레이 문제 해결

| Problem / 문제 | Solution / 해결 방법 |
|---|---|
| Cannot connect / 연결 안 됨 | Check port forwarding on host router. Try ZeroTier/Hamachi. / 호스트 공유기 포트 포워딩 확인. ZeroTier/Hamachi 시도. |
| Game desyncs / 게임 동기화 오류 | Both players must use **identical ROM** and **same core version**. / 동일한 ROM 파일과 동일한 코어 버전 필수. |
| Heavy input lag / 입력 지연 과다 | Reduce **Delay** setting. Check actual RTT value shown in status. / Delay 값 줄이기. 상태 표시줄의 RTT 값 확인. |
| Stuttering / 끊김 현상 | Increase **Delay** by 1f. High packet loss on unstable connections. / Delay를 1f 증가. 불안정한 연결에서의 패킷 손실. |
| "ROM not found" on guest / 참가자 ROM 없음 | Guest must have the same ROM file in their ROMs folder. / 참가자 ROM 폴더에 동일한 ROM 파일 필요. |
| Room Code invalid / 룸 코드 오류 | Make sure to copy the full 11-character code (format: `XXXXX-XXX-XXX`). / 11자리 전체 복사 확인 (형식: `XXXXX-XXX-XXX`). |

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

**Q: How do I host a netplay game? / 넷플레이 방장은 어떻게 하나요?**  
**A:** Options → Netplay → set Port & Delay → **HOST GAME** → share the Room Code → select a game → **START GAME (HOST)**.  
> Options → Netplay → 포트와 딜레이 설정 → **HOST GAME** → 생성된 룸 코드를 상대방에게 공유 → 게임 선택 → **START GAME (HOST)**.

**Q: How do I join a netplay game? / 넷플레이 참가는 어떻게 하나요?**  
**A:** Options → Netplay → enter the **Room Code** in the "Room Code" field → **JOIN GAME**. The game loads automatically.  
> Options → Netplay → "Room Code" 칸에 **룸 코드** 입력 → **JOIN GAME**. 게임이 자동으로 로드됩니다.

**Q: I can't connect to the host. / 호스트에 연결이 안 돼요.**  
**A:** The host must forward port `7845` (UDP) on their router. Alternatively, use **ZeroTier** or **Hamachi** to create a virtual LAN — no port forwarding needed.  
> 호스트 공유기에서 `7845`번 포트(UDP)를 포트 포워딩해야 합니다. 어렵다면 **ZeroTier** 또는 **Hamachi**를 이용해 가상 LAN을 구성하면 포트 포워딩 없이 가능합니다.

**Q: What Delay value should I use? / 딜레이 값은 얼마로 설정해야 하나요?**  
**A:** Set Delay(f) ≥ RTT(ms) ÷ 33. For domestic Korea: 1–2f. Asia: 2–3f. International: 4–6f. RTT is shown in the status bar after connecting.  
> Delay(f) ≥ RTT(ms) ÷ 33으로 설정하세요. 국내: 1–2f, 아시아: 2–3f, 해외: 4–6f. RTT는 연결 후 상태 표시줄에서 확인 가능합니다.

**Q: The vertical shooter screen is sideways. / 세로형 슈팅게임 화면이 옆으로 나와요.**  
**A:** Press `F8` to cycle Tate rotation (AUTO → 90°CCW → 90°CW → OFF). Most vertical games rotate automatically on load.  
> `F8`을 눌러 Tate 회전을 순환하세요 (AUTO → 90°CCW → 90°CW → OFF). 대부분의 세로형 게임은 자동으로 회전됩니다.

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
