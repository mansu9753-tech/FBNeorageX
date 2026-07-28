---
layout: default
title: "FBNeoRageX — 아케이드 에뮬레이터 프론트엔드 (윈도우 · 스팀덱)"
description: "FinalBurn Neo(libretro) 기반 아케이드 에뮬레이터 프론트엔드. Qt6, GGPO 넷플레이, 한글 게임명, 스팀덱 지원."
---

# FBNeoRageX

**A modern arcade emulator frontend powered by [FinalBurn Neo](https://github.com/finalburnneo/FBNeo) (libretro core).**
Built with C++17 + Qt6 for **Windows** and **Steam Deck**.

**FinalBurn Neo libretro 코어 기반의 아케이드 에뮬레이터 프론트엔드입니다.**
C++17 + Qt6로 제작되었으며 **윈도우**와 **스팀덱**을 지원합니다.

[⬇️ Download / 다운로드 (v2.1)](https://github.com/mansu9753-tech/FBNeorageX/releases/latest){: .btn }
[📖 GitHub](https://github.com/mansu9753-tech/FBNeorageX){: .btn }

---

## ✨ Features / 주요 기능

| English | 한국어 |
|---|---|
| **4-panel UI** — game list / options / preview / event log | **4분할 UI** — 게임 목록 · 옵션 · 프리뷰 · 이벤트 로그 |
| **Korean game names** — `gamelist.xml`, or one-line overrides in `names.txt` | **한글 게임명** — `gamelist.xml` 지원, `names.txt`로 한 줄씩 직접 지정 |
| **Preview** — image → video → image, cycling automatically with sound | **프리뷰** — 이미지 → 영상 → 이미지 자동 순환 (사운드 포함) |
| **Pure GGPO netplay** — 6-char room code, STUN + UDP hole-punching, rollback | **순수 GGPO 넷플레이** — 6자리 룸 코드, STUN + UDP 홀펀칭, 롤백 |
| **Steam Deck ready** — Gaming Mode / Desktop Mode, gamepad navigation | **스팀덱 지원** — 게임 모드·데스크톱 모드, 패드로 메뉴 조작 |
| **Save states, fast-forward, recording, screenshots** | **세이브스테이트 · 배속 · 녹화 · 스크린샷** |
| **Per-platform / per-game settings** — controls & DIP switches | **기종별/게임별 설정** — 컨트롤·DIP 스위치 |
| **Flash reduction** — eases full-screen white flashes (eye protection) | **플래시 감소** — 전체 화면 번쩍임 억제 (눈 보호) |
| **KO / EN menu toggle**, configurable hotkeys, CRT shader, TATE mode | **메뉴 한/영 전환**, 핫키 설정, CRT 셰이더, TATE 모드 |

---

## ⬇️ Download / 다운로드

| File / 파일 | Platform |
|---|---|
| `FBNeoRageX.exe` | Windows 10/11 x64 — single portable executable / 단일 포터블 실행 파일 |
| `FBNeoRageX-linux-x86_64.tar.gz` | Steam Deck · Linux x86_64 |

> ⚠️ **The FBNeo core is not included / FBNeo 코어는 포함되어 있지 않습니다.**
> Put `fbneo_libretro.dll` next to the executable (Windows), or `fbneo_libretro.so` in `~/FBNeoRageX/bin/` (Steam Deck).
> 윈도우는 실행 파일 옆에 `fbneo_libretro.dll`, 스팀덱은 `~/FBNeoRageX/bin/` 에 `fbneo_libretro.so` 를 넣어 주세요.

**Steam Deck 설치:**

```bash
tar -xzf FBNeoRageX-linux-x86_64.tar.gz -C ~/
~/FBNeoRageX/FBNeoRageX.sh
```

---

## 🏷️ Korean game names / 한글 게임명

Rename list entries one line at a time in `names.txt` (UTF-8) — no XML editing needed.
`names.txt`(UTF-8)에 한 줄씩 적기만 하면 됩니다. XML을 편집할 필요가 없습니다.

```ini
kof94  = 더 킹 오브 파이터즈 94
mslug3 = 메탈슬러그 3
sfa3   = 스트리트 파이터 제로 3
```

Priority / 우선순위: `names.txt` → `gamelist.xml` → built-in DB → ROM name

---

## 📖 Documentation / 문서

Full guide — installation, controls, hotkeys, netplay, options, FAQ — is in the repository README.
설치·조작·단축키·넷플레이·옵션·FAQ 전체 안내는 저장소 README에 있습니다.

[README (English / 한국어)](https://github.com/mansu9753-tech/FBNeorageX#readme) ·
[Release notes / 릴리스 노트](https://github.com/mansu9753-tech/FBNeorageX/releases/latest)

---

## 🙏 Credits / 크레딧

- [FinalBurn Neo](https://github.com/finalburnneo/FBNeo) — arcade emulation core / 아케이드 에뮬레이션 코어
- [libretro](https://www.libretro.com/) — core/frontend interface / 코어·프론트엔드 인터페이스
- [Qt6](https://www.qt.io/) — UI framework / UI 프레임워크
- [라즈겜동](https://cafe.naver.com/razberry) — Korean game name database / 한글 게임명 데이터베이스
