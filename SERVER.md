# 계정·로비 서버 설치 / Account & Lobby Server Setup

한국어 → 아래 / English → [below](#english)

## 한국어

이 서버는 **Cloudflare Workers** 에 올립니다(무료 플랜으로 충분합니다). 서버 주소에 내 이름이 들어가지 않게 하려면 1~2단계를 꼭 확인하세요.

### 1. Cloudflare 계정 만들기
1. https://dash.cloudflare.com/sign-up 에서 가입합니다 (이메일로 인증).
2. 주소에 쓰일 **계정 서브도메인**을 정합니다: 대시보드 왼쪽 **Workers & Pages** → 처음 들어가면 `workers.dev` 서브도메인을 만들라고 나옵니다. 여기에 **내 이름이 들어가지 않는 단어**(예: `nrx-play-7421`)를 넣으세요. 나중에 바꾸려면 *Workers & Pages → 오른쪽 위 Account details → Change* 에서 바꿀 수 있습니다.
   - 이메일 주소는 서버 주소에 나타나지 않습니다. 보이는 것은 서브도메인뿐입니다.
   - 더 깔끔한 주소가 필요하면 도메인을 사서 Workers 에 연결하는 방법도 있습니다(선택, 유료).

### 2. 도구 설치 (내 PC, 한 번만)
1. Node.js LTS 설치: https://nodejs.org
2. 명령 프롬프트(또는 PowerShell)에서:
   ```
   npm install -g wrangler
   wrangler login
   ```
   `wrangler login` 을 하면 브라우저가 열리고 Cloudflare 에서 **허용**을 누르면 됩니다.

### 3. 서버 올리기
1. 이 프로젝트의 `server` 폴더로 이동합니다.
   ```
   cd server
   ```
2. (선택) `wrangler.toml` 맨 위 `name = "nrx-play"` 를 원하는 이름으로 바꿉니다. 이 이름이 주소 앞부분이 됩니다 → `https://<name>.<서브도메인>.workers.dev`
3. 올립니다.
   ```
   wrangler deploy
   ```
4. 끝에 출력되는 주소(`https://nrx-play.xxxxx.workers.dev`)를 복사합니다.
5. 브라우저로 그 주소를 열어 `FBNeoRageX Play Server OK` 가 보이면 성공입니다.

데이터(계정, 방 목록)는 Cloudflare 의 Durable Object 저장소에 저장되며 별도 설정이 필요 없습니다.

### 4. 프로그램에 주소 넣기
1. 프로그램 → **멀티플레이어 → 내 프로필 → 서버 주소 (고급)** 에 복사한 주소를 붙여 넣습니다 (입력은 가려져 보입니다).
2. **계정 → 새 계정** 으로 아이디/비밀번호를 만듭니다. 이후에는 같은 곳에서 로그인합니다.
3. 멀티플레이어 첫 화면(로비)에 열려 있는 방 목록이 나옵니다. 방은 **방 만들기** 로 만들고, 다른 사람은 목록에서 골라 들어갑니다 (그 방의 게임 ROM 이 있어야 입장됩니다).

### 5. 모두가 주소를 입력하지 않게 하려면 (배포용)
`src/AppSettings.h` 의 `builtinServerUrl()` 이 `return "https://내주소";` 를 돌려주도록 고치고 다시 빌드해서 배포합니다. (그러면 설치만 하면 바로 로비가 보입니다.)

### 6. 예전 서버(개인 주소) 정리
- 예전 `fbneoragex-relay` 는 더 쓰지 않습니다. 예전 Cloudflare 계정에서 **Workers & Pages → fbneoragex-relay → Settings → Delete** 로 지우면 됩니다.
- 이미 공개된 GitHub 기록과 이전에 배포한 exe 에는 예전 주소가 남아 있습니다. 지워도 기록은 지워지지 않으니, 서버를 삭제해서 주소가 쓸모없게 만드는 것이 확실합니다.

### 서버가 저장하는 것 / 하지 않는 것
저장: 아이디, 비밀번호의 해시(원문 아님), 닉네임, 경험치, 캐릭터 부품 번호, 열려 있는 방 정보(45초만).
저장하지 않음: 이메일, 실명, IP(가입 횟수 제한용으로 하루만 셈). 이메일이 없어서 **비밀번호를 잊으면 복구할 수 없습니다**(새 계정을 만들어야 합니다).

### 문제가 생기면
- `https://주소/` 가 열리지 않는다 → `wrangler deploy` 를 다시 실행하고 오류 문구를 확인하세요.
- 프로그램에서 "서버에 연결할 수 없습니다" → 주소 끝에 `/` 나 공백이 없는지, `https://` 로 시작하는지 확인하세요.
- 방 목록이 비어 있다 → 방장이 방을 만든 지 5초 정도 기다린 뒤 **새로고침**.

---

## English

This server runs on **Cloudflare Workers** (the free plan is enough).

1. **Account**: sign up at https://dash.cloudflare.com/sign-up. When Workers asks for your `workers.dev` **subdomain**, choose a neutral word (not your name). Only the subdomain appears in the URL, not your e-mail.
2. **Tools**: install Node.js LTS, then `npm install -g wrangler` and `wrangler login` (approve in the browser).
3. **Deploy**: `cd server`, optionally rename `name = "nrx-play"` in `wrangler.toml`, then `wrangler deploy`. Copy the printed URL (`https://<name>.<subdomain>.workers.dev`); opening it should show `FBNeoRageX Play Server OK`.
4. **In the app**: MULTIPLAYER → MY PROFILE → SERVER (ADVANCED) → paste the URL. Then ACCOUNT → NEW ACCOUNT. The first screen of MULTIPLAYER is now the lobby: open rooms are listed (host, game, players); you can only enter a room if you own its game ROM.
5. **Ship it preconfigured**: make `builtinServerUrl()` in `src/AppSettings.h` return your URL and rebuild.
6. **Retire the old server**: delete the old `fbneoragex-relay` worker in the old Cloudflare account. The old URL remains in old releases / git history, so deleting the worker is the reliable fix.

Stored: ID, password hash, nickname, EXP, avatar part ids, open-room info (45 s). Not stored: e-mail, real name, IP (counted for one day to limit sign-ups). There is no password recovery.
