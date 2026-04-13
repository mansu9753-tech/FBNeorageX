# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for FBNeoRageX (Windows, onefile)
# 빌드: pyinstaller FBNeoRageX_Windows.spec --noconfirm
#
# 배포 구조:
#   FBNeoRageX.exe          <- 이것 하나만 배포
#   fbneo_libretro.dll      <- 사용자가 exe 옆에 놓기
#
# fbneo_libretro.dll 은 번들에 포함하지 않음 — exe 옆에 별도 배치

import os
from PyInstaller.utils.hooks import collect_dynamic_libs, collect_submodules

# ── 번들에 포함할 읽기 전용 리소스 ───────────────────────
datas_list = []
if os.path.exists('assets'):
    datas_list.append(('assets', 'assets'))
if os.path.exists('game_names_db.json'):
    datas_list.append(('game_names_db.json', '.'))

# ── 동적 라이브러리 (sounddevice PortAudio 등) ────────────
binaries_list = []
try:
    binaries_list += collect_dynamic_libs('sounddevice')
except Exception:
    pass

# ── hiddenimports ─────────────────────────────────────────
hidden = [
    # PySide6 (Qt6)
    'PySide6.QtCore',
    'PySide6.QtGui',
    'PySide6.QtWidgets',
    'PySide6.QtOpenGLWidgets',
    'PySide6.QtOpenGL',
    'PySide6.QtMultimedia',
    'PySide6.QtMultimediaWidgets',
    # PyOpenGL
    'OpenGL',
    'OpenGL.GL',
    'OpenGL.GL.ARB',
    'OpenGL.arrays',
    'OpenGL.arrays.numpymodule',
    'OpenGL.platform',
    'OpenGL.platform.win32',
    'OpenGL.raw.GL',
    'OpenGL.raw.GL.ARB',
    # numpy
    'numpy',
    'numpy.core',
    'numpy.core._multiarray_umath',
    # Pillow
    'PIL',
    'PIL.Image',
    'PIL.JpegImagePlugin',
    'PIL.PngImagePlugin',
    'PIL.BmpImagePlugin',
    # sounddevice
    'sounddevice',
]
try:
    hidden += collect_submodules('sounddevice')
except Exception:
    pass

a = Analysis(
    ['FBNeoRageX_v1.7.py'],
    pathex=[],
    binaries=binaries_list,
    datas=datas_list,
    hiddenimports=hidden,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=['tkinter', 'matplotlib', 'scipy', 'PyQt5', 'PyQt6'],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=None,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=None)

# ── onefile 모드: 모든 파일을 exe 하나로 번들 ─────────────
exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    name='FBNeoRageX',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=['Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll'],
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon='assets\\Neo.ico' if os.path.exists('assets\\Neo.ico') else None,
)
