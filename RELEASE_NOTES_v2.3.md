# FBNeoRageX v2.3

> The whole GUI is rebuilt as a NeoRageX 0.6b style canvas, with a pixel-font Korean/English UI, reworked controls and hotkeys, a frame-by-frame screenshot lab, and a startup intro.
> GUI 전체를 NeoRageX 0.6b 스타일 캔버스로 다시 만들었습니다. 도트 폰트 한/영 UI, 컨트롤·핫키 개편, 프레임 단위 스크린샷 랩, 시작 오프닝이 추가됩니다.

---

## 🗑️ Removed / 삭제된 사항

Things that existed in v2.2 and are **gone** in v2.3. / v2.2 에 있었지만 v2.3 에서 **없어진** 것들입니다.

| English | 한국어 |
|---|---|
| **Old Qt option windows** (Controls / Directories / Video / Audio / Machine / Shots / Cheats / Netplay tabs) are gone. Every category is now a native page drawn inside the new UI — nothing was dropped, the features moved. | **옛 Qt 옵션 창**(컨트롤 / 디렉터리 / 비디오 / 오디오 / 머신 / 샷 / 치트 / 넷플레이 탭)이 사라졌습니다. 모든 카테고리가 새 UI 안에 직접 그려지는 페이지로 옮겨졌으며, 기능이 빠진 것은 아닙니다. |
| **Home-screen button bar** (LAUNCH / STOP / RESET / 1P-2P / FULLSCREEN / EXIT and the SLOT / SAVE / LOAD / SHOT / REC / FF buttons) is gone. LAUNCH / IMPORT / EXIT are the three buttons under the options panel; the rest moved to **OPTIONS → SYSTEM** (STOP GAME, RESET GAME, 1P/2P, TATE, save slot, save/load state, screenshot, record, fast-forward) and **VIDEO OPTIONS** (fullscreen). | **홈 화면 하단 버튼 바**(LAUNCH / STOP / RESET / 1P-2P / FULLSCREEN / EXIT, SLOT / SAVE / LOAD / SHOT / REC / FF 버튼)가 사라졌습니다. 옵션 패널 아래의 LAUNCH / IMPORT / EXIT 세 버튼만 남고, 나머지는 **OPTIONS → SYSTEM**(게임 종료, 게임 리셋, 1P/2P, 세로 화면 회전, 저장 슬롯, 상태 저장/불러오기, 스크린샷, 녹화, 빨리 감기)과 **VIDEO OPTIONS**(전체화면)로 옮겨졌습니다. |
| **Hotkey remapping UI** is gone. Keyboard hotkeys are now fixed (listed in the Controls page and in `CONTROLS.md`); hotkeys saved by older versions are ignored. | **핫키 재배정 화면**이 사라졌습니다. 키보드 핫키는 고정이며(컨트롤 화면과 `CONTROLS.md` 에 목록 있음), 이전 버전에서 저장한 핫키는 무시됩니다. |
| **Save-scope selector** (global / per-platform / per-game radio buttons) is gone. Control changes are now saved **automatically per game**, plus an explicit "save for all NEOGEO / CPS / OTHER games" action. | **저장 범위 선택**(전역 / 기종별 / 게임별)이 사라졌습니다. 컨트롤을 바꾸면 **게임별로 자동 저장**되고, 필요하면 "NEOGEO / CPS / 기타 기종 전체에 저장"을 따로 누릅니다. |
| **L3-based gamepad hotkeys** and the "hold SELECT+START for 2 s" menu shortcut are gone — replaced by SELECT-combo hotkeys (below). L3 / R3 are no longer used. | **L3 기반 게임패드 핫키**와 "SELECT+START 2초 길게 누르기" 메뉴 진입이 사라졌습니다 — SELECT 조합 핫키(아래)로 대체되었고 L3 / R3 는 더 이상 쓰지 않습니다. |
| **Region combo box** is gone. It only wrote a setting that nothing ever read; change the region with the game's own **DIP switch** (MACHINE SETTINGS). | **지역(Region) 콤보**가 사라졌습니다. 저장만 되고 어디에서도 읽히지 않던 설정이라 뺐고, 지역은 게임 자체의 **DIP 스위치**(MACHINE SETTINGS)에서 바꿉니다. |
| **3D pipe / drop-shadow frame code (`BorderPanel`)** and the hidden legacy game-list widget were deleted. | **3D 파이프·그림자 테두리 코드(`BorderPanel`)**와 숨겨져 있던 옛 게임 목록 위젯을 삭제했습니다. |
| **DISABLE ALL** (cheats) now really switches everything off; the old "clear all" actually unloaded the cheat list. | 치트 **DISABLE ALL** 은 이제 이름 그대로 전부 끕니다. 옛 "전체 해제"는 치트 목록 자체를 언로드했습니다. |
| ~20 unused functions, settings (`region`, `audio_drc_max`, `hotkey_map`) and the legacy global XInput mapping store were removed. | 쓰이지 않던 함수 약 20개, 설정(`region`, `audio_drc_max`, `hotkey_map`), 옛 전역 XInput 매핑 저장소를 정리했습니다. |
| The GitHub Actions release workflow and the 66 MB `release/FBNeoRageX.exe` that was committed to the repo were removed (binaries go to Releases only). | GitHub Actions 릴리스 워크플로와 저장소에 들어 있던 66MB `release/FBNeoRageX.exe` 를 삭제했습니다(실행 파일은 Releases 로만 배포). |

---

## ✨ New / 새 기능

### 🎨 NeoRageX canvas UI / NeoRageX 캔버스 UI
| English | 한국어 |
|---|---|
| Frames are **drawn with a pen** on start; the pen tip is now a flat, highlighter-like edge. Panels are translucent so your background image shows through, with smoother panel edges and a `Ver 2.3` / `FBNeoRageX 2026` footer. | 시작할 때 테두리가 **펜으로 그려지고**, 펜 끝은 형광펜처럼 평평한 단면입니다. 패널은 반투명이라 배경 이미지가 비치고, 가장자리가 더 부드러우며 하단에 `Ver 2.3` / `FBNeoRageX 2026` 이 표시됩니다. |
| **Search bar** under the filter tabs: `/` or Ctrl+F or a click; Esc/Enter end typing; Ctrl+U clears; Korean IME supported; gamepad **Y** opens the on-screen keyboard. | 필터 탭 아래 **검색창**: `/` 또는 Ctrl+F 또는 클릭, Esc/Enter 로 입력 종료, Ctrl+U 로 지우기, 한글 입력(IME) 지원, 게임패드 **Y** 는 화면 키보드. |
| **Mouse scrollbars** for the game list and every sub-menu (arrows repeat, track pages, thumb drags). | 게임 목록과 모든 하위 메뉴에 **마우스로 조작되는 스크롤바**(화살표 반복, 트랙 페이지 이동, 썸 끌기). |
| **Long game names scroll** left after the cursor rests on them for ~0.25 s, so you can read the end of the title. | 커서를 올리고 약 0.25초 뒤 **긴 게임 이름이 왼쪽으로 흘러** 뒷부분을 읽을 수 있습니다. |
| **Cursor zones**: filter tabs ↔ game list ↔ options. Gamepad **LB / RB** (keys `[` `]`) move between them. Sub-menus have a **BACK** button at the bottom. | **커서 영역**: 필터 탭 ↔ 게임 목록 ↔ 옵션. 게임패드 **LB / RB**(키보드 `[` `]`)로 영역을 옮기고, 하위 메뉴 아래에는 **뒤로 가기(BACK)** 버튼이 있습니다. |
| Full **Korean menus** (main option names are transliterated: 컨트롤, 디렉터리, 비디오 옵션 …) in the bundled **Galmuri Mono** dot font — Latin and Hangul share one size and weight. | 번들 **Galmuri Mono** 도트 폰트로 **메뉴 전체 한글화**(메인 옵션은 음역: 컨트롤, 디렉터리, 비디오 옵션 …). 영문과 한글이 같은 크기·굵기입니다. |
| Your own `mousepoint.png` pointer is used; the pointer stays hidden while a game runs. | 직접 만든 `mousepoint.png` 포인터를 쓰고, 게임 중에는 포인터가 계속 숨겨집니다. |
| **DIRECTORIES** lists every fixed folder/file the program really reads (saves, screenshots, recordings, cheats, shaders, bezels, BIOS, core, `config.json` …) with its path and a description. | **DIRECTORIES** 에 프로그램이 실제로 읽는 고정 폴더·파일(세이브, 스크린샷, 녹화, 치트, 셰이더, 베젤, BIOS, 코어, `config.json` …)이 경로와 설명과 함께 표시됩니다. |

### 🎮 Controls / 컨트롤
| English | 한국어 |
|---|---|
| The Controls page is split into **[Device] [Button mapping] [Save] [Turbo] [Hotkeys]**. | 컨트롤 화면이 **[장치] [키 매핑] [저장] [터보] [핫키]** 로 구분됩니다. |
| **Neo Geo buttons** are listed A B C D CD AB …; **CPS 6-button fighters** show 약손 중손 강손 / 약발 중발 강발 (LP MP HP / LK MK HK) with the 3x macros hidden; **CPS 2–3 button games** simply show A B C (A = attack, B = jump in belt-scrollers). | **네오지오 버튼**은 A B C D CD AB … 순서로, **CPS 6버튼 격투**는 약손 중손 강손 / 약발 중발 강발(3연타 매크로는 숨김)로, **CPS 2~3버튼 게임**은 그냥 A B C(벨트스크롤은 A 공격, B 점프)로 표시됩니다. |
| Default keyboard for 6-button fighters: **A S D** = punches, **Z X C** = kicks. Default pad: **X Y R1** = punches, **A B R2** = kicks. | 6버튼 격투 기본 키보드: **A S D** = 손, **Z X C** = 발. 기본 패드: **X Y R1** = 손, **A B R2** = 발. |
| Turbo buttons use the same names as the mapping table (TURBO A, TURBO 약손 …). | 터보 버튼 이름이 매핑 표와 같습니다(터보 A, 터보 약손 …). |
| **Auto-save per game** for key mapping and turbo, plus **"Save for all NEOGEO / CPS / OTHER games"** (6-button fighters are stored separately) and "clear this game's saved controls". | 키 매핑과 터보를 **게임별로 자동 저장**하고, **"NEOGEO / CPS / 기타 기종 전체에 저장"**(6버튼 격투는 따로 저장)과 "이 게임 저장값 지우기"가 있습니다. |
| **Gamepad & arcade-stick hotkeys** (RetroArch style, hold **SELECT**): +START menu, +Y exit, +L1 service, +R1 fast-forward, +A save state, +B load state, +X screenshot, +→ next slot, +← preview image, +↑ fullscreen, +↓ 1P/2P swap, +L2 record, +R2 preview clip. SELECT alone = coin. Arcade stick: button 9 = SELECT, 10 = START, 1–4 = A/B/X/Y, 5/6 = L1/R1, 7/8 = L2/R2. | **게임패드·아케이드 스틱 핫키**(RetroArch 방식, **SELECT** 를 누른 채): +START 메뉴, +Y 종료, +L1 서비스, +R1 빨리 감기, +A 상태 저장, +B 상태 불러오기, +X 스크린샷, +→ 다음 슬롯, +← 프리뷰 이미지, +↑ 전체화면, +↓ 1P/2P 스왑, +L2 녹화, +R2 프리뷰 영상. SELECT 만 누르면 코인. 스틱: 버튼 9 = SELECT, 10 = START, 1~4 = A/B/X/Y, 5/6 = L1/R1, 7/8 = L2/R2. |
| Windows XInput pads are now recognised by the controls page, so pad remapping and the 6-button layout actually apply on Windows. | Windows XInput 패드를 컨트롤 화면이 인식해 패드 리매핑과 6버튼 배치가 Windows 에서도 실제로 적용됩니다. |

### 🎞️ Shots Factory — Frame Lab / 샷 팩토리 — 프레임 랩
Pause a game, open **SHOTS FACTORY → FRAME LAB**, and step frame by frame (◀ ▶, PgUp/PgDn = 10, Home/End). A grid of neighbouring frames lets you pick the best one; **Enter / gamepad A** saves it as a PNG. Past the end of the recorded history the game advances one frame at a time (up to 600 frames), then stops with a notice.
게임을 멈추고 **SHOTS FACTORY → FRAME LAB** 을 열어 프레임 단위로 넘기며(◀ ▶, PgUp/PgDn = 10프레임, Home/End) 앞뒤 프레임을 격자로 보고 고릅니다. **Enter / 게임패드 A** 로 PNG 저장. 기록 끝을 넘으면 게임을 한 프레임씩 진행시키며(최대 600프레임) 한계에서 안내와 함께 멈춥니다.
> Hiding sprite layers is **not** possible with the stock FBNeo core (it exposes no layer switches). / 기본 FBNeo 코어는 레이어 끄기 기능을 제공하지 않아 스프라이트 레이어 숨기기는 지원하지 않습니다.

### 🎬 Startup intro / 시작 오프닝
A short built-in intro plays at launch. Put your own video at `assets/intro/intro.mp4` (webm/mkv/avi/mov/wmv also work) to replace it; any key / click / pad button skips it. Turn it off in **SYSTEM → STARTUP INTRO**.
실행할 때 짧은 내장 오프닝이 나옵니다. `assets/intro/intro.mp4`(webm/mkv/avi/mov/wmv 도 가능)를 넣으면 그 영상으로 바뀌고, 아무 키·클릭·패드 버튼으로 건너뜁니다. **SYSTEM → STARTUP INTRO** 에서 끌 수 있습니다.

### 💤 Screensaver / 화면보호기
Starts after **5 minutes** without input and plays random preview videos full-screen. When you wake it, the game list cursor jumps to the game that was playing (filters/search are cleared if they hide it).
무입력 **5분** 뒤에 켜져 프리뷰 영상을 무작위 전체화면으로 재생합니다. 해제하면 게임 목록 커서가 방금 재생되던 게임으로 이동합니다(필터·검색에 가려져 있으면 풀어 줍니다).

---

## 🐛 Fixed / 수정

| English | 한국어 |
|---|---|
| **Wrong button names after switching games** — the FBNeo core only sends its button definitions for the first game of a session, so after a Neo Geo game, Street Fighter II showed Neo Geo buttons. The core is now reloaded for every new game. | **게임을 바꾸면 버튼 이름이 틀리던 문제** — FBNeo 코어는 세션의 첫 게임에서만 버튼 정의를 보내서, 네오지오 다음에 스트리트 파이터 II 를 켜면 네오지오 버튼이 남았습니다. 이제 게임을 켤 때마다 코어를 다시 불러옵니다. |
| **Frame Lab freeze** at ~600 frames (endless loop) → capped with a notice. | 프레임 랩이 약 600프레임에서 멈추던 문제(무한 반복) → 한계에서 안내와 함께 멈춥니다. |
| **Broken `??` glyphs** in the event log (emoji / symbols missing from the pixel font) → replaced by matching symbols. | 이벤트 로그의 **`??` 깨짐**(도트 폰트에 없는 이모지·기호) → 비슷한 기호로 대체. |
| **Cheat names** no longer start with `[Cheat][game.ini]`. | 치트 이름 앞의 `[Cheat][game.ini]` 머리말을 제거했습니다. |
| **Steam Deck fullscreen toggle** showed OFF and cut off the right side; window size is now bounded to the screen. | **스팀덱 전체화면 토글**이 OFF 로 보이고 오른쪽이 잘리던 문제 → 창 크기를 화면 안으로 제한. |
| Key capture no longer erases the old binding if cancelled; the B button no longer triggers "back" right after being mapped. | 키 캡처를 취소해도 기존 배정이 지워지지 않고, 매핑 직후 B 버튼이 "뒤로 가기"로 처리되지 않습니다. |
| Shader preset default wrap is `clamp_to_border` (RetroArch-compatible) so bezel shaders no longer stretch the edge pixels. | 셰이더 프리셋 기본 wrap 을 `clamp_to_border`(RetroArch 호환)로 바꿔 베젤 셰이더에서 가장자리가 늘어나지 않습니다. |
| Smaller download: Release builds are stripped (Windows exe 89 → 55 MB) and the bundled font is a subset (5.2 MB → 0.7 MB). | 용량 감소: Release 빌드에서 심볼을 제거했고(Windows exe 89 → 55MB) 번들 폰트를 필요한 글자만 남겼습니다(5.2MB → 0.7MB). |

---

## 📦 Downloads / 다운로드

| File / 파일 | Platform |
|---|---|
| `FBNeoRageX.exe` | Windows 10/11 x64 — single portable executable / 단일 포터블 실행 파일 |
| `FBNeoRageX-linux-x86_64.tar.gz` | Steam Deck · Linux x86_64 |

> ⚠️ **The FBNeo core is not included / FBNeo 코어는 포함되어 있지 않습니다.**
> Put `fbneo_libretro.dll` next to the executable (Windows), or `fbneo_libretro.so` in `~/FBNeoRageX/bin/` (Steam Deck).
> 윈도우는 실행 파일 옆에 `fbneo_libretro.dll`, 스팀덱은 `~/FBNeoRageX/bin/` 에 `fbneo_libretro.so` 를 넣어 주세요.

**Steam Deck:**
```bash
tar -xzf FBNeoRageX-linux-x86_64.tar.gz -C ~/
~/FBNeoRageX/FBNeoRageX.sh
```

## 🙏 Third-party / 서드파티
The UI font is **Galmuri Mono 11** by Lee Minseo (quiple), [SIL Open Font License 1.1](assets/fonts/LICENSE-Galmuri.txt) — https://github.com/quiple/galmuri (a glyph subset is bundled).
UI 폰트는 이민서(quiple)의 **Galmuri Mono 11**, [SIL Open Font License 1.1](assets/fonts/LICENSE-Galmuri.txt) 입니다 — https://github.com/quiple/galmuri (필요한 글자만 추린 서브셋을 포함).
