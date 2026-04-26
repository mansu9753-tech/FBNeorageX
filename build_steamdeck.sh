#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════
#  FBNeoRageX — Steam Deck / Linux x86_64  tar.gz 번들 빌더
# ═══════════════════════════════════════════════════════════════════════
#
#  사용법 (WSL2 Ubuntu 22.04 권장):
#    build_steamdeck_wsl.bat  ← Windows에서 실행
#
#  결과물:
#    build_linux/FBNeoRageX-linux-x86_64.tar.gz
#
#  스팀덱 설치:
#    tar -xzf FBNeoRageX-linux-x86_64.tar.gz -C ~/
#    ~/FBNeoRageX/FBNeoRageX.sh
# ═══════════════════════════════════════════════════════════════════════

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_linux"
# ※ cmake 빌드 바이너리: $BUILD_DIR/FBNeoRageX  (파일)
# ※ 번들 스테이징:       $BUILD_DIR/pkg/FBNeoRageX  (디렉토리 — 이름 충돌 방지)
BUNDLE_STAGE="$BUILD_DIR/pkg"
BUNDLE_ROOT="$BUNDLE_STAGE/FBNeoRageX"
TOOLS_DIR="$SCRIPT_DIR/.deploy_tools"
FINAL_TAR="$BUILD_DIR/FBNeoRageX-linux-x86_64.tar.gz"
LOG="$SCRIPT_DIR/build_linux.log"

RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'; NC='\033[0m'
info() { echo -e "${GRN}[INFO]${NC}  $*"; }
warn() { echo -e "${YLW}[WARN]${NC}  $*"; }
die()  { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

exec > >(tee -a "$LOG") 2>&1
echo "═══════════════════════════════════════════════════════"
echo "  FBNeoRageX Steam Deck Build  $(date '+%Y-%m-%d %H:%M')"
echo "═══════════════════════════════════════════════════════"
echo

# ════════════════════════════════════════════════════════════════════
#  함수
# ════════════════════════════════════════════════════════════════════

download_tool() {
    local name="$1" url="$2" dest="$3"
    if [ -f "$dest" ]; then
        info "$name 이미 존재함"
    else
        info "$name 다운로드 중..."
        wget -q --show-progress -O "$dest" "$url" \
            || die "$name 다운로드 실패 — 네트워크를 확인하세요."
        chmod +x "$dest"
    fi
}

# ldd 기반 의존성 라이브러리 수집
# 제외: 시스템 ABI 라이브러리 (libc, libstdc++, GL, X11, wayland 계열)
# → 스팀덱 SteamOS에 이미 존재하며 번들하면 오히려 충돌
collect_deps() {
    local binary="$1"
    local dest_lib="$2"
    info "의존성 수집 (ldd): $binary"
    while IFS= read -r line; do
        local lib
        lib=$(echo "$line" | awk '{print $3}')
        [ -f "$lib" ] || continue
        local name
        name=$(basename "$lib")
        # 시스템 라이브러리 제외 패턴
        case "$name" in
            libc.so*|libpthread*|libm.so*|libdl.so*|librt.so*) continue ;;
            libgcc_s*|libstdc++*|libgomp*) continue ;;
            libGL*|libEGL*|libGLdispatch*|libGLX*) continue ;;
            libdrm*|libgbm*) continue ;;
            libX*|libxcb*|libxkb*) continue ;;
            libwayland*) continue ;;
            libffi*|libz.so*) continue ;;
            ld-linux*) continue ;;
        esac
        cp -Pn "$lib" "$dest_lib/" 2>/dev/null || true
    done < <(ldd "$binary" 2>/dev/null)
}

# ════════════════════════════════════════════════════════════════════
#  [0] 아키텍처 확인
# ════════════════════════════════════════════════════════════════════
ARCH=$(uname -m)
[ "$ARCH" = "x86_64" ] || die "x86_64 전용 스크립트입니다. (현재: $ARCH)"
info "아키텍처: $ARCH"

# ════════════════════════════════════════════════════════════════════
#  [1] 의존성 설치
# ════════════════════════════════════════════════════════════════════
echo
echo "━━━ [1/5] 의존성 확인 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if command -v apt-get &>/dev/null; then
    PKGS=(
        cmake ninja-build g++ pkg-config patchelf wget file rsync
        qt6-base-dev qt6-multimedia-dev qt6-base-dev-tools
        libqt6opengl6-dev qt6-wayland
        libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev
        libxcb-xinerama0-dev libxcb-cursor-dev libxcb-icccm4-dev
        libfontconfig1-dev libfreetype-dev
        libxkbcommon-dev libwayland-dev
    )
    MISSING=()
    for pkg in "${PKGS[@]}"; do
        dpkg -l "$pkg" 2>/dev/null | grep -q "^ii" || MISSING+=("$pkg")
    done
    if [ "${#MISSING[@]}" -gt 0 ]; then
        info "누락 패키지 설치 중: ${MISSING[*]}"
        sudo apt-get update -qq
        sudo apt-get install -y "${MISSING[@]}"
    else
        info "모든 의존성 충족됨"
    fi
else
    warn "apt-get 없음. cmake ninja g++ qt6-base-dev patchelf wget 가 필요합니다."
fi

# Qt6 qmake 경로
QMAKE6=""
for q in /usr/lib/qt6/bin/qmake \
         /usr/lib/x86_64-linux-gnu/qt6/bin/qmake \
         qmake6 qmake; do
    if command -v "$q" &>/dev/null 2>&1 || [ -x "$q" ]; then
        if "$q" --version 2>/dev/null | grep -qi "qt version 6"; then
            QMAKE6="$q"; break
        fi
    fi
done
[ -n "$QMAKE6" ] || die "Qt6 qmake 없음 — qt6-base-dev-tools 설치 필요"
info "Qt6 qmake: $QMAKE6"

# Qt6 CMake prefix
QT6_CMAKE=""
for d in \
    /usr/lib/x86_64-linux-gnu/cmake/Qt6 \
    /usr/lib/cmake/Qt6 \
    /usr/local/lib/cmake/Qt6; do
    [ -f "$d/Qt6Config.cmake" ] && QT6_CMAKE="$d" && break
done
[ -n "$QT6_CMAKE" ] || die "Qt6 CMake 설정 없음"
QT6_PREFIX="$(dirname "$(dirname "$QT6_CMAKE")")"
info "Qt6 prefix: $QT6_PREFIX"

# Qt6 plugins 경로
QT6_PLUGINS=""
for d in \
    "${QT6_PREFIX}/plugins" \
    /usr/lib/x86_64-linux-gnu/qt6/plugins \
    /usr/lib/qt6/plugins; do
    [ -d "$d" ] && QT6_PLUGINS="$d" && break
done
[ -n "$QT6_PLUGINS" ] || warn "Qt6 plugins 경로 없음 — 플러그인이 누락될 수 있음"
info "Qt6 plugins: $QT6_PLUGINS"

# ════════════════════════════════════════════════════════════════════
#  [2] CMake 빌드
# ════════════════════════════════════════════════════════════════════
echo
echo "━━━ [2/5] CMake 빌드 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

mkdir -p "$BUILD_DIR"

# 바이너리(또는 이전 실패로 생긴 동명 디렉토리)를 삭제 → ninja 강제 재링크
# -rf: 디렉토리로 남아있는 경우도 처리
rm -rf "$BUILD_DIR/FBNeoRageX"

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT6_PREFIX" \
    -DCMAKE_CXX_FLAGS="-O2"

JOBS=$(nproc 2>/dev/null || echo 4)
info "빌드 중 (${JOBS}코어)..."
cmake --build "$BUILD_DIR" --parallel "$JOBS"

[ -f "$BUILD_DIR/FBNeoRageX" ] || die "빌드 실패: FBNeoRageX 없음"
info "빌드 완료"

# ════════════════════════════════════════════════════════════════════
#  [3] linuxdeploy 다운로드
# ════════════════════════════════════════════════════════════════════
echo
echo "━━━ [3/5] 배포 도구 준비 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

mkdir -p "$TOOLS_DIR"

LDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

download_tool "linuxdeploy" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
    "$LDEPLOY"

download_tool "linuxdeploy-plugin-qt" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
    "$LDEPLOY_QT"

# ════════════════════════════════════════════════════════════════════
#  [4] 번들 디렉토리 구성
#
#  FBNeoRageX/
#    FBNeoRageX.sh     ← 런처 (이걸 실행)
#    bin/
#      FBNeoRageX      ← 바이너리
#      assets/
#      fbneo_libretro.so
#    lib/              ← Qt + 의존 .so
#    plugins/          ← Qt 플러그인
# ════════════════════════════════════════════════════════════════════
echo
echo "━━━ [4/5] 번들 구성 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

rm -rf "$BUNDLE_ROOT"
mkdir -p "$BUNDLE_ROOT/bin"
mkdir -p "$BUNDLE_ROOT/lib"
mkdir -p "$BUNDLE_ROOT/plugins"

# ── 바이너리 ────────────────────────────────────────────────────
cp "$BUILD_DIR/FBNeoRageX" "$BUNDLE_ROOT/bin/"
chmod +x "$BUNDLE_ROOT/bin/FBNeoRageX"

# ── assets ──────────────────────────────────────────────────────
if [ -d "$SCRIPT_DIR/assets" ]; then
    cp -r "$SCRIPT_DIR/assets" "$BUNDLE_ROOT/bin/assets"
    info "assets 복사 완료"
fi

# ── libretro 코어 ────────────────────────────────────────────────
if [ -f "$SCRIPT_DIR/fbneo_libretro.so" ]; then
    cp "$SCRIPT_DIR/fbneo_libretro.so" "$BUNDLE_ROOT/bin/"
    info "fbneo_libretro.so 포함"
else
    warn "fbneo_libretro.so 없음 — 나중에 ~/FBNeoRageX/bin/ 에 복사 필요"
fi

# ── 컨트롤러 설정 가이드 ─────────────────────────────────────────
if [ -f "$SCRIPT_DIR/steamdeck_input_guide.txt" ]; then
    cp "$SCRIPT_DIR/steamdeck_input_guide.txt" "$BUNDLE_ROOT/"
    info "컨트롤러 가이드 포함"
fi

# ── Qt 라이브러리 수집 (linuxdeploy 활용) ────────────────────────
info "linuxdeploy 로 Qt6 라이브러리 수집 중..."

# linuxdeploy 는 AppDir 구조를 필요로 하므로 임시 AppDir 생성
TMPAPPDIR="$BUILD_DIR/.tmp_appdir"
rm -rf "$TMPAPPDIR"
mkdir -p "$TMPAPPDIR/usr/bin"
cp "$BUILD_DIR/FBNeoRageX" "$TMPAPPDIR/usr/bin/"

# 프로젝트 루트의 .desktop 파일 우선 사용, 없으면 인라인 생성
if [ -f "$SCRIPT_DIR/FBNeoRageX.desktop" ]; then
    cp "$SCRIPT_DIR/FBNeoRageX.desktop" "$TMPAPPDIR/FBNeoRageX.desktop"
else
    cat > "$TMPAPPDIR/FBNeoRageX.desktop" << 'DESKEOF'
[Desktop Entry]
Version=1.0
Type=Application
Name=FBNeoRageX
Comment=FinalBurn Neo libretro frontend
Exec=FBNeoRageX
Icon=FBNeoRageX
Terminal=false
Categories=Game;Emulator;
DESKEOF
fi

# 더미 아이콘 (linuxdeploy 요구)
if command -v convert &>/dev/null; then
    convert -size 256x256 xc:"#1e3c78" "$TMPAPPDIR/FBNeoRageX.png" 2>/dev/null \
        || python3 -c "
import struct,zlib
def c(t,d): x=t+d; return struct.pack('>I',len(d))+x+struct.pack('>I',zlib.crc32(x)&0xffffffff)
w=h=256; r=b''.join(b'\x00'+bytes([30,60,120]*w) for _ in range(h))
p=b'\x89PNG\r\n\x1a\n'+c(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+c(b'IDAT',zlib.compress(r))+c(b'IEND',b'')
open('$TMPAPPDIR/FBNeoRageX.png','wb').write(p)"
else
    python3 -c "
import struct,zlib
def c(t,d): x=t+d; return struct.pack('>I',len(d))+x+struct.pack('>I',zlib.crc32(x)&0xffffffff)
w=h=256; r=b''.join(b'\x00'+bytes([30,60,120]*w) for _ in range(h))
p=b'\x89PNG\r\n\x1a\n'+c(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+c(b'IDAT',zlib.compress(r))+c(b'IEND',b'')
open('$TMPAPPDIR/FBNeoRageX.png','wb').write(p)"
fi

# linuxdeploy: AppDir에 Qt 라이브러리 + 플러그인 배포
export QMAKE="$QMAKE6"
export EXTRA_QT_PLUGINS="multimedia;multimediawidgets;network"
export EXTRA_PLATFORM_PLUGINS="wayland"
export DEPLOY_PLATFORM_THEMES=1
export LD_LIBRARY_PATH="${QT6_PREFIX}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

set +e
"$LDEPLOY" --appimage-extract-and-run \
    --appdir "$TMPAPPDIR" \
    --executable "$TMPAPPDIR/usr/bin/FBNeoRageX" \
    --desktop-file "$TMPAPPDIR/FBNeoRageX.desktop" \
    --icon-file "$TMPAPPDIR/FBNeoRageX.png" \
    --plugin qt \
    --verbosity 1 2>&1
LDEPLOY_RC=$?
set -e

if [ "$LDEPLOY_RC" -eq 0 ] && [ -d "$TMPAPPDIR/usr/lib" ]; then
    info "linuxdeploy Qt 배포 성공 — 라이브러리 복사 중..."

    # Qt 라이브러리
    if [ -d "$TMPAPPDIR/usr/lib" ]; then
        cp -Pn "$TMPAPPDIR/usr/lib/"* "$BUNDLE_ROOT/lib/" 2>/dev/null || true
    fi

    # Qt 플러그인
    if [ -d "$TMPAPPDIR/usr/plugins" ]; then
        cp -r "$TMPAPPDIR/usr/plugins/"* "$BUNDLE_ROOT/plugins/" 2>/dev/null || true
    fi
else
    warn "linuxdeploy 실패 (rc=$LDEPLOY_RC) — 수동 수집으로 전환"

    # Qt 라이브러리 수동 수집
    QT_LIBS=(
        libQt6Core libQt6Gui libQt6Widgets
        libQt6OpenGL libQt6OpenGLWidgets
        libQt6Multimedia libQt6MultimediaWidgets
        libQt6Network libQt6DBus libQt6XcbQpa
        libQt6WaylandClient libicudata libicui18n libicuuc
    )
    for lib in "${QT_LIBS[@]}"; do
        for f in "${QT6_PREFIX}/lib/${lib}"*.so* \
                 /usr/lib/x86_64-linux-gnu/"${lib}"*.so*; do
            [ -f "$f" ] && cp -Pn "$f" "$BUNDLE_ROOT/lib/" 2>/dev/null || true
        done
    done
fi

# Qt 플러그인 — 최소 필수 (없으면 수동 복사)
if [ -n "$QT6_PLUGINS" ]; then
    for ptype in platforms imageformats xcbglintegrations wayland-shell-integration wayland-graphics-integration-client wayland-decoration-client; do
        if [ -d "$QT6_PLUGINS/$ptype" ]; then
            mkdir -p "$BUNDLE_ROOT/plugins/$ptype"
            cp "$QT6_PLUGINS/$ptype/"*.so \
               "$BUNDLE_ROOT/plugins/$ptype/" 2>/dev/null || true
        fi
    done
fi

# ldd 기반 추가 의존성 수집
collect_deps "$BUILD_DIR/FBNeoRageX" "$BUNDLE_ROOT/lib"

# Qt 플러그인들의 의존성도 수집
find "$BUNDLE_ROOT/plugins" -name "*.so" 2>/dev/null | while read -r plugin; do
    collect_deps "$plugin" "$BUNDLE_ROOT/lib"
done

info "라이브러리 수집 완료: $(ls "$BUNDLE_ROOT/lib" | wc -l) 개"
info "플러그인 수집 완료: $(find "$BUNDLE_ROOT/plugins" -name '*.so' | wc -l) 개"

# ── patchelf: RPATH 설정 ($ORIGIN/../lib 로 번들 lib 우선 탐색) ──
if command -v patchelf &>/dev/null; then
    info "patchelf: RPATH 설정 중..."
    patchelf --set-rpath '$ORIGIN/../lib' "$BUNDLE_ROOT/bin/FBNeoRageX" 2>/dev/null || true
fi

# ── 런처 스크립트 생성 ───────────────────────────────────────────
info "런처 스크립트 생성..."
cat > "$BUNDLE_ROOT/FBNeoRageX.sh" << 'LAUNCHER'
#!/bin/bash
# FBNeoRageX 런처 — Steam Deck / Linux x86_64
HERE="$(dirname "$(readlink -f "$0")")"
LOG="$HERE/launch.log"

# 실행 환경 로그 (튕김 발생 시 진단용)
{
echo "=== FBNeoRageX Launch $(date) ==="
echo "DISPLAY=$DISPLAY"
echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-}"
echo "QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-}"
echo "SteamGameId=${SteamGameId:-}"
} >> "$LOG" 2>&1

# 번들 라이브러리 우선 사용
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Qt 플러그인 경로
export QT_PLUGIN_PATH="$HERE/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/plugins/platforms"

# Qt6 멀티미디어 백엔드
# FFmpeg 플러그인이 번들에 있으면 사용, 없으면 시스템 GStreamer 사용
if [ -f "$HERE/plugins/multimedia/libQtMultimediaPlugin_ffmpeg.so" ] || \
   ls "$HERE/plugins/multimedia/"*ffmpeg*.so 2>/dev/null | grep -q .; then
    export QT_MEDIA_BACKEND=ffmpeg
fi
# GStreamer: 번들 내에 없으면 시스템 것을 그대로 사용 (SteamOS에 포함됨)
# export GST_PLUGIN_SYSTEM_PATH_1_0=""  ← 의도적으로 비활성화하지 않음

# 플랫폼 자동 선택
# - Gaming Mode(Gamescope): WAYLAND_DISPLAY 설정됨 → wayland 우선
# - Desktop Mode: xcb(XWayland) 사용
if [ -z "${QT_QPA_PLATFORM:-}" ]; then
    if [ -n "${WAYLAND_DISPLAY:-}" ]; then
        export QT_QPA_PLATFORM=wayland
    else
        export QT_QPA_PLATFORM=xcb
    fi
fi
export QT_XCB_NO_MITSHM=1

# 실행 (stdout/stderr 로그 저장)
exec "$HERE/bin/FBNeoRageX" "$@" >> "$LOG" 2>&1
LAUNCHER
chmod +x "$BUNDLE_ROOT/FBNeoRageX.sh"

# Steam 비-Steam 게임용 .desktop 파일도 번들에 포함
cat > "$BUNDLE_ROOT/FBNeoRageX.desktop" << DESKEOF2
[Desktop Entry]
Version=1.0
Type=Application
Name=FBNeoRageX
GenericName=Arcade Emulator
Comment=FinalBurn Neo libretro frontend
Exec=${HOME}/FBNeoRageX/FBNeoRageX.sh
Icon=FBNeoRageX
Terminal=false
StartupNotify=false
Categories=Game;Emulator;
DESKEOF2

info "런처 생성 완료: FBNeoRageX.sh"

# 임시 AppDir 정리
rm -rf "$TMPAPPDIR"

# ════════════════════════════════════════════════════════════════════
#  [5] tar.gz 패키징
# ════════════════════════════════════════════════════════════════════
echo
echo "━━━ [5/5] tar.gz 패키징 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

rm -f "$FINAL_TAR"
cd "$BUNDLE_STAGE"
tar -czf "$FINAL_TAR" FBNeoRageX/
cd "$SCRIPT_DIR"

[ -f "$FINAL_TAR" ] || die "tar.gz 생성 실패"

SIZE=$(du -sh "$FINAL_TAR" | cut -f1)
LIBCOUNT=$(ls "$BUNDLE_ROOT/lib" 2>/dev/null | wc -l)
PLUGCOUNT=$(find "$BUNDLE_ROOT/plugins" -name '*.so' 2>/dev/null | wc -l)

echo
echo -e "  ${GRN}✔ 빌드 완료!${NC}"
echo "  패키지  : $FINAL_TAR"
echo "  크기    : $SIZE"
echo "  라이브러리: $LIBCOUNT 개  |  플러그인: $PLUGCOUNT 개"
echo
echo "  ── Steam Deck 설치 방법 ──────────────────────────────────"
echo "  1. tar.gz 파일을 Steam Deck 으로 복사"
echo "     (USB 드라이브 또는 네트워크 공유)"
echo
echo "  2. Konsole 에서:"
echo "       tar -xzf FBNeoRageX-linux-x86_64.tar.gz -C ~/"
echo "       ~/FBNeoRageX/FBNeoRageX.sh"
echo
echo "  3. fbneo_libretro.so 를 ~/FBNeoRageX/bin/ 에 복사"
echo
echo "  ── Steam 게임 모드 등록 ─────────────────────────────────"
echo "  Steam → 게임 추가 → 비-Steam 게임 → FBNeoRageX.sh 선택"
echo "  시작 옵션: (없음)"
echo "  ─────────────────────────────────────────────────────────"
echo
echo "  로그: $LOG"
