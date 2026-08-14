# FBNeoRageX v2.2

> Classic NeoRageX look, game-list platform tabs, mouse click sound.
> 클래식 NeoRageX 디자인, 게임 목록 기종 탭, 마우스 클릭음.

---

## 🎨 Classic NeoRageX UI / 클래식 NeoRageX 디자인

The frame styling is rebuilt after the original **NeoRageX 0.6b**, while keeping the menus and
features we already have.
기존 메뉴·기능은 그대로 두고, **원본 NeoRageX 0.6b** 스타일로 화면 구성을 다시 만들었습니다.

| English | 한국어 |
|---|---|
| Square bright-blue frames with the title at the top-left (`GAMELIST (12/123)`) | 각진 파란 테두리 + 좌상단 제목 (`GAMELIST (12/123)`) |
| Border thickness doubled so boxes read clearly on Steam Deck | 테두리 두께 2배 — 스팀덱에서도 박스가 또렷하게 보임 |
| Centered plain-text option menu, filled blue buttons | 가운데 정렬 텍스트 메뉴, 파란 채움 버튼 |
| `Ver` / name footer along the bottom | 하단 `Ver` · 이름 푸터 |
| Panels stay translucent — your own background image shows through | 패널이 반투명이라 직접 만든 배경 이미지가 그대로 비쳐 보임 |

> The previous 3D pipe frame renderer is kept in the code behind a flag, so it can be restored.
> 이전 3D 파이프 테두리 코드는 플래그 뒤에 그대로 남겨 두어 언제든 되돌릴 수 있습니다.

---

## 🗂️ Game list platform tabs / 게임 목록 기종 탭

| Tab / 탭 | Shows / 표시 |
|---|---|
| `ALL` | Every game / 전체 |
| `FAV` | Favorites — **regardless of platform** / 즐겨찾기 — **기종 무관 전체** |
| `NOFAV` | Games not favorited / 즐겨찾기 안 한 게임 |
| `NEOGEO` | Neo Geo MVS / AES |
| `CPS` | CPS1 + CPS2 + CPS3 |
| `ETC` | Everything else / 그 외 전부 |

Each tab shows how many ROMs you own, and tabs are only created for hardware you actually have.
각 탭에 보유 개수가 표시되며, 실제로 가지고 있는 기종의 탭만 만들어집니다.

---

## 🔊 Mouse click sound / 마우스 클릭음

Left click plays the classic UI click. The sound is embedded in the executable, so the
single-file Windows build needs no extra asset. Right click and wheel stay silent.
좌클릭 시 클래식 UI 클릭음이 재생됩니다. 실행 파일에 내장되어 윈도우 단일 exe 에서도 별도
파일이 필요 없습니다. 우클릭·휠은 무음입니다.

---

## 🖥️ Steam Deck / 스팀덱

Starts fullscreen every time, and the FULLSCREEN button is hidden — windowed mode has no use there.
항상 전체화면으로 시작하며 FULLSCREEN 버튼은 숨깁니다(창모드가 의미 없으므로).

---

## 🐛 Fixed / 수정

**Broken star glyphs / 깨진 별표 표시** — `★` and `☆` have no glyph in Courier New, so favorite
markers rendered as garbage (`*B2`). Markers and filter labels are ASCII now.
`★`·`☆` 가 Courier New 에 글리프가 없어 즐겨찾기 표시가 `*B2` 처럼 깨져 보이던 문제. 표시와
필터 라벨을 ASCII 로 바꿨습니다.

---

## 📦 Downloads / 다운로드

| File / 파일 | Platform |
|---|---|
| `FBNeoRageX.exe` | Windows 10/11 x64 — single portable executable / 단일 포터블 실행 파일 |
| `FBNeoRageX-linux-x86_64.tar.gz` | Steam Deck · Linux x86_64 |

> ⚠️ **The FBNeo core is not included / FBNeo 코어는 포함되어 있지 않습니다.**
> Put `fbneo_libretro.dll` next to the executable (Windows), or `fbneo_libretro.so` in
> `~/FBNeoRageX/bin/` (Steam Deck).
> 윈도우는 실행 파일 옆에 `fbneo_libretro.dll`, 스팀덱은 `~/FBNeoRageX/bin/` 에
> `fbneo_libretro.so` 를 넣어 주세요.

**Steam Deck 설치:**

```bash
tar -xzf FBNeoRageX-linux-x86_64.tar.gz -C ~/
~/FBNeoRageX/FBNeoRageX.sh
```
