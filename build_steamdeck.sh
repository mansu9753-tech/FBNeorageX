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

# ★ WSL 비대화형 셸(wsl --exec bash)은 로그인 프로파일을 안 읽어 PATH 가
#   최소화되는 경우가 있음 → /usr/bin 의 apt-get·qmake6 를 못 찾아 실패.
#   표준 시스템 경로를 명시적으로 앞에 붙여 보장한다.
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:${PATH:-}"

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
            # VAAPI/VDPAU: 실행 시스템 GPU 드라이버와 짝이 맞아야 함 → 번들 금지
            libva.so*|libva-*.so*|libvdpau*) continue ;;
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
        # Qt6 Network + TLS (Fightcade 넷플레이 공개 IP 조회 / HTTPS)
        libssl-dev ca-certificates
        # FFmpeg dev 헤더 — VideoRecorder(프리뷰/일반 녹화)에 필수.
        #   이게 없으면 CMake 가 HAVE_FFMPEG=0 으로 빌드해 Linux 녹화가 통째로
        #   비활성화된다(스샷만 되고 녹화 실패). 런타임 .so 는 시스템에 있어도
        #   dev 헤더가 없으면 컴파일 단계에서 빠진다.
        libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
    )
    MISSING=()
    for pkg in "${PKGS[@]}"; do
        # 이미 설치됨 → 건너뜀
        #   ※ `dpkg -l | grep -q` 는 쓰지 않는다. grep -q 가 매치 즉시 종료하며
        #     dpkg 에 SIGPIPE 를 유발 → `set -o pipefail` 과 만나 오판정된다.
        #     dpkg-query 로 상태만 직접 받아온다 (파이프 없음).
        status=$(dpkg-query -W -f='${db:Status-Status}' "$pkg" 2>/dev/null || true)
        [ "$status" = "installed" ] && continue
        # ★ Ubuntu 버전에 따라 존재하지 않는 패키지명은 무시한다.
        #   예: Ubuntu 24.04 에는 libqt6opengl6-dev 가 없고(OpenGL dev 는
        #   qt6-base-dev 에 포함됨), 설치 후보가 없어 유령 누락으로 잡히던 문제.
        #   ※ apt-cache show 는 유령 패키지에도 성공을 반환하므로 부적합.
        #     apt-cache policy 의 "Candidate:" 가 (none) 이 아닌지로 판단.
        #   ※※ 파이프 + grep -q 조합 금지! grep -q 는 매치 즉시 종료하므로
        #      apt-cache 가 SIGPIPE 로 죽고, 이 스크립트의 `set -o pipefail`
        #      때문에 파이프라인이 '실패'로 판정된다 → 후보가 있어도 매번
        #      거짓이 되어 모든 패키지가 누락 목록에서 빠지고 "모든 의존성
        #      충족됨" 으로 넘어가 버렸다(FFmpeg dev 미설치 → 녹화 불가 원인).
        #      → 명령치환으로 전체 출력을 받아 검사한다 (SIGPIPE 없음).
        #   awk 에 exit 를 쓰지 않는다 — 조기 종료하면 apt-cache 가 SIGPIPE 로
        #   죽어 pipefail 에 걸린다. 전체 입력을 읽게 두고 마지막 값을 취한다.
        cand=$(apt-cache policy "$pkg" 2>/dev/null | awk -F': ' '/Candidate:/{v=$2} END{print v}' || true)
        if [ -n "$cand" ] && [ "$cand" != "(none)" ]; then
            MISSING+=("$pkg")
        fi
    done
    if [ "${#MISSING[@]}" -gt 0 ]; then
        # ★ 이 스크립트는 wsl --exec 로 "비대화형" 실행되므로 sudo 가
        #   비밀번호를 물으면 입력할 수 없어 무한 대기(hang) 한다.
        #   → sudo -n(비대화형)로 먼저 확인. 비밀번호가 필요하면
        #     설치 명령을 안내하고 즉시 종료 (대기 방지).
        if sudo -n true 2>/dev/null; then
            info "누락 패키지 설치 중: ${MISSING[*]}"
            sudo -n apt-get update -qq
            sudo -n apt-get install -y "${MISSING[@]}" \
                || die "패키지 설치 실패 — 위 오류를 확인하세요."
        else
            echo
            warn "sudo 가 비밀번호를 요구합니다. 비대화형 빌드에서는 자동 설치가 불가합니다."
            warn "아래 명령을 WSL 터미널에서 한 번 직접 실행(비밀번호 입력)한 뒤 빌드를 다시 하세요:"
            echo
            echo "    sudo apt-get update"
            echo "    sudo apt-get install -y ${MISSING[*]}"
            echo
            die "의존성 미설치 — 위 명령 실행 후 다시 빌드하세요."
        fi
    else
        info "모든 의존성 충족됨"
    fi
else
    die "apt-get 없음 — Ubuntu WSL 이 아니거나 PATH 문제입니다. (Ubuntu 배포판 확인)"
fi

# Qt6 qmake 경로
# Ubuntu 22.04/24.04 에서 qmake6 위치가 배포판마다 다름 → 다중 경로 탐색
QMAKE6=""
for q in qmake6 \
         /usr/lib/qt6/bin/qmake6 \
         /usr/lib/qt6/bin/qmake \
         /usr/lib/x86_64-linux-gnu/qt6/bin/qmake6 \
         /usr/lib/x86_64-linux-gnu/qt6/bin/qmake \
         /usr/bin/qmake6 \
         qmake; do
    # command -v 로 PATH 탐색, [ -x ] 로 절대경로 확인
    if command -v "$q" &>/dev/null || [ -x "$q" ]; then
        if "$q" --version 2>/dev/null | grep -qi "qt version 6"; then
            QMAKE6="$q"; break
        fi
    fi
done
if [ -z "$QMAKE6" ]; then
    warn "Qt6 qmake 를 못 찾았습니다."
    warn "설치:  sudo apt-get install -y qt6-base-dev-tools qmake6"
    warn "확인:  which qmake6 && qmake6 --version"
    die  "Qt6 qmake 없음 — 위 패키지 설치 후 다시 실행하세요."
fi
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

# ── 게임 이름 간편 변경 파일 (사용자가 편집) ─────────────────────
# 번들 루트(= FBNeoRageX.sh 옆)에 두면 앱이 자동으로 읽는다.
# 이미 있으면 덮어쓰지 않는다 — 사용자가 편집한 내용 보존.
if [ -f "$SCRIPT_DIR/names.txt" ]; then
    cp -n "$SCRIPT_DIR/names.txt" "$BUNDLE_ROOT/names.txt" 2>/dev/null || true
    info "names.txt 포함 (게임 이름 간편 변경)"
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
# ★ EXTRA_PLATFORM_PLUGINS=wayland 은 linuxdeploy-plugin-qt 가
#   "platforms/wayland" 를 파일로 오인해 하드 실패시키는 버그가 있음.
#   → wayland 플랫폼 플러그인은 아래 수동 수집 블록(platforms 전체 복사)이
#     처리하므로 여기서는 넘기지 않아 linuxdeploy 실패를 방지한다.
export QMAKE="$QMAKE6"
export EXTRA_QT_PLUGINS="multimedia;multimediawidgets;network;tls"
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
        # TLS (Qt6 Network HTTPS — 공개 IP 조회, 넷플레이 룸 코드)
        libQt6NetworkAuth libssl libcrypto
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
    for ptype in platforms imageformats xcbglintegrations tls wayland-shell-integration wayland-graphics-integration-client wayland-decoration-client; do
        if [ -d "$QT6_PLUGINS/$ptype" ]; then
            mkdir -p "$BUNDLE_ROOT/plugins/$ptype"
            cp "$QT6_PLUGINS/$ptype/"*.so \
               "$BUNDLE_ROOT/plugins/$ptype/" 2>/dev/null || true
        fi
    done

    # ── Qt Multimedia 백엔드 (프리뷰 영상 재생에 필수) ───────────────
    # 런처가 QT_PLUGIN_PATH 를 번들로 고정하므로, 이 플러그인이 없으면
    # QMediaPlayer 가 백엔드를 찾지 못해 영상 재생 시 프로그램이 죽는다.
    #   · ffmpeg 백엔드만 넣는다 — 이미 FFmpeg 라이브러리를 번들하므로
    #     일관되고, gstreamer 백엔드는 GStreamer 의존성 전체를 끌어온다.
    if [ -d "$QT6_PLUGINS/multimedia" ]; then
        mkdir -p "$BUNDLE_ROOT/plugins/multimedia"
        cp "$QT6_PLUGINS/multimedia/"*ffmpeg*.so \
           "$BUNDLE_ROOT/plugins/multimedia/" 2>/dev/null || true
        if ls "$BUNDLE_ROOT/plugins/multimedia/"*.so >/dev/null 2>&1; then
            info "Qt Multimedia 백엔드(ffmpeg) 번들 완료 — 프리뷰 영상 재생 가능"
        else
            warn "Qt Multimedia 백엔드 복사 실패 — 프리뷰 영상 재생 불가"
        fi
    else
        warn "Qt multimedia 플러그인 폴더 없음 — 프리뷰 영상 재생 불가"
    fi
fi

# ldd 기반 추가 의존성 수집
collect_deps "$BUILD_DIR/FBNeoRageX" "$BUNDLE_ROOT/lib"

# Qt 플러그인들의 의존성도 수집
find "$BUNDLE_ROOT/plugins" -name "*.so" 2>/dev/null | while read -r plugin; do
    collect_deps "$plugin" "$BUNDLE_ROOT/lib"
done

# ── GPU 드라이버 결합 라이브러리 제거 (스팀덱 실행 불가 방지) ────────
# FFmpeg(libavcodec)를 넣으면서 VAAPI/VDPAU 하드웨어 가속 라이브러리가
# linuxdeploy 를 통해 함께 번들되는데, 이들은 실행 시스템의 GPU 드라이버
# (SteamOS = Mesa)와 버전이 정확히 맞아야 한다. 우분투판을 번들해 스팀덱에서
# 로드하면 드라이버 스택이 어긋나 실행 자체가 실패한다.
#   · 본체/libavcodec/libavutil 모두 이들을 DT_NEEDED 로 요구하지 않는다(확인함)
#     → 제거해도 로딩에 문제 없고, 필요 시 시스템 것이 쓰인다.
#   · 녹화는 소프트웨어 인코딩이라 VAAPI 없이도 정상 동작한다.
for risky in libva libva-drm libva-x11 libva-glx libvdpau; do
    rm -f "$BUNDLE_ROOT/lib/${risky}.so"* 2>/dev/null || true
done
info "GPU 드라이버 결합 라이브러리 제외 완료 (libva/libvdpau → 시스템 것 사용)"

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

# 프리뷰 영상은 앱이 자체 소프트웨어 디코더(PreviewVideo)로 재생하므로
# Qt 멀티미디어의 하드웨어 디코더 탐색 경로를 타지 않는다.
#   → 예전처럼 VAAPI/VDPAU/Vulkan 을 환경변수로 막을 필요가 없다.
#     (드라이버를 막으면 OpenGL 이 Vulkan 위에서 도는 환경(Zink)에서
#      게임 화면 렌더링까지 깨질 수 있어 오히려 위험했다)

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
