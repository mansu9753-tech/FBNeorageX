import sys, os, ctypes, shutil, json, struct, signal, re, time
from ctypes import *
from datetime import datetime
from PySide6.QtWidgets import *
from PySide6.QtCore import *
from PySide6.QtGui import *
from PySide6.QtOpenGLWidgets import QOpenGLWidget
from PySide6.QtOpenGL import QOpenGLShaderProgram, QOpenGLShader
from PySide6.QtGui import QSurfaceFormat
from OpenGL.GL import *

# ── OpenGL 호환 프로파일 강제 (QApplication 생성 전에 반드시 설정) ────────
# Core Profile 에서는 glBegin/glEnd, glVertexAttribPointer-without-VAO 모두 불가.
# Compatibility Profile 로 강제해야 RetroArch GLSL 1.10/1.20 쉐이더와
# 기존 고정 파이프라인 렌더링이 동시에 동작한다.
_gl_fmt = QSurfaceFormat()
_gl_fmt.setProfile(QSurfaceFormat.OpenGLContextProfile.CompatibilityProfile)
if IS_WINDOWS:
    # Windows: 2.1 명시 → GLSL 1.20 쉐이더 호환
    _gl_fmt.setVersion(2, 1)
# Linux/SteamDeck: 버전 미지정 → Mesa가 지원 최고 버전(보통 4.6) 자동 선택
# XWayland GLX 에서 2.1 CompatibilityProfile 을 못 찾는 문제 우회
_gl_fmt.setDepthBufferSize(0)      # 2D 렌더링만 하므로 depth 불필요
_gl_fmt.setStencilBufferSize(0)
QSurfaceFormat.setDefaultFormat(_gl_fmt)

IS_LINUX   = sys.platform.startswith('linux')
IS_WINDOWS = sys.platform == 'win32'
CORE_LIB   = "fbneo_libretro.so" if IS_LINUX else "fbneo_libretro.dll"

try:
    from PySide6.QtMultimedia import QAudioFormat, QAudioSink, QMediaDevices, QMediaPlayer
except ImportError:
    QAudioFormat = QAudioSink = QMediaDevices = None
    QMediaPlayer = None

try:
    import numpy as np
except ImportError:
    np = None

AUDIO_AVAILABLE = (QAudioSink is not None)

try:
    from PySide6.QtMultimediaWidgets import QVideoWidget
    VIDEO_PREVIEW_OK = (QMediaPlayer is not None)
except ImportError:
    VIDEO_PREVIEW_OK = False
    QVideoWidget = None

CURRENT_PATH = os.path.dirname(os.path.abspath(sys.argv[0]))
os.chdir(CURRENT_PATH)
CONFIG_FILE = os.path.join(CURRENT_PATH, "config.json")

# PyInstaller onefile 번들 감지
# frozen 실행 시 번들 리소스(dll, assets 등)는 _MEIPASS 임시폴더에 압축 해제됨
# 사용자 데이터(config, roms, saves)는 exe 옆 CURRENT_PATH 에 유지
if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
    BUNDLE_PATH = sys._MEIPASS   # 번들 내부 리소스 경로
else:
    BUNDLE_PATH = CURRENT_PATH   # 일반 실행 시 동일

# ════════════════════════════════════════════════════════════
#  설정
# ════════════════════════════════════════════════════════════
class AppSettings:
    rom_path            = os.path.join(CURRENT_PATH, "roms")
    preview_path        = os.path.join(CURRENT_PATH, "previews")
    screenshot_path     = os.path.join(CURRENT_PATH, "screenshots")
    audio_volume        = 100
    audio_sample_rate   = 48000
    audio_buffer_ms     = 64    # DRC 목표 버퍼 크기 (ms). 권장: 32~128
    audio_drc_max       = 0.005 # DRC 최대 보정률 (0.005 = ±0.5%)
    video_scale_mode    = "Fill"
    video_smooth        = False
    video_crt_mode      = False
    video_crt_intensity = 0.4
    video_frameskip     = 0     # 0=OFF, 1~5=프레임 스킵 N개, -1=AUTO
    video_vsync         = True  # VSYNC ON/OFF
    video_shader_path   = ""    # GLSL 쉐이더 파일 경로 (비어있으면 기본 파이프라인)
    region              = "USA"

settings = AppSettings()

# ════════════════════════════════════════════════════════════
#  키 바인딩
#  Libretro joypad: B=0 Y=1 SEL=2 STA=3 UP=4 DN=5 L=6 R=7
#                   A=8 X=9 L1=10 R1=11
#  NeoGeo 대응:
#    B(0)=NeoGeo-A(약손)  Y(1)=NeoGeo-B(약발)
#    A(8)=NeoGeo-C(강손)  X(9)=NeoGeo-D(강발)
# ════════════════════════════════════════════════════════════
ACTION_DEFS = {
    'up':    (4,  "↑  UP",                  Qt.Key_Up),
    'down':  (5,  "↓  DOWN",                Qt.Key_Down),
    'left':  (6,  "←  LEFT",               Qt.Key_Left),
    'right': (7,  "→  RIGHT",              Qt.Key_Right),
    'a':     (0,  "BTN A  (약손 / Neo-A)", Qt.Key_A),   # libretro B(0)
    'b':     (1,  "BTN B  (약발 / Neo-B)", Qt.Key_S),   # libretro Y(1)
    'c':     (8,  "BTN C  (강손 / Neo-C)", Qt.Key_D),   # libretro A(8)
    'd':     (9,  "BTN D  (강발 / Neo-D)", Qt.Key_F),   # libretro X(9)
    'e':     (10, "BTN E  (MK / SF-E)",    Qt.Key_G),   # libretro L1(10)
    'f':     (11, "BTN F  (HK / SF-F)",    Qt.Key_H),   # libretro R1(11)
    'start': (3,  "START",                 Qt.Key_1),
    'coin':  (2,  "COIN / INSERT",         Qt.Key_5),
}

HOTKEY_DEFS = {
    'save_state':    ("💾 SAVE STATE",   Qt.Key_F5),
    'load_state':    ("📂 LOAD STATE",   Qt.Key_F7),
    'fast_forward':  ("⏩ FAST FORWARD", Qt.Key_F6),
    'slot_next':     ("→ SLOT +1",       Qt.Key_F2),
    'slot_prev':     ("← SLOT -1",       Qt.Key_F1),
    'record_toggle': ("🎬 RECORD TOGGLE", Qt.Key_F9),
}

key_bindings:    dict = {k: v[2] for k, v in ACTION_DEFS.items()}
hotkey_bindings: dict = {k: v[1] for k, v in HOTKEY_DEFS.items()}
game_key_bindings:  dict = {}   # {gamename: {action: qt_key_int}}
board_key_bindings: dict = {}   # {board: {action: qt_key_int}}

# ── 터보 설정 ─────────────────────────────────────────────
TURBO_BUTTON_ACTIONS = ['a', 'b', 'c', 'd', 'e', 'f']   # 방향키 제외
turbo_enabled: dict  = {k: False for k in TURBO_BUTTON_ACTIONS}
turbo_period:  int   = 3   # 1프레임 ON + 1프레임 OFF 단위 (1~8 조절 가능)

# ── 즐겨찾기 ───────────────────────────────────────────────
favorites: list = []   # 즐겨찾기 게임명 목록 (순서 유지)

# ── 게임별 머신(DIP) 설정 저장 ────────────────────────────
game_dip_settings: dict = {}   # {gamename: {key: value}}

# ── 기판 정의 ─────────────────────────────────────────────
BOARD_LIST = ["NEOGEO", "CPS1", "CPS2", "IREM", "TAITO", "KONAMI", "SEGA", "기타"]

BOARD_PREFIXES = {
    "NEOGEO": ["kof", "garou", "samsh", "samsho", "mslug", "metal", "rbff", "svc",
               "kizuna", "aof", "kotm", "neo", "wh1", "wh2", "fatfury", "pulstar",
               "blazstar", "magdrop", "puzzledp", "viewpoin", "janshin", "pspikes"],
    "CPS1":   ["sf2", "ffight", "ghosts", "ghouls", "strider", "1941", "mercs",
               "willow", "unsquad", "dynwar", "knights", "cawing", "varth", "nemo"],
    "CPS2":   ["ssf2", "msh", "xmcota", "mvc", "xmvsf", "mshvsf", "avsp",
               "ddsom", "dstlk", "cybots", "progear", "dimahoo", "19xx", "spf2t",
               "armwar", "batcir", "ringdest"],
    "IREM":   ["rtype", "vigilant", "imgfight", "lottofighter", "gunforce"],
    "TAITO":  ["bubble", "rastan", "arkanoid", "darius", "bonzeadv", "sagaia"],
    "KONAMI": ["contra", "gradius", "twinbee", "salamande", "gyruss"],
    "SEGA":   ["altbeast", "alien3", "sor", "combatr", "fantzone", "shinobi"],
}

# ════════════════════════════════════════════════════════════
#  게임 표시 이름 데이터베이스 — 외부 파일 (game_names_db.json) 에서 로드
#  ※ 직접 편집: game_names_db.json  (UTF-8, {"rom이름": "표시이름"})
#  ※ 한글패치 롬(kr/k 접미사)은 get_display_name()에서 자동 처리
# ════════════════════════════════════════════════════════════
# frozen exe: _internal/ 안에 번들된 버전 우선, 없으면 exe 옆 파일 사용
_gn_bundle  = os.path.join(BUNDLE_PATH,  "game_names_db.json")
_gn_current = os.path.join(CURRENT_PATH, "game_names_db.json")
GAME_NAMES_DB_FILE = _gn_bundle if os.path.exists(_gn_bundle) else _gn_current

def _load_game_names_db() -> dict:
    """game_names_db.json 로드. 파일 없으면 빈 dict 반환."""
    try:
        with open(GAME_NAMES_DB_FILE, 'r', encoding='utf-8') as _f:
            return json.load(_f)
    except FileNotFoundError:
        print(f"[WARN] game_names_db.json 없음 — 게임 표시 이름 DB를 로드하지 못했습니다.")
        return {}
    except Exception as _e:
        print(f"[WARN] game_names_db.json 로드 실패: {_e}")
        return {}

def reload_game_names_db():
    """런타임 중 game_names_db.json 재로드 (변경 사항 즉시 반영)."""
    global GAME_NAMES_DB
    GAME_NAMES_DB = _load_game_names_db()

GAME_NAMES_DB: dict = _load_game_names_db()

# 사용자 정의 게임 이름 (names.json 에서 로드, save_config/load_config 로 관리)
game_display_names: dict = {}

NAMES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "names.json")


def _load_names_json():
    """names.json 에서 사용자 정의 게임 이름 로드"""
    global game_display_names
    if os.path.exists(NAMES_FILE):
        try:
            with open(NAMES_FILE, 'r', encoding='utf-8') as f:
                game_display_names.update(json.load(f))
        except: pass

def _save_names_json():
    """game_display_names 를 names.json 에 저장"""
    try:
        with open(NAMES_FILE, 'w', encoding='utf-8') as f:
            json.dump(game_display_names, f, ensure_ascii=False, indent=2)
    except: pass

def load_gamelist_file(rom_path: str):
    """
    gamelist.xml (EmulationStation 포맷) 또는 gamelist.txt (key=value) 로드.
    로드된 이름은 game_display_names 에 병합됨 (names.json 최우선, gamelist 차순위).
    파일 위치: <rom_path>/gamelist.xml 또는 <rom_path>/gamelist.txt,
              혹은 앱 폴더/gamelist.xml|txt
    """
    global game_display_names
    loaded = {}

    def _try_xml(path):
        try:
            import xml.etree.ElementTree as ET
            tree = ET.parse(path)
            root = tree.getroot()
            for game in root.findall('game'):
                p = game.findtext('path', '')
                n = game.findtext('name', '')
                if p and n:
                    # path: "./kof94.zip" → rom_id = "kof94"
                    rom_id = os.path.splitext(os.path.basename(p))[0].lstrip('.').lstrip('/')
                    if rom_id:
                        loaded[rom_id] = n
        except: pass

    def _try_txt(path):
        try:
            with open(path, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('#'): continue
                    if '=' in line:
                        k, _, v = line.partition('=')
                        k = k.strip(); v = v.strip()
                        if k and v:
                            loaded[k] = v
        except: pass

    for base in [rom_path, CURRENT_PATH]:
        for fname in ['gamelist.xml', 'gamelist.txt']:
            p = os.path.join(base, fname)
            if os.path.exists(p):
                if fname.endswith('.xml'): _try_xml(p)
                else: _try_txt(p)

    # gamelist가 names.json보다 낮은 우선순위 — 이미 있는 항목은 덮어쓰지 않음
    for k, v in loaded.items():
        if k not in game_display_names:
            game_display_names[k] = v

def get_display_name(rom_name: str) -> str:
    """ROM 파일명 → 표시 이름 변환
    우선순위: 사용자 정의 > 내장 DB > kr/k 한글패치 자동감지 > ROM 파일명 그대로"""
    if rom_name in game_display_names:
        return game_display_names[rom_name]
    if rom_name in GAME_NAMES_DB:
        return GAME_NAMES_DB[rom_name]
    # 한글패치 자동 감지: 'kr' 접미사 (예: kof97kr → 킹 오브 파이터즈 '97 (한글패치))
    if rom_name.endswith('kr'):
        base = rom_name[:-2]
        base_name = GAME_NAMES_DB.get(base) or game_display_names.get(base)
        if base_name:
            return f"{base_name} (한글패치)"
    # 한글패치 자동 감지: 'k' 접미사, 베이스 롬이 DB에 있을 때만
    if len(rom_name) > 3 and rom_name.endswith('k'):
        base = rom_name[:-1]
        base_name = GAME_NAMES_DB.get(base) or game_display_names.get(base)
        if base_name:
            return f"{base_name} (한글패치)"
    return rom_name

def detect_board(game_name: str) -> str:
    name = game_name.lower()
    for board, prefixes in BOARD_PREFIXES.items():
        for prefix in prefixes:
            if name.startswith(prefix):
                return board
    return "기타"

def key_to_str(qt_key: int) -> str:
    s = QKeySequence(qt_key).toString()
    return s if s else f"Key({qt_key})"

# ── Config 저장/로드 ─────────────────────────────────────────
def save_config():
    data = {
        "global_bindings":  dict(key_bindings),
        "hotkey_bindings":  dict(hotkey_bindings),
        "game_bindings":    game_key_bindings,
        "board_bindings":   board_key_bindings,
        "turbo_enabled":    dict(turbo_enabled),
        "turbo_period":     turbo_period,
        "favorites":        list(favorites),
        "game_dip_settings": {g: dict(d) for g, d in game_dip_settings.items()},
        "gamepad_linux_map": {str(k): v for k, v in LinuxGamepad._BTN_MAP.items()},
        "gamepad_xi_map":    {str(k): int(v) for k, v in _XI_BTN_MAP.items()},
        "settings": {
            "rom_path":            settings.rom_path,
            "preview_path":        settings.preview_path,
            "screenshot_path":     settings.screenshot_path,
            "audio_volume":        settings.audio_volume,
            "audio_sample_rate":   settings.audio_sample_rate,
            "audio_buffer_ms":     settings.audio_buffer_ms,
            "audio_drc_max":       settings.audio_drc_max,
            "video_scale_mode":    settings.video_scale_mode,
            "video_smooth":        settings.video_smooth,
            "video_crt_mode":      settings.video_crt_mode,
            "video_crt_intensity": settings.video_crt_intensity,
            "video_frameskip":     settings.video_frameskip,
            "video_vsync":         settings.video_vsync,
            "video_shader_path":   settings.video_shader_path,
            "region":              settings.region,
        }
    }
    try:
        with open(CONFIG_FILE, 'w') as f:
            json.dump(data, f, indent=2)
    except: pass

def load_config():
    global key_bindings, hotkey_bindings, game_key_bindings, board_key_bindings
    global turbo_enabled, turbo_period, favorites, game_dip_settings, _XI_BTN_MAP
    if not os.path.exists(CONFIG_FILE): return
    try:
        with open(CONFIG_FILE) as f:
            data = json.load(f)
        for k, v in data.get("global_bindings", {}).items():
            if k in key_bindings: key_bindings[k] = v
        for k, v in data.get("hotkey_bindings", {}).items():
            if k in hotkey_bindings: hotkey_bindings[k] = v
        game_key_bindings.update(data.get("game_bindings", {}))
        board_key_bindings.update(data.get("board_bindings", {}))
        for k, v in data.get("turbo_enabled", {}).items():
            if k in turbo_enabled: turbo_enabled[k] = bool(v)
        turbo_period = int(data.get("turbo_period", turbo_period))
        favorites[:] = [str(g) for g in data.get("favorites", [])]
        game_dip_settings.update(data.get("game_dip_settings", {}))
        # 게임패드 Linux 버튼 매핑 복원
        for k, v in data.get("gamepad_linux_map", {}).items():
            try: LinuxGamepad._BTN_MAP[int(k)] = str(v)
            except: pass
        # 게임패드 XInput 매핑 복원 (저장 형식: {action: xinput_mask_int})
        saved_xi = data.get("gamepad_xi_map", {})
        if saved_xi:
            try:
                _XI_BTN_MAP.clear()
                _XI_BTN_MAP.update({str(k): int(v) for k, v in saved_xi.items()})
            except: pass
        for attr, v in data.get("settings", {}).items():
            if hasattr(settings, attr): setattr(settings, attr, v)
    except: pass
    _load_names_json()                              # names.json 우선순위 1
    load_gamelist_file(settings.rom_path)           # gamelist.xml/txt 우선순위 2

# ════════════════════════════════════════════════════════════
#  XInput 게임패드 (Windows)
# ════════════════════════════════════════════════════════════
class XINPUT_GAMEPAD(Structure):
    _fields_ = [('wButtons',c_ushort),('bLeftTrigger',c_ubyte),
                ('bRightTrigger',c_ubyte),('sThumbLX',c_short),
                ('sThumbLY',c_short),('sThumbRX',c_short),('sThumbRY',c_short)]
class XINPUT_STATE(Structure):
    _fields_ = [('dwPacketNumber',c_ulong),('Gamepad',XINPUT_GAMEPAD)]

XI_DPAD_UP=0x0001; XI_DPAD_DOWN=0x0002; XI_DPAD_LEFT=0x0004; XI_DPAD_RIGHT=0x0008
XI_START=0x0010; XI_BACK=0x0020
XI_A=0x1000; XI_B=0x2000; XI_X=0x4000; XI_Y=0x8000
XI_LB=0x0100; XI_RB=0x0200
DEADZONE = 8000

try:
    _xi = ctypes.WinDLL("xinput1_4")
    _xi.XInputGetState.argtypes = [c_ulong, POINTER(XINPUT_STATE)]
    _xi.XInputGetState.restype  = c_ulong
    XINPUT_OK = True
except: XINPUT_OK = False

# XInput: South(A)=강손, East(B)=강발, West(X)=약손, North(Y)=약발
# 실제 FBNeo libretro: B(0)=약손, Y(1)=강손, A(8)=약발, X(9)=강발
# → A버튼(강손)=action'b', B버튼(강발)=action'd', X버튼(약손)=action'a', Y버튼(약발)=action'c'
_XI_BTN_MAP = {
    'b': XI_A, 'd': XI_B, 'a': XI_X, 'c': XI_Y,
    'e': XI_LB, 'f': XI_RB, 'start': XI_START, 'coin': XI_BACK,
}

def _poll_xinput() -> dict:
    if not XINPUT_OK: return {}
    xi = XINPUT_STATE()
    if _xi.XInputGetState(0, byref(xi)) != 0: return {}
    btn = xi.Gamepad.wButtons
    lx, ly = xi.Gamepad.sThumbLX, xi.Gamepad.sThumbLY
    r = {a: 1 if (btn & m) else 0 for a, m in _XI_BTN_MAP.items()}
    r['up']    = 1 if (btn & XI_DPAD_UP)    or ly >  DEADZONE else 0
    r['down']  = 1 if (btn & XI_DPAD_DOWN)  or ly < -DEADZONE else 0
    r['left']  = 1 if (btn & XI_DPAD_LEFT)  or lx < -DEADZONE else 0
    r['right'] = 1 if (btn & XI_DPAD_RIGHT) or lx >  DEADZONE else 0
    return r

# ════════════════════════════════════════════════════════════
#  Linux 게임패드 (/dev/input/js0)  — Steam Deck 포함
# ════════════════════════════════════════════════════════════
class LinuxGamepad:
    """Linux joystick API — non-blocking /dev/input/js0"""
    JS_BTN  = 0x01
    JS_AXIS = 0x02
    JS_INIT = 0x80
    _STRUCT = struct.Struct('<IhBB')   # time(u32) value(s16) type(u8) number(u8)

    # 표준 XInput 호환 Linux 버튼 (xpad/SDL2 드라이버)
    # 0=South(A/×)  1=East(B/○)  2=West(X/□)  3=North(Y/△)
    # 4=LB  5=RB  6=Back/Select  7=Start
    # A(0)=강손, B(1)=강발, X(2)=약손, Y(3)=약발
    _BTN_MAP = {0:'b', 1:'d', 2:'a', 3:'c', 4:'e', 5:'f', 6:'coin', 7:'start'}
    _DZ = 20000

    def __init__(self, dev='/dev/input/js0'):
        self._fd   = None
        self._btns: dict = {}
        self._axes: dict = {}
        try:
            import fcntl
            fd = open(dev, 'rb', buffering=0)
            fcntl.fcntl(fd, fcntl.F_SETFL, os.O_NONBLOCK)
            self._fd = fd
        except: pass

    @property
    def available(self): return self._fd is not None

    def poll(self) -> dict:
        if not self._fd: return {}
        try:
            while True:
                raw = self._fd.read(8)
                if not raw or len(raw) < 8: break
                _t, value, etype, number = self._STRUCT.unpack(raw)
                etype &= ~self.JS_INIT
                if   etype == self.JS_BTN:  self._btns[number] = bool(value)
                elif etype == self.JS_AXIS: self._axes[number] = value
        except BlockingIOError: pass
        except Exception: pass
        return self._to_actions()

    def _to_actions(self) -> dict:
        r = {a: (1 if self._btns.get(n, False) else 0) for n, a in self._BTN_MAP.items()}
        ax  = self._axes.get(0, 0)   # LStick X
        ay  = self._axes.get(1, 0)   # LStick Y
        dpx = self._axes.get(6, 0)   # DPad X
        dpy = self._axes.get(7, 0)   # DPad Y
        dz  = self._DZ
        r['left']  = 1 if dpx < -dz//2 or ax < -dz else 0
        r['right'] = 1 if dpx >  dz//2 or ax >  dz else 0
        r['up']    = 1 if dpy < -dz//2 or ay < -dz else 0
        r['down']  = 1 if dpy >  dz//2 or ay >  dz else 0
        return r

_linux_gp = LinuxGamepad() if IS_LINUX else None

def poll_gamepad() -> dict:
    if IS_WINDOWS: return _poll_xinput()
    if IS_LINUX and _linux_gp: return _linux_gp.poll()
    return {}

GAMEPAD_OK = (XINPUT_OK if IS_WINDOWS else
              (bool(_linux_gp and _linux_gp.available) if IS_LINUX else False))

# ════════════════════════════════════════════════════════════
#  에뮬레이터 상태
# ════════════════════════════════════════════════════════════
class EmulatorState:
    video_buffer = None
    width = 320; height = 224; pitch = 0
    pixel_format = 0
    keys      = [0] * 16   # P1 입력
    p2_keys   = [0] * 16   # P2 입력 (넷플레이 클라이언트/2P 로컬)
    is_paused = False
    game_loaded = False
    save_slot = 0
    fast_forward = False
    dip_variables: dict = {}
    # 녹화
    is_recording:    bool      = False
    record_audio_buf: bytearray = bytearray()  # 프레임 간 오디오 누적 버퍼
    # 터보 (게임 루프에서 사용)
    turbo_held:   set  = set()   # 터보 중인 libretro 버튼 인덱스
    _turbo_ticks: dict = {}      # {idx: frame_counter}
    # 키보드 누름 상태 (게임패드 폴링이 덮어쓰지 않도록 분리 관리)
    kb_held:      set  = set()   # 현재 눌린 keyboard key의 libretro 인덱스
    # 오디오 버퍼 (C 콜백 → _emu_loop에서 QAudioSink로 push)
    audio_pending: bytearray = bytearray()
    # 프레임 카운터 (쉐이더 FrameCount 용)
    frame_count:      int  = 0
    # 게임 로드 시 frame_count 스냅샷 — 치트 딜레이 계산용
    game_load_frame:  int  = 0
    # 활성 치트 목록 — 매 프레임 RAM 패치 재적용 [(cpu_addr, value), ...]
    active_cheats:    list = []
    # 코어 실제 FPS (게임 로드 시 retro_get_system_av_info로 읽음)
    core_fps:         float = 60.0
    # DRC PID 제어기 (게임 로드 시 reset)
    drc_pid:          object = None
    # DRC 위상 연속성 리샘플러 (게임 로드 시 reset)
    drc_resampler:    object = None
    # bytesFree() 5프레임 이동 평균 필터
    drc_free_avg:     object = None

state = EmulatorState()
audio_sink = None   # QAudioSink
audio_io   = None   # QAudioSink.start() 반환 QIODevice (push 모드)

# ════════════════════════════════════════════════════════════
#  넷플레이 (LAN P2P, Delay-Based)
#  Host = P1 로컬 + P2 원격  /  Client = P2 로컬 + P1 원격
# ════════════════════════════════════════════════════════════
import socket as _socket
import threading as _threading

class NetplayManager:
    """지연 기반 P2P 넷플레이 (TCP, INPUT_DELAY 프레임 딜레이)"""
    INPUT_DELAY    = 3       # 입력 딜레이 프레임 수
    DEFAULT_PORT   = 7845
    CONNECT_TIMEOUT = 10.0  # 접속 대기 타임아웃(초)
    RECV_TIMEOUT   = 0.12   # 프레임당 원격 입력 수신 최대 대기(초) ≒ 2프레임

    def __init__(self):
        self.active      = False
        self.is_host     = False
        self._sock       = None    # 연결된 소켓 (양방향)
        self._srv_sock   = None    # 호스트만 사용: 대기 소켓
        self._lock       = _threading.Lock()
        self._remote_q   = []      # 수신된 원격 입력 큐
        self._local_q    = []      # 딜레이 큐 (INPUT_DELAY 프레임 buffering)
        self._recv_th    = None

    # ── 공개 IP 조회 ──────────────────────────────────────
    @staticmethod
    def local_ip() -> str:
        try:
            s = _socket.socket(_socket.AF_INET, _socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]; s.close(); return ip
        except:
            return "127.0.0.1"

    # ── 호스트: 포트 바인드 & 비동기 클라이언트 대기 ────
    def host_listen(self, port: int = DEFAULT_PORT):
        """소켓을 바인드하고 accept()를 백그라운드에서 대기."""
        self._srv_sock = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
        self._srv_sock.setsockopt(_socket.SOL_SOCKET, _socket.SO_REUSEADDR, 1)
        self._srv_sock.bind(('', port))
        self._srv_sock.listen(1)
        self._srv_sock.settimeout(120.0)   # 2분 내 연결 없으면 포기
        th = _threading.Thread(target=self._accept_loop, daemon=True)
        th.start()

    def _accept_loop(self):
        try:
            conn, _ = self._srv_sock.accept()
            conn.setsockopt(_socket.IPPROTO_TCP, _socket.TCP_NODELAY, 1)
            with self._lock:
                self._sock = conn
                self._local_q.clear(); self._remote_q.clear()
            self.is_host = True
            self.active  = True
            self._start_recv()
            if self._on_connected:
                self._on_connected(True)   # 메인 스레드 콜백
        except Exception as e:
            if self._on_error:
                self._on_error(str(e))
        finally:
            try: self._srv_sock.close()
            except: pass

    # ── 클라이언트: 호스트에 접속 ────────────────────────
    def client_connect(self, ip: str, port: int = DEFAULT_PORT):
        def _conn():
            try:
                s = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
                s.settimeout(self.CONNECT_TIMEOUT)
                s.connect((ip, port))
                s.setsockopt(_socket.IPPROTO_TCP, _socket.TCP_NODELAY, 1)
                with self._lock:
                    self._sock = s
                    self._local_q.clear(); self._remote_q.clear()
                self.is_host = False
                self.active  = True
                self._start_recv()
                if self._on_connected:
                    self._on_connected(False)
            except Exception as e:
                if self._on_error:
                    self._on_error(str(e))
        _threading.Thread(target=_conn, daemon=True).start()

    # ── 수신 스레드 ───────────────────────────────────────
    def _start_recv(self):
        self._recv_th = _threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_th.start()

    def _recv_loop(self):
        while self.active:
            data = self._recv_exact(2)
            if data is None: break
            bits = struct.unpack('<H', data)[0]
            with self._lock:
                self._remote_q.append(bits)
        self.active = False
        if self._on_disconnected:
            self._on_disconnected()

    def _recv_exact(self, n: int):
        data = b''
        try:
            while len(data) < n:
                chunk = self._sock.recv(n - len(data))
                if not chunk: return None
                data += chunk
        except:
            return None
        return data

    # ── 프레임 입력 교환 (에뮬루프에서 호출) ─────────────
    def exchange(self, local_bits: int):
        """
        로컬 입력을 보내고 원격 입력을 받는다.
        Returns (my_bits, remote_bits) — 딜레이 적용 후 실제 사용할 값.
        """
        # 딜레이 큐에 현재 입력 넣기
        self._local_q.append(local_bits)

        # INPUT_DELAY 프레임이 쌓이기 전에는 0 전송
        delayed = self._local_q.pop(0) if len(self._local_q) > self.INPUT_DELAY else 0

        # 지연된 로컬 입력 전송
        try:
            with self._lock:
                sock = self._sock
            if sock:
                sock.sendall(struct.pack('<H', delayed))
        except:
            self.active = False
            return delayed, 0

        # 원격 입력 수신 대기 (타임아웃 100ms)
        deadline = time.perf_counter() + self.RECV_TIMEOUT
        while self.active:
            with self._lock:
                if self._remote_q:
                    return delayed, self._remote_q.pop(0)
            if time.perf_counter() > deadline:
                return delayed, 0
            time.sleep(0.001)
        return delayed, 0

    # ── 종료 ─────────────────────────────────────────────
    def stop(self):
        self.active = False
        for s in [self._sock, self._srv_sock]:
            if s:
                try: s.close()
                except: pass
        self._sock = self._srv_sock = None
        self._local_q.clear(); self._remote_q.clear()
        for i in range(16): state.p2_keys[i] = 0

    # 콜백 (메인 스레드로 Signal emit 용도 — NeoRageXApp에서 연결)
    _on_connected    = None   # callable(is_host: bool)
    _on_disconnected = None   # callable()
    _on_error        = None   # callable(msg: str)

netplay = NetplayManager()
_dip_value_bufs: dict = {}

# ════════════════════════════════════════════════════════════
#  Libretro 구조체 & 콜백
# ════════════════════════════════════════════════════════════
class RetroGameInfo(Structure):
    _fields_ = [("path",c_char_p),("data",c_void_p),("size",c_size_t),("meta",c_char_p)]

class RetroGameGeometry(Structure):
    _fields_ = [("base_width",c_uint),("base_height",c_uint),
                ("max_width",c_uint),("max_height",c_uint),("aspect_ratio",c_float)]

class RetroSystemTiming(Structure):
    _fields_ = [("fps",c_double),("sample_rate",c_double)]

class RetroSystemAVInfo(Structure):
    _fields_ = [("geometry",RetroGameGeometry),("timing",RetroSystemTiming)]

# ════════════════════════════════════════════════════════════
#  DRC — PID 기반 Dynamic Rate Control + Cubic 리샘플링
#  Windows WASAPI / Steam Deck PipeWire(QAudioSink) 공통
# ════════════════════════════════════════════════════════════

# Fixed chunk size: QAudioSink에 항상 이 단위로 write
# 512 samples × 4 bytes(int16 stereo) = 2048 bytes ≈ 11.6ms @44100
DRC_CHUNK_SAMPLES = 512
DRC_CHUNK_BYTES   = DRC_CHUNK_SAMPLES * 4

class DrcPid:
    """
    PID 제어기 기반 DRC 리샘플링 비율 계산.

    P: 현재 오차에 즉각 반응
    I: 누적 오차 — 지속적 드리프트(예: 59.18Hz vs 60Hz) 흡수
    D: 오차 변화율 — 급격한 변동 억제, 부드러운 수렴

    출력 범위: 1.0 ± max_adjust (기본 ±0.5%)
    사람이 음정 변화를 인지하려면 약 ±3% 이상이므로 완전히 안전.
    """
    def __init__(self, kp=0.04, ki=0.0002, kd=0.008, max_adj=0.005):
        self.kp      = kp
        self.ki      = ki
        self.kd      = kd
        self.max_adj = max_adj
        self._integral   = 0.0
        self._prev_error = 0.0

    def reset(self):
        self._integral   = 0.0
        self._prev_error = 0.0

    def update(self, pending_bytes: int, target_bytes: int) -> float:
        if target_bytes <= 0:
            return 1.0

        fill  = pending_bytes / target_bytes   # 0.0=비어있음, 1.0=목표, 2.0=2배 초과
        error = fill - 0.5                     # 목표: 50% 점유 유지

        # Steam Deck(Linux) 언더런 하한선 강화:
        # 버퍼 50% 이하일 때만 감속, 이상일 때는 P+I+D 전체 동작
        if IS_LINUX and fill < 0.5:
            # 언더런 위험 — 속도 감속만 (D 생략, 급격한 반응 방지)
            adj = max(-self.max_adj, error * self.kp)
            return 1.0 + adj

        # P
        p = self.kp * error
        # I (적분 와인드업 방지: ±max_adj 범위로 클램프)
        self._integral += error
        self._integral  = max(-self.max_adj / self.ki,
                              min( self.max_adj / self.ki, self._integral))
        i = self.ki * self._integral
        # D
        d = self.kd * (error - self._prev_error)
        self._prev_error = error

        adj = p + i + d
        adj = max(-self.max_adj, min(self.max_adj, adj))
        return 1.0 + adj


def _set_audio_thread_priority():
    """
    현재 스레드(오디오 write 담당)에 OS 높은 우선순위 부여.
    Windows : SetThreadPriority HIGHEST(2)
    Linux   : os.nice(-5) — 권한 불필요, SteamOS 안전
    """
    try:
        if IS_WINDOWS:
            import ctypes as _c
            _h = _c.windll.kernel32.GetCurrentThread()
            _c.windll.kernel32.SetThreadPriority(_h, 2)
        else:
            import os as _o
            try: _o.nice(-5)
            except: pass
    except Exception:
        pass


class FractionalResampler:
    """
    위상 연속성 보장 분수 리샘플러 (Catmull-Rom Cubic, 상태 유지).

    매 프레임 _phase(위상)를 보존 → 프레임 경계 글리치 원천 차단.

    ratio 1.001 → 입력 735개에서 출력 736개 (버퍼 초과 시 빠른 소비)
    ratio 0.999 → 입력 735개에서 출력 734개 (버퍼 부족 시 느린 소비)

    단순 linspace 방식(매 프레임 초기화)과 달리
    위상 누산기(phase accumulator)로 소수점 드리프트를 프레임 간 이어받음.
    """
    def __init__(self):
        self.reset()

    def reset(self):
        self._phase = 0.0   # 다음 프레임 시작 위치 (입력 좌표 소수점)
        self._tail  = None  # 이전 프레임 마지막 3샘플 (경계 Cubic 보간용)

    def process(self, data_bytes: bytes, ratio: float) -> bytes:
        if np is None:
            return data_bytes

        src  = np.frombuffer(data_bytes, dtype=np.int16).reshape(-1, 2).astype(np.float32)
        n_in = len(src)
        if n_in == 0:
            return data_bytes

        # 보정 불필요 구간 → 위상/꼬리만 갱신
        if abs(ratio - 1.0) < 0.0002:
            self._tail = src[-3:].copy() if n_in >= 3 else src.copy()
            return data_bytes

        # 이전 프레임 꼬리 앞에 붙여 경계 Cubic 보간 가능하게 함
        if self._tail is not None:
            ext    = np.concatenate([self._tail, src])
            n_tail = len(self._tail)
        else:
            pad    = np.tile(src[:1], (3, 1))   # 첫 샘플로 패드
            ext    = np.concatenate([pad, src])
            n_tail = 3
        n_ext = len(ext)

        # 위상 누산기: 출력 1개당 입력 1/ratio 소비
        step     = 1.0 / ratio
        # 출력 위치 배열 (src 좌표계: 0 ~ n_in)
        n_out_est = max(1, int((n_in - self._phase) / step) + 2)
        pos_src   = self._phase + np.arange(n_out_est, dtype=np.float64) * step
        pos_src   = pos_src[pos_src < n_in]   # n_in 미만만 유효

        if len(pos_src) == 0:
            # 이 프레임에서 생산할 샘플 없음 (위상 초과 — 극히 희귀)
            self._phase -= n_in
            self._tail   = src[-3:].copy() if n_in >= 3 else src.copy()
            return b'\x00' * DRC_CHUNK_BYTES

        # ext 좌표계로 변환 후 Catmull-Rom 인덱스
        pos = np.clip(pos_src + n_tail, 0.0, n_ext - 1.001)
        i1  = pos.astype(np.int32)
        i0  = np.maximum(i1 - 1, 0)
        i2  = np.minimum(i1 + 1, n_ext - 1)
        i3  = np.minimum(i1 + 2, n_ext - 1)
        t   = (pos - i1)[:, np.newaxis].astype(np.float32)

        p0, p1, p2, p3 = ext[i0], ext[i1], ext[i2], ext[i3]
        out = 0.5 * (
            (2.0*p1) + (-p0+p2)*t +
            (2.0*p0 - 5.0*p1 + 4.0*p2 - p3)*t*t +
            (-p0 + 3.0*p1 - 3.0*p2 + p3)*t*t*t
        )

        # 다음 프레임 위상: 마지막 출력 다음 위치 - n_in
        self._phase = (pos_src[-1] + step) - n_in
        self._tail  = src[-3:].copy() if n_in >= 3 else src.copy()

        # np.rint() → 반올림(truncation 아님) → DC 오프셋 드리프트 방지
        return np.rint(out).clip(-32768, 32767).astype(np.int16).tobytes()


class _MovingAvg:
    """
    N점 이동 평균 필터.
    QAudioSink.bytesFree()의 순간 이상값(스파이크) 방어.
    최근 5프레임 평균 → 급격한 수치 변동으로 인한 오판 차단.
    """
    __slots__ = ('_buf', '_n')
    def __init__(self, n: int = 5):
        self._n   = n
        self._buf = []
    def update(self, v: int) -> float:
        self._buf.append(v)
        if len(self._buf) > self._n:
            self._buf.pop(0)
        return sum(self._buf) / len(self._buf)
    def reset(self):
        self._buf = []


class RetroVariable(Structure):
    _fields_ = [("key",c_char_p),("value",c_char_p)]

def _parse_retro_variables(ptr):
    """RETRO_ENVIRONMENT_SET_VARIABLES / SET_CORE_OPTIONS 파싱 공통 함수"""
    i = 0
    while True:
        try:
            if not ptr[i].key: break
            key = ptr[i].key.decode('utf-8', 'replace')
        except:
            break
        try:
            raw = ptr[i].value.decode('utf-8', 'replace') if ptr[i].value else ""
            if ';' in raw:
                desc, opts_str = raw.split(';', 1)
                opts = [o.strip() for o in opts_str.split('|') if o.strip()]
                if opts:
                    cur = state.dip_variables.get(key, (None, None, opts[0]))[2]
                    if cur not in opts:
                        cur = opts[0]
                    state.dip_variables[key] = (desc.strip(), opts, cur)
        except: pass
        i += 1

class _CoreOptionValue(Structure):
    _fields_ = [('value', c_char_p), ('label', c_char_p)]

class _CoreOptionDef(Structure):
    # key, desc, info, values[128], default_value
    _fields_ = [('key', c_char_p), ('desc', c_char_p), ('info', c_char_p),
                ('values', _CoreOptionValue * 128), ('default_value', c_char_p)]

def env_cb(cmd, data):
    """libretro 환경 콜백"""
    global _dip_value_bufs
    try:
        # ── SET_PIXEL_FORMAT (10) ────────────────────────────
        if cmd == 10 and data:
            state.pixel_format = cast(data, POINTER(c_int)).contents.value
            return True

        # ── SET_VARIABLES: 구버전=9, 신버전=16 ──────────────
        if cmd in (9, 16) and data:
            try:
                _parse_retro_variables(cast(data, POINTER(RetroVariable)))
            except: pass
            return True

        # ── GET_VARIABLE: 구버전=4, 신버전=15 ───────────────
        if cmd in (4, 15) and data:
            try:
                ptr = cast(data, POINTER(RetroVariable))
                key = ptr[0].key.decode('utf-8', 'replace') if ptr[0].key else ""
                if key in state.dip_variables:
                    cur = state.dip_variables[key][2]
                    buf = cur.encode('utf-8') + b'\x00'
                    _dip_value_bufs[key] = buf
                    ptr[0].value = cast(c_char_p(buf), c_char_p).value
                    return True
            except: pass
            return False

        # ── GET_VARIABLE_UPDATE (17) ─────────────────────────
        if cmd == 17 and data:
            try:
                cast(data, POINTER(c_bool)).contents.value = False
            except: pass
            return True

        # ── SET_CORE_OPTIONS (52/67) ─────────────────────────
        # 구조체 레이아웃이 플랫폼마다 달라 직접 파싱 안 함
        if cmd in (52, 67):
            return True

        # ── GET_OVERSCAN (2) ─────────────────────────────────
        if cmd == 2 and data:
            try: cast(data, POINTER(c_bool)).contents.value = False
            except: pass
            return True

        # ── GET_CAN_DUPE (8) ─────────────────────────────────
        if cmd == 8 and data:
            try: cast(data, POINTER(c_bool)).contents.value = True
            except: pass
            return True

    except: pass
    return False

def video_cb(data, width, height, pitch):
    if data and data != 0:
        try:
            state.width, state.height, state.pitch = width, height, pitch
            state.video_buffer = string_at(data, pitch * height)
        except: pass

def audio_batch_cb(data, frames):
    # C 콜백: retro_run() 안에서 호출됨 (메인 스레드)
    if data:
        try:
            buf = string_at(data, frames * 4)
            state.audio_pending.extend(buf)
            if state.is_recording:
                state.record_audio_buf.extend(bytes(buf))
        except: pass
    return frames

def input_state_cb(port, device, index, id):
    if id < 16:
        if port == 0: return state.keys[id]
        if port == 1: return state.p2_keys[id]
    return 0

ENV_CB   = CFUNCTYPE(c_bool,   c_uint, c_void_p)
VIDEO_CB = CFUNCTYPE(None,     c_void_p, c_uint, c_uint, c_size_t)
AUDIO_CB = CFUNCTYPE(None,     c_int16, c_int16)
BATCH_CB = CFUNCTYPE(c_size_t, POINTER(c_int16), c_size_t)
POLL_CB  = CFUNCTYPE(None)
STATE_CB = CFUNCTYPE(c_int16,  c_uint, c_uint, c_uint, c_uint)

# ════════════════════════════════════════════════════════════
#  OpenGL 게임 캔버스
# ════════════════════════════════════════════════════════════
class GameCanvas(QOpenGLWidget):
    # GL 진단 메시지 → 메인 윈도우 이벤트 패널로 전달
    gl_log_signal = Signal(str)

    # ── 기본 쉐이더 (쉐이더 파일 없을 때 사용) ──────────────
    # RetroArch gl2 표준 패스스루 vertex 쉐이더
    # VertexCoord 는 [0,1] 공간, MVPMatrix 는 ortho(0,1,0,1,-1,1)
    _DEFAULT_VERT = """
#version 120
attribute vec4 VertexCoord;
attribute vec4 TexCoord;
varying   vec2 vTexCoord;
uniform   mat4 MVPMatrix;
void main() {
    vTexCoord   = TexCoord.xy;
    gl_Position = MVPMatrix * VertexCoord;
}
"""
    _DEFAULT_FRAG = """
#version 120
uniform sampler2D Texture;
varying vec2 vTexCoord;
void main() {
    gl_FragColor = texture2D(Texture, vTexCoord);
}
"""
    _PASSTHROUGH_VERT = """
#version 110
attribute vec4 VertexCoord;
attribute vec4 TexCoord;
varying   vec2 vTexCoord;
uniform   mat4 MVPMatrix;
void main() {
    vTexCoord   = TexCoord.xy;
    gl_Position = MVPMatrix * VertexCoord;
}
"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._tex  = None
        self._prog: QOpenGLShaderProgram | None = None
        self._shader_path_loaded = ""
        self._shader_error       = ""
        # RetroArch 방식 렌더링용 오브젝트 (0 = 미초기화)
        self._vao     = 0   # Vertex Array Object
        self._vbo_pos = 0   # 위치 VBO — 매 프레임 갱신 (GL_DYNAMIC_DRAW)
        self._vbo_uv  = 0   # UV VBO   — 고정 (GL_STATIC_DRAW)
        # 쉐이더 링크 후 조회한 uniform 타입 맵 {name: GL_type_enum}
        # vec2 / vec4 자동 판별에 사용 (RetroArch 구형 쉐이더는 vec2 사용)
        self._uniform_types: dict = {}
        # GL 에러를 매 프레임 반복 로그하지 않도록 방지 플래그
        self._shader_gl_error_logged = False

    def initializeGL(self):
        glClearColor(0.0, 0.0, 0.0, 1.0)
        self._tex = self._gl_id(glGenTextures(1))   # numpy array → int 안전 변환
        try:
            self._init_vbo()
        except Exception as _e:
            print(f"[GameCanvas] VBO/VAO init failed: {_e}")
        if settings.video_shader_path:
            self._compile_shader(settings.video_shader_path)

    @staticmethod
    def _gl_id(raw):
        """PyOpenGL glGen* 반환값을 Python int 로 안전하게 변환.
        버전에 따라 numpy array, scalar, ctypes 등 다양하게 반환된다."""
        if hasattr(raw, 'flat'):     # numpy array
            return int(raw.flat[0])
        if hasattr(raw, '__len__'):  # list / tuple
            return int(raw[0])
        return int(raw)              # scalar

    def _init_vbo(self):
        """RetroArch gl2 backend 방식의 quad VAO/VBO 초기화.

        ▸ attribute 위치 0 = VertexCoord (pos,  [0,1] 공간, vec4)
        ▸ attribute 위치 1 = TexCoord    (uv,   [0,1] 공간, vec4)
        ▸ GL_TRIANGLE_STRIP 순서: BL → TL → BR → TR
        ▸ UV 는 고정값 — 게임 텍스처가 0-1 을 항상 채우므로 변경 불필요
        ▸ 위치 VBO 는 매 프레임 갱신 (게임 크기/스케일 모드에 따라 변함)"""
        import numpy as np

        # ── VAO ────────────────────────────────────────────────
        self._vao = self._gl_id(glGenVertexArrays(1))
        glBindVertexArray(self._vao)

        # ── VBO pos (location 0) ────────────────────────────────
        self._vbo_pos = self._gl_id(glGenBuffers(1))
        glBindBuffer(GL_ARRAY_BUFFER, self._vbo_pos)
        glBufferData(GL_ARRAY_BUFFER, np.zeros(16, dtype=np.float32).nbytes,
                     np.zeros(16, dtype=np.float32), GL_DYNAMIC_DRAW)
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, None)
        glEnableVertexAttribArray(0)

        # ── VBO uv (location 1) ─────────────────────────────────
        # RetroArch UV 배치 (TRIANGLE_STRIP: BL→TL→BR→TR):
        #   BL: UV(0,1) → screen 하단 = 게임 하단 (OpenGL row-0 = 게임 상단이므로 y-flip)
        #   TL: UV(0,0) → screen 상단 = 게임 상단
        #   BR: UV(1,1), TR: UV(1,0)
        self._vbo_uv = self._gl_id(glGenBuffers(1))
        uv = np.array([
            0.0, 1.0, 0.0, 0.0,   # BL
            0.0, 0.0, 0.0, 0.0,   # TL
            1.0, 1.0, 0.0, 0.0,   # BR
            1.0, 0.0, 0.0, 0.0,   # TR
        ], dtype=np.float32)
        glBindBuffer(GL_ARRAY_BUFFER, self._vbo_uv)
        glBufferData(GL_ARRAY_BUFFER, uv.nbytes, uv, GL_STATIC_DRAW)
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, None)
        glEnableVertexAttribArray(1)

        glBindBuffer(GL_ARRAY_BUFFER, 0)
        glBindVertexArray(0)

    def _compile_shader(self, path: str):
        """쉐이더 프로그램 컴파일. path가 비어 있으면 기본 패스스루 사용."""
        self._shader_error = ""
        vert_src = self._DEFAULT_VERT
        frag_src = self._DEFAULT_FRAG

        if path and os.path.isfile(path):
            try:
                raw = open(path, encoding='utf-8', errors='replace').read()
                vert_src, frag_src = self._parse_glsl_file(raw)
            except Exception as e:
                self._shader_error = str(e)
                path = ""   # 오류 시 기본 쉐이더로 폴백

        prog = QOpenGLShaderProgram(self)
        ok_v = prog.addShaderFromSourceCode(QOpenGLShader.Vertex, vert_src)
        ok_f = prog.addShaderFromSourceCode(QOpenGLShader.Fragment, frag_src)
        # 링크 전에 attribute 위치를 고정 (RetroArch 표준 이름 + 기존 이름 모두 0·1 고정)
        # → VAO/VBO 가 매 프레임 attributeLocation 조회 없이 위치 0·1 을 직접 사용 가능
        for _aname in ("VertexCoord", "a_pos"):
            prog.bindAttributeLocation(_aname, 0)
        for _aname in ("TexCoord", "a_uv"):
            prog.bindAttributeLocation(_aname, 1)
        prog.bindAttributeLocation("COLOR", 2)
        ok_l = prog.link()
        if ok_v and ok_f and ok_l:
            if self._prog:
                self._prog.release()
                self._prog = None
            self._prog = prog
            self._shader_path_loaded = path
            self._shader_gl_error_logged = False
            # ── 링크 후 uniform 타입 조회 ─────────────────────────
            # 일부 RetroArch 구형 쉐이더(crt-pi 등)는 TextureSize/InputSize/OutputSize
            # 를 vec4 가 아닌 vec2 로 선언한다.  잘못된 타입으로 setUniformValue 를
            # 호출하면 GL_INVALID_OPERATION → 유니폼이 0 으로 유지 → NaN 계산 → 검정 화면.
            # glGetActiveUniform 으로 타입을 조회해 두고 _paint_shader 에서 참조한다.
            self._uniform_types = {}
            try:
                from OpenGL.GL import glGetProgramiv, glGetActiveUniform, GL_ACTIVE_UNIFORMS
                pid = prog.programId()
                n = glGetProgramiv(pid, GL_ACTIVE_UNIFORMS)
                n = int(n.flat[0]) if hasattr(n, 'flat') else int(n)
                for i in range(n):
                    uname, usize, utype = glGetActiveUniform(pid, i)
                    if isinstance(uname, bytes):
                        uname = uname.decode('utf-8', errors='replace')
                    self._uniform_types[uname] = int(utype)
            except Exception as _ue:
                pass   # 구형 PyOpenGL 미지원 시 무시
            # 로그: 쉐이더 파일명 + 검출된 주요 유니폼 타입
            label = os.path.basename(path) if path else "기본 패스스루"
            GL_FLOAT_VEC2 = 0x8B50
            vec2_names = [k for k, v in self._uniform_types.items()
                          if v == GL_FLOAT_VEC2 and k in ("TextureSize","InputSize","OutputSize")]
            detail = f" [vec2: {','.join(vec2_names)}]" if vec2_names else ""
            self.gl_log_signal.emit(f"[쉐이더] {label} 컴파일 성공{detail}")
        else:
            self._shader_error = prog.log() or "쉐이더 링크 실패"
            self.gl_log_signal.emit(f"[쉐이더 오류] {self._shader_error[:120]}")
            # 기본 쉐이더로 폴백 (이미 기본 사용 중이 아닌 경우)
            if path:
                self._compile_shader("")

    def _parse_glsl_file(self, src: str):
        """RetroArch 단일 파일 GLSL 쉐이더 파싱.

        지원 방식 1 — #if defined(VERTEX) / #elif defined(FRAGMENT) :
            #version 줄을 먼저 추출한 뒤 #define VERTEX/FRAGMENT 를 삽입.
            GLSL 규격상 #version 은 반드시 소스 첫 줄이어야 하므로
            #define 을 앞에 붙이면 컴파일 오류가 난다 → 분리해서 처리.

        지원 방식 2 — #pragma stage vertex / #pragma stage fragment :
            구분자 이전 헤더(#version, 공통 선언)를 vertex/fragment 양쪽에 복사.
            헤더를 fragment 에만 넣던 기존 방식은 vertex 에서 varying 미선언
            문제가 생기므로 양쪽에 모두 포함.

        둘 다 없으면 전체를 fragment 로, 기본 passthrough vertex 사용."""

        # ── 방식 1: #if defined(VERTEX/FRAGMENT) ──────────────
        if "#if defined(VERTEX)" in src or "#ifdef VERTEX" in src:
            # #version 줄 추출 (없으면 빈 문자열)
            ver_line = ""
            rest = src
            for line in src.splitlines(True):
                if line.strip().startswith("#version"):
                    ver_line = line
                    rest = src.replace(line, "", 1)
                    break
            vert_src = ver_line + "#define VERTEX\n"   + rest
            frag_src = ver_line + "#define FRAGMENT\n" + rest
            return vert_src, frag_src

        # ── 방식 2: #pragma stage ─────────────────────────────
        lines = src.splitlines(keepends=True)
        header_lines, vert_lines, frag_lines = [], [], []
        current = "header"
        has_pragma = False
        for line in lines:
            stripped = line.strip().lower()
            if stripped.startswith("#pragma stage vertex"):
                current = "vertex";   has_pragma = True; continue
            elif stripped.startswith("#pragma stage fragment"):
                current = "fragment"; has_pragma = True; continue
            if current == "header":   header_lines.append(line)
            elif current == "vertex": vert_lines.append(line)
            else:                     frag_lines.append(line)

        if not has_pragma:
            # fragment-only: passthrough vertex + 전체 소스를 fragment 로
            return self._PASSTHROUGH_VERT, src

        header = "".join(header_lines)
        # 헤더(#version + 공통 varying 선언)를 vertex/fragment 양쪽에 복사
        vert_src = (header + "".join(vert_lines)) if vert_lines else self._PASSTHROUGH_VERT
        frag_src = (header + "".join(frag_lines)) if frag_lines else self._DEFAULT_FRAG
        return vert_src, frag_src

    def reload_shader(self):
        """설정이 바뀌었을 때 메인 루프에서 호출."""
        if self._tex is None: return   # GL 초기화 전
        self.makeCurrent()
        self._compile_shader(settings.video_shader_path)
        self.doneCurrent()

    def _upload(self):
        if not state.video_buffer: return
        glBindTexture(GL_TEXTURE_2D, self._tex)
        filt = GL_LINEAR if settings.video_smooth else GL_NEAREST
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)
        w, h = state.width, state.height
        if state.pixel_format == 1:
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_BGRA,GL_UNSIGNED_BYTE,state.video_buffer)
        elif state.pixel_format == 2:
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,w,h,0,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,state.video_buffer)
        else:
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB5_A1,w,h,0,GL_BGRA,GL_UNSIGNED_SHORT_1_5_5_5_REV,state.video_buffer)

    def _calc_rect(self):
        W, H = self.width(), self.height()
        sw, sh = state.width, state.height
        mode = settings.video_scale_mode
        if mode == "Integer":
            s = max(1, min(W // sw, H // sh)); dw, dh = sw*s, sh*s
        elif mode == "Aspect":
            ratio = sw/sh
            if W/H > ratio: dw, dh = int(H*ratio), H
            else:           dw, dh = W, int(W/ratio)
        else: dw, dh = W, H
        ox=(W-dw)/2; oy=(H-dh)/2
        x0=ox/W*2-1; y0=oy/H*2-1; x1=(ox+dw)/W*2-1; y1=(oy+dh)/H*2-1
        return x0,y0,x1,y1,dh

    def paintGL(self):
        if not state.video_buffer: return
        glClear(GL_COLOR_BUFFER_BIT)
        self._upload()
        x0,y0,x1,y1,dh = self._calc_rect()

        # 셰이더 파일이 지정된 경우에만 셰이더 파이프라인 사용
        # 기본(셰이더 없음)은 항상 고정 기능 파이프라인 사용
        use_shader = (settings.video_shader_path
                      and self._prog and self._prog.isLinked()
                      and self._shader_path_loaded)
        if use_shader:
            try:
                self._paint_shader(x0,y0,x1,y1)
            except Exception:
                use_shader = False

        if not use_shader:
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, self._tex)
            glColor4f(1,1,1,1); glBegin(GL_QUADS)
            glTexCoord2f(0,0); glVertex2f(x0,y1)
            glTexCoord2f(1,0); glVertex2f(x1,y1)
            glTexCoord2f(1,1); glVertex2f(x1,y0)
            glTexCoord2f(0,1); glVertex2f(x0,y0)
            glEnd(); glDisable(GL_TEXTURE_2D)

        if settings.video_crt_mode: self._draw_scanlines(x0,y0,x1,y1,dh)

    def _paint_shader(self, x0, y0, x1, y1):
        """RetroArch gl2 backend 방식 쉐이더 렌더링.

        좌표계:
          ▸ VertexCoord (loc=0): [0,1] x [0,1] 정규화 공간
            NDC (x0,y0)-(x1,y1) → (vx0,vy0)-(vx1,vy1) 변환
          ▸ MVPMatrix: ortho(0,1, 0,1, -1,1) — [0,1]→NDC
          ▸ TexCoord   (loc=1): [0,1] 고정 UV (VAO 에 사전 설정)
          ▸ 그리기: GL_TRIANGLE_STRIP, 4 vertices (BL→TL→BR→TR)

        VAO 가 attribute 상태를 저장하므로 매 프레임 attrib 재설정 불필요.
        위치 VBO 만 매 프레임 갱신.

        ★ vec2/vec4 uniform 자동 판별:
          crt-pi 등 구형 RetroArch 쉐이더는 TextureSize/InputSize/OutputSize 를
          vec2 로 선언한다.  vec4 로 setUniformValue 하면 GL_INVALID_OPERATION →
          유니폼이 기본값 0 으로 유지 → texcoord 계산에서 0 나누기(NaN) → 검정 화면.
          _compile_shader 에서 미리 조회한 _uniform_types 를 참조해 올바른 타입 사용."""
        import numpy as np
        from PySide6.QtGui import QMatrix4x4, QVector4D
        from OpenGL.GL import glUniform1i, glUniform2f, glUniform4f

        if not self._vao:
            raise RuntimeError("shader VAO not initialized")

        # ── GL 렌더링 상태 초기화 (이전 고정 파이프라인 상태 잔류 방지) ──
        glDisable(GL_BLEND)
        glDisable(GL_CULL_FACE)
        glDisable(GL_DEPTH_TEST)
        glDisable(GL_TEXTURE_2D)   # 고정 기능 텍스처 유닛 OFF (프로그래머블 유닛 별도)
        glDepthMask(GL_FALSE)

        # ── NDC → [0,1] 변환 (RetroArch vertex 공간) ──────────
        vx0 = (x0 + 1.0) * 0.5
        vx1 = (x1 + 1.0) * 0.5
        vy0 = (y0 + 1.0) * 0.5
        vy1 = (y1 + 1.0) * 0.5

        # TRIANGLE_STRIP 순서: BL(vx0,vy0) → TL(vx0,vy1) → BR(vx1,vy0) → TR(vx1,vy1)
        pos = np.array([
            vx0, vy0, 0.0, 1.0,   # BL
            vx0, vy1, 0.0, 1.0,   # TL
            vx1, vy0, 0.0, 1.0,   # BR
            vx1, vy1, 0.0, 1.0,   # TR
        ], dtype=np.float32)

        # 위치 VBO 갱신 (VAO 바인드 전에 수행 — VAO 상태에 포함됨)
        glBindBuffer(GL_ARRAY_BUFFER, self._vbo_pos)
        glBufferData(GL_ARRAY_BUFFER, pos.nbytes, pos, GL_DYNAMIC_DRAW)
        glBindBuffer(GL_ARRAY_BUFFER, 0)

        self._prog.bind()
        glActiveTexture(GL_TEXTURE0)
        glBindTexture(GL_TEXTURE_2D, self._tex)

        # ── Uniforms ──────────────────────────────────────────
        tw = float(state.width);  th = float(state.height)
        ow = float(self.width()); oh = float(self.height())
        inv_tw = 1.0 / tw if tw > 0 else 0.0
        inv_th = 1.0 / th if th > 0 else 0.0
        inv_ow = 1.0 / ow if ow > 0 else 0.0
        inv_oh = 1.0 / oh if oh > 0 else 0.0

        # ── vec2/vec4 자동 판별 헬퍼 ─────────────────────────
        GL_FLOAT_VEC2 = 0x8B50
        def _su(name, a, b, c=None, d=None):
            """uniform을 vec2 또는 vec4 타입에 맞게 설정."""
            loc = self._prog.uniformLocation(name)
            if loc < 0: return
            if self._uniform_types.get(name, 0) == GL_FLOAT_VEC2:
                glUniform2f(loc, a, b)
            else:
                ic = c if c is not None else (1.0/a if a > 0 else 0.0)
                id_ = d if d is not None else (1.0/b if b > 0 else 0.0)
                glUniform4f(loc, a, b, ic, id_)

        # Sampler
        for uname in ("Texture", "u_tex"):
            loc = self._prog.uniformLocation(uname)
            if loc >= 0: glUniform1i(loc, 0)

        # MVPMatrix — RetroArch 표준: ortho(0,1, 0,1, -1,1)
        mvp = QMatrix4x4()
        mvp.ortho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0)
        loc = self._prog.uniformLocation("MVPMatrix")
        if loc >= 0: self._prog.setUniformValue(loc, mvp)

        # 크기 uniforms — vec2 / vec4 자동 판별
        _su("OutputSize",   ow, oh, inv_ow, inv_oh)
        _su("TextureSize",  tw, th, inv_tw, inv_th)
        _su("InputSize",    tw, th, inv_tw, inv_th)
        _su("u_tex_size",   tw, th)

        # FrameCount / FrameDirection / u_smooth
        loc = self._prog.uniformLocation("FrameCount")
        if loc >= 0: self._prog.setUniformValue(loc, int(state.frame_count))
        loc = self._prog.uniformLocation("FrameDirection")
        if loc >= 0: self._prog.setUniformValue(loc, 1)
        loc = self._prog.uniformLocation("u_smooth")
        if loc >= 0: self._prog.setUniformValue(loc, 1 if settings.video_smooth else 0)

        # ── Draw — VAO 가 attribute 상태 복원, TRIANGLE_STRIP ──
        glVertexAttrib4f(2, 1.0, 1.0, 1.0, 1.0)   # COLOR = 흰색 (상수)

        glBindVertexArray(self._vao)
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4)
        glBindVertexArray(0)

        self._prog.release()

        # ── GL 에러 체크 (첫 발생 시만 이벤트 패널에 로그) ───────
        err = glGetError()
        if err != GL_NO_ERROR and not self._shader_gl_error_logged:
            self._shader_gl_error_logged = True
            self.gl_log_signal.emit(f"[GL 에러] _paint_shader glGetError={err:#x}")

    def _draw_scanlines(self, x0,y0,x1,y1,dh):
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        glColor4f(0,0,0,settings.video_crt_intensity)
        line_h=(y1-y0)/max(dh,1); glBegin(GL_QUADS); y=y1
        while y>y0:
            glVertex2f(x0,y); glVertex2f(x1,y)
            glVertex2f(x1,y-line_h*0.5); glVertex2f(x0,y-line_h*0.5); y-=line_h
        glEnd(); glDisable(GL_BLEND)

# ════════════════════════════════════════════════════════════
#  RecordOverlay — 게임 화면 위 녹화 상태 표시 (녹화에 포함 안됨)
# ════════════════════════════════════════════════════════════
class RecordOverlay(QWidget):
    """게임 화면 위 투명 오버레이 — 녹화 시작/종료 시각 표시"""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WA_TransparentForMouseEvents)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self._recording = False
        self._flash_text = ""
        self._flash_alpha = 0
        self._blink_on = False

        self._blink_tmr = QTimer(self)
        self._blink_tmr.setInterval(500)
        self._blink_tmr.timeout.connect(self._do_blink)

        self._flash_tmr = QTimer(self)
        self._flash_tmr.setInterval(25)
        self._flash_tmr.timeout.connect(self._do_fade)
        self.hide()

    def show_start(self):
        self._recording = True
        self._blink_on = True
        self._blink_tmr.start()
        self._set_flash("● REC START")
        self.show(); self.raise_()

    def show_stop(self):
        self._recording = False
        self._blink_tmr.stop()
        self._set_flash("■ REC STOP")

    def _set_flash(self, text):
        self._flash_text = text
        self._flash_alpha = 255
        if not self._flash_tmr.isActive():
            self._flash_tmr.start()
        self.update()

    def _do_blink(self):
        self._blink_on = not self._blink_on; self.update()

    def _do_fade(self):
        self._flash_alpha = max(0, self._flash_alpha - 7)
        self.update()
        if self._flash_alpha <= 0:
            self._flash_tmr.stop()
            if not self._recording: self.hide()

    def paintEvent(self, event):
        if not self._recording and self._flash_alpha <= 0: return
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        W, H = self.width(), self.height()

        # 녹화 중 — 빨간 점 + "REC" (좌상단)
        if self._recording:
            a = 220 if self._blink_on else 70
            p.setPen(Qt.NoPen)
            p.setBrush(QColor(255, 0, 0, a))
            p.drawEllipse(12, 12, 14, 14)
            p.setPen(QColor(255, 60, 60, 220))
            p.setFont(QFont('Courier New', 16, QFont.Bold))
            p.drawText(32, 28, "REC")

        # 플래시 텍스트 (화면 중앙)
        if self._flash_alpha > 0:
            f = QFont('Courier New', 36, QFont.Bold)
            p.setFont(f)
            fm = QFontMetrics(f)
            tw = fm.horizontalAdvance(self._flash_text)
            x = (W - tw) // 2; y = H // 2
            p.setPen(QColor(0, 0, 0, min(255, self._flash_alpha)))
            p.drawText(x + 2, y + 2, self._flash_text)
            p.setPen(QColor(255, 230, 60, self._flash_alpha))
            p.drawText(x, y, self._flash_text)
        p.end()


# ════════════════════════════════════════════════════════════
#  LinuxVideoPlayer — Qt Multimedia 미사용 안전 영상 재생기
#  (Steam Deck / Linux: QVideoWidget + QOpenGL 충돌 방지)
# ════════════════════════════════════════════════════════════
class LinuxVideoPlayer(QWidget):
    """MJPEG AVI / GIF 프레임을 QLabel에 직접 표시 + QAudioSink로 오디오 재생
    (QVideoWidget 미사용 → Steam Deck OpenGL 충돌 없음)
    A/V 싱크: processedUSecs() 기반으로 오디오 재생 위치를 기준으로 영상 프레임 결정"""
    def __init__(self, parent=None):
        super().__init__(parent)
        self._frames:     list  = []   # list[bytes] — JPEG raw
        self._audio_data: bytes = b''  # PCM raw (stereo 16-bit)
        self._fps:  float = 30.0
        self._idx   = 0
        self._t0    = 0.0   # wall-clock 재생 시작 시각 (time.monotonic)
        self._audio_sink = None
        self._audio_buf  = None

        lbl = QLabel(self)
        lbl.setAlignment(Qt.AlignCenter)
        lbl.setStyleSheet("background:black;")
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(lbl)
        self._lbl = lbl
        self._tmr = QTimer(self)
        self._tmr.timeout.connect(self._next_frame)

    # ── 공개 API ───────────────────────────────────────────
    def load(self, path: str) -> bool:
        self.stop()
        ext = os.path.splitext(path)[1].lower()
        if ext == '.avi':
            self._frames, self._audio_data, self._fps = self._read_avi(path)
        elif ext == '.gif':
            self._frames     = self._read_gif(path)
            self._audio_data = b''
            self._fps        = 15.0
        else:
            self._frames = []; self._audio_data = b''; self._fps = 30.0
        return bool(self._frames)

    def play(self):
        if not self._frames: return
        self._idx = 0
        self._show()
        self._start_audio()          # 오디오 먼저 시작
        import time as _time
        self._t0 = _time.monotonic()   # wall-clock 기준 시작 시각
        self._tmr.start(14)            # ~70fps 폴링 → 싱크 정밀도 향상

    def stop(self):
        self._tmr.stop()
        self._frames = []; self._audio_data = b''
        self._lbl.clear()
        self._stop_audio()

    # ── 오디오 재생 (QAudioSink — Linux에서 안전) ────────────
    def _start_audio(self):
        if not self._audio_data or not AUDIO_AVAILABLE: return
        try:
            from PySide6.QtMultimedia import QAudioFormat, QAudioSink, QMediaDevices
            fmt = QAudioFormat()
            fmt.setSampleRate(settings.audio_sample_rate)
            fmt.setChannelCount(2)
            fmt.setSampleFormat(QAudioFormat.Int16)
            self._audio_buf = QBuffer()
            self._audio_buf.setData(self._audio_data)
            self._audio_buf.open(QBuffer.OpenModeFlag.ReadOnly)
            self._audio_sink = QAudioSink(QMediaDevices.defaultAudioOutput(), fmt)
            self._audio_sink.setVolume(settings.audio_volume / 100.0)
            self._audio_sink.start(self._audio_buf)
        except Exception:
            pass

    def _stop_audio(self):
        if self._audio_sink:
            try: self._audio_sink.stop()
            except: pass
            self._audio_sink = None
        if self._audio_buf:
            try: self._audio_buf.close()
            except: pass
            self._audio_buf = None

    # ── 프레임 표시 (wall-clock 기준 A/V 싱크) ───────────────
    def _next_frame(self):
        n = len(self._frames)
        if n == 0: return
        import time as _time
        elapsed = _time.monotonic() - self._t0
        target  = int(elapsed * self._fps) % n
        if target != self._idx:
            self._idx = target
            self._show()

    def _show(self):
        if not self._frames: return
        pm = QPixmap(); pm.loadFromData(self._frames[self._idx])
        if not pm.isNull():
            self._lbl.setPixmap(pm.scaled(
                self.width(), self.height(),
                Qt.KeepAspectRatio, Qt.SmoothTransformation))

    def resizeEvent(self, e):
        super().resizeEvent(e); self._show()

    # ── AVI 파서 — 비디오(00dc) + 오디오(01wb) 추출 + FPS 읽기 ──
    @staticmethod
    def _read_avi(path: str):
        """Returns (jpeg_frames: list[bytes], audio_pcm: bytes, fps: float)"""
        frames, audio = [], []
        fps = 30.0
        try:
            with open(path, 'rb') as f:
                data = f.read()

            # avih에서 dwMicroSecPerFrame 읽어 FPS 계산
            avih_pos = data.find(b'avih')
            if avih_pos >= 0:
                us_per_frame = struct.unpack_from('<I', data, avih_pos + 8)[0]
                if us_per_frame > 0:
                    fps = 1_000_000.0 / us_per_frame

            movi = data.find(b'movi')
            if movi < 0: return frames, b'', fps
            pos, end = movi + 4, len(data)
            while pos + 8 <= end:
                tag = data[pos:pos+4]
                sz  = struct.unpack_from('<I', data, pos+4)[0]
                if sz == 0 or sz > end - pos - 8:
                    pos += 8; continue
                chunk = data[pos+8: pos+8+sz]
                if tag == b'00dc' and chunk[:2] == b'\xff\xd8':
                    frames.append(chunk)
                elif tag == b'01wb' and chunk:
                    audio.append(chunk)
                pos += 8 + sz + (sz & 1)   # RIFF 2바이트 정렬
        except: pass
        return frames, b''.join(audio), fps

    # ── GIF 프레임 추출 (Pillow) ─────────────────────────────
    @staticmethod
    def _read_gif(path: str) -> list:
        frames = []
        try:
            from PIL import Image as _PI
            from io import BytesIO
            img = _PI.open(path)
            for i in range(getattr(img, 'n_frames', 1)):
                try:
                    img.seek(i); buf = BytesIO()
                    img.convert('RGB').save(buf, 'JPEG', quality=80)
                    frames.append(buf.getvalue())
                except: pass
        except: pass
        return frames

# ════════════════════════════════════════════════════════════
#  BorderPanel — NeoRageX 스타일 애니메이션 테두리 패널
# ════════════════════════════════════════════════════════════
class BorderPanel(QWidget):
    """NeoRAGE 클래식 스타일 패널 — 두꺼운 파란 테두리 + 픽셀 폰트"""
    _BDR  = QColor(68, 102, 255)   # 밝은 파란색 (#4466FF)
    _BDR2 = QColor(30,  60, 180)   # 내부 어두운 파란선
    _BDR3 = QColor(120, 160, 255)  # 하이라이트 (테두리 안쪽 밝은선)
    _TTL  = QColor(255, 255, 255)  # 타이틀: 흰색
    _BW   = 4                      # 테두리 두께

    def __init__(self, title='', parent=None):
        super().__init__(parent)
        self._title = title.upper()
        self._prog  = 0.0
        self._tmr   = QTimer(self)
        self._tmr.setInterval(12)    # 12ms 간격 → 더 부드럽고 느린 애니메이션
        self._tmr.timeout.connect(self._tick)
        lay = QVBoxLayout(self)
        top_m = 24 if title else self._BW + 2   # 타이틀 폰트 커진 만큼 여백 증가
        lay.setContentsMargins(self._BW+4, top_m, self._BW+4, self._BW+4)
        lay.setSpacing(3)

    def set_title(self, t):
        self._title = t.upper(); self.update()

    def start_anim(self, delay=0):
        self._prog = 0.0
        if delay: QTimer.singleShot(delay, self._tmr.start)
        else: self._tmr.start()

    def _tick(self):
        self._prog = min(1.0, self._prog + 0.025)  # 0.025씩 증가 → 약 1.6초 완주 (느린 채움)
        self.update()
        if self._prog >= 1.0: self._tmr.stop()

    def paintEvent(self, event):
        super().paintEvent(event)
        qp = QPainter(self)
        qp.setRenderHint(QPainter.Antialiasing, False)
        W, H, bw = self.width(), self.height(), self._BW

        # ── 폰트 설정 ──────────────────────────────────────────
        # "Press Start 2P" 설치돼 있으면 사용, 없으면 Courier New fallback
        _pf = 'Press Start 2P'
        _fb = 'Courier New'
        font = QFont(_pf, 8, QFont.Bold)
        if not font.exactMatch():
            font = QFont(_fb, 11, QFont.Bold)
        qp.setFont(font)
        fm        = qp.fontMetrics()
        title_str = f' {self._title} ' if self._title else ''
        tw = fm.horizontalAdvance(title_str) if self._title else 0
        th = fm.height()
        tx = 10

        # ── 가장 바깥 테두리 (메인 파란선) ──────────────────────
        rem = (2*(W + H - 2)) * self._prog
        pen = QPen(self._BDR, bw); pen.setCapStyle(Qt.FlatCap)
        qp.setPen(pen)

        if rem > 0 and self._title:
            seg_left = min(tx, rem)
            if seg_left > 0: qp.drawLine(0, 0, int(seg_left), 0)
            rem -= seg_left
            rem -= min(tw, max(0, rem))
            if rem > 0:
                seg_right = min(W - tx - tw - 1, rem)
                if seg_right > 0:
                    qp.drawLine(int(tx + tw), 0, int(tx + tw + seg_right), 0)
                rem -= seg_right
        elif rem > 0:
            d = min(W - 1, rem); qp.drawLine(0, 0, int(d), 0); rem -= d

        if rem > 0:
            d = min(H - 1, rem); qp.drawLine(W-1, 0, W-1, int(d)); rem -= d
        if rem > 0:
            d = min(W - 1, rem); qp.drawLine(W-1, H-1, int(W-1-d), H-1); rem -= d
        if rem > 0:
            d = min(H - 1, rem); qp.drawLine(0, H-1, 0, int(H-1-d))

        if self._prog >= 1.0:
            # 내부 어두운 선 (입체감)
            pen2 = QPen(self._BDR2, 1); qp.setPen(pen2)
            ofs = bw + 1
            qp.drawRect(ofs, ofs, W - ofs*2 - 1, H - ofs*2 - 1)
            # 맨 안쪽 밝은 하이라이트 선 (NeoRAGE 3중 테두리 느낌)
            pen3 = QPen(self._BDR3, 1); qp.setPen(pen3)
            ofs2 = bw + 2
            qp.drawRect(ofs2, ofs2, W - ofs2*2 - 1, H - ofs2*2 - 1)

        # ── 타이틀 텍스트 ──────────────────────────────────────
        if self._title and self._prog > 0.05:
            alpha = min(255, int(self._prog * 400))
            qp.fillRect(tx - 1, 0, tw + 2, bw + 1, QColor(0, 0, 0))
            c = QColor(self._TTL); c.setAlpha(alpha); qp.setPen(c)
            qp.setFont(font)
            qp.drawText(tx, th - 2, title_str)
        qp.end()

# ════════════════════════════════════════════════════════════
#  스타일시트
# ════════════════════════════════════════════════════════════
# NeoRAGE 클래식 픽셀 폰트 — 설치돼 있으면 사용, 없으면 Courier New
_PF  = "'Press Start 2P', 'Courier New'"
_PFS = "8px"          # Press Start 2P 권장 크기 (비트맵이라 작아도 선명)
_PFM = "10px"         # 중간 크기
_PFL = "11px"         # 큰 크기 (리스트 등)

_OPT_STYLE = f"""
QWidget       {{ background: transparent; }}
QLabel        {{ color: #ffffff; font-family: {_PF}; font-size: {_PFS}; background: transparent; }}
QTabWidget::pane {{ border: 3px solid #4466ff; background: rgba(0,0,20,230); }}
QTabBar::tab  {{ background: #000033; color: #aabbff; padding: 7px 14px;
                font-family: {_PF}; font-size: {_PFS}; font-weight: bold;
                border: 2px solid #2244cc; border-bottom: none; }}
QTabBar::tab:selected {{ background: #001166; color: #ffffff;
                          border-top: 3px solid #4466ff; }}
QTabBar::tab:hover {{ background: #001144; color: #ccddff; }}
QLineEdit {{ background: #000022; color: #ffffff; border: 3px solid #4466ff;
            padding: 4px; border-radius: 0px; font-family: {_PF}; font-size: {_PFS}; }}
QLineEdit:focus {{ border: 3px solid #88aaff; color: #ffffff; }}
QTextEdit {{ background: rgba(0,0,15,230); border: 3px solid #4466ff; color: #aaccff;
            font-family: {_PF}; font-size: {_PFS}; }}
QPushButton {{ background: #000055; color: #aaccff; border: 2px solid #4466ff;
              min-height: 28px; padding: 0 10px;
              font-family: {_PF}; font-size: {_PFS}; border-radius: 0px; }}
QPushButton:hover   {{ background: #0000aa; color: #ffffff; border-color: #88aaff; }}
QPushButton:pressed {{ background: #000022; color: #8899ff; }}
QSlider::groove:horizontal {{ height: 5px; background: #000033; border: 2px solid #4466ff; }}
QSlider::handle:horizontal {{ background: #4466ff; width: 14px; height: 14px;
                               border-radius: 0px; margin: -6px 0; border: 2px solid #aabbff; }}
QSlider::sub-page:horizontal {{ background: #2244cc; }}
QComboBox {{ background: #000033; color: #ffffff; border: 3px solid #4466ff;
            padding: 4px; min-height: 28px; border-radius: 0px;
            font-family: {_PF}; font-size: {_PFS}; }}
QComboBox:hover {{ border-color: #88aaff; }}
QComboBox QAbstractItemView {{ background: #000044; color: #ffffff;
                               selection-background-color: #2244cc;
                               border: 3px solid #4466ff; }}
QComboBox::drop-down {{ border: none; }}
QGroupBox {{ color: #aaccff; border: 3px solid #4466ff; border-radius: 0px;
            padding-top: 22px; margin-top: 8px;
            font-weight: bold; font-family: {_PF}; font-size: {_PFS}; }}
QGroupBox::title {{ subcontrol-origin: margin; left: 8px; padding: 0 4px;
                   color: #ffffff; background: #000000; }}
QCheckBox {{ color: #aaccff; font-family: {_PF}; font-size: {_PFS}; }}
QCheckBox::indicator {{ width: 14px; height: 14px; border: 2px solid #4466ff; background: #000022; }}
QCheckBox::indicator:checked {{ background: #4466ff; border-color: #aabbff; }}
QCheckBox::indicator:hover {{ border-color: #88aaff; }}
QListWidget {{ background: #000022; color: #ffffff; border: 3px solid #4466ff;
              font-family: {_PF}; font-size: {_PFL}; border-radius: 0px; }}
QListWidget::item {{ padding: 4px 6px; }}
QListWidget::item:hover {{ background: #001155; color: #ffffff; }}
QListWidget::item:selected {{ background: #2244cc; color: #ffffff;
                              border-left: 4px solid #88aaff; }}
QScrollArea {{ border: none; background: transparent; }}
QScrollBar:vertical {{ background: #000022; width: 12px; border: none; }}
QScrollBar::handle:vertical {{ background: #4466ff; min-height: 24px; }}
QScrollBar::handle:vertical:hover {{ background: #6688ff; }}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0px; }}
"""
_BTN_STYLE = f"""
QPushButton {{ background-color: #000066; color: #ffffff; font-weight: bold;
              border: 3px solid #4466ff; min-height: 42px;
              font-family: {_PF}; font-size: {_PFM}; border-radius: 0px; }}
QPushButton:hover   {{ background-color: #0000cc; color: #ffffff; border-color: #88aaff; }}
QPushButton:pressed {{ background-color: #000033; color: #aabbff; }}
"""

# ════════════════════════════════════════════════════════════
#  메인 윈도우
# ════════════════════════════════════════════════════════════
class NeoRageXApp(QMainWindow):
    # 백그라운드 스레드 → 메인 스레드 안전 통신용 Signal
    _sig_save_done        = Signal(str, str)  # (msg, filepath)
    _sig_save_progress    = Signal(int)       # 0-100
    # 넷플레이 시그널 (백그라운드 소켓 스레드 → 메인 스레드)
    _sig_net_connected    = Signal(bool)      # is_host
    _sig_net_disconnected = Signal()
    _sig_net_error        = Signal(str)

    # NeoRAGE 클래식 체커보드 배경 (paintEvent)
    _CHECKER_A = QColor(38,  38,  52)   # 어두운 칸
    _CHECKER_B = QColor(48,  48,  68)   # 밝은 칸
    _CHECKER_SZ = 16                    # 칸 크기(px)

    def paintEvent(self, event):
        super().paintEvent(event)
        qp = QPainter(self)
        sz = self._CHECKER_SZ
        W, H = self.width(), self.height()
        for row in range(H // sz + 1):
            for col in range(W // sz + 1):
                c = self._CHECKER_A if (row + col) % 2 == 0 else self._CHECKER_B
                qp.fillRect(col * sz, row * sz, sz, sz, c)
        qp.end()

    def __init__(self):
        super().__init__()
        # 필요 폴더 없으면 자동 생성
        for _d in [settings.rom_path, settings.preview_path,
                   settings.screenshot_path,
                   os.path.join(CURRENT_PATH, "assets"),
                   os.path.join(CURRENT_PATH, "saves"),
                   os.path.join(CURRENT_PATH, "cheats")]:
            os.makedirs(_d, exist_ok=True)
        self.setWindowTitle("FBNEORAGEX Core Edition 1.8v")
        self._windowed_size = QSize(1280, 800)
        self._is_fullscreen = False
        self.resize(self._windowed_size)
        self.setMinimumSize(800, 600)
        _ico_path = os.path.join(BUNDLE_PATH, "assets", "Neo.ico")
        if os.path.exists(_ico_path):
            icon = QIcon()
            icon.addFile(_ico_path, QSize(16,16))
            icon.addFile(_ico_path, QSize(24,24))
            icon.addFile(_ico_path, QSize(32,32))
            icon.addFile(_ico_path, QSize(48,48))
            icon.addFile(_ico_path, QSize(256,256))
            self.setWindowIcon(icon)
        load_config()

        self.core             = None
        self.selected_game    = None
        self.remapping_action = None
        self.remapping_hotkey = None
        self.remap_buttons:  dict = {}
        self.hotkey_buttons: dict = {}
        self.dip_combos:     dict = {}
        self.cmb_board:      QComboBox = None

        # 녹화 상태
        self._recording         = False
        self._record_pairs:     list = []   # [(jpeg_bytes, audio_bytes), ...]
        self._record_frame_skip = 0

        # 프리뷰 경로 캐시
        self._preview_img_path: str = None
        self._preview_vid_path: str = None

        self.stack = QStackedWidget()
        self.setCentralWidget(self.stack)
        self.gui_widget = QWidget()
        self._build_ui()
        self.stack.addWidget(self.gui_widget)
        self.canvas = GameCanvas()
        self.canvas.gl_log_signal.connect(self.log)   # GL 진단 메시지 → 이벤트 패널
        self.stack.addWidget(self.canvas)
        # 녹화 오버레이 (canvas 자식 위젯 — 녹화 버퍼에 포함 안됨)
        self._rec_overlay = RecordOverlay(self.canvas)

        self.timer = QTimer()
        self.timer.setTimerType(Qt.PreciseTimer)   # 고정밀 타이머 — OS 최고 해상도 사용
        self.timer.timeout.connect(self._emu_loop)

        self._ecb  = ENV_CB(env_cb)
        self._vcb  = VIDEO_CB(video_cb)
        self._acb  = AUDIO_CB(lambda l, r: None)
        self._abcb = BATCH_CB(audio_batch_cb)
        self._pcb  = POLL_CB(lambda: None)
        self._scb  = STATE_CB(input_state_cb)

        self._init_audio()
        self._setup_core()
        self.scan_roms()
        QApplication.instance().installEventFilter(self)
        # 백그라운드 저장 신호 연결
        self._sig_save_done.connect(self._on_save_done)
        self._sig_save_progress.connect(self._on_save_progress)

        # GUI 화면 게임패드 네비게이션 타이머
        self._gp_gui_timer = QTimer()
        self._gp_gui_timer.timeout.connect(self._poll_gui_gamepad)
        self._gp_gui_timer.start(50)   # 50ms = 20fps 폴링

        # 넷플레이 시그널 연결 (소켓 스레드 → 메인 스레드)
        self._sig_net_connected.connect(self._on_net_connected)
        self._sig_net_disconnected.connect(self._on_net_disconnected)
        self._sig_net_error.connect(self._on_net_error)
        netplay._on_connected    = lambda h: self._sig_net_connected.emit(h)
        netplay._on_disconnected = lambda: self._sig_net_disconnected.emit()
        netplay._on_error        = lambda m: self._sig_net_error.emit(m)
        self._netplay_status_lbl = None   # 넷플레이 상태 표시 레이블 (탭에서 생성)

    # ── 전역 이벤트 필터 ─────────────────────────────────────
    # 모든 게임 키 입력을 여기서 처리 (QApplication 레벨 — 포커스와 무관하게 동작)
    def eventFilter(self, obj, event):
        t = event.type()

        # ── KeyPress ────────────────────────────────────────
        if t == QEvent.KeyPress:
            k   = event.key()
            ar  = event.isAutoRepeat()

            # 리매핑 모드 — 메인 윈도우 이벤트일 때만 1번 처리
            if not ar and obj is self:
                if self.remapping_action is not None:
                    self._finish_remap(k); return True
                if self.remapping_hotkey is not None:
                    self._finish_hotkey_remap(k); return True

            # Tab: 게임화면 ↔ 메뉴화면 자유 전환 (항상 소비)
            if k == Qt.Key_Tab and not ar:
                self._toggle_pause()
                return True

            # Alt+Enter: 전체화면 ↔ 창 전환 (게임 화면 중에만)
            if (k in (Qt.Key_Return, Qt.Key_Enter) and not ar
                    and (event.modifiers() & Qt.AltModifier)
                    and self.stack.currentIndex() == 1):
                self._toggle_fullscreen()
                return True

            # GUI 화면 방향키 네비게이션 — 게임리스트 Up/Down 만 처리
            if self.stack.currentIndex() == 0 and not ar and obj is self:
                if self._handle_gui_nav(k): return True

            # ── 게임 화면 ─────────────────────────────────
            if self.stack.currentIndex() == 1:
                # 핫키 — obj is self 로 1번만 처리 (autorepeat 무시)
                if not ar and obj is self:
                    if k == Qt.Key_Escape:
                        self.stop_game(); return True
                    if k == Qt.Key_F12:
                        self._take_screenshot(); return True
                    if k == Qt.Key_F10:
                        self._save_as_preview_image(); return True
                    if k == hotkey_bindings.get('save_state'):
                        self._save_state(); return True
                    if k == hotkey_bindings.get('load_state'):
                        self._load_state(); return True
                    if k == hotkey_bindings.get('slot_next'):
                        state.save_slot = (state.save_slot+1) % 10
                        self.log(f"💾 Slot → {state.save_slot}"); return True
                    if k == hotkey_bindings.get('slot_prev'):
                        state.save_slot = (state.save_slot-1) % 10
                        self.log(f"💾 Slot → {state.save_slot}"); return True
                    if k == hotkey_bindings.get('record_toggle'):
                        self._toggle_record(); return True

                # FastForward — obj is self 로 1번만 처리
                if k == hotkey_bindings.get('fast_forward') and not ar and obj is self:
                    state.fast_forward = not state.fast_forward
                    self.log(f"⏩ FF {'ON' if state.fast_forward else 'OFF'}")
                    return True

                # ── 게임 액션 키 (autorepeat 무시) ─────────
                # state.keys는 idx 기준 중복 방지: kb_held에 이미 있으면 skip
                if not ar:
                    for action, qt_key in key_bindings.items():
                        if k == qt_key:
                            idx = ACTION_DEFS[action][0]
                            if idx in state.kb_held:
                                return True   # 이미 처리됨
                            state.keys[idx] = 1
                            state.kb_held.add(idx)
                            if (action in TURBO_BUTTON_ACTIONS
                                    and turbo_enabled.get(action, False)):
                                state.turbo_held.add(idx)
                                state._turbo_ticks[idx] = 0
                            return True   # 소비 — 이벤트 전파 차단

        # ── KeyRelease ──────────────────────────────────────
        elif t == QEvent.KeyRelease:
            # autorepeat 에 의한 가짜 Release 완전 차단
            if not event.isAutoRepeat() and self.stack.currentIndex() == 1:
                k = event.key()
                for action, qt_key in key_bindings.items():
                    if k == qt_key:
                        idx = ACTION_DEFS[action][0]
                        state.kb_held.discard(idx)
                        state.keys[idx] = 0
                        state.turbo_held.discard(idx)
                        state._turbo_ticks.pop(idx, None)
                        break
            return False   # Release 는 소비하지 않음

        # ── FocusOut: 포커스를 잃으면 kb_held 완전 초기화 ──
        elif t == QEvent.FocusOut:
            if obj is self or obj is self.canvas:
                state.kb_held.clear()
                for i in range(16): state.keys[i] = 0
                state.turbo_held.clear(); state._turbo_ticks.clear()

        return super().eventFilter(obj, event)

    # ── GUI 네비게이션 (방향키 / Enter / Esc) ─────────────────
    # 포커스: 게임리스트 ↔ 옵션메뉴 두 영역을 방향키로 이동
    def _handle_gui_nav(self, k: int) -> bool:
        """GUI 화면에서 방향키·Enter 처리. True 반환 시 이벤트 소비."""
        lst = self.game_list
        cur = lst.currentRow(); cnt = lst.count()

        # ── UP / DOWN → 한 칸 이동 ────────────────────────
        if k in (Qt.Key_Up, Qt.Key_Down):
            if cnt == 0: return True
            nxt = max(0, cur - 1) if k == Qt.Key_Up else min(cnt - 1, cur + 1)
            if nxt != cur:
                lst.setCurrentRow(nxt)
                self.select_game(lst.item(nxt))
            return True

        # ── LEFT / RIGHT → 페이지 단위 이동 ──────────────
        if k in (Qt.Key_Left, Qt.Key_Right):
            if cnt == 0: return True
            page = max(1, lst.height() // max(1, lst.sizeHintForRow(0)))
            if k == Qt.Key_Left:
                nxt = max(0, cur - page)
            else:
                nxt = min(cnt - 1, cur + page)
            if nxt != cur:
                lst.setCurrentRow(nxt)
                self.select_game(lst.item(nxt))
            return True

        # ── Enter → 선택된 게임 실행 ──────────────────────
        if k in (Qt.Key_Return, Qt.Key_Enter):
            if lst.currentItem():
                self.launch_game()
            return True

        return False

    def _update_options_focus(self):
        """옵션 메뉴 버튼에 시각적 포커스 표시."""
        opts_page = self.options_stack.currentIndex()
        if opts_page != 0: return
        # options_menu 위젯의 버튼들 순서: CONTROLS, DIRECTORIES, VIDEO OPTIONS, ...
        menu_w = self.options_stack.widget(0)
        buttons = [w for w in menu_w.findChildren(QPushButton)]
        focus_idx = getattr(self, '_options_focus_idx', 0)
        for i, btn in enumerate(buttons):
            if i == focus_idx:
                btn.setStyleSheet(
                    "QPushButton{background:rgba(0,0,180,160);color:#ffffff;"
                    "font-family:'Courier New';font-size:26px;font-weight:bold;"
                    "border:2px solid #4466ff;padding:10px 0;text-align:center;letter-spacing:2px;}"
                    "QPushButton:hover{color:#ffffff;}")
            else:
                btn.setStyleSheet(
                    "QPushButton{background:transparent;color:#8899dd;"
                    "font-family:'Courier New';font-size:26px;font-weight:bold;"
                    "border:none;padding:10px 0;text-align:center;letter-spacing:2px;}"
                    "QPushButton:hover{color:#ffffff;background:rgba(0,0,120,100);}"
                    "QPushButton:pressed{color:#ffff00;}")

    # ── 게임패드로 GUI 네비게이션 (타이머 기반) ───────────────
    def _poll_gui_gamepad(self):
        """GUI 화면이 열려 있을 때 게임패드 입력으로 리스트 이동·실행."""
        if self.stack.currentIndex() != 0: return
        gp = poll_gamepad()
        if not gp: return

        lst  = self.game_list
        now  = QTime.currentTime().msecsSinceStartOfDay()
        prev = getattr(self, '_prev_gui_gp', {})

        if not hasattr(self, '_gp_nav_last'):  self._gp_nav_last  = 0
        if not hasattr(self, '_gp_a_last'):    self._gp_a_last    = 0
        if not hasattr(self, '_gp_btn_last'):  self._gp_btn_last  = {}

        def _just_pressed(btn):
            return gp.get(btn, 0) == 1 and prev.get(btn, 0) == 0

        in_options = getattr(self, '_nav_in_options', False)
        opts_page  = self.options_stack.currentIndex()

        # ── D-Pad/스틱 UP/DOWN → 게임리스트 이동 ──────────────
        move = 0
        if gp.get('up') == 1:   move = -1
        if gp.get('down') == 1: move =  1

        if move != 0:
            elapsed = now - self._gp_nav_last
            delay = 140 if elapsed > 400 else 180
            if elapsed >= delay:
                self._gp_nav_last = now
                cur = lst.currentRow(); cnt = lst.count()
                nxt = max(0, min(cnt - 1, cur + move))
                if nxt != cur and cnt > 0:
                    lst.setCurrentRow(nxt)
                    self.select_game(lst.item(nxt))

        # ── LEFT / RIGHT → 페이지 단위 이동 ─────────────────
        for _dir, _sign in (('left', -1), ('right', 1)):
            if _just_pressed(_dir):
                cnt = lst.count()
                if cnt > 0:
                    page = max(1, lst.height() // max(1, lst.sizeHintForRow(0)))
                    nxt = max(0, min(cnt - 1, lst.currentRow() + _sign * page))
                    if nxt != lst.currentRow():
                        lst.setCurrentRow(nxt)
                        self.select_game(lst.item(nxt))

        # ── A 버튼 → 게임 실행 ────────────────────────────────
        if _just_pressed('b'):
            if now - self._gp_a_last > 400:
                self._gp_a_last = now
                if lst.currentItem(): self.launch_game()

        self._prev_gui_gp = dict(gp)

    def _toggle_pause(self):
        if self.stack.currentIndex() == 1:
            self.timer.stop(); state.is_paused = True
            # 모든 키 상태 초기화 (일시정지 시 눌린 키 잔류 방지)
            state.kb_held.clear()
            for i in range(16): state.keys[i] = 0
            self.stack.setCurrentIndex(0)
            # 일시정지 시 DIP 최신 상태 반영
            self._rebuild_machine_tab()
            self.log("⏸  일시정지")
        elif state.is_paused:
            state.is_paused = False
            self.stack.setCurrentIndex(1)
            self._afl_last_t = time.perf_counter()
            self.canvas.setFocus(); self.timer.start(1)   # 1ms: AFL이 내부에서 timing 제어
            self.log("▶  재개")

    def stop_game(self):
        self.timer.stop()
        state.turbo_held.clear(); state._turbo_ticks.clear(); state.kb_held.clear()
        if self._recording: self._stop_record()
        if self.core and state.game_loaded:
            try: self.core.retro_unload_game()
            except: pass
        state.game_loaded=False; state.is_paused=False
        state.video_buffer=None; state.dip_variables.clear()
        # 전체화면 중 게임 종료 → 창 모드로 복귀
        if self._is_fullscreen:
            self._is_fullscreen = False
            self.showNormal()
            self.resize(self._windowed_size)
        self.stack.setCurrentIndex(0)
        self.log("⏹  게임 종료")

    # ── 세이브 / 로드 ──────────────────────────────────────────
    def _save_state(self):
        if not self.core or not state.game_loaded: return
        try:
            sz = self.core.retro_serialize_size()
            buf = (ctypes.c_char * sz)()
            if self.core.retro_serialize(buf, sz):
                with open(self._state_path(state.save_slot),'wb') as f: f.write(bytes(buf))
                self.log(f"💾 Saved → Slot {state.save_slot}")
        except Exception as e: self.log(f"❌ Save: {e}")

    def _load_state(self):
        if not self.core or not state.game_loaded: return
        path = self._state_path(state.save_slot)
        if not os.path.exists(path):
            self.log(f"⚠  Slot {state.save_slot} 없음"); return
        try:
            with open(path,'rb') as f: db = f.read()
            buf = (ctypes.c_char*len(db))(*db)
            if self.core.retro_unserialize(buf,len(db)):
                self.log(f"📂 Loaded ← Slot {state.save_slot}")
            else: self.log("❌ Load failed")
        except Exception as e: self.log(f"❌ Load: {e}")

    def _state_path(self, slot):
        d = os.path.join(CURRENT_PATH,"saves"); os.makedirs(d,exist_ok=True)
        return os.path.join(d,f"{self.selected_game or 'unknown'}_slot{slot}.sav")

    # ════════════════════════════════════════════════════════════
    #  UI 레이아웃
    # ════════════════════════════════════════════════════════════
    def _build_ui(self):
        bg_file = os.path.join(BUNDLE_PATH,"assets","background.png").replace('\\','/')
        bg_css  = (f"background-image:url('{bg_file}');" if os.path.exists(bg_file)
                   else "background-color:#000005;")
        self.gui_widget.setObjectName("gui_main")
        self.gui_widget.setStyleSheet(
            f"QWidget#gui_main{{{bg_css}}}\n"
            "QListWidget{background:rgba(0,0,8,210);border:none;color:#99ccee;"
            "font-family:'Courier New';font-size:17px;outline:none;}"
            "QListWidget::item{padding:4px 8px;}"
            "QListWidget::item:hover{background:#001144;color:#cceeff;}"
            "QListWidget::item:selected{background:#0044cc;color:#ffffff;"
            "border-left:3px solid #00aaff;font-weight:bold;}\n"
            + _OPT_STYLE)

        root = QVBoxLayout(self.gui_widget)
        root.setContentsMargins(6,6,6,4); root.setSpacing(4)

        # ── 상단: 게임리스트 + 옵션 ───────────────────
        top = QHBoxLayout(); top.setSpacing(6)

        # 게임리스트 패널
        self.gamelist_panel = BorderPanel('GAMELIST')
        self.gamelist_panel.setStyleSheet("background:transparent;")

        # 즐겨찾기 필터 버튼
        FAV_BTN = ("QPushButton{background:#000044;color:#6688cc;border:1px solid #0000aa;"
                   "font-family:'Courier New';font-size:15px;font-weight:bold;"
                   "padding:3px 8px;border-radius:2px;}"
                   "QPushButton:hover{background:#0000aa;color:#ffffff;}"
                   "QPushButton:checked{background:#aa4400;color:#ffcc00;border-color:#ff8800;}")
        fav_bar = QHBoxLayout(); fav_bar.setSpacing(4); fav_bar.setContentsMargins(0,0,0,2)
        self._fav_btn_all  = QPushButton("ALL");  self._fav_btn_all.setCheckable(True);  self._fav_btn_all.setChecked(True)
        self._fav_btn_fav  = QPushButton("★ FAV"); self._fav_btn_fav.setCheckable(True)
        self._fav_btn_star = QPushButton("☆")
        self._fav_btn_star.setToolTip("Toggle Favorite")
        for b in (self._fav_btn_all, self._fav_btn_fav, self._fav_btn_star):
            b.setStyleSheet(FAV_BTN); fav_bar.addWidget(b)
        fav_bar.addStretch(1)
        self._fav_btn_all.clicked.connect(lambda: self._set_fav_filter(False))
        self._fav_btn_fav.clicked.connect(lambda: self._set_fav_filter(True))
        self._fav_btn_star.clicked.connect(self._toggle_favorite)
        self.gamelist_panel.layout().addLayout(fav_bar)

        self._fav_filter = False   # True = 즐겨찾기만 표시
        self.game_list = QListWidget()
        self.game_list.itemClicked.connect(self.select_game)
        self.game_list.itemDoubleClicked.connect(lambda _: self.launch_game())
        # 방향키로 이동 시에도 즉시 프리뷰·선택 반영
        self.game_list.currentItemChanged.connect(
            lambda cur, _prev: self.select_game(cur) if cur else None)
        self.gamelist_panel.layout().addWidget(self.game_list)
        top.addWidget(self.gamelist_panel, 13)

        # 옵션 패널
        self.options_panel = BorderPanel('OPTIONS')
        self.options_panel.setStyleSheet("background:transparent;")
        self.options_stack = self._build_options_stack()
        self.options_panel.layout().addWidget(self.options_stack)
        top.addWidget(self.options_panel, 29)

        root.addLayout(top, 9)

        # ── 버튼 바 ──────────────────────────────────
        bar = QHBoxLayout(); bar.setSpacing(6)
        BTN_S = ("QPushButton{background:#000055;color:#99bbff;border:2px solid #0055cc;"
                 "font-family:'Courier New';font-size:17px;font-weight:bold;"
                 "min-height:46px;border-radius:0px;letter-spacing:1px;}"
                 "QPushButton:hover{background:#0000cc;color:#ffffff;border-color:#00aaff;}"
                 "QPushButton:pressed{background:#000022;color:#aaddff;}")
        for txt, fn in [("▶  LAUNCH / RESUME", self.launch_game),
                        ("⏹  STOP GAME",       self.stop_game),
                        ("📥  IMPORT ROM",      self.import_rom),
                        ("✖  EXIT",             self.close)]:
            b = QPushButton(txt); b.setStyleSheet(BTN_S)
            b.clicked.connect(fn); bar.addWidget(b, 1)
        root.addLayout(bar)

        # ── 하단: 프리뷰 + 이벤트 ────────────────────
        bot = QHBoxLayout(); bot.setSpacing(6)

        # 프리뷰 패널
        self.preview_panel = BorderPanel('PREVIEW')
        self.preview_panel.setStyleSheet("background:transparent;")
        pv_stack_container = QWidget()
        pv_stack_container.setStyleSheet("background:transparent;")
        pv_lay = QVBoxLayout(pv_stack_container)
        pv_lay.setContentsMargins(0,0,0,0)
        self.preview_stack = QStackedWidget()
        self.preview_label = QLabel("NO PREVIEW")
        self.preview_label.setAlignment(Qt.AlignCenter)
        self.preview_label.setStyleSheet("background:rgba(0,0,0,160);color:#333355;"
                                         "font-size:18px;font-family:'Courier New';")
        self.preview_stack.addWidget(self.preview_label)
        self._has_video_preview = False
        self._preview_use_linux_player = False

        if IS_LINUX:
            # Steam Deck / Linux: QVideoWidget + QOpenGL 충돌 방지 → 순수 Python 재생기
            try:
                self._linux_player = LinuxVideoPlayer()
                self._linux_player.setStyleSheet("background:black;")
                self.preview_stack.addWidget(self._linux_player)
                self._has_video_preview = True
                self._preview_use_linux_player = True
            except: pass
        elif VIDEO_PREVIEW_OK:
            # Windows: Qt Multimedia 사용 (오디오 포함)
            try:
                self.preview_video = QVideoWidget()
                self.preview_video.setStyleSheet("background:black;")
                self.preview_stack.addWidget(self.preview_video)
                self.preview_player = QMediaPlayer()
                try:
                    self.preview_player.setLoops(QMediaPlayer.Infinite)
                except Exception:
                    # 구버전 PySide6 폴백 — EndOfMedia 재시작
                    self.preview_player.mediaStatusChanged.connect(self._on_preview_media_status)
                from PySide6.QtMultimedia import QAudioOutput as _QAO
                self._preview_audio_out = _QAO()
                self.preview_player.setAudioOutput(self._preview_audio_out)
                self.preview_player.setVideoOutput(self.preview_video)
                self._has_video_preview = True
            except: pass
        pv_lay.addWidget(self.preview_stack)
        self.preview_panel.layout().addWidget(pv_stack_container)
        self.preview_img_timer = QTimer()
        self.preview_img_timer.setSingleShot(True)
        self.preview_img_timer.timeout.connect(self._start_preview_video)
        bot.addWidget(self.preview_panel, 13)

        # 이벤트(로그) 패널
        self.events_panel = BorderPanel('EVENTS')
        self.events_panel.setStyleSheet("background:transparent;")
        self.log_view = QTextEdit(); self.log_view.setReadOnly(True)
        self.log_view.setStyleSheet(
            "background:rgba(0,0,0,170);border:none;"
            "color:#00ddff;font-family:'Courier New';font-size:15px;")
        self.events_panel.layout().addWidget(self.log_view)
        bot.addWidget(self.events_panel, 29)

        root.addLayout(bot, 4)

        # ── 푸터 ─────────────────────────────────────
        footer = QHBoxLayout()
        vl = QLabel("Ver 1.8"); vl.setStyleSheet("color:#3344aa;font-family:'Courier New';font-size:14px;")
        cr = QLabel("FBNeoRageX © 2025"); cr.setStyleSheet("color:#3344aa;font-family:'Courier New';font-size:14px;")
        footer.addWidget(vl); footer.addStretch(); footer.addWidget(cr)
        root.addLayout(footer)

        # 애니메이션 시작
        self.gamelist_panel.start_anim(0)
        self.options_panel.start_anim(80)
        self.preview_panel.start_anim(160)
        self.events_panel.start_anim(240)

    # ── 옵션 스택 ──────────────────────────────────────────────
    def _build_options_stack(self) -> QStackedWidget:
        stk = QStackedWidget()
        stk.setStyleSheet("background:transparent;")
        stk.addWidget(self._build_options_menu())            # 0 menu
        stk.addWidget(self._wrap_back(self._tab_controls())) # 1
        stk.addWidget(self._wrap_back(self._tab_directory()))# 2
        stk.addWidget(self._wrap_back(self._tab_video()))    # 3
        stk.addWidget(self._wrap_back(self._tab_audio()))    # 4
        stk.addWidget(self._wrap_back(self._tab_machine()))  # 5
        stk.addWidget(self._wrap_back(self._tab_shotfactory())) # 6
        stk.addWidget(self._wrap_back(self._tab_cheats()))   # 7
        stk.addWidget(self._wrap_back(self._tab_multiplayer())) # 8
        return stk

    def _build_options_menu(self) -> QWidget:
        w = QWidget(); w.setStyleSheet("background:transparent;")
        v = QVBoxLayout(w); v.setContentsMargins(0,0,0,0); v.setSpacing(0)
        v.addStretch(1)
        MENU_STYLE = (
            "QPushButton{background:transparent;color:#5599dd;"
            "font-family:'Courier New';font-size:20px;font-weight:bold;"
            "border:none;border-left:3px solid transparent;"
            "padding:8px 12px;text-align:left;letter-spacing:2px;}"
            "QPushButton:hover{color:#ffffff;background:rgba(0,40,140,120);"
            "border-left:3px solid #00aaff;}"
            "QPushButton:pressed{color:#00ffff;background:rgba(0,20,80,150);}")
        items = [("CONTROLS",1),("DIRECTORIES",2),("VIDEO OPTIONS",3),
                 ("AUDIO OPTIONS",4),("MACHINE SETTINGS",5),("SHOTS FACTORY",6),
                 ("CHEATS",7),("MULTIPLAYER",8)]
        for label, idx in items:
            btn = QPushButton(label); btn.setStyleSheet(MENU_STYLE)
            btn.clicked.connect(lambda _,i=idx: self._show_options_page(i))
            v.addWidget(btn, 0, Qt.AlignHCenter)
        v.addStretch(1)
        return w

    def _wrap_back(self, widget: QWidget) -> QWidget:
        wrapper = QWidget(); wrapper.setStyleSheet("background:transparent;")
        v = QVBoxLayout(wrapper); v.setContentsMargins(0,0,0,0); v.setSpacing(3)
        back = QPushButton("◀  BACK"); back.setFixedHeight(26)
        back.setStyleSheet("QPushButton{background:#000044;color:#6688cc;"
                           "border:1px solid #0000aa;font-family:'Courier New';"
                           "font-size:15px;}QPushButton:hover{color:#ffffff;background:#0000aa;}")
        back.clicked.connect(lambda: self._show_options_page(0))
        v.addWidget(back); v.addWidget(widget, 1)
        return wrapper

    _OPTION_TITLES = {0:'OPTIONS',1:'CONTROLS',2:'DIRECTORIES',3:'VIDEO',
                      4:'AUDIO',5:'MACHINE',6:'SHOT FACTORY',7:'CHEATS',
                      8:'MULTIPLAYER'}

    def _show_options_page(self, idx: int):
        self.options_stack.setCurrentIndex(idx)
        if hasattr(self,'options_panel'):
            self.options_panel.set_title(self._OPTION_TITLES.get(idx,'OPTIONS'))

    # ── ① CONTROLS ──────────────────────────────────────────
    def _tab_controls(self) -> QWidget:
        outer = QWidget()
        outer_v = QVBoxLayout(outer); outer_v.setContentsMargins(0,0,0,0)

        # 게임 프로파일 바
        prof_bar = QHBoxLayout()
        prof_bar.addWidget(QLabel("프로파일:"))
        self.lbl_profile = QLabel("[ 전역 기본값 ]")
        self.lbl_profile.setStyleSheet("color:#00ffff;")
        save_prof = QPushButton("💾 이 게임에 저장")
        del_prof  = QPushButton("🗑 삭제")
        reset_btn = QPushButton("↺ 기본값")
        save_prof.setFixedWidth(150); del_prof.setFixedWidth(90); reset_btn.setFixedWidth(90)
        save_prof.clicked.connect(self._save_game_profile)
        del_prof.clicked.connect(self._del_game_profile)
        reset_btn.clicked.connect(self._reset_keybindings)
        prof_bar.addWidget(self.lbl_profile); prof_bar.addStretch()
        prof_bar.addWidget(save_prof); prof_bar.addWidget(del_prof); prof_bar.addWidget(reset_btn)

        scroll = QScrollArea(); scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)  # 가로 스크롤 제거
        inner  = QWidget(); scroll.setWidget(inner)
        v = QVBoxLayout(inner); v.setContentsMargins(10,6,10,6); v.setSpacing(3)

        # 키보드 매핑
        grp_kb = QGroupBox("⌨  KEYBOARD MAPPING")
        g = QGridLayout(grp_kb); g.setHorizontalSpacing(12); g.setVerticalSpacing(3)
        for col,(txt,c) in enumerate([("ACTION","#ffff00"),("KEYBOARD","#ffff00")]):
            lbl=QLabel(txt); lbl.setStyleSheet(f"color:{c};font-weight:bold;"); g.addWidget(lbl,0,col)
        for row,(action,(_,disp,_d)) in enumerate(ACTION_DEFS.items(),1):
            g.addWidget(QLabel(disp),row,0)
            btn=QPushButton(key_to_str(key_bindings[action])); btn.setFixedWidth(120)
            btn.setStyleSheet("color:#00ffff;background:#000033;border:1px solid #003399;")
            btn.clicked.connect(lambda _,a=action: self._start_remap(a))
            self.remap_buttons[action]=btn; g.addWidget(btn,row,1)
        specials=[("PAUSE ↔ GUI","TAB"),("STOP GAME","ESC"),("SCREENSHOT","F12")]
        off=len(ACTION_DEFS)+1
        for i,(d,k) in enumerate(specials):
            g.addWidget(QLabel(d),off+i,0)
            fl=QLabel(f"[ {k} ]"); fl.setStyleSheet("color:#ffaa00;"); g.addWidget(fl,off+i,1)
        v.addWidget(grp_kb)

        # 핫키 매핑
        grp_hk = QGroupBox("⚡  HOTKEYS")
        h = QGridLayout(grp_hk); h.setHorizontalSpacing(12); h.setVerticalSpacing(3)
        for col,(txt,c) in enumerate([("FUNCTION","#ffff00"),("KEY","#ffff00")]):
            lbl=QLabel(txt); lbl.setStyleSheet(f"color:{c};font-weight:bold;"); h.addWidget(lbl,0,col)
        for row,(hk,(disp,_dflt)) in enumerate(HOTKEY_DEFS.items(),1):
            h.addWidget(QLabel(disp),row,0)
            btn=QPushButton(key_to_str(hotkey_bindings[hk])); btn.setFixedWidth(120)
            btn.setStyleSheet("color:#ffaa00;background:#000033;border:1px solid #664400;")
            btn.clicked.connect(lambda _,a=hk: self._start_hotkey_remap(a))
            self.hotkey_buttons[hk]=btn; h.addWidget(btn,row,1)
        v.addWidget(grp_hk)

        # 게임패드 상태 + 버튼 리매핑
        grp_gp = QGroupBox("🎮  GAMEPAD")
        gp_v = QVBoxLayout(grp_gp)
        if IS_WINDOWS:
            status = "✅ XInput 사용 가능" if XINPUT_OK else "❌ XInput 없음"
            color  = "#00ff88" if XINPUT_OK else "#ff4444"
            sl = QLabel(status); sl.setStyleSheet(f"color:{color};"); gp_v.addWidget(sl)
        elif IS_LINUX:
            ok = _linux_gp and _linux_gp.available
            status = "✅ /dev/input/js0 감지됨" if ok else "⚠ /dev/input/js0 없음 (패드 연결 확인)"
            color  = "#00ff88" if ok else "#ffaa00"
            sl = QLabel(status); sl.setStyleSheet(f"color:{color};"); gp_v.addWidget(sl)

        # 게임패드 버튼 리매핑 (세로 레이아웃 — 가로 스크롤 방지)
        gp_map_lbl = QLabel("버튼 → 액션 매핑:")
        gp_map_lbl.setStyleSheet("color:#aaaacc;font-size:14px;margin-top:4px;")
        gp_v.addWidget(gp_map_lbl)
        BTN_STYLE = ("QComboBox{color:#00ccff;background:#000033;border:1px solid #003399;"
                     "font-size:14px;padding:2px 4px;min-height:24px;}"
                     "QComboBox:hover{border-color:#00aaff;}"
                     "QComboBox QAbstractItemView{background:#000033;color:#aaddff;"
                     "selection-background-color:#0033aa;border:2px solid #0055cc;}")

        if IS_LINUX:
            phys_labels = ["South (×/A)", "East (○/B)", "West (□/X)",
                           "North (△/Y)", "L1", "R1"]
            phys_keys   = [0, 1, 2, 3, 4, 5]
        else:
            phys_labels = ["South (A)", "East (B)", "West (X)",
                           "North (Y)", "LB", "RB"]
            phys_keys   = ['XI_A', 'XI_B', 'XI_X', 'XI_Y', 'XI_LB', 'XI_RB']

        action_choices = [f"{k}  ({ACTION_DEFS[k][1].split('(')[0].strip()})"
                          for k in ['a','b','c','d','e','f','start','coin']]
        action_keys    = ['a','b','c','d','e','f','start','coin']

        # 3행 2열로 배치 (좌: 버튼이름+콤보, 우: 버튼이름+콤보)
        gp_form = QGridLayout()
        gp_form.setSpacing(6)
        gp_form.setColumnStretch(1, 1)
        gp_form.setColumnStretch(3, 1)
        gp_form.setColumnMinimumWidth(2, 16)  # 중간 간격

        xi_map_rev = {v: k for k, v in _XI_BTN_MAP.items()}
        xi_phys_mask = [XI_A, XI_B, XI_X, XI_Y, XI_LB, XI_RB]

        self._gp_remap_combos = {}
        for i, (lbl_txt, pkey) in enumerate(zip(phys_labels, phys_keys)):
            row = i // 2
            base_col = (i % 2) * 3  # 0 or 3

            lbl = QLabel(f"● {lbl_txt}")
            lbl.setStyleSheet("color:#88bbdd;font-size:14px;")
            lbl.setMinimumWidth(110)
            cmb = QComboBox()
            cmb.addItems(action_choices)
            cmb.setStyleSheet(BTN_STYLE)
            if IS_LINUX:
                cur_action = LinuxGamepad._BTN_MAP.get(pkey, 'a')
            else:
                cur_action = xi_map_rev.get(xi_phys_mask[i], 'a')
            if cur_action in action_keys:
                cmb.setCurrentIndex(action_keys.index(cur_action))
            cmb.currentIndexChanged.connect(
                lambda idx, pk=pkey, al=action_keys: self._remap_gamepad_btn(pk, al[idx]))
            gp_form.addWidget(lbl, row, base_col)
            gp_form.addWidget(cmb, row, base_col + 1)
            self._gp_remap_combos[pkey] = cmb

        gp_v.addLayout(gp_form)
        v.addWidget(grp_gp)

        # 터보 설정
        grp_turbo = QGroupBox("⚡  TURBO MODE (버튼별 연타)")
        turbo_v = QVBoxLayout(grp_turbo)
        turbo_note = QLabel("활성화된 버튼은 누르는 동안 자동 연타됩니다  (방향키 적용 안됨)")
        turbo_note.setStyleSheet("color:#555577;font-size:14px;")
        turbo_v.addWidget(turbo_note)
        turbo_row = QHBoxLayout()
        self.turbo_checks = {}
        for act in TURBO_BUTTON_ACTIONS:
            short_lbl = ACTION_DEFS[act][1].split('(')[0].strip()
            cb = QCheckBox(short_lbl); cb.setChecked(turbo_enabled.get(act, False))
            cb.toggled.connect(lambda chk, a=act: self._set_turbo(a, chk))
            self.turbo_checks[act] = cb; turbo_row.addWidget(cb)
        turbo_row.addStretch()
        turbo_v.addLayout(turbo_row)
        spd_row = QHBoxLayout()
        spd_row.addWidget(QLabel("연타 간격:"))
        self._turbo_slider = QSlider(Qt.Horizontal)
        self._turbo_slider.setRange(1, 30); self._turbo_slider.setValue(turbo_period)
        self._turbo_slider.setFixedWidth(160)
        self._turbo_spd_lbl = QLabel(f"{turbo_period}f")
        self._turbo_spd_lbl.setStyleSheet("color:#00ffff;min-width:35px;")
        spd_note = QLabel("(1=최고속, 30=저속  /  ON·OFF 각 Nf 간격)")
        spd_note.setStyleSheet("color:#444466;font-size:14px;")
        self._turbo_slider.valueChanged.connect(self._update_turbo_speed)
        spd_row.addWidget(self._turbo_slider); spd_row.addWidget(self._turbo_spd_lbl)
        spd_row.addStretch()
        turbo_v.addLayout(spd_row)
        turbo_v.addWidget(spd_note)
        v.addWidget(grp_turbo)

        # 기판별 통합 프로파일
        grp_board = QGroupBox("🏆  기판별 통합 프로파일")
        board_v = QVBoxLayout(grp_board)
        board_row = QHBoxLayout()
        board_row.addWidget(QLabel("기판:"))
        self.cmb_board = QComboBox(); self.cmb_board.addItems(BOARD_LIST)
        self.cmb_board.setFixedWidth(120)
        btn_sb = QPushButton("💾 저장"); btn_lb = QPushButton("📂 적용"); btn_db = QPushButton("🗑 삭제")
        for b in [btn_sb, btn_lb, btn_db]: b.setFixedWidth(85)
        btn_sb.clicked.connect(self._save_board_profile)
        btn_lb.clicked.connect(self._load_board_profile)
        btn_db.clicked.connect(self._del_board_profile)
        board_row.addWidget(self.cmb_board)
        board_row.addWidget(btn_sb); board_row.addWidget(btn_lb); board_row.addWidget(btn_db)
        board_row.addStretch()
        note = QLabel("같은 기판 게임에 동일한 버튼 설정 자동 적용  (게임 선택 시 기판 자동 감지)")
        note.setStyleSheet("color:#555577;font-size:14px;")
        board_v.addLayout(board_row); board_v.addWidget(note)
        v.addWidget(grp_board)
        v.addStretch()

        outer_v.addLayout(prof_bar)
        outer_v.addWidget(scroll)
        return outer

    def _start_remap(self, action):
        self.remapping_action = action
        btn = self.remap_buttons[action]
        btn.setText("Press any key..."); btn.setStyleSheet("color:#ffff00;background:#110000;border:1px solid #ff0000;")

    def _finish_remap(self, qt_key):
        action = self.remapping_action; self.remapping_action = None
        key_bindings[action] = qt_key
        btn = self.remap_buttons[action]
        btn.setText(key_to_str(qt_key))
        btn.setStyleSheet("color:#00ffff;background:#000033;border:1px solid #003399;")
        self.log(f"🕹  {ACTION_DEFS[action][1]} → [ {key_to_str(qt_key)} ]")

    def _start_hotkey_remap(self, hk):
        self.remapping_hotkey = hk
        btn = self.hotkey_buttons[hk]
        btn.setText("Press any key..."); btn.setStyleSheet("color:#ffff00;background:#110000;border:1px solid #ff0000;")

    def _finish_hotkey_remap(self, qt_key):
        hk = self.remapping_hotkey; self.remapping_hotkey = None
        hotkey_bindings[hk] = qt_key
        btn = self.hotkey_buttons[hk]
        btn.setText(key_to_str(qt_key))
        btn.setStyleSheet("color:#ffaa00;background:#000033;border:1px solid #664400;")
        self.log(f"⚡ {HOTKEY_DEFS[hk][0]} → [ {key_to_str(qt_key)} ]")

    def _reset_keybindings(self):
        global key_bindings, hotkey_bindings
        key_bindings    = {k: v[2] for k, v in ACTION_DEFS.items()}
        hotkey_bindings = {k: v[1] for k, v in HOTKEY_DEFS.items()}
        for a, btn in self.remap_buttons.items():
            btn.setText(key_to_str(ACTION_DEFS[a][2]))
            btn.setStyleSheet("color:#00ffff;background:#000033;border:1px solid #003399;")
        for hk, btn in self.hotkey_buttons.items():
            btn.setText(key_to_str(HOTKEY_DEFS[hk][1]))
            btn.setStyleSheet("color:#ffaa00;background:#000033;border:1px solid #664400;")
        self.log("🕹  키 바인딩 기본값 초기화")

    # 게임 프로파일
    def _save_game_profile(self):
        if not self.selected_game:
            self.log("⚠  게임을 먼저 선택하세요"); return
        game_key_bindings[self.selected_game] = dict(key_bindings)
        save_config(); self.log(f"💾 게임 프로파일 저장: {self.selected_game}")
        self._update_profile_label()

    def _del_game_profile(self):
        if not self.selected_game: return
        game_key_bindings.pop(self.selected_game, None)
        save_config(); self._update_profile_label()
        self.log(f"🗑  게임 프로파일 삭제: {self.selected_game}")

    def _update_profile_label(self):
        if self.selected_game and self.selected_game in game_key_bindings:
            self.lbl_profile.setText(f"[ {self.selected_game} ]")
        elif self.selected_game:
            board = detect_board(self.selected_game)
            if board in board_key_bindings:
                self.lbl_profile.setText(f"[ 기판: {board} ]")
            else:
                self.lbl_profile.setText("[ 전역 기본값 ]")
        else:
            self.lbl_profile.setText("[ 전역 기본값 ]")

    def _load_game_profile(self, game: str):
        global key_bindings
        if game in game_key_bindings:
            key_bindings = dict(game_key_bindings[game])
        else:
            board = detect_board(game)
            if board in board_key_bindings:
                key_bindings = dict(board_key_bindings[board])
            else:
                key_bindings = {k: v[2] for k, v in ACTION_DEFS.items()}
        for a, btn in self.remap_buttons.items():
            btn.setText(key_to_str(key_bindings.get(a, ACTION_DEFS[a][2])))
        self._update_profile_label()

    # 기판 프로파일
    def _save_board_profile(self):
        board = self.cmb_board.currentText() if self.cmb_board else "기타"
        board_key_bindings[board] = dict(key_bindings)
        save_config(); self.log(f"💾 기판 프로파일 저장: {board}")

    def _load_board_profile(self):
        global key_bindings
        board = self.cmb_board.currentText() if self.cmb_board else "기타"
        if board not in board_key_bindings:
            self.log(f"⚠  기판 프로파일 없음: {board}"); return
        key_bindings = dict(board_key_bindings[board])
        for a, btn in self.remap_buttons.items():
            btn.setText(key_to_str(key_bindings.get(a, ACTION_DEFS[a][2])))
        self.log(f"📂 기판 프로파일 적용: {board}")

    def _del_board_profile(self):
        board = self.cmb_board.currentText() if self.cmb_board else "기타"
        board_key_bindings.pop(board, None)
        save_config(); self.log(f"🗑  기판 프로파일 삭제: {board}")

    # ── ② DIRECTORY ─────────────────────────────────────────
    def _tab_directory(self) -> QWidget:
        w = QWidget(); v = QVBoxLayout(w); v.setContentsMargins(14,10,14,10); v.setSpacing(10)
        self.e_rom        = self._dir_row(v, "ROM 폴더",       settings.rom_path)
        self.e_preview    = self._dir_row(v, "프리뷰 폴더",    settings.preview_path)
        self.e_screenshot = self._dir_row(v, "스크린샷 폴더",  settings.screenshot_path)
        ab = QPushButton("✅  경로 적용"); ab.setFixedWidth(130); ab.clicked.connect(self._apply_dirs)
        v.addWidget(ab); v.addStretch(); return w

    def _dir_row(self, parent, label, default):
        parent.addWidget(QLabel(label+":"))
        row=QHBoxLayout(); edit=QLineEdit(default); btn=QPushButton("Browse")
        btn.setFixedWidth(68); btn.clicked.connect(lambda _,e=edit: self._browse_dir(e))
        row.addWidget(edit); row.addWidget(btn); parent.addLayout(row); return edit

    def _browse_dir(self, edit):
        d=QFileDialog.getExistingDirectory(self,"폴더 선택",edit.text())
        if d: edit.setText(d)

    def _apply_dirs(self):
        settings.rom_path = self.e_rom.text()
        settings.preview_path = self.e_preview.text()
        settings.screenshot_path = self.e_screenshot.text()
        if hasattr(self,'e_shot_path'): self.e_shot_path.setText(settings.screenshot_path)
        self.scan_roms(); save_config()
        self.log(f"📁  경로 적용")

    # ── ③ VIDEO ─────────────────────────────────────────────
    def _tab_video(self) -> QWidget:
        w = QWidget()
        w.setStyleSheet("background:transparent;")
        root = QVBoxLayout(w); root.setContentsMargins(16, 10, 16, 10); root.setSpacing(0)

        # ── 제목 ────────────────────────────────────────────
        title = QLabel("VIDEO OPTIONS")
        title.setStyleSheet(
            "color:#00eeff;font-family:'Courier New';font-size:20px;font-weight:bold;"
            "letter-spacing:4px;padding:6px 0 10px 0;")
        title.setAlignment(Qt.AlignCenter)
        root.addWidget(title)

        # ── NeoRageX 스타일 옵션 행 빌더 ────────────────────
        ROW_H  = 36
        LBL_W  = 200
        VAL_W  = 160
        LBL_SS = ("color:#ccddee;font-family:'Courier New';font-size:15px;"
                  "font-weight:bold;letter-spacing:1px;")
        VAL_SS = ("color:#ffff44;font-family:'Courier New';font-size:15px;"
                  "font-weight:bold;")
        BTN_SS = ("QPushButton{color:#00aaff;background:transparent;"
                  "border:none;font-size:18px;font-weight:bold;padding:0 4px;}"
                  "QPushButton:hover{color:#ffffff;}")
        SEP_SS = "background:#002244;min-height:1px;max-height:1px;"

        def _add_sep():
            sep = QFrame(); sep.setStyleSheet(SEP_SS); root.addWidget(sep)

        def _make_row(label_txt, values, get_idx, set_fn, note=""):
            """◄ VALUE ► 형식의 NeoRageX 스타일 행 생성"""
            row_w = QWidget(); row_w.setFixedHeight(ROW_H)
            h = QHBoxLayout(row_w); h.setContentsMargins(4, 0, 4, 0); h.setSpacing(8)
            lbl = QLabel(label_txt)
            lbl.setStyleSheet(LBL_SS); lbl.setFixedWidth(LBL_W)
            val_lbl = QLabel(values[get_idx()])
            val_lbl.setStyleSheet(VAL_SS); val_lbl.setFixedWidth(VAL_W)
            val_lbl.setAlignment(Qt.AlignCenter)
            btn_l = QPushButton("◄"); btn_l.setStyleSheet(BTN_SS); btn_l.setFixedWidth(28)
            btn_r = QPushButton("►"); btn_r.setStyleSheet(BTN_SS); btn_r.setFixedWidth(28)

            def _prev():
                i = (get_idx() - 1) % len(values)
                set_fn(i); val_lbl.setText(values[i])
            def _next():
                i = (get_idx() + 1) % len(values)
                set_fn(i); val_lbl.setText(values[i])

            btn_l.clicked.connect(_prev); btn_r.clicked.connect(_next)
            h.addWidget(lbl); h.addWidget(btn_l); h.addWidget(val_lbl); h.addWidget(btn_r)
            if note:
                note_lbl = QLabel(note)
                note_lbl.setStyleSheet("color:#445566;font-size:12px;font-family:'Courier New';")
                h.addWidget(note_lbl)
            h.addStretch()
            return row_w

        # ── COLOR DEPTH (읽기 전용 표시) ────────────────────
        cd_row = QWidget(); cd_row.setFixedHeight(ROW_H)
        cd_h = QHBoxLayout(cd_row); cd_h.setContentsMargins(4,0,4,0); cd_h.setSpacing(8)
        cd_lbl = QLabel("COLOR DEPTH"); cd_lbl.setStyleSheet(LBL_SS); cd_lbl.setFixedWidth(LBL_W)
        pf_map = {0:"16 BIT (0RGB1555)", 1:"32 BIT (XRGB8888)", 2:"16 BIT (RGB565)"}
        self.lbl_pixfmt = QLabel(pf_map.get(state.pixel_format, "감지 전"))
        self.lbl_pixfmt.setStyleSheet("color:#aaaaaa;font-family:'Courier New';font-size:15px;")
        cd_h.addWidget(cd_lbl); cd_h.addWidget(self.lbl_pixfmt); cd_h.addStretch()
        root.addWidget(cd_row); _add_sep()

        # ── RESOLUTION (읽기 전용) ───────────────────────────
        res_row = QWidget(); res_row.setFixedHeight(ROW_H)
        res_h = QHBoxLayout(res_row); res_h.setContentsMargins(4,0,4,0); res_h.setSpacing(8)
        res_lbl = QLabel("RESOLUTION"); res_lbl.setStyleSheet(LBL_SS); res_lbl.setFixedWidth(LBL_W)
        self.lbl_res = QLabel(f"{state.width} × {state.height}")
        self.lbl_res.setStyleSheet("color:#aaaaaa;font-family:'Courier New';font-size:15px;")
        res_h.addWidget(res_lbl); res_h.addWidget(self.lbl_res); res_h.addStretch()
        root.addWidget(res_row); _add_sep()

        # ── SCALE MODE ──────────────────────────────────────
        _scales = ["FILL", "ASPECT", "INTEGER"]
        def _get_scale(): return _scales.index(settings.video_scale_mode.upper()) if settings.video_scale_mode.upper() in _scales else 0
        def _set_scale(i): settings.video_scale_mode = _scales[i].capitalize(); self.log(f"🖥 Scale→{settings.video_scale_mode}"); save_config()
        root.addWidget(_make_row("SCALE MODE", _scales, _get_scale, _set_scale,
                                 "FILL=전체채움 / ASPECT=비율유지 / INTEGER=정수배"))
        _add_sep()

        # ── SCANLINES (CRT) ─────────────────────────────────
        _scan_vals = ["NO", "LIGHT", "HEAVY"]
        def _get_scan():
            if not settings.video_crt_mode: return 0
            return 1 if settings.video_crt_intensity <= 0.5 else 2
        def _set_scan(i):
            if i == 0:
                settings.video_crt_mode = False
            else:
                settings.video_crt_mode = True
                settings.video_crt_intensity = 0.35 if i == 1 else 0.65
            self.log(f"📺 Scanlines → {_scan_vals[i]}")
        root.addWidget(_make_row("SCANLINES", _scan_vals, _get_scan, _set_scan))
        _add_sep()

        # ── INTERPOLATION (Smooth) ──────────────────────────
        _interp_vals = ["NO", "YES"]
        def _get_interp(): return 1 if settings.video_smooth else 0
        def _set_interp(i):
            settings.video_smooth = bool(i)
            self.log(f"🖥 Interpolation → {'ON' if i else 'OFF'}")
        root.addWidget(_make_row("INTERPOLATION", _interp_vals, _get_interp, _set_interp,
                                 "Bilinear Filtering"))
        _add_sep()

        # ── FRAMESKIP ───────────────────────────────────────
        _fskip_vals = ["AUTO", "OFF", "1", "2", "3", "4", "5"]
        def _get_fskip():
            v = settings.video_frameskip
            if v == -1: return 0   # AUTO
            if v == 0:  return 1   # OFF
            return min(v + 1, 6)
        def _set_fskip(i):
            settings.video_frameskip = -1 if i == 0 else (0 if i == 1 else i - 1)
            self.log(f"⏩ Frameskip → {_fskip_vals[i]}")
        root.addWidget(_make_row("FRAMESKIP", _fskip_vals, _get_fskip, _set_fskip))
        _add_sep()

        # ── VSYNC ───────────────────────────────────────────
        _vsync_vals = ["NO", "YES"]
        def _get_vsync(): return 1 if settings.video_vsync else 0
        def _set_vsync(i):
            settings.video_vsync = bool(i)
            # QOpenGLWidget swap interval 적용
            if hasattr(self, 'canvas'):
                fmt = self.canvas.format()
                fmt.setSwapInterval(1 if i else 0)
                self.canvas.setFormat(fmt)
            self.log(f"🖥 VSync → {'ON' if i else 'OFF'}")
        root.addWidget(_make_row("VSYNC", _vsync_vals, _get_vsync, _set_vsync))
        _add_sep()

        # ── GLSL SHADER ──────────────────────────────────────
        sh_row = QWidget(); sh_row.setFixedHeight(ROW_H)
        sh_h = QHBoxLayout(sh_row); sh_h.setContentsMargins(4,0,4,0); sh_h.setSpacing(8)
        sh_lbl = QLabel("GLSL SHADER"); sh_lbl.setStyleSheet(LBL_SS); sh_lbl.setFixedWidth(LBL_W)

        sh_name = os.path.basename(settings.video_shader_path) if settings.video_shader_path else "NONE"
        self._lbl_shader = QLabel(sh_name)
        self._lbl_shader.setStyleSheet(VAL_SS)
        self._lbl_shader.setFixedWidth(200)

        btn_sh_pick = QPushButton("📂 불러오기"); btn_sh_pick.setFixedWidth(110)
        btn_sh_pick.setStyleSheet(BTN_SS)
        btn_sh_clear = QPushButton("✕ 해제"); btn_sh_clear.setFixedWidth(80)
        btn_sh_clear.setStyleSheet(BTN_SS)
        self._lbl_shader_err = QLabel("")
        self._lbl_shader_err.setStyleSheet("color:#ff6644;font-size:11px;font-family:'Courier New';")

        def _shader_pick():
            path, _ = QFileDialog.getOpenFileName(
                self, "GLSL 쉐이더 파일 선택", CURRENT_PATH,
                "GLSL Shader (*.glsl *.frag *.fs *.vert *.vs);;All Files (*)")
            if not path: return
            settings.video_shader_path = path
            self._lbl_shader.setText(os.path.basename(path))
            self._lbl_shader_err.setText("")
            self.canvas.reload_shader()
            err = self.canvas._shader_error
            if err:
                self._lbl_shader_err.setText(f"⚠ {err[:60]}")
                self.log(f"⚠ 쉐이더 오류: {err}")
            else:
                self.log(f"✅ 쉐이더 로드: {os.path.basename(path)}")
            save_config()

        def _shader_clear():
            settings.video_shader_path = ""
            self._lbl_shader.setText("NONE")
            self._lbl_shader_err.setText("")
            self.canvas.reload_shader()
            self.log("🖥 쉐이더 해제 → 기본 파이프라인")
            save_config()

        btn_sh_pick.clicked.connect(_shader_pick)
        btn_sh_clear.clicked.connect(_shader_clear)
        sh_h.addWidget(sh_lbl); sh_h.addWidget(self._lbl_shader)
        sh_h.addWidget(btn_sh_pick); sh_h.addWidget(btn_sh_clear)
        sh_h.addWidget(self._lbl_shader_err); sh_h.addStretch()
        root.addWidget(sh_row); _add_sep()

        # ── TRIPLE BUFFERING (미구현) ────────────────────────
        tb_row = QWidget(); tb_row.setFixedHeight(ROW_H)
        tb_h = QHBoxLayout(tb_row); tb_h.setContentsMargins(4,0,4,0); tb_h.setSpacing(8)
        tb_lbl = QLabel("TRIPLE BUFFERING"); tb_lbl.setStyleSheet(LBL_SS + "color:#445566;"); tb_lbl.setFixedWidth(LBL_W)
        tb_val = QLabel("N/A")
        tb_val.setStyleSheet("color:#334455;font-family:'Courier New';font-size:15px;")
        tb_note = QLabel("(OS/드라이버 레벨 — Qt에서 직접 제어 불가)")
        tb_note.setStyleSheet("color:#334455;font-size:12px;font-family:'Courier New';")
        tb_h.addWidget(tb_lbl); tb_h.addWidget(tb_val); tb_h.addWidget(tb_note); tb_h.addStretch()
        root.addWidget(tb_row)

        root.addStretch()
        return w

    def _on_scale_mode(self, txt):
        settings.video_scale_mode = txt; self.log(f"🖥 Scale→{txt}"); save_config()

    def _on_crt_toggle(self, c):
        settings.video_crt_mode = c; self.log(f"📺 CRT {'ON' if c else 'OFF'}")

    def _on_crt_intensity(self, val):
        settings.video_crt_intensity = val / 10.0
        if hasattr(self, 'lbl_crt_val'): self.lbl_crt_val.setText(f"{val*10}%")

    def _refresh_video_labels(self):
        if not hasattr(self, 'lbl_pixfmt'): return
        pf_map = {0:"16 BIT (0RGB1555)", 1:"32 BIT (XRGB8888)", 2:"16 BIT (RGB565)"}
        self.lbl_pixfmt.setText(pf_map.get(state.pixel_format, f"fmt {state.pixel_format}"))
        self.lbl_res.setText(f"{state.width} × {state.height}")

    # ── ④ AUDIO ─────────────────────────────────────────────
    def _tab_audio(self) -> QWidget:
        w=QWidget(); v=QVBoxLayout(w); v.setContentsMargins(14,10,14,10); v.setSpacing(10)
        grp=QGroupBox("Audio Settings"); fl=QFormLayout(grp); fl.setSpacing(10)
        self.sld_vol=QSlider(Qt.Horizontal); self.sld_vol.setRange(0,100)
        self.sld_vol.setValue(settings.audio_volume)
        self.lbl_vol=QLabel(f"{settings.audio_volume}%"); self.lbl_vol.setStyleSheet("color:#00ffff;min-width:38px;")
        self.sld_vol.valueChanged.connect(self._on_volume_change)
        vol_row=QHBoxLayout(); vol_row.addWidget(self.sld_vol); vol_row.addWidget(self.lbl_vol)
        self.cmb_rate=QComboBox(); self.cmb_rate.addItems(["22050","44100","48000"])
        self.cmb_rate.setCurrentText(str(settings.audio_sample_rate)); self.cmb_rate.setFixedWidth(110)
        # DRC 버퍼 크기 슬라이더 (32~256ms)
        self.sld_buf=QSlider(Qt.Horizontal); self.sld_buf.setRange(32,256)
        self.sld_buf.setValue(settings.audio_buffer_ms); self.sld_buf.setSingleStep(8)
        self.lbl_buf=QLabel(f"{settings.audio_buffer_ms}ms"); self.lbl_buf.setStyleSheet("color:#00ffff;min-width:45px;")
        self.sld_buf.valueChanged.connect(lambda v: (setattr(settings,'audio_buffer_ms',v), self.lbl_buf.setText(f"{v}ms")))
        buf_row=QHBoxLayout(); buf_row.addWidget(self.sld_buf); buf_row.addWidget(self.lbl_buf)
        reinit=QPushButton("🔁 오디오 재초기화"); reinit.setFixedWidth(155); reinit.clicked.connect(self._reinit_audio)
        status="✅ 활성화 (QAudioSink+DRC)" if AUDIO_AVAILABLE else "❌ QtMultimedia 없음"
        self.audio_status=QLabel(status)
        self.audio_status.setStyleSheet("color:#00ff88;" if AUDIO_AVAILABLE else "color:#ff4444;")
        fl.addRow(QLabel("볼륨:"),vol_row); fl.addRow(QLabel("샘플레이트:"),self.cmb_rate)
        fl.addRow(QLabel("DRC 버퍼:"),buf_row)
        fl.addRow(reinit,self.audio_status)
        v.addWidget(grp); v.addStretch(); return w

    def _on_volume_change(self,val):
        settings.audio_volume=val; self.lbl_vol.setText(f"{val}%")
        if audio_sink:
            try: audio_sink.setVolume(val / 100.0)
            except: pass

    def _reinit_audio(self):
        settings.audio_sample_rate=int(self.cmb_rate.currentText())
        self._init_audio(); save_config()
        self.log(f"🔊 오디오 재초기화 ({settings.audio_sample_rate}Hz)")
        if hasattr(self,'audio_status') and AUDIO_AVAILABLE:
            self.audio_status.setText("✅ 재초기화 완료")

    # ── ⑤ MACHINE ──────────────────────────────────────────
    def _tab_machine(self) -> QWidget:
        self._machine_widget = QWidget()
        self._rebuild_machine_tab(); return self._machine_widget

    def _rebuild_machine_tab(self):
        old=self._machine_widget.layout()
        if old:
            while old.count():
                item=old.takeAt(0)
                if item.widget(): item.widget().deleteLater()
            QWidget().setLayout(old)
        v=QVBoxLayout(self._machine_widget); v.setContentsMargins(14,10,14,10); v.setSpacing(10)
        grp1=QGroupBox("지역 / 시스템"); fl1=QFormLayout(grp1); fl1.setSpacing(8)
        self.cmb_region=QComboBox(); self.cmb_region.addItems(["USA","Japan","Europe","Asia","World"])
        self.cmb_region.setCurrentText(settings.region); self.cmb_region.setFixedWidth(120)
        self.cmb_region.currentTextChanged.connect(
            lambda t:(setattr(settings,'region',t),save_config(),self.log(f"⚙ 지역: {t}")))
        fl1.addRow(QLabel("지역:"),self.cmb_region); v.addWidget(grp1)
        grp2=QGroupBox("DIP Switches"); dip_v=QVBoxLayout(grp2); dip_v.setSpacing(6)
        self.dip_combos.clear()
        if state.dip_variables:
            scroll=QScrollArea(); scroll.setWidgetResizable(True)
            dip_inner=QWidget(); dip_form=QFormLayout(dip_inner); dip_form.setSpacing(5)
            for key,(desc,opts,cur) in state.dip_variables.items():
                cmb=QComboBox(); cmb.addItems(opts); cmb.setCurrentText(cur); cmb.setMaximumWidth(200)
                cmb.currentTextChanged.connect(lambda t,k=key:self._on_dip_change(k,t))
                short_key=key.split('-')[-1] if '-' in key else key
                dip_form.addRow(QLabel(f"{desc} [{short_key}]:"),cmb)
                self.dip_combos[key]=cmb
            scroll.setWidget(dip_inner); dip_v.addWidget(scroll)
        else:
            dip_v.addWidget(QLabel("게임 로드 후 DIP 스위치가 표시됩니다."))
        # 저장 버튼은 항상 표시 (게임 로드 여부 무관)
        def _save_dip_now():
            if not self.selected_game:
                self.log("⚠ 게임을 먼저 선택하세요"); return
            if not state.dip_variables:
                self.log("⚠ DIP 스위치 없음 (게임 로드 후 사용)"); return
            game_dip_settings[self.selected_game] = {
                k: v[2] for k, v in state.dip_variables.items()}
            save_config()
            self.log(f"💾 DIP 저장됨: {self.selected_game}")
        apply_dip = QPushButton("💾  DIP 설정 저장  (다음 실행부터 자동 적용)")
        apply_dip.setStyleSheet(
            "QPushButton{background:#003300;color:#00ff88;border:1px solid #006600;"
            "font-family:'Courier New';font-size:15px;font-weight:bold;min-height:30px;}"
            "QPushButton:hover{background:#005500;color:#ffffff;}")
        apply_dip.clicked.connect(_save_dip_now)
        dip_v.addWidget(apply_dip)
        v.addWidget(grp2)
        grp3=QGroupBox("코어 정보"); fl3=QFormLayout(grp3); fl3.setSpacing(5)
        for k,val in [("코어","FBNeo"),("API","Libretro"),("코어파일",CORE_LIB),("버전","v1.8")]:
            lbl=QLabel(val); lbl.setStyleSheet("color:#00ffff;"); fl3.addRow(QLabel(k+":"),lbl)
        v.addWidget(grp3); v.addStretch()

    def _on_dip_change(self,key,val):
        if key in state.dip_variables:
            desc,opts,_=state.dip_variables[key]; state.dip_variables[key]=(desc,opts,val)
        # 게임별 DIP 설정 자동 저장
        if self.selected_game:
            if self.selected_game not in game_dip_settings:
                game_dip_settings[self.selected_game] = {}
            game_dip_settings[self.selected_game][key] = val
            save_config()
        self.log(f"⚙ DIP [{key.split('-')[-1]}] = {val}  (저장됨)")

    # ── ⑥ SHOT FACTORY ──────────────────────────────────────
    def _tab_shotfactory(self) -> QWidget:
        w=QWidget(); v=QVBoxLayout(w); v.setContentsMargins(14,8,14,8); v.setSpacing(6)
        title=QLabel("📸  SHOT FACTORY"); title.setStyleSheet("color:#00ffff;font-size:16px;font-weight:bold;")
        v.addWidget(title)

        # 스크린샷 경로
        v.addWidget(QLabel("스크린샷 저장 경로:"))
        row=QHBoxLayout(); self.e_shot_path=QLineEdit(settings.screenshot_path)
        bb=QPushButton("Browse"); bb.setFixedWidth(68); bb.clicked.connect(lambda:self._browse_dir(self.e_shot_path))
        row.addWidget(self.e_shot_path); row.addWidget(bb); v.addLayout(row)

        fmt_row=QHBoxLayout(); self.cmb_fmt=QComboBox()
        self.cmb_fmt.addItems(["PNG","JPG","BMP"]); self.cmb_fmt.setFixedWidth(90)
        fmt_row.addWidget(QLabel("포맷:")); fmt_row.addWidget(self.cmb_fmt); fmt_row.addStretch()
        v.addLayout(fmt_row)

        # 버튼 3종
        grp_btns = QGroupBox("캡처 / 저장")
        btn_v = QVBoxLayout(grp_btns)

        sb = QPushButton("📷  스크린샷 저장  (F12)")
        sb.clicked.connect(self._take_screenshot)
        btn_v.addWidget(sb)

        pb = QPushButton("🖼  현재 프레임 → 프리뷰 이미지로 저장  (F10)")
        pb.clicked.connect(self._save_as_preview_image)
        btn_v.addWidget(pb)

        self._record_btn = QPushButton("🎬  프리뷰 영상 녹화 시작  (F9)")
        self._record_btn.clicked.connect(self._toggle_record)
        self._record_btn.setStyleSheet("color:#ff6666;")
        btn_v.addWidget(self._record_btn)

        rec_note = QLabel("녹화: AVI(내장 MJPEG+Audio) → GIF(pillow) 자동 선택 / 시간 제한 없음")
        rec_note.setStyleSheet("color:#555577;font-size:13px;")
        btn_v.addWidget(rec_note)
        v.addWidget(grp_btns)

        self.shot_status=QLabel(""); self.shot_status.setStyleSheet("color:#00ff88;font-size:14px;")
        v.addWidget(self.shot_status)

        # 저장 진행률 표시바
        self._save_prog = QProgressBar()
        self._save_prog.setRange(0, 100); self._save_prog.setValue(0)
        self._save_prog.setMaximumHeight(10)
        self._save_prog.setTextVisible(False)
        self._save_prog.setStyleSheet(
            "QProgressBar{background:#000022;border:1px solid #000066;border-radius:2px;}"
            "QProgressBar::chunk{background:#0044ff;border-radius:2px;}")
        self._save_prog.setVisible(False)
        v.addWidget(self._save_prog)
        v.addWidget(QLabel("최근 스크린샷:"))
        self.shot_list=QListWidget(); self.shot_list.setMaximumHeight(90)
        self._refresh_shot_list(); v.addWidget(self.shot_list)
        return w

    # ── ⑦ CHEATS ─────────────────────────────────────────────
    def _tab_cheats(self) -> QWidget:
        """
        치트 탭 — INI 파일 로드 후 체크박스로 ON/OFF
        [📂 INI 파일 로드] 버튼으로 게임별 .ini 치트 파일 불러오기
        체크 후 [▶ 게임에 적용] → RAM 직접 패치 (매 프레임 유지)
        """
        self._cheat_widget = QWidget()
        self._rebuild_cheat_tab()
        return self._cheat_widget

    def _rebuild_cheat_tab(self):
        # 기존 레이아웃 교체
        old = self._cheat_widget.layout()
        if old:
            while old.count():
                item = old.takeAt(0)
                if item.widget(): item.widget().deleteLater()
            QWidget().setLayout(old)

        v = QVBoxLayout(self._cheat_widget)
        v.setContentsMargins(14, 8, 14, 8); v.setSpacing(8)

        # 헤더: 게임명
        game = self.selected_game or ""
        self._cheat_game_lbl = QLabel(
            f"게임: {get_display_name(game) if game else '(게임 미선택)'}")
        self._cheat_game_lbl.setStyleSheet("color:#00ffff;font-size:15px;font-weight:bold;")
        v.addWidget(self._cheat_game_lbl)

        # 상태
        self._cheat_status = QLabel("[📂 INI 파일 로드] → 체크박스 선택 → [▶ 게임에 적용]")
        self._cheat_status.setStyleSheet("color:#556677;font-size:13px;font-family:'Courier New';")
        v.addWidget(self._cheat_status)

        # 치트 목록
        grp = QGroupBox("치트 목록  (INI 파일 로드 후 표시 — 추가/삭제 가능)")
        grp_v = QVBoxLayout(grp)
        self._cheat_list = QListWidget()
        self._cheat_list.setStyleSheet(
            "QListWidget{background:#000010;border:1px solid #003366;color:#88bbdd;"
            "font-size:14px;font-family:'Courier New';}"
            "QListWidget::item{padding:5px 8px;}"
            "QListWidget::item:selected{background:#002244;color:#ffffff;}"
            "QListWidget::item:hover{background:#001133;}")
        grp_v.addWidget(self._cheat_list)
        v.addWidget(grp, 1)

        # 버튼 바
        BTN = ("QPushButton{background:#000044;color:#99bbff;border:1px solid #0044aa;"
               "font-size:14px;min-height:30px;font-family:'Courier New';border-radius:0px;}"
               "QPushButton:hover{background:#0000aa;color:#ffffff;border-color:#00aaff;}")
        btn_row = QHBoxLayout(); btn_row.setSpacing(6)

        btn_apply   = QPushButton("▶  게임에 적용")
        btn_reset   = QPushButton("⏹  모두 해제")
        btn_add     = QPushButton("➕  코드 추가")
        btn_del     = QPushButton("🗑  선택 삭제")
        btn_file    = QPushButton("📂  INI 파일 로드")
        btn_apply.setStyleSheet(BTN.replace("#000044","#002200").replace("#0044aa","#006600")
                                   .replace("#99bbff","#00ff88"))
        btn_file.setStyleSheet(BTN.replace("#000044","#001122").replace("#0044aa","#004466")
                                  .replace("#99bbff","#55ccff"))
        for b in [btn_reset, btn_add, btn_del]: b.setStyleSheet(BTN)
        btn_row.addWidget(btn_apply); btn_row.addWidget(btn_reset)
        btn_row.addWidget(btn_add);   btn_row.addWidget(btn_del)
        btn_row.addWidget(btn_file)
        btn_apply.clicked.connect(self._cheat_apply)
        btn_reset.clicked.connect(self._cheat_reset_all)
        btn_add.clicked.connect(self._cheat_add_entry)
        btn_del.clicked.connect(self._cheat_del_entry)
        btn_file.clicked.connect(self._cheat_load_ini_dialog)
        v.addLayout(btn_row)

        # 안내
        note = QLabel(
            "INI 치트 파일: [📂 INI 파일 로드] 버튼으로 게임별 .ini 파일 불러오기\n"
            "커스텀 코드 직접 추가: 포맷 0xADDRESS=0xVALUE\n"
            "게임 실행 중 [▶ 게임에 적용] 클릭 → 체크된 치트를 매 프레임 RAM 패치")
        note.setStyleSheet("color:#334455;font-size:13px;font-family:'Courier New';")
        note.setWordWrap(True)
        v.addWidget(note)

    @staticmethod
    def _find_cheat_ini(game_id: str) -> tuple:
        """cheats/{game_id}.ini 탐색. 없으면 부모롬 ini 탐색.
        반환: (ini_path, used_id)  —  찾지 못하면 (None, None)

        부모롬 탐색 방식: FBNeo 명명 규칙상 자식롬은 부모롬명에 접미사를 붙임
        (예: kof97k → kof97, mslug3h → mslug3 또는 mslug)
        뒤에서 한 글자씩 제거하며 .ini 파일이 존재하는 가장 긴 이름을 부모로 간주."""
        cheats_dir = os.path.join(CURRENT_PATH, "cheats")
        # 1순위: 자기 자신
        p = os.path.join(cheats_dir, f"{game_id}.ini")
        if os.path.isfile(p):
            return p, game_id
        # 2순위: 접미사를 줄여가며 부모롬 탐색 (최소 3자)
        name = game_id
        while len(name) > 3:
            name = name[:-1]
            p = os.path.join(cheats_dir, f"{name}.ini")
            if os.path.isfile(p):
                return p, name
        return None, None

    def _auto_load_game_cheats(self, game_id: str):
        """게임 선택 시 ini 자동 로드 (자식롬은 부모롬 ini 공유, 치트 목록 교체)."""
        if not hasattr(self, '_cheat_list'): return
        self._cheat_list.clear()
        ini_path, used_id = self._find_cheat_ini(game_id)
        if ini_path is None:
            if hasattr(self, '_cheat_status'):
                self._cheat_status.setText(
                    f"cheats/{game_id}.ini 없음  —  [📂 INI 파일 로드] 또는 직접 배치")
            return
        cheats = self._parse_cheat_ini(ini_path)
        for name, code in cheats:
            item = QListWidgetItem(f"  {name}")
            item.setData(Qt.UserRole, (name, code))
            item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
            item.setCheckState(Qt.Unchecked)
            item.setToolTip(f"코드: {code}")
            self._cheat_list.addItem(item)
        if hasattr(self, '_cheat_status'):
            if cheats:
                # 부모롬 ini를 사용한 경우 표시에 명시
                src = f"cheats/{used_id}.ini" + (
                    f"  (부모롬: {used_id})" if used_id != game_id else "")
                self._cheat_status.setText(
                    f"✅ {len(cheats)}개 치트 로드됨  [{src}]  —  체크 후 [▶ 게임에 적용]")
            else:
                self._cheat_status.setText(
                    f"⚠ {used_id}.ini 에서 치트를 찾을 수 없음")

    def _cheat_apply(self):
        """체크된 치트를 RAM 직접 패치 방식으로 즉시 적용 (매 프레임 유지)"""
        if not self.core or not state.game_loaded:
            if hasattr(self, '_cheat_status'):
                self._cheat_status.setText("⚠ 게임 실행 중에만 적용 가능 (LAUNCH 후 사용)")
            return
        try:
            new_cheats = []
            for i in range(self._cheat_list.count()):
                item = self._cheat_list.item(i)
                if item.checkState() == Qt.Checked:
                    _, code = item.data(Qt.UserRole)
                    pairs = self._parse_cheat_code(code)
                    new_cheats.extend(pairs)
            state.active_cheats = new_cheats
            if new_cheats:
                self._apply_active_cheats()
            if hasattr(self, '_cheat_status'):
                self._cheat_status.setText(
                    f"✅ {len(new_cheats)}개 패치 적용 (매 프레임 유지)" if new_cheats
                    else "⚠ 체크된 치트 없음")
        except Exception as e:
            if hasattr(self, '_cheat_status'):
                self._cheat_status.setText(f"❌ 적용 오류: {e}")

    def _cheat_reset_all(self):
        """모든 치트 해제 & 체크 초기화"""
        state.active_cheats = []   # 매 프레임 재적용 중단
        for i in range(self._cheat_list.count()):
            self._cheat_list.item(i).setCheckState(Qt.Unchecked)
        if hasattr(self, '_cheat_status'):
            self._cheat_status.setText("⏹ 모든 치트 해제됨")

    def _cheat_add_entry(self):
        """커스텀 치트 코드 직접 추가"""
        from PySide6.QtWidgets import QDialog, QDialogButtonBox
        dlg = QDialog(self)
        dlg.setWindowTitle("치트 코드 추가")
        dlg.setMinimumWidth(400)
        dlg.setStyleSheet("background:#000020;color:#aaddff;font-family:'Courier New';font-size:14px;")
        vl = QVBoxLayout(dlg)
        vl.addWidget(QLabel("이름  (예: P1 무한 체력):"))
        e_name = QLineEdit(); e_name.setPlaceholderText("치트 이름")
        vl.addWidget(e_name)
        vl.addWidget(QLabel("코드  (RetroArch 포맷  예: 0xFF1234=0xFF):"))
        e_code = QLineEdit(); e_code.setPlaceholderText("0XADDRESS=0XVALUE")
        vl.addWidget(e_code)
        hint = QLabel("힌트: RetroArch 치트 DB의 cheat_code 값을 그대로 입력 가능")
        hint.setStyleSheet("color:#334466;font-size:12px;"); vl.addWidget(hint)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(dlg.accept); bb.rejected.connect(dlg.reject)
        vl.addWidget(bb)
        if dlg.exec() == QDialog.Accepted:
            name = e_name.text().strip()
            code = e_code.text().strip()
            if name and code:
                item = QListWidgetItem(f"  {name}")
                item.setData(Qt.UserRole, (name, code))
                item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
                item.setCheckState(Qt.Unchecked)
                item.setToolTip(f"코드: {code}")
                self._cheat_list.addItem(item)
                if hasattr(self, '_cheat_status'):
                    self._cheat_status.setText(f"➕ 추가됨: {name}")

    def _cheat_del_entry(self):
        row = self._cheat_list.currentRow()
        if row >= 0:
            self._cheat_list.takeItem(row)
            if hasattr(self, '_cheat_status'):
                self._cheat_status.setText("🗑 삭제됨")

    # ── RAM 직접 패치 ──────────────────────────────────────────
    RETRO_MEMORY_SYSTEM_RAM = 2

    def _apply_active_cheats(self):
        """활성 치트 목록을 system RAM에 직접 패치. _emu_loop에서 매 프레임 호출."""
        mem_ptr  = self.core.retro_get_memory_data(self.RETRO_MEMORY_SYSTEM_RAM)
        mem_size = self.core.retro_get_memory_size(self.RETRO_MEMORY_SYSTEM_RAM)
        if not mem_ptr or not mem_size:
            return
        # c_void_p → POINTER(c_uint8) 로 캐스트하여 개별 바이트 쓰기
        mem = ctypes.cast(mem_ptr, ctypes.POINTER(ctypes.c_uint8))
        for addr, val in state.active_cheats:
            offset = self._cpu_addr_to_offset(addr, mem_size)
            if offset is not None and 0 <= offset < mem_size:
                mem[offset] = val & 0xFF

    @staticmethod
    def _cpu_addr_to_offset(addr: int, mem_size: int):
        """CPU 절대 주소 → system RAM 바이트 오프셋 변환.

        FBNeo는 68000(빅엔디언) RAM을 x86(리틀엔디언)에서 16비트 워드 단위로
        바이트 스왑하여 저장 → 올바른 바이트 위치: (raw_offset ^ 1)

        NeoGeo work RAM: 0x100000-0x10FFFF
        CPS  work RAM:   0xFF0000-0xFFFFFF
        기타: 스왑 없이 직접 오프셋으로 시도."""
        if 0x100000 <= addr <= 0x10FFFF:   # NeoGeo 68000 work RAM
            return (addr - 0x100000) ^ 1
        if 0xFF0000 <= addr <= 0xFFFFFF:   # CPS 68000 work RAM
            return (addr - 0xFF0000) ^ 1
        if addr < mem_size:                 # 기타 하드웨어 (직접 오프셋)
            return addr
        return None

    @staticmethod
    def _parse_cheat_code(code: str) -> list:
        """치트 코드 문자열 → [(cpu_addr, value), ...] 파싱.
        지원 포맷: 0xADDR=0xVAL  또는  0xADDR+0xVAL (+ 구분 다중 패치)."""
        result = []
        for part in re.split(r'\+(?=0[xX])', code):
            part = part.strip()
            m = re.match(r'(0[xX][0-9A-Fa-f]+)\s*[=:]\s*(0[xX][0-9A-Fa-f]+|\d+)', part)
            if m:
                try:
                    result.append((int(m.group(1), 16), int(m.group(2), 0)))
                except ValueError:
                    pass
        return result

    # ── INI 치트 파일 로드 ─────────────────────────────────────
    def _cheat_load_ini_dialog(self):
        """INI 치트 파일 선택 다이얼로그 — 기존 목록 교체"""
        cheats_dir = os.path.join(CURRENT_PATH, "cheats")
        path, _ = QFileDialog.getOpenFileName(
            self, "치트 INI 파일 선택",
            cheats_dir if os.path.isdir(cheats_dir) else CURRENT_PATH,
            "INI 치트 파일 (*.ini);;모든 파일 (*.*)")
        if not path:
            return
        cheats = self._parse_cheat_ini(path)
        self._cheat_list.clear()          # 기존 목록 모두 제거 후 교체
        state.active_cheats = []          # 이전 적용 치트도 중단
        if not cheats:
            if hasattr(self, '_cheat_status'):
                self._cheat_status.setText("⚠ 파일에서 치트를 찾을 수 없음 (형식 확인 필요)")
            return
        for name, code in cheats:
            item = QListWidgetItem(f"  {name}")
            item.setData(Qt.UserRole, (name, code))
            item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
            item.setCheckState(Qt.Unchecked)
            item.setToolTip(f"코드: {code}\n출처: {os.path.basename(path)}")
            self._cheat_list.addItem(item)
        if hasattr(self, '_cheat_status'):
            self._cheat_status.setText(
                f"📂 {os.path.basename(path)} — {len(cheats)}개 치트 로드됨  ·  체크 후 [▶ 게임에 적용]")

    @staticmethod
    def _parse_cheat_ini(filepath: str) -> list:
        """
        INI 포맷 치트 파일 파싱 → [(이름, 코드문자열), ...]

        포맷:
            cheat "Name"
            default 0
            0 "Disabled"
            1 "Label",  offset, addr1, val1 [, offset2, addr2, val2 ...]
            2 "Label2", offset, addr1, val1 [, ...]

        규칙:
          - index 0 / "Disabled" 포함 항목 → 건너뜀
          - 파라미터 그룹: (offset[skip], address, value) 반복
          - 옵션이 1개: 치트 이름 그대로 사용
          - 옵션이 2개 이상: "치트명 — 옵션명" 형태로 분리
          - 이름이 공백이거나 유효한 주소가 없는 블록 → 건너뜀
        """
        cheats = []
        try:
            try:
                text = open(filepath, 'r', encoding='utf-8').read()
            except UnicodeDecodeError:
                text = open(filepath, 'r', encoding='cp949').read()

            lines = text.splitlines()
            i = 0
            while i < len(lines):
                line = lines[i].strip()

                # ── cheat "Name" 블록 시작 ──
                m = re.match(r'^cheat\s+"([^"]*)"', line, re.IGNORECASE)
                if m:
                    cheat_name = m.group(1).strip()
                    options = []   # [(label, [(addr, val), ...]), ...]
                    i += 1

                    while i < len(lines):
                        l = lines[i].strip()
                        # 다음 cheat 블록 시작이면 현재 블록 종료
                        if re.match(r'^cheat\s+"', l, re.IGNORECASE):
                            break

                        # N "Label" [, offset, addr, val, offset, addr, val ...]
                        em = re.match(r'^(\d+)\s+"([^"]*)"(.*)', l)
                        if em:
                            idx  = int(em.group(1))
                            lbl  = em.group(2).strip()
                            rest = em.group(3)

                            # index 0 또는 Disabled 항목 건너뜀
                            if idx == 0 or re.search(r'[Dd]isabl', lbl):
                                i += 1
                                continue

                            # 파라미터 파싱: (offset, addr, val) 그룹 반복
                            params = [p.strip() for p in rest.split(',') if p.strip()]
                            patches = []
                            j = 0
                            while j + 2 < len(params):
                                try:
                                    # params[j]   = offset  (건너뜀)
                                    # params[j+1] = address
                                    # params[j+2] = value
                                    addr = int(params[j + 1], 0)
                                    val  = int(params[j + 2], 0)
                                    patches.append((addr, val))
                                except (ValueError, IndexError):
                                    pass
                                j += 3

                            if patches:
                                options.append((lbl, patches))

                        i += 1

                    # 이름이 공백이거나 유효 옵션이 없으면 건너뜀
                    if not cheat_name or not options:
                        continue

                    if len(options) == 1:
                        _, patches = options[0]
                        code = "+".join(f"0x{a:X}=0x{v:X}" for a, v in patches)
                        cheats.append((cheat_name, code))
                    else:
                        # 다중 옵션: "치트명 — 옵션명" 으로 각각 분리
                        for lbl, patches in options:
                            name = f"{cheat_name} — {lbl}"
                            code = "+".join(f"0x{a:X}=0x{v:X}" for a, v in patches)
                            cheats.append((name, code))

                    continue

                i += 1

        except Exception:
            pass
        return cheats

    def _take_screenshot(self):
        shot_dir = self.e_shot_path.text() if hasattr(self,'e_shot_path') else settings.screenshot_path
        fmt_str  = self.cmb_fmt.currentText().lower() if hasattr(self,'cmb_fmt') else 'png'
        settings.screenshot_path=shot_dir; os.makedirs(shot_dir,exist_ok=True)
        if not state.video_buffer:
            if hasattr(self,'shot_status'): self.shot_status.setText("⚠ 게임 실행 중 아님"); return
        try:
            stamp=datetime.now().strftime("%Y%m%d_%H%M%S")
            fname=os.path.join(shot_dir,f"shot_{stamp}.{fmt_str}")
            img=self._current_qimage()
            if img and img.save(fname):
                self.log(f"📸 {fname}"); self._refresh_shot_list()
                if hasattr(self,'shot_status'): self.shot_status.setText(f"✅ {os.path.basename(fname)}")
            else:
                if hasattr(self,'shot_status'): self.shot_status.setText("❌ 저장 실패")
        except Exception as e:
            if hasattr(self,'shot_status'): self.shot_status.setText(f"❌ {e}")

    def _save_as_preview_image(self):
        if not state.video_buffer:
            if hasattr(self,'shot_status'): self.shot_status.setText("⚠ 게임 실행 중 아님"); return
        if not self.selected_game:
            if hasattr(self,'shot_status'): self.shot_status.setText("⚠ 게임을 먼저 선택하세요"); return
        try:
            os.makedirs(settings.preview_path, exist_ok=True)
            fname = os.path.join(settings.preview_path, f"{self.selected_game}.png")
            img = self._current_qimage()
            if img and img.save(fname):
                self.log(f"🖼 프리뷰 이미지 저장: {fname}")
                if hasattr(self,'shot_status'): self.shot_status.setText(f"✅ 프리뷰 저장: {self.selected_game}.png")
                # 현재 게임의 프리뷰 즉시 갱신
                self._preview_img_path = fname
                px=QPixmap(fname).scaled(380,220,Qt.KeepAspectRatio,Qt.SmoothTransformation)
                self.preview_label.setPixmap(px)
                self.preview_stack.setCurrentIndex(0)
            else:
                if hasattr(self,'shot_status'): self.shot_status.setText("❌ 저장 실패")
        except Exception as e:
            if hasattr(self,'shot_status'): self.shot_status.setText(f"❌ {e}")

    # ── 녹화 ─────────────────────────────────────────────────
    def _toggle_record(self):
        if self._recording: self._stop_record()
        else: self._start_record()

    def _start_record(self):
        if not state.video_buffer:
            self.log("⚠ 게임 실행 중 아님 (녹화 불가)"); return
        self._recording = True
        self._record_pairs = []
        self._record_frame_skip = 0
        state.record_audio_buf = bytearray()
        state.is_recording = True
        # 게임 화면 오버레이 (녹화 버퍼에는 포함 안됨)
        self._rec_overlay.resize(self.canvas.size())
        self._rec_overlay.show_start()
        if hasattr(self,'_record_btn'):
            self._record_btn.setText("⏹  녹화 중지  (F9)")
            self._record_btn.setStyleSheet(
                "QPushButton{color:#ff2222;font-weight:bold;background:#220000;"
                "border:1px solid #ff0000;min-height:28px;font-family:'Courier New';font-size:15px;}"
                "QPushButton:hover{background:#440000;}")
        if hasattr(self,'shot_status'): self.shot_status.setText("🔴 녹화 중... (F9로 중지)")
        self.log(f"🎬 녹화 시작 — F9로 중지  [{self.selected_game or '?'}]")

    def _stop_record(self):
        self._recording = False
        state.is_recording = False
        state.record_audio_buf = bytearray()   # 잔여 버퍼 초기화
        self._rec_overlay.show_stop()
        if hasattr(self,'_record_btn'):
            self._record_btn.setText("🎬  프리뷰 영상 녹화 시작  (F9)")
            self._record_btn.setStyleSheet("color:#ff6666;")
        pairs = list(self._record_pairs)   # 페어(영상+오디오) 복사 (스레드 안전)
        self._record_pairs = []
        n = len(pairs)
        total_audio = sum(len(p[1]) for p in pairs)
        self.log(f"🎬 녹화 중지 — {n}프레임 / 오디오 {total_audio//4}샘플")
        if n > 0:
            game = self.selected_game or "unknown"
            if hasattr(self,'shot_status'): self.shot_status.setText(f"💾 AVI 저장 중... ({n}f)")
            if hasattr(self,'_save_prog'):
                self._save_prog.setValue(0); self._save_prog.setVisible(True)
            import threading
            t = threading.Thread(target=self._save_video_bg, args=(pairs, game), daemon=True)
            t.start()
        else:
            if hasattr(self,'shot_status'): self.shot_status.setText("⚠ 저장할 프레임 없음")

    def _capture_record_frame(self):
        """영상 프레임 + 해당 구간 오디오를 페어로 함께 캡처 (AV싱크 보장)"""
        self._record_frame_skip += 1
        if self._record_frame_skip % 2 != 0: return
        if not state.video_buffer: return
        try:
            img = self._current_qimage()
            if img is None: return
            buf = QBuffer(); buf.open(QBuffer.WriteOnly)
            img.save(buf, "JPEG", 75)
            jpeg = bytes(buf.data())
            buf.close()
            # 이 프레임 구간에 쌓인 오디오를 즉시 뽑아냄 → 영상과 묶음
            audio_chunk = bytes(state.record_audio_buf)
            state.record_audio_buf = bytearray()
            self._record_pairs.append((jpeg, audio_chunk))
            n = len(self._record_pairs)
            if n % 30 == 0:
                secs = n // 30
                if hasattr(self, 'shot_status'):
                    self.shot_status.setText(f"🔴 녹화 중... {secs}초  ({n}프레임)")
        except: pass

    # ── 백그라운드 영상 저장 (UI 스레드 차단 없음) ──────────────
    def _save_video_bg(self, pairs: list, game: str):
        """백그라운드 스레드 — pairs=[(jpeg,audio_chunk),...] / QImage/Qt 금지"""
        def prog(pct): self._sig_save_progress.emit(pct)
        try:
            os.makedirs(settings.preview_path, exist_ok=True)
            N = len(pairs)

            # 1) Pure Python MJPEG AVI (의존성 없음, 페어 기반 AV싱크)
            try:
                path = os.path.join(settings.preview_path, f"{game}.avi")
                if self._write_mjpeg_avi(pairs, path, fps=30, progress_cb=prog):
                    has_audio = any(len(p[1]) > 0 for p in pairs)
                    suffix = "+audio" if has_audio else ""
                    self._sig_save_done.emit(f"✅ AVI{suffix}: {game}.avi ({N}f)", path)
                    return
            except Exception as e:
                self._sig_save_done.emit(f"❌ avi: {e}", ""); return

            # 2) Pillow GIF fallback (오디오 없음)
            try:
                from PIL import Image as _PI
                from io import BytesIO
                path = os.path.join(settings.preview_path, f"{game}.gif")
                pframes = []
                for i, (jpeg, _) in enumerate(pairs):
                    try:
                        pi = _PI.open(BytesIO(jpeg)).convert('RGB')
                        pframes.append(pi.quantize(colors=128, dither=1))
                    except: pass
                    prog(int((i + 1) / N * 100))
                if pframes:
                    pframes[0].save(path, save_all=True, append_images=pframes[1:],
                                    loop=0, duration=33)
                    self._sig_save_done.emit(f"✅ GIF: {game}.gif ({len(pframes)}f)", path)
                    return
            except ImportError: pass
            except Exception as e:
                self._sig_save_done.emit(f"❌ gif: {e}", ""); return

            self._sig_save_done.emit("❌ 저장 실패", "")
        except Exception as e:
            self._sig_save_done.emit(f"❌ 저장 오류: {e}", "")

    def _on_save_done(self, msg: str, filepath: str):
        """메인 스레드에서 UI 업데이트 (Signal 콜백)"""
        if hasattr(self, 'shot_status'): self.shot_status.setText(msg)
        if hasattr(self, '_save_prog'):
            self._save_prog.setValue(100)
            QTimer.singleShot(800, lambda: self._save_prog.setVisible(False))
        self.log(f"🎬 {msg}")
        if filepath:
            self._preview_vid_path = filepath

    def _on_save_progress(self, pct: int):
        """백그라운드 저장 진행률 업데이트"""
        if hasattr(self, '_save_prog'):
            self._save_prog.setValue(pct)

    def _set_turbo(self, action: str, enabled: bool):
        turbo_enabled[action] = enabled
        save_config()

    def _remap_gamepad_btn(self, phys_key, action: str):
        """게임패드 물리 버튼 → 액션 재매핑 (런타임 즉시 적용 + config 저장)"""
        if IS_LINUX and _linux_gp:
            LinuxGamepad._BTN_MAP[phys_key] = action
            # 전역 인스턴스에도 반영
            _linux_gp.__class__._BTN_MAP = LinuxGamepad._BTN_MAP
        elif IS_WINDOWS:
            xi_masks = {'XI_A': XI_A, 'XI_B': XI_B, 'XI_X': XI_X,
                        'XI_Y': XI_Y, 'XI_LB': XI_LB, 'XI_RB': XI_RB}
            mask = xi_masks.get(phys_key)
            if mask is not None:
                # 기존에 같은 mask가 있으면 제거 후 새 키로 추가
                global _XI_BTN_MAP
                _XI_BTN_MAP = {k: v for k, v in _XI_BTN_MAP.items() if v != mask}
                _XI_BTN_MAP[action] = mask
        save_config()
        self.log(f"🎮 게임패드 {phys_key} → {action}")

    def _update_turbo_speed(self, val: int):
        global turbo_period
        turbo_period = val
        if hasattr(self, '_turbo_spd_lbl'):
            self._turbo_spd_lbl.setText(f"{val}f")
        save_config()

    @staticmethod
    def _jpeg_wh(data: bytes):
        """JPEG 헤더에서 가로×세로 파싱 (QImage 없이, 백그라운드 스레드 안전)"""
        import struct
        i = 2  # SOI 마커(FF D8) 이후부터 탐색
        while i + 4 <= len(data):
            if data[i] != 0xFF:
                break
            m = data[i + 1]
            if m in (0xC0, 0xC1, 0xC2):  # SOF0/SOF1/SOF2
                h = struct.unpack('>H', data[i + 5:i + 7])[0]
                w = struct.unpack('>H', data[i + 7:i + 9])[0]
                return w, h
            seg_len = struct.unpack('>H', data[i + 2:i + 4])[0]
            i += 2 + seg_len
        return 320, 224   # 파싱 실패 시 기본값

    def _write_mjpeg_avi(self, pairs: list, path: str, fps: int = 30,
                         progress_cb=None):
        """Pure-Python MJPEG+PCM AVI writer — 오디오 균등 재분배로 강력한 AV싱크 보장
        pairs = [(jpeg_bytes, audio_bytes_for_this_frame), ...]"""
        if not pairs: return False
        import struct as _s
        jpeg_list = [p[0] for p in pairs]
        N         = len(pairs)
        W, H      = self._jpeg_wh(jpeg_list[0])
        max_sz    = max(len(j) for j in jpeg_list)
        p         = _s.pack

        def ck(fc, d):
            d = bytes(d); s = len(d)
            return fc[:4].encode() + p('<I', s) + d + (b'\x00' if s % 2 else b'')
        def lst(tp, d):
            inner = tp[:4].encode() + bytes(d)
            return b'LIST' + p('<I', len(inner)) + inner

        # ── 오디오 사전 처리: 전체 합산 후 프레임별 균등 재분배 ──────
        sr      = settings.audio_sample_rate   # 48000
        ch      = 2; bps = 16
        blk     = ch * bps // 8               # nBlockAlign = 4  (1스테레오샘플)
        avg_bps = sr * blk                    # nAvgBytesPerSec = 192000

        total_audio = b''.join(p2[1] for p2 in pairs)
        has_audio   = len(total_audio) > 0

        if has_audio:
            # 프레임당 정확한 오디오 바이트 수 계산 (blk 단위 정렬)
            # 이론값: 48000 * 4 / 30 = 6400 bytes/frame
            ideal = avg_bps / fps               # bytes per video frame (float)
            bpf   = max(blk, int(ideal) // blk * blk)   # blk-aligned bytes per frame

            # 균등 분배: 모든 프레임에 동일한 bpf 오디오 할당
            audio_list = []
            for i in range(N):
                chunk = total_audio[i * bpf : (i + 1) * bpf]
                # 마지막 청크가 짧으면 무음(0)으로 패딩
                if len(chunk) < bpf:
                    chunk = chunk + b'\x00' * (bpf - len(chunk))
                audio_list.append(chunk)
            # 실제 총 오디오 (패딩 포함)
            total_audio = b''.join(audio_list)
            total_blks  = len(total_audio) // blk
            max_audio_chunk = bpf
        else:
            audio_list = [b''] * N

        # ── 비디오 스트림 헤더 ─────────────────────────────────
        strh_v = (b'vids' + b'MJPG'
                  + p('<IHHIIIIII', 0, 0, 0, 0, 1, fps, 0, N, max_sz)
                  + p('<I', 0xFFFFFFFF) + p('<I', 0) + p('<hhhh', 0, 0, W, H))
        strf_v = p('<IiiHHIIiiII', 40, W, H, 1, 24, 0x47504A4D, W*H*3, 0, 0, 0, 0)
        strl_v = lst('strl', ck('strh', strh_v) + ck('strf', strf_v))

        # ── 오디오 스트림 헤더 ─────────────────────────────────
        if has_audio:
            strh_a = (b'auds' + p('<I', 1)
                      + p('<IHHIIIIII', 0, 0, 0, 0,
                           blk, avg_bps, 0, total_blks, max_audio_chunk)
                      + p('<I', 0xFFFFFFFF) + p('<I', blk) + p('<hhhh', 0, 0, 0, 0))
            strf_a = p('<HHIIHHh', 1, ch, sr, avg_bps, blk, bps, 0)
            strl_a = lst('strl', ck('strh', strh_a) + ck('strf', strf_a))
            n_streams = 2
        else:
            strl_a = b''; n_streams = 1

        # ── AVI 메인 헤더 ─────────────────────────────────────
        avih = (p('<IIIIIIII', 1000000 // fps, max_sz * fps, 0, 0x10,
                   N, 0, n_streams, max_sz)
                + p('<II', W, H) + p('<IIII', 0, 0, 0, 0))
        hdrl = lst('hdrl', ck('avih', avih) + strl_v + strl_a)

        # ── movi: 영상+오디오 프레임 단위 인터리브 ──────────────
        chunks = []
        for i, jpeg in enumerate(jpeg_list):
            chunks.append(ck('00dc', jpeg))
            if has_audio:
                chunks.append(ck('01wb', audio_list[i]))
            if progress_cb: progress_cb(int((i + 1) / N * 100))

        movi = lst('movi', b''.join(chunks))
        riff = b'AVI ' + hdrl + movi
        with open(path, 'wb') as f:
            f.write(b'RIFF' + p('<I', len(riff)) + riff)
        return True

    def _current_qimage(self):
        if not state.video_buffer: return None
        if state.pixel_format == 1:
            return QImage(bytes(state.video_buffer),state.width,state.height,state.pitch,QImage.Format_RGB32)
        elif state.pixel_format == 2:
            return QImage(bytes(state.video_buffer),state.width,state.height,state.pitch,QImage.Format_RGB16)
        else:
            return QImage(bytes(state.video_buffer),state.width,state.height,state.pitch,QImage.Format_RGB555)

    def _refresh_shot_list(self):
        if not hasattr(self,'shot_list'): return
        self.shot_list.clear()
        d = self.e_shot_path.text() if hasattr(self,'e_shot_path') else settings.screenshot_path
        if os.path.exists(d):
            files=sorted([f for f in os.listdir(d)
                          if f.lower().endswith(('.png','.jpg','.bmp'))],reverse=True)[:20]
            for f in files: self.shot_list.addItem(f)

    # ════════════════════════════════════════════════════════════
    #  프리뷰 — 이미지 3초 후 영상 전환
    # ════════════════════════════════════════════════════════════
    def _start_preview_video(self):
        if not self._preview_vid_path: return
        if not self._has_video_preview: return
        try:
            if self._preview_use_linux_player:
                # Linux: LinuxVideoPlayer — Qt Multimedia 없이 안전하게 재생
                if self._linux_player.load(self._preview_vid_path):
                    self.preview_stack.setCurrentIndex(1)
                    self._linux_player.play()
            else:
                # Windows: QMediaPlayer
                self.preview_player.setSource(QUrl.fromLocalFile(self._preview_vid_path))
                self.preview_stack.setCurrentIndex(1)
                self.preview_player.play()
        except Exception as e:
            self.log(f"⚠ 영상 재생 오류: {e}")

    def _on_preview_media_status(self, status):
        # Windows QMediaPlayer 전용 — 영상 종료 시 루프
        try:
            from PySide6.QtMultimedia import QMediaPlayer as _QMP
            if status == _QMP.EndOfMedia:
                self.preview_player.setPosition(0)
                self.preview_player.play()
        except: pass

    def _stop_preview(self):
        self.preview_img_timer.stop()
        if self._has_video_preview:
            try:
                if self._preview_use_linux_player:
                    self._linux_player.stop()
                else:
                    self.preview_player.stop()
            except: pass
        self.preview_stack.setCurrentIndex(0)

    # ════════════════════════════════════════════════════════════
    #  오디오 초기화
    # ════════════════════════════════════════════════════════════
    def _init_audio(self):
        global audio_sink, audio_io
        if not AUDIO_AVAILABLE: return
        try:
            _set_audio_thread_priority()
            if audio_sink:
                try: audio_sink.stop()
                except: pass
                audio_sink = None
            audio_io = None
            state.audio_pending.clear()

            sr = settings.audio_sample_rate
            fmt = QAudioFormat()
            fmt.setSampleRate(sr)
            fmt.setChannelCount(2)
            fmt.setSampleFormat(QAudioFormat.Int16)
            audio_sink = QAudioSink(QMediaDevices.defaultAudioOutput(), fmt)
            audio_sink.setVolume(settings.audio_volume / 100.0)
            # QAudioSink 내부 버퍼 = audio_buffer_ms 설정값과 일치
            audio_sink.setBufferSize(int(sr * settings.audio_buffer_ms / 1000 * 4))
            audio_io = audio_sink.start()
            if hasattr(self, 'log_view'):
                self.log(f"Audio OK (QAudioSink push, {sr}Hz)")
        except Exception as e:
            if hasattr(self, 'log_view'): self.log(f"⚠ Audio: {e}")

    # ════════════════════════════════════════════════════════════
    #  코어 초기화
    # ════════════════════════════════════════════════════════════
    def _setup_core(self):
        # 1순위: 번들 내부(_MEIPASS 또는 스크립트 폴더)
        # 2순위: exe/스크립트 옆에 별도 배치된 DLL (frozen 시 사용자가 직접 복사한 경우)
        _bundle_dll  = os.path.join(BUNDLE_PATH,  CORE_LIB)
        _current_dll = os.path.join(CURRENT_PATH, CORE_LIB)
        if os.path.exists(_bundle_dll):
            dll = _bundle_dll
        else:
            dll = _current_dll
        try:
            self.core=ctypes.CDLL(dll) if IS_LINUX else ctypes.CDLL(dll,winmode=0)

            # retro_run argtypes/restype 명시 — Linux에서 잘못된 호출 규약 방지
            self.core.retro_run.restype  = None
            self.core.retro_run.argtypes = []
            self.core.retro_init.restype  = None
            self.core.retro_init.argtypes = []
            self.core.retro_load_game.restype  = ctypes.c_bool
            self.core.retro_load_game.argtypes = [ctypes.POINTER(RetroGameInfo)]
            self.core.retro_unload_game.restype  = None
            self.core.retro_unload_game.argtypes = []

            self.core.retro_set_environment(self._ecb)
            self.core.retro_set_video_refresh(self._vcb)
            self.core.retro_set_audio_sample(self._acb)
            self.core.retro_set_audio_sample_batch(self._abcb)
            self.core.retro_set_input_poll(self._pcb)
            self.core.retro_set_input_state(self._scb)
            self.core.retro_init()
            # 치트 API argtypes 설정
            try:
                self.core.retro_cheat_reset.restype  = None
                self.core.retro_cheat_reset.argtypes = []
                self.core.retro_cheat_set.restype    = None
                self.core.retro_cheat_set.argtypes   = [ctypes.c_uint, ctypes.c_bool, ctypes.c_char_p]
            except: pass
            # 메모리 직접 접근 API (RAM 치트 패치용)
            try:
                self.core.retro_get_memory_data.restype  = ctypes.c_void_p
                self.core.retro_get_memory_data.argtypes = [ctypes.c_uint]
                self.core.retro_get_memory_size.restype  = ctypes.c_size_t
                self.core.retro_get_memory_size.argtypes = [ctypes.c_uint]
            except: pass
            self.log("✅ Engine Ready | FBNeo Core Loaded")
        except Exception as e:
            self.log(f"❌ Core Error: {e}")

    # ── ROM 스캔 / 선택 / 실행 ──────────────────────────────
    def scan_roms(self):
        load_gamelist_file(settings.rom_path)  # gamelist.xml/txt 갱신
        prev_sel = self.selected_game
        self.game_list.blockSignals(True)
        self.game_list.clear()
        count = 0
        all_games = []
        if os.path.exists(settings.rom_path):
            for f in sorted(os.listdir(settings.rom_path)):
                if f.endswith(".zip") and f.lower() != "neogeo.zip":
                    all_games.append(os.path.splitext(f)[0])

        # 즐겨찾기 필터 적용
        fav_filter = getattr(self, '_fav_filter', False)
        fav_set = set(favorites)
        if fav_filter:
            show_games = sorted([g for g in all_games if g in fav_set],
                                key=lambda g: get_display_name(g).lower())
        else:
            # ALL: 즐겨찾기를 상단에 이름순 정렬, 나머지도 이름순
            fav_sorted  = sorted([g for g in all_games if g in fav_set],
                                 key=lambda g: get_display_name(g).lower())
            rest_sorted = [g for g in all_games if g not in fav_set]
            show_games  = fav_sorted + rest_sorted

        for g in show_games:
            prefix = "★ " if g in fav_set else "  "
            display = get_display_name(g)
            item = QListWidgetItem(prefix + display)
            item.setData(Qt.UserRole, g)   # UserRole에 ROM 파일명 저장
            if g in fav_set:
                item.setForeground(QColor("#ffcc44"))
            self.game_list.addItem(item)
            count += 1

        if hasattr(self,'gamelist_panel'):
            title = f'GAMELIST ★({count})' if fav_filter else f'GAMELIST ({count})'
            self.gamelist_panel.set_title(title)

        # 이전 선택 복원 (UserRole 기준)
        if prev_sel:
            for i in range(self.game_list.count()):
                if self.game_list.item(i).data(Qt.UserRole) == prev_sel:
                    self.game_list.setCurrentRow(i)
                    break
        self.game_list.blockSignals(False)

    def _set_fav_filter(self, fav_only: bool):
        self._fav_filter = fav_only
        self._fav_btn_all.setChecked(not fav_only)
        self._fav_btn_fav.setChecked(fav_only)
        self.scan_roms()

    def _toggle_favorite(self):
        if not self.selected_game:
            return
        game = self.selected_game
        if game in favorites:
            favorites.remove(game)
            self.log(f"★ 즐겨찾기 제거: {game}")
        else:
            favorites.append(game)
            self.log(f"★ 즐겨찾기 추가: {game}")
        save_config()
        self.scan_roms()
        # 선택 복원 (UserRole 기준)
        self.game_list.blockSignals(True)
        for i in range(self.game_list.count()):
            if self.game_list.item(i).data(Qt.UserRole) == game:
                self.game_list.setCurrentRow(i)
                break
        self.game_list.blockSignals(False)

    def select_game(self, item):
        # UserRole에 저장된 ROM 파일명 사용 (없으면 텍스트에서 파싱)
        rom = item.data(Qt.UserRole)
        self.selected_game = rom if rom else item.text().lstrip("★ \u2605").strip()
        self._load_game_profile(self.selected_game)

        # 즐겨찾기 버튼 상태 갱신
        is_fav = self.selected_game in favorites
        self._fav_btn_star.setText("★" if is_fav else "☆")
        self._fav_btn_star.setChecked(is_fav)

        # 기판 자동 감지 → 드롭다운 업데이트
        if self.cmb_board:
            board = detect_board(self.selected_game)
            idx   = self.cmb_board.findText(board)
            if idx >= 0: self.cmb_board.setCurrentIndex(idx)

        # 기존 프리뷰 중지
        self._stop_preview()

        # 이미지 검색
        self._preview_img_path = None
        for ext in ('.png','.jpg','.jpeg','.PNG','.JPG','.JPEG'):
            p = os.path.join(settings.preview_path, f"{self.selected_game}{ext}")
            if os.path.exists(p):
                self._preview_img_path = p; break

        # 영상 검색
        self._preview_vid_path = None
        for ext in ('.mp4','.avi','.mkv','.webm','.gif','.MP4','.AVI','.MKV'):
            p = os.path.join(settings.preview_path, f"{self.selected_game}{ext}")
            if os.path.exists(p):
                self._preview_vid_path = p; break

        # 이미지 표시
        if self._preview_img_path:
            px=QPixmap(self._preview_img_path).scaled(380,220,Qt.KeepAspectRatio,Qt.SmoothTransformation)
            self.preview_label.setPixmap(px)
        else:
            self.preview_label.clear(); self.preview_label.setText("NO PREVIEW")
        self.preview_stack.setCurrentIndex(0)

        # 영상이 있으면 3초 후 전환
        if self._preview_vid_path and self._has_video_preview:
            self.preview_img_timer.start(3000)

        # 치트 탭 게임명 갱신 + 이전 치트 중단 + 자동 INI 로드
        if self.selected_game:
            state.active_cheats = []   # 다른 게임 치트 즉시 중단
            if hasattr(self, '_cheat_game_lbl'):
                self._cheat_game_lbl.setText(
                    f"게임: {get_display_name(self.selected_game)}")
            self._auto_load_game_cheats(self.selected_game)

    # ── ⑧ MULTIPLAYER ────────────────────────────────────────
    def _tab_multiplayer(self) -> QWidget:
        """LAN P2P 넷플레이 탭"""
        STYLE_BTN = (
            "QPushButton{background:#000055;color:#88aaff;border:1px solid #0000cc;"
            "font-family:'Courier New';font-size:15px;font-weight:bold;"
            "padding:6px 14px;border-radius:3px;}"
            "QPushButton:hover{background:#0000aa;color:#ffffff;}"
            "QPushButton:disabled{color:#445566;border-color:#223355;}")
        STYLE_INPUT = (
            "QLineEdit{background:#000022;color:#88ccff;border:1px solid #003388;"
            "font-family:'Courier New';font-size:15px;padding:4px 8px;}"
            "QLineEdit:focus{border-color:#0077ff;}")
        STYLE_LBL_H = "color:#ffdd88;font-family:'Courier New';font-size:15px;font-weight:bold;"
        STYLE_LBL   = "color:#aabbcc;font-family:'Courier New';font-size:14px;"

        w = QWidget(); w.setStyleSheet("background:transparent;")
        v = QVBoxLayout(w); v.setContentsMargins(10,8,10,8); v.setSpacing(10)

        # ── 상태 표시 ────────────────────────────────────
        self._netplay_status_lbl = QLabel("● 오프라인")
        self._netplay_status_lbl.setStyleSheet("color:#ff4444;font-size:16px;font-weight:bold;")
        v.addWidget(self._netplay_status_lbl)

        _sep1 = QFrame(); _sep1.setFrameShape(QFrame.HLine); _sep1.setStyleSheet("background:#223355;min-height:1px;max-height:1px;")
        v.addWidget(_sep1)

        # ── 호스트 섹션 ──────────────────────────────────
        grp_host = QGroupBox("🖥  HOST  —  게임 방 만들기")
        gh = QVBoxLayout(grp_host); gh.setSpacing(6)

        ip_row = QHBoxLayout()
        ip_lbl = QLabel(f"내 IP 주소:"); ip_lbl.setStyleSheet(STYLE_LBL)
        my_ip  = NetplayManager.local_ip()
        ip_val = QLabel(f"  {my_ip}"); ip_val.setStyleSheet("color:#00ff88;font-size:15px;font-weight:bold;")
        port_lbl = QLabel("  포트:"); port_lbl.setStyleSheet(STYLE_LBL)
        self._np_host_port = QLineEdit(str(NetplayManager.DEFAULT_PORT))
        self._np_host_port.setFixedWidth(80); self._np_host_port.setStyleSheet(STYLE_INPUT)
        ip_row.addWidget(ip_lbl); ip_row.addWidget(ip_val)
        ip_row.addStretch(); ip_row.addWidget(port_lbl); ip_row.addWidget(self._np_host_port)
        gh.addLayout(ip_row)

        hint = QLabel("친구에게 위 IP와 포트를 알려주면 접속할 수 있습니다.")
        hint.setStyleSheet(STYLE_LBL); gh.addWidget(hint)

        self._np_btn_host = QPushButton("▶  HOST 시작  (방 만들기)")
        self._np_btn_host.setStyleSheet(STYLE_BTN)
        self._np_btn_host.clicked.connect(self._netplay_host)
        gh.addWidget(self._np_btn_host)
        v.addWidget(grp_host)

        # ── 클라이언트 섹션 ──────────────────────────────
        grp_join = QGroupBox("🔗  JOIN  —  방에 참가하기")
        gj = QVBoxLayout(grp_join); gj.setSpacing(6)

        self._np_host_ip = QLineEdit("192.168.0."); self._np_host_ip.setFixedWidth(160)
        self._np_host_ip.setStyleSheet(STYLE_INPUT)
        self._np_join_port = QLineEdit(str(NetplayManager.DEFAULT_PORT))
        self._np_join_port.setFixedWidth(80); self._np_join_port.setStyleSheet(STYLE_INPUT)

        join_row = QHBoxLayout(); join_row.setSpacing(8)
        lbl_ip = QLabel("호스트 IP:"); lbl_ip.setStyleSheet(STYLE_LBL)
        lbl_pt = QLabel("포트:");     lbl_pt.setStyleSheet(STYLE_LBL)
        join_row.addWidget(lbl_ip); join_row.addWidget(self._np_host_ip)
        join_row.addWidget(lbl_pt); join_row.addWidget(self._np_join_port)
        join_row.addStretch()
        gj.addLayout(join_row)

        self._np_btn_join = QPushButton("🔗  JOIN  (방에 참가)")
        self._np_btn_join.setStyleSheet(STYLE_BTN)
        self._np_btn_join.clicked.connect(self._netplay_join)
        gj.addWidget(self._np_btn_join)
        v.addWidget(grp_join)

        # ── 연결 해제 ────────────────────────────────────
        self._np_btn_disc = QPushButton("✖  연결 해제")
        self._np_btn_disc.setStyleSheet(
            STYLE_BTN.replace("#000055","#330000").replace("#0000cc","#550000")
                     .replace("#0000aa","#660000"))
        self._np_btn_disc.setEnabled(False)
        self._np_btn_disc.clicked.connect(self._netplay_disconnect)
        v.addWidget(self._np_btn_disc)

        _sep2 = QFrame(); _sep2.setFrameShape(QFrame.HLine); _sep2.setStyleSheet("background:#223355;min-height:1px;max-height:1px;")
        v.addWidget(_sep2)

        # ── 안내 ─────────────────────────────────────────
        guide = QLabel(
            "사용 방법\n"
            "1. HOST가 먼저 [HOST 시작] → 대기 상태\n"
            "2. CLIENT가 IP/포트 입력 후 [JOIN]\n"
            "3. 둘 다 같은 ROM을 LAUNCH\n"
            "4. HOST = P1 조작,  CLIENT = P2 조작\n"
            "5. 게임 종료(ESC) 시 연결 유지 (재시작 가능)\n\n"
            "⚠  같은 ROM 파일이 양쪽에 있어야 합니다.\n"
            "⚠  공유기 사용 시 포트포워딩이 필요할 수 있습니다."
        )
        guide.setStyleSheet(STYLE_LBL); guide.setWordWrap(True)
        v.addWidget(guide)
        v.addStretch()

        # 초기 버튼 상태 설정
        self._np_btn_host.setEnabled(True)
        self._np_btn_join.setEnabled(True)
        self._np_btn_disc.setEnabled(False)

        return w

    def _netplay_host(self):
        if netplay.active:
            self.log("🌐 이미 연결됨"); return
        try:
            port = int(self._np_host_port.text().strip())
        except:
            port = NetplayManager.DEFAULT_PORT
        try:
            netplay.host_listen(port)
            ip = NetplayManager.local_ip()
            self.log(f"🌐 HOST 대기 중 — {ip}:{port}  클라이언트 접속 기다리는 중...")
            if self._netplay_status_lbl:
                self._netplay_status_lbl.setText(f"◌ 대기 중  [{ip}:{port}]")
                self._netplay_status_lbl.setStyleSheet("color:#ffdd00;font-weight:bold;")
            self._np_btn_host.setEnabled(False)
            self._np_btn_join.setEnabled(False)
        except Exception as e:
            self.log(f"🌐 HOST 오류: {e}")

    def _netplay_join(self):
        if netplay.active:
            self.log("🌐 이미 연결됨"); return
        ip = self._np_host_ip.text().strip()
        if not ip:
            self.log("🌐 호스트 IP를 입력하세요"); return
        try:
            port = int(self._np_join_port.text().strip())
        except:
            port = NetplayManager.DEFAULT_PORT
        self.log(f"🌐 {ip}:{port} 에 접속 중...")
        if self._netplay_status_lbl:
            self._netplay_status_lbl.setText(f"◌ 접속 중  [{ip}:{port}]")
            self._netplay_status_lbl.setStyleSheet("color:#ffdd00;font-weight:bold;")
        self._np_btn_host.setEnabled(False)
        self._np_btn_join.setEnabled(False)
        netplay.client_connect(ip, port)

    def _netplay_disconnect(self):
        netplay.stop()
        self.log("🌐 넷플레이 연결 해제")
        if self._netplay_status_lbl:
            self._netplay_status_lbl.setText("● 오프라인")
            self._netplay_status_lbl.setStyleSheet("color:#ff4444;")
        self._refresh_netplay_ui()

    def launch_game(self):
        if not self.core or not self.selected_game: return
        if state.is_paused:
            state.is_paused=False; self.stack.setCurrentIndex(1)
            self._afl_last_t = time.perf_counter()
            self.canvas.setFocus(); self.timer.start(1); self.log("▶ 재개"); return
        if state.game_loaded:
            self.timer.stop()
            try: self.core.retro_unload_game()
            except: pass
            state.game_loaded=False; state.dip_variables.clear()
        self.timer.stop()
        path=os.path.join(settings.rom_path,self.selected_game+".zip").encode("utf-8")
        info=RetroGameInfo(path=path,data=None,size=0,meta=None)
        if self.core.retro_load_game(byref(info)):
            state.game_loaded=True; state.is_paused=False
            state.active_cheats = []                      # 이전 게임 치트 중단
            state.game_load_frame = state.frame_count     # BIOS 딜레이 기준점
            self._refresh_video_labels()
            # 저장된 DIP 설정 복원 (게임 로드 직후 환경 콜백 재호출 전)
            if self.selected_game in game_dip_settings:
                saved_dip = game_dip_settings[self.selected_game]
                for key, val in saved_dip.items():
                    if key in state.dip_variables:
                        desc, opts, _ = state.dip_variables[key]
                        state.dip_variables[key] = (desc, opts, val)
            # 코어의 실제 FPS를 읽어 타이머 간격 정확히 설정
            # Neo Geo=59.18Hz → 16.9ms, CPS1=59.63Hz → 16.8ms, M92=60Hz → 16.7ms
            _core_fps = 60.0
            try:
                _av = RetroSystemAVInfo()
                self.core.retro_get_system_av_info(byref(_av))
                if 20.0 < _av.timing.fps < 200.0:
                    _core_fps = _av.timing.fps
            except: pass
            _interval_ms = max(1, round(1000.0 / _core_fps))
            # QTimer는 정수 ms → 소수점 FPS는 미세 오차 남음
            # _emu_loop에서 실제 FPS로 오디오 버퍼 크기 계산하도록 저장
            state.core_fps = _core_fps
            # DRC PID + Resampler 초기화 (게임 전환 시 이전 오차/위상 리셋)
            if state.drc_pid is None:
                state.drc_pid = DrcPid(max_adj=settings.audio_drc_max)
            else:
                state.drc_pid.max_adj = settings.audio_drc_max
                state.drc_pid.reset()
            if state.drc_resampler is None:
                state.drc_resampler = FractionalResampler()
            else:
                state.drc_resampler.reset()
            if state.drc_free_avg is None:
                state.drc_free_avg = _MovingAvg(5)
            else:
                state.drc_free_avg.reset()
            # Audio-Synced Frame Limiter: 1ms 타이머로 정밀 timing 제어
            self._afl_last_t = time.perf_counter()

            self.log(f"🚀 Playing: {self.selected_game}  [{_core_fps:.2f}Hz → timer {_interval_ms}ms]")
            self._stop_preview()
            self.stack.setCurrentIndex(1); self.canvas.setFocus()
            self.timer.start(1)   # 1ms: AFL이 내부에서 timing 제어
            # DIP 스위치는 첫 retro_run() 후에 확정되므로 200ms 지연 후 탭 재빌드
            QTimer.singleShot(200, self._rebuild_machine_tab)
        else:
            self.log("❌ ROM 로드 실패 (neogeo.zip 및 ROM 파일 확인)")

    # ── 에뮬레이터 루프 ─────────────────────────────────────
    def _emu_loop(self):
        try:
            # ══ Audio-Synced Frame Limiter (AFL) ══════════════
            # 1) bytesFree() 5프레임 이동 평균으로 이상값 방어
            # 2) 버퍼 70%+ 차 있으면 이번 틱 건너뜀 → QAudioSink가 소화할 시간 부여
            # 3) 정밀 타이밍: perf_counter로 core_fps 간격 미달 시 즉시 반환
            if state.game_loaded and audio_sink:
                _afl_total = audio_sink.bufferSize()
                if _afl_total > 0:
                    _afl_raw  = audio_sink.bytesFree()
                    if state.drc_free_avg is not None:
                        _afl_free = state.drc_free_avg.update(_afl_raw)
                    else:
                        _afl_free = float(_afl_raw)
                    _afl_fill = 1.0 - _afl_free / _afl_total
                    if _afl_fill > 0.70:
                        return   # 버퍼 충분 → 이번 틱 건너뜀

            _afl_fps = state.core_fps if state.core_fps > 0 else 60.0
            _afl_interval = 1.0 / _afl_fps
            _afl_now = time.perf_counter()
            _afl_last = getattr(self, '_afl_last_t', 0.0)
            if (_afl_now - _afl_last) < _afl_interval * 0.90:
                return   # 프레임 타임 미달 → 다음 1ms 틱에서 재시도
            self._afl_last_t = _afl_now
            # ══════════════════════════════════════════════════

            gp = poll_gamepad()
            prev_gp = getattr(self, '_prev_gp', {})

            for action, pressed in gp.items():
                if action in ACTION_DEFS:
                    idx = ACTION_DEFS[action][0]
                    prev = prev_gp.get(action, 0)

                    if pressed:
                        # 터보 버튼이고 turbo 활성화 → rising edge에서 즉시 터보 시작
                        if (action in TURBO_BUTTON_ACTIONS
                                and turbo_enabled.get(action, False)
                                and prev == 0):
                            state.turbo_held.add(idx)
                            state._turbo_ticks[idx] = 0   # 첫 프레임부터 ON
                        elif idx not in state.turbo_held:
                            state.keys[idx] = 1
                    else:
                        # 버튼 뗌 → 터보 & 일반 모두 해제 (키보드가 누르고 있으면 덮어쓰기 금지)
                        if idx not in state.kb_held:
                            state.keys[idx] = 0
                        state.turbo_held.discard(idx)
                        state._turbo_ticks.pop(idx, None)

            self._prev_gp = dict(gp)

            # 터보 처리 — 누르고 있는 버튼을 빠르게 ON/OFF 무한 반복
            for idx in list(state.turbo_held):
                tick = state._turbo_ticks.get(idx, 0)
                half = max(1, turbo_period)
                state.keys[idx] = 1 if (tick % (half * 2)) < half else 0
                state._turbo_ticks[idx] = tick + 1

            # ── 넷플레이: 입력 교환 ────────────────────────────
            if netplay.active:
                local_bits = sum(state.keys[i] << i for i in range(16))
                local_bits, remote_bits = netplay.exchange(local_bits)
                if netplay.is_host:
                    # 호스트 = P1(로컬), P2(원격)
                    for i in range(16):
                        state.keys[i]    = (local_bits  >> i) & 1
                        state.p2_keys[i] = (remote_bits >> i) & 1
                else:
                    # 클라이언트 = P2(로컬), P1(원격)
                    for i in range(16):
                        state.p2_keys[i] = (local_bits  >> i) & 1
                        state.keys[i]    = (remote_bits >> i) & 1

            runs = 3 if state.fast_forward else 1
            _pre_audio = len(state.audio_pending)   # DRC: retro_run 전 버퍼 크기
            for _ in range(runs): self.core.retro_run()

            # 프레임 카운터 갱신
            state.frame_count += 1

            # 활성 치트 매 프레임 RAM 재패치
            # NeoGeo BIOS RAM 자가진단(약 300프레임)이 끝난 후에만 적용
            if (state.active_cheats and state.game_loaded
                    and state.frame_count - state.game_load_frame > 300):
                try: self._apply_active_cheats()
                except: pass

            # ── DRC: PID + FractionalResampler + Fixed Chunk ─
            if audio_io and audio_sink:
                _sr     = settings.audio_sample_rate
                _target = int(_sr * settings.audio_buffer_ms / 1000 * 4)

                # 1) 이번 프레임 생산분 추출
                _new_audio = bytes(state.audio_pending[_pre_audio:])
                del state.audio_pending[_pre_audio:]

                # 2) PID → FractionalResampler (위상 연속 Catmull-Rom)
                if (_new_audio
                        and state.drc_pid       is not None
                        and state.drc_resampler is not None):
                    state.drc_pid.max_adj = settings.audio_drc_max
                    _ratio     = state.drc_pid.update(_pre_audio, _target)
                    _resampled = state.drc_resampler.process(_new_audio, _ratio)
                    state.audio_pending.extend(_resampled)
                elif _new_audio:
                    state.audio_pending.extend(_new_audio)

                # 3) Fixed Chunk(512 samples = 2048 bytes) 단위로 write
                _free    = audio_sink.bytesFree()
                _written = 0
                while (_free - _written >= DRC_CHUNK_BYTES
                       and len(state.audio_pending) >= DRC_CHUNK_BYTES):
                    audio_io.write(bytes(state.audio_pending[:DRC_CHUNK_BYTES]))
                    del state.audio_pending[:DRC_CHUNK_BYTES]
                    _written += DRC_CHUNK_BYTES

                # 4) 목표 버퍼 3배 초과 시 드롭
                _max = _target * 3
                if len(state.audio_pending) > _max:
                    del state.audio_pending[:len(state.audio_pending) - _max]

            if self._recording: self._capture_record_frame()

            # 프레임스킵 처리
            fskip = settings.video_frameskip
            if fskip == -1:
                # AUTO: 타이머 과부하 시 렌더 스킵 (간단 구현: 항상 렌더)
                do_render = True
            elif fskip > 0:
                self._fskip_cnt = getattr(self, '_fskip_cnt', 0) + 1
                do_render = (self._fskip_cnt % (fskip + 1) == 0)
            else:
                do_render = True

            if do_render and self.stack.currentIndex() == 1:
                self.canvas.update()
        except Exception as e:
            self.timer.stop(); self.log(f"⚠ Loop: {e}")

    # ── 전체화면 토글 (Alt+Enter) ────────────────────────────
    def _toggle_fullscreen(self):
        if self._is_fullscreen:
            self._is_fullscreen = False
            self.showNormal()
            self.resize(self._windowed_size)
            self.log("🖥  창 모드")
        else:
            self._is_fullscreen = True
            self._windowed_size = self.size()
            self.showFullScreen()
            self.log("🖥  전체화면")

    # ── 넷플레이 시그널 핸들러 (메인 스레드) ────────────────
    def _on_net_connected(self, is_host: bool):
        role = "HOST (P1)" if is_host else "CLIENT (P2)"
        self.log(f"🌐 넷플레이 연결됨 — {role}")
        if self._netplay_status_lbl:
            self._netplay_status_lbl.setText(f"● 연결됨  [{role}]")
            self._netplay_status_lbl.setStyleSheet("color:#00ff88;font-weight:bold;")
        self._refresh_netplay_ui()

    def _on_net_disconnected(self):
        self.log("🌐 넷플레이 연결 끊김")
        netplay.stop()
        for i in range(16): state.p2_keys[i] = 0
        if self._netplay_status_lbl:
            self._netplay_status_lbl.setText("● 오프라인")
            self._netplay_status_lbl.setStyleSheet("color:#ff4444;")
        self._refresh_netplay_ui()

    def _on_net_error(self, msg: str):
        self.log(f"🌐 넷플레이 오류: {msg}")
        netplay.stop()
        if self._netplay_status_lbl:
            self._netplay_status_lbl.setText(f"● 오류: {msg}")
            self._netplay_status_lbl.setStyleSheet("color:#ff8800;")
        self._refresh_netplay_ui()

    def _refresh_netplay_ui(self):
        """넷플레이 탭 UI 버튼 상태 갱신."""
        if not hasattr(self, '_np_btn_host'): return
        connected = netplay.active
        self._np_btn_host.setEnabled(not connected)
        self._np_btn_join.setEnabled(not connected)
        self._np_btn_disc.setEnabled(connected)

    def keyPressEvent(self, e):
        # 게임 입력은 eventFilter에서 전량 처리 (이중처리 방지)
        super().keyPressEvent(e)

    def keyReleaseEvent(self, e):
        # 게임 입력은 eventFilter에서 전량 처리 (이중처리 방지)
        super().keyReleaseEvent(e)

    def import_rom(self):
        files,_=QFileDialog.getOpenFileNames(self,"ROM 가져오기","","ROM Files (*.zip)")
        if not files: return
        os.makedirs(settings.rom_path,exist_ok=True)
        for f in files: shutil.copy(f,settings.rom_path)
        self.scan_roms(); self.log(f"📥 {len(files)}개 ROM 임포트")

    def log(self, msg: str):
        self.log_view.append(msg)

    def closeEvent(self, e):
        save_config(); self.timer.stop()
        self._stop_preview()
        netplay.stop()
        if self.core:
            try:
                if state.game_loaded: self.core.retro_unload_game()
                self.core.retro_deinit()
            except: pass
        global audio_sink, audio_io
        if audio_sink:
            try: audio_sink.stop()
            except: pass
            audio_sink = None
        audio_io = None
        # Windows 타이머 해상도 복원
        if IS_WINDOWS:
            try:
                import ctypes as _ct
                _ct.windll.winmm.timeEndPeriod(1)
            except: pass
        e.accept()


# ════════════════════════════════════════════════════════════
if __name__ == "__main__":
    if IS_WINDOWS:
        try:
            import ctypes as _ct
            _ct.windll.shell32.SetCurrentProcessExplicitAppUserModelID('FBNeoRageX.1.8')
        except: pass
        # Windows 멀티미디어 타이머 해상도를 1ms로 향상
        # 기본값 15.6ms → 이 때문에 QTimer(16ms)가 15.6ms / 31.2ms 교대로 발생
        # → 31.2ms 구간에서 오디오 버퍼 고갈 → 깨짐의 근본 원인
        try:
            import ctypes as _ct2
            _ct2.windll.winmm.timeBeginPeriod(1)
        except: pass

    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    # NeoRAGE 클래식 전역 폰트: Press Start 2P 우선, 없으면 Courier New
    _gf = QFont("Press Start 2P", 8)
    if not _gf.exactMatch():
        _gf = QFont("Courier New", 10, QFont.Bold)
    app.setFont(_gf)

    # 전역 다크 팔레트 (체커보드 패턴 배경은 메인 윈도우 paintEvent에서)
    _pal = app.palette()
    _R = QPalette.ColorRole
    _pal.setColor(_R.Window,          QColor(0,  0,  20))
    _pal.setColor(_R.WindowText,      QColor(255,255,255))
    _pal.setColor(_R.Base,            QColor(0,  0,  30))
    _pal.setColor(_R.AlternateBase,   QColor(0,  0,  40))
    _pal.setColor(_R.Text,            QColor(255,255,255))
    _pal.setColor(_R.Button,          QColor(0,  0,  80))
    _pal.setColor(_R.ButtonText,      QColor(255,255,255))
    _pal.setColor(_R.Highlight,       QColor(34, 68, 204))
    _pal.setColor(_R.HighlightedText, QColor(255,255,255))
    app.setPalette(_pal)

    # 전역 스타일시트 — 체커보드 배경
    app.setStyleSheet(f"""
        QMainWindow {{ background-color: #000014; }}
        QMainWindow::centralWidget {{
            background-color: #000014;
        }}
        QWidget#central_bg {{
            background-image: url();
        }}
    """)

    _ico_path = os.path.join(CURRENT_PATH, "assets", "Neo.ico")
    if os.path.exists(_ico_path):
        icon = QIcon()
        icon.addFile(_ico_path, QSize(16,16))
        icon.addFile(_ico_path, QSize(24,24))
        icon.addFile(_ico_path, QSize(32,32))
        icon.addFile(_ico_path, QSize(48,48))
        icon.addFile(_ico_path, QSize(256,256))
        app.setWindowIcon(icon)
    win = NeoRageXApp()
    win.show()
    sys.exit(app.exec())
