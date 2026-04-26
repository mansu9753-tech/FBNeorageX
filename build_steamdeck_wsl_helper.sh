#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════
#  FBNeoRageX — WSL2 내부 빌드 헬퍼
#  build_steamdeck_wsl.bat 에 의해 자동 호출됩니다. 직접 실행 금지.
# ═══════════════════════════════════════════════════════════════════════

set -euo pipefail

WIN_SRC="${1:-/mnt/c/fbneoragex}"
WSL_BUILD="${HOME}/FBNeoRageX_wsl_build"

RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'; NC='\033[0m'
info() { echo -e "${GRN}[WSL]${NC}  $*"; }
warn() { echo -e "${YLW}[WSL]${NC}  $*"; }
die()  { echo -e "${RED}[WSL ERROR]${NC} $*"; exit 1; }

# ── [A] 소스 동기화: NTFS → WSL2 ext4 ──────────────────────────────
info "소스 동기화: $WIN_SRC → $WSL_BUILD"
mkdir -p "$WSL_BUILD"

if ! command -v rsync &>/dev/null; then
    info "rsync 설치 중..."
    sudo apt-get install -y rsync -qq
fi

rsync -a --delete \
    --exclude=build_linux/ \
    --exclude=build_static/ \
    --exclude='.deploy_tools/' \
    --exclude='.appimage_tools/' \
    --exclude='*.exe' \
    --exclude='*.bat' \
    --exclude='build_linux.log' \
    "$WIN_SRC/" "$WSL_BUILD/"

info "동기화 완료"

# ── [B] 빌드 실행 ────────────────────────────────────────────────────
info "빌드 시작..."
cd "$WSL_BUILD"

# set -e 를 잠시 해제하여 빌드 실패 시에도 로그를 복사할 수 있게 함
set +e
bash build_steamdeck.sh
BUILD_RC=$?
set -e

# ── [C] 로그는 성공/실패 무관하게 항상 Windows로 복사 ────────────────
WIN_OUT="$WIN_SRC/build_linux"
mkdir -p "$WIN_OUT"
if [ -f "$WSL_BUILD/build_linux.log" ]; then
    cp -f "$WSL_BUILD/build_linux.log" "$WIN_SRC/"
    info "로그 복사 완료: $WIN_SRC/build_linux.log"
fi

# 빌드 실패 시 여기서 종료
if [ "$BUILD_RC" -ne 0 ]; then
    die "빌드 실패 (exit code: $BUILD_RC) — 로그: $WIN_SRC/build_linux.log"
fi

# ── [D] tar.gz → Windows 출력 폴더로 복사 ────────────────────────────
TARBALL="$WSL_BUILD/build_linux/FBNeoRageX-linux-x86_64.tar.gz"

if [ ! -f "$TARBALL" ]; then
    die "tar.gz 없음: $TARBALL"
fi

cp -f "$TARBALL" "$WIN_OUT/"

info "tar.gz 복사 완료: $WIN_OUT/FBNeoRageX-linux-x86_64.tar.gz"
SIZE=$(du -sh "$WIN_OUT/FBNeoRageX-linux-x86_64.tar.gz" | cut -f1)
info "크기: $SIZE"
