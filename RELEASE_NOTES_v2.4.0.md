# FBNeoRageX v2.4.0

> A brand-new NeoRageX-style menu shell, Korean text support, a first (work-in-progress) account-based multiplayer lobby with avatars, and more.
> 오리지널 NeoRageX 스타일의 새 메뉴 화면, 한글 표시, 계정 기반 멀티플레이 로비(공사중)와 아바타 등을 추가했습니다.

---

## ✨ New / 새 기능

| English | 한국어 |
|---|---|
| **New NeoRageX-style menu shell**: game list, filters, favorites, and every option category (VIDEO, AUDIO, MACHINE, CHEATS, CONTROLS, DIRECTORIES, SYSTEM) rebuilt as native pages. Keyboard, gamepad and mouse share the same navigation. | **NeoRageX 스타일의 새 메뉴 화면**: 게임 목록·필터·즐겨찾기와 모든 옵션 카테고리(VIDEO, AUDIO, MACHINE, CHEATS, CONTROLS, DIRECTORIES, SYSTEM)를 새 화면으로 다시 만들었습니다. 키보드·게임패드·마우스가 같은 조작 체계를 씁니다. |
| **Korean text** in the menu (Noto Sans KR subset embedded, see `assets/fonts/LICENSE-NotoSansKR.txt`). | 메뉴의 **한글 표시**(Noto Sans KR 부분 폰트 내장, 라이선스는 `assets/fonts/LICENSE-NotoSansKR.txt`). |
| **Mouse**: in an opened option submenu, clicking empty space no longer closes it; use the back button. The same applies to the multiplayer room screen. | **마우스**: 열린 옵션 하위 메뉴에서 빈 곳을 눌러도 닫히지 않습니다. 뒤로 가기 단추로만 나갑니다. 멀티플레이 방 화면도 같습니다. |
| **MULTIPLAYER (under construction)**: login / sign-up screen, room list showing host nickname and game, "create room" via game picker, account info and avatar editing (body, face, top, hat; gender-aware). A built-in default server is used. | **멀티플레이어(공사중)**: 로그인/회원가입 화면, 방장 닉네임·게임이 보이는 방 목록, 게임 선택으로 방 만들기, 계정 정보와 아바타 꾸미기(몸·얼굴·상의·모자, 성별별 구분). 기본 서버가 내장됩니다. |
| Server setup guide `SERVER.md` and tools for generating avatar art (`tools/`). | 서버 설치 안내 `SERVER.md` 와 아바타 이미지 생성 도구(`tools/`). |

## ⚠️ Known notes / 알려 둘 점

- **MULTIPLAYER is still under construction** and is labelled "(UNDER CONSTRUCTION / 공사중)" in the menu. Joining rooms from another PC and long sessions are not fully verified yet.
  **멀티플레이는 아직 공사중**이라 메뉴에 "(공사중)"으로 표시됩니다. 다른 PC에서 방에 들어가기와 장시간 플레이는 아직 충분히 검증되지 않았습니다.
- The Steam Deck package was built but not run-tested for this release.
  이번 스팀덱 패키지는 빌드만 했고 실제 구동 시험은 하지 못했습니다.
- Windows Defender may flag the unsigned executable (heuristic). Verify the SHA256 if needed.
  서명 없는 실행 파일이라 Windows Defender 가 오탐할 수 있습니다.

## 📦 Downloads / 다운로드

| File / 파일 | Platform |
|---|---|
| `FBNeoRageX.exe` | Windows 10/11 x64 — single portable executable / 단일 포터블 실행 파일 |
| `FBNeoRageX-linux-x86_64.tar.gz` | Steam Deck · Linux x86_64 |
