# FBNeoRageX v2.1

> Preview video rebuilt, portable folder layout, simple game-name file, KO/EN menu toggle.
> 프리뷰 영상 재작성, 포터블 폴더 구조, 게임 이름 간편 변경, 메뉴 한/영 전환.

---

## ✨ New / 새 기능

| | English | 한국어 |
|---|---|---|
| 🏷️ | **`names.txt`** — Rename game list entries one line at a time: `kof94 = 더 킹 오브 파이터즈 94`. Takes priority over `gamelist.xml`, so you can override only the titles you want. UTF-8, `#`/`;` comments, `=` or Tab. | **`names.txt`** — `kof94 = 더 킹 오브 파이터즈 94` 처럼 한 줄로 이름 지정. `gamelist.xml` 보다 우선하므로 원하는 게임만 덮어쓰기 가능. UTF-8, `#`/`;` 주석, `=` 또는 탭. |
| 🌐 | **KO / EN menu toggle** — 🌐 button in the Options panel switches menu text instantly; the choice is saved. | **메뉴 한/영 전환** — 옵션 패널의 🌐 버튼으로 즉시 전환, 설정 저장. |
| 🔧 | **Dedicated Service (TEST) key** — Service input is sent only by its own hotkey (default `` ` ``), fully separated from START-hold and gamepad L2. | **서비스(TEST) 전용 키** — 전용 핫키(기본 `` ` ``)로만 서비스 입력 전송. START 길게 누르기·패드 L2와 완전 분리. |
| 📁 | **Portable folder layout** — All user data sits next to the program on Windows and Steam Deck alike. Old Linux settings are migrated automatically. | **포터블 폴더 구조** — 모든 사용자 데이터가 Windows·스팀덱 모두 프로그램 폴더 아래로. 기존 Linux 설정은 자동 이관. |
| 📌 | **Game list remembers your place** — Last played game is restored on next launch, and favoriting no longer jumps back to the top of the list. | **게임 목록 위치 기억** — 마지막 플레이 게임 복원, 즐겨찾기해도 목록이 맨 위로 튀지 않음. |

## 🔧 Changed / 변경

| | English | 한국어 |
|---|---|---|
| 👁️ | **Flash Reduction rebuilt** — Matches a white-flash frame to the surrounding brightness instead of inverting colors, following Xbox XAG 118 / WCAG 2.3.1 (reduce contrast, don't invert). No color distortion, no trailing. | **플래시 감소 재작성** — 색반전 대신 번쩍임 프레임을 주변 밝기에 맞춤(Xbox XAG 118 / WCAG 2.3.1 권고 = 대비 감소). 색 뒤틀림·잔상 없음. |
| 🖼️ | **Preview cycle** — Preview now shows image → video → image repeatedly, like other frontends. | **프리뷰 순환** — 이미지 → 영상 → 이미지 반복 표시. |

## 🐛 Fixed / 수정

| | English | 한국어 |
|---|---|---|
| 💥 | **Preview video crashed on Steam Deck** — Qt's FFmpeg backend picked a Vulkan hardware decoder the Deck GPU cannot use for video (`VK_KHR_video_decode_queue` unsupported). Replaced with a built-in software decoder — no hardware path at all — with sound and audio-master A/V sync. | **스팀덱 프리뷰 영상 크래시** — Qt FFmpeg 백엔드가 스팀덱 GPU가 지원하지 않는 Vulkan 하드웨어 디코더를 선택하던 문제. 자체 소프트웨어 디코더로 교체(하드웨어 경로 없음), 사운드 + 오디오 기준 싱크 적용. |
| 🎨 | **Recorded video colors (Linux)** — Red and blue were swapped in recordings: `AV_PIX_FMT_BGR32` resolves to `rgba` on little-endian, but libretro XRGB8888 is `bgra`. Screenshots were unaffected. | **녹화 영상 색상 (Linux)** — 녹화본 R/B 반전 수정. 리틀엔디언에서 `AV_PIX_FMT_BGR32`는 실제로 `rgba`, libretro XRGB8888은 `bgra`. 스크린샷은 영향 없었음. |
| 📹 | **Recording disabled on Steam Deck** — FFmpeg dev headers were missing from build dependencies, so video recording was silently compiled out. | **스팀덱 녹화 불가** — FFmpeg 개발 헤더가 빌드 의존성에서 빠져 녹화가 비활성화되던 문제. |
| ⌨️ | **Stuck keys after Tab** — Returning from the GUI to the game could leave every key pressed until you toggled twice. | **탭 전환 후 키 고착** — GUI에서 게임 복귀 시 모든 키가 눌린 채로 남던 문제. |
| 🌐 | **Netplay DISCONNECT** — The relay polling loop stayed alive, so re-hosting or re-joining failed until restart. Relay address is no longer shown anywhere, including error logs. | **넷플레이 디스커넥트** — 릴레이 폴링이 살아남아 재호스트·재조인이 안 되던 문제. 릴레이 주소는 오류 로그 포함 어디에도 미표시. |
| 📐 | **GUI box sizes** — Panels are sized purely by ratio (top:bottom 5:4, left:right 3:5) and no longer shift as the game list or event log fills. Window resizing scales them proportionally. | **GUI 박스 크기 고정** — 비율(위:아래 5:4, 좌:우 3:5)로만 결정되어 목록·로그가 채워져도 변하지 않음. 창 크기 변경 시 비례 스케일. |
| 🖱️ | **Options scrolling & key mapping** — The Controls page scrolls, so keyboard/gamepad/arcade-stick mapping tables are reachable. Combo boxes no longer swallow the mouse wheel (which blocked scrolling and changed values by accident). | **옵션 스크롤·키 매핑** — 컨트롤 페이지 스크롤로 매핑 표 접근 가능. 콤보박스가 휠을 가로채 스크롤을 막고 값이 바뀌던 문제 수정. |
| 🃏 | **Cheats** — Cheat files are handed to FBNeo's own cheat engine (`<system>/fbneo/cheats/`), so they behave exactly as in RetroArch instead of relying on address guessing. | **치트** — 치트 파일을 FBNeo 자체 엔진(`<system>/fbneo/cheats/`)에 전달해 RetroArch와 동일하게 동작. 주소 추측 방식 제거. |
| 🖼️ | **Preview save** — Preview shots/clips save as `{rom}.png` / `{rom}.mp4`, overwrite cleanly, and show immediately without reselecting the game. | **프리뷰 저장** — `{rom}.png` / `{rom}.mp4` 저장·덮어쓰기, 재선택 없이 즉시 반영. |
| 🛠️ | **Steam Deck build** — Dependency detection silently reported every package as present (`grep -q` closed the pipe early and tripped `set -o pipefail`). | **스팀덱 빌드** — 의존성 검사가 모든 패키지를 설치됨으로 오판하던 문제(`grep -q` 조기 종료 + `set -o pipefail`). |

---

## 📦 Downloads / 다운로드

| File / 파일 | Platform |
|---|---|
| `FBNeoRageX.exe` | Windows 10/11 x64 (single portable executable / 단일 포터블 실행 파일) |
| `FBNeoRageX-linux-x86_64.tar.gz` | Steam Deck / Linux x86_64 |

> ⚠️ **The FBNeo core is not included / FBNeo 코어는 포함되어 있지 않습니다.**
> Place `fbneo_libretro.dll` next to the executable (Windows), or `fbneo_libretro.so` in
> `~/FBNeoRageX/bin/` (Steam Deck).
> Windows는 실행 파일 옆에 `fbneo_libretro.dll`, 스팀덱은 `~/FBNeoRageX/bin/` 에
> `fbneo_libretro.so` 를 넣어 주세요.

### Steam Deck

```bash
tar -xzf FBNeoRageX-linux-x86_64.tar.gz -C ~/
~/FBNeoRageX/FBNeoRageX.sh
```

---

## ⬆️ Upgrading / 업그레이드 안내

- **Folder layout changed / 폴더 구조 변경** — user data now lives next to the program.
  Settings are migrated automatically, but move your existing `previews`, `saves` and `cheats`
  folders if you want to keep them:
  사용자 데이터가 프로그램 폴더 아래로 모입니다. 설정은 자동 이관되지만 기존
  `previews`·`saves`·`cheats` 폴더는 직접 옮겨 주세요:

  ```bash
  cp -rn ~/.local/share/FBNeoRageX/FBNeoRageX/{previews,saves,cheats} ~/FBNeoRageX/
  ```

- **Re-record preview clips / 프리뷰 영상 재녹화** — the color fix applies at recording time, so
  clips recorded with older builds still have red and blue swapped.
  색상 수정은 녹화 시점에 적용되므로, 이전 빌드로 녹화한 영상은 여전히 R/B가 뒤바뀐 상태입니다.
  새로 녹화해 주세요.
