# FBNeoRageX — Controls & Hotkeys / 조작 및 단축키 가이드 (v2.3)

## Contents / 목차
- [Menu navigation / 메뉴 조작](#menu-navigation--메뉴-조작)
- [Keyboard hotkeys / 키보드 단축키](#keyboard-hotkeys--키보드-단축키)
- [Gamepad & arcade-stick hotkeys / 게임패드·아케이드 스틱 핫키](#gamepad--arcade-stick-hotkeys--게임패드아케이드-스틱-핫키)
- [Default game input / 기본 게임 입력](#default-game-input--기본-게임-입력)
- [Saving your mapping / 매핑 저장](#saving-your-mapping--매핑-저장)
- [Turbo / 터보](#turbo--터보)
- [Frame Lab / 프레임 랩](#frame-lab--프레임-랩)
- [Steam Deck tips / 스팀덱 팁](#steam-deck-tips--스팀덱-팁)

---

## Menu navigation / 메뉴 조작

The menu has three cursor zones: **filter tabs → game list → options**.
메뉴에는 **필터 탭 → 게임 목록 → 옵션** 세 개의 커서 영역이 있습니다.

| Action / 동작 | Keyboard / 키보드 | Gamepad / 게임패드 | Mouse / 마우스 |
|---|---|---|---|
| Move / 이동 | `↑ ↓` (`PgUp` `PgDn` `Home` `End`) | D-pad | click / wheel, drag scrollbars |
| Change value / 값 변경 | `← →` | D-pad ◀ ▶ | click the ◀ ▶ buttons |
| Launch / open / 실행·열기 | `Enter` | **A** | click |
| Back / 뒤로 | `Esc` / `Backspace` | **B** | BACK button |
| Switch zone / 영역 이동 | `[` `]` | **LB** `RB` | — |
| Favorite / 즐겨찾기 | `Space` | **X** | right-click |
| Search / 검색 | `/` or `Ctrl+F` | **Y** (on-screen keyboard) | click the search bar |

Search: `Enter` / `Esc` end typing, `Ctrl+U` (or the `x` button) clears.
검색: `Enter` / `Esc` 로 입력 종료, `Ctrl+U`(또는 `x` 버튼)로 지우기.

---

## Keyboard hotkeys / 키보드 단축키

Keyboard hotkeys are fixed (no remapping screen). / 키보드 핫키는 고정입니다(재배정 화면 없음).

| Key / 키 | Action / 기능 |
|---|---|
| `Tab` | Game ↔ menu / 게임 ↔ 메뉴 |
| `Esc` | Stop game / 게임 종료 |
| `` ` `` | Service (TEST) input / 서비스(TEST) 입력 |
| `F11` | Fast forward / 패스트포워드 |
| `Alt+Enter` | Fullscreen / 전체화면 |
| `Shift+F1`–`F8` | Save state to slot 1–8 / 스테이트 저장 (슬롯 1–8) |
| `F1`–`F8` | Load state / 스테이트 불러오기 |
| `F12` | Screenshot → `screenshots/` / 스크린샷 |
| `Ctrl+F12` | Save preview image → `previews/{rom}.png` / 프리뷰 이미지 저장 |
| `F9` | Start / stop recording → `recordings/` / 녹화 시작·중지 |
| `Ctrl+F9` | Record preview clip → `previews/{rom}.mp4` / 프리뷰 영상 녹화 |
| `F10` | Swap 1P ↔ 2P / 1P ↔ 2P 스왑 |

---

## Gamepad & arcade-stick hotkeys / 게임패드·아케이드 스틱 핫키

RetroArch style: **hold SELECT**, then press another button. L3 / R3 are not used.
RetroArch 방식: **SELECT 를 누른 채** 다른 버튼을 누릅니다. L3 / R3 는 쓰지 않습니다.

| Action / 기능 | Gamepad / 게임패드 | Arcade stick / 아케이드 스틱 |
|---|---|---|
| Game ↔ menu / 게임 ↔ 메뉴 | SELECT + START | 9 + 10 |
| Exit game / 게임 종료 | SELECT + Y | 9 + 4 |
| Service (TEST) / 서비스 | SELECT + L1 | 9 + 5 |
| Fast forward / 패스트포워드 | SELECT + R1 | 9 + 6 |
| Save state / 상태 저장 | SELECT + A | 9 + 1 |
| Load state / 상태 불러오기 | SELECT + B | 9 + 2 |
| Screenshot / 스크린샷 | SELECT + X | 9 + 3 |
| Next save slot (1→8) / 다음 슬롯 | SELECT + → | 9 + → |
| Save preview image / 프리뷰 이미지 저장 | SELECT + ← | 9 + ← |
| Fullscreen / 전체화면 | SELECT + ↑ | 9 + ↑ |
| Swap 1P ↔ 2P / 스왑 | SELECT + ↓ | 9 + ↓ |
| Record / 녹화 | SELECT + L2 | 9 + 7 |
| Record preview clip / 프리뷰 영상 | SELECT + R2 | 9 + 8 |

- While SELECT is held, other buttons are **not** sent to the game. Tapping SELECT alone (no combo) sends **coin** when you release it.
  SELECT 를 누르는 동안 다른 버튼은 게임에 전달되지 않습니다. 조합 없이 SELECT 만 눌렀다 떼면 뗄 때 **코인**이 들어갑니다.
- Arcade stick numbers are button numbers (1-based); the stick/POV is the direction. / 스틱의 숫자는 버튼 번호(1부터)이고 방향은 스틱·POV 입니다.

---

## Default game input / 기본 게임 입력

The Controls page (OPTIONS → CONTROLS) shows the real button names of the running game.
컨트롤 화면(OPTIONS → CONTROLS)에는 실행 중인 게임의 실제 버튼 이름이 표시됩니다.

### Keyboard / 키보드

| Game type / 게임 종류 | Keys / 키 |
|---|---|
| Neo Geo, CPS 2–3 button, others / 네오지오·CPS 2~3버튼·기타 | `A` `S` `D` `F` = **A B C D**, `Z` `X` = CD / AB (Neo Geo only) |
| CPS 6-button fighters / CPS 6버튼 격투 | `A` `S` `D` = **약손 중손 강손** (LP MP HP), `Z` `X` `C` = **약발 중발 강발** (LK MK HK) |
| Common / 공통 | `Enter` or `1` = START · `Space` or `2` = SELECT / COIN · arrow keys |

CPS 2–3 button games (belt-scrollers) just show **A B C** — A = attack, B = jump. / CPS 2~3버튼 게임(벨트스크롤)은 **A B C** 로 표시되며 A 가 공격, B 가 점프입니다.

### Gamepad / 게임패드

| Game type / 게임 종류 | Mapping / 배치 |
|---|---|
| Neo Geo & standard / 네오지오·일반 | X = A, A = B, Y = C, B = D (face buttons as a fight-stick layout) |
| 6-button fighters / 6버튼 격투 | **X Y R1** = 약손 중손 강손 · **A B R2** = 약발 중발 강발 |
| Common / 공통 | Start = START · Select = SELECT / COIN · D-pad |

### Arcade stick (WinMM) / 아케이드 스틱

Buttons 1–4 = A B C D, 5/6 = L / R, 7/8 = L2 / R2, 9 = SELECT, 10 = START.
버튼 1~4 = A B C D, 5/6 = L / R, 7/8 = L2 / R2, 9 = SELECT, 10 = START.

---

## Saving your mapping / 매핑 저장

- Every change to the key mapping or turbo is **saved automatically for the current game**. Other games are untouched.
  키 매핑·터보를 바꾸면 **지금 게임에 자동 저장**됩니다. 다른 게임은 그대로입니다.
- **SAVE FOR ALL NEOGEO / CPS / OTHER GAMES** stores the current keyboard, gamepad, stick and turbo for the whole platform. 6-button fighters and standard layouts are stored separately.
  **NEOGEO / CPS / 기타 기종 전체에 저장** 은 지금 키보드·게임패드·스틱·터보를 그 기종 전체에 저장합니다. 6버튼 격투와 일반 배치는 따로 저장됩니다.
- Priority / 적용 순서: **game > platform > global > default** (게임별 > 기종별 > 전역 > 기본값).
- **CLEAR THIS GAME'S SAVED CONTROLS** (press twice) removes the game's own settings. / **이 게임 저장값 지우기**(두 번 누르기)는 그 게임 전용 설정을 지웁니다.
- RESET KEYBOARD / GAMEPAD / ARCADE STICK writes the default table for the current game. / 초기화 항목은 기본값 표를 지금 게임에 저장합니다.

## Turbo / 터보

Each button has its own ON/OFF switch (TURBO A, TURBO B …, or TURBO 약손 … for 6-button fighters) and a shared period in frames. Saved per game like the mapping.
버튼마다 켜고 끄는 터보 항목(터보 A, 터보 B … / 6버튼 격투는 터보 약손 …)과 공통 주기(프레임)가 있고, 매핑처럼 게임별로 저장됩니다.

## Frame Lab / 프레임 랩

Pause a game → **OPTIONS → SHOTS FACTORY → FRAME LAB**.

| Key / 키 | Gamepad / 패드 | Action / 기능 |
|---|---|---|
| `←` `→` | D-pad ◀ ▶ | ±1 frame / 1프레임 이동 |
| `PgUp` `PgDn` | LB / RB | ±10 frames / 10프레임 이동 |
| `Home` `End` | — | first / last frame / 처음·끝 |
| `Enter` | A | save this frame as PNG / 이 프레임 PNG 저장 |
| `Esc` | B | close / 닫기 |
| mouse / 마우스 | | click a thumbnail, wheel = step, right-click = close |

Going past the recorded history advances the game one frame at a time, up to 600 frames, then stops with a notice.
기록된 프레임을 넘어가면 게임을 한 프레임씩 진행하며(최대 600프레임) 한계에서 안내와 함께 멈춥니다.

## Steam Deck tips / 스팀덱 팁

- The window is always fullscreen on Steam Deck. / 스팀덱에서는 항상 전체화면입니다.
- The Deck's **View** button is SELECT, **Menu** is START. All gamepad hotkeys above work without Steam Input remapping.
  스팀덱의 **뷰** 버튼이 SELECT, **메뉴** 버튼이 START 입니다. 위의 게임패드 핫키는 Steam Input 설정 없이 그대로 동작합니다.
- The Steam Input layout should send gamepad buttons as a plain gamepad (not keyboard/mouse) for hotkeys and the 6-button layout to work.
  핫키와 6버튼 배치가 동작하려면 Steam Input 은 게임패드 버튼을 일반 게임패드로(키보드·마우스가 아니라) 보내야 합니다.
