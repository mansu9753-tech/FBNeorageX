# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for FBNeoRageX (Windows, onefile)
# 빌드: pyinstaller FBNeoRageX_Windows_onefile_sd.spec

import os
from PyInstaller.utils.hooks import collect_dynamic_libs, collect_submodules, collect_data_files

# ── 번들에 포함할 데이터 파일 ──────────────────────────────
datas_list = []
if os.path.exists('config.json'):
    datas_list.append(('config.json', '.'))
if os.path.exists('game_names_db.json'):
    datas_list.append(('game_names_db.json', '.'))
if os.path.exists('assets'):
    datas_list.append(('assets', 'assets'))

# ── 번들에 포함할 바이너리 ────────────────────────────────
binaries_list = []
if os.path.exists('fbneo_libretro.dll'):
    binaries_list.append(('fbneo_libretro.dll', '.'))

# sounddevice PortAudio DLL 자동 수집
try:
    binaries_list += collect_dynamic_libs('sounddevice')
except Exception:
    pass

# ── hiddenimports ─────────────────────────────────────────
hidden = [
    # PySide6
    'PySide6.QtCore',
    'PySide6.QtGui',
    'PySide6.QtWidgets',
    'PySide6.QtOpenGLWidgets',
    'PySide6.QtOpenGL',
    'PySide6.QtMultimedia',
    'PySide6.QtMultimediaWidgets',
    # OpenGL
    'OpenGL',
    'OpenGL.GL',
    'OpenGL.GL.ARB',
    'OpenGL.arrays',
    'OpenGL.arrays.numpymodule',
    'OpenGL.platform',
    'OpenGL.platform.win32',
    # numpy
    'numpy',
    'numpy.core',
    'numpy.core._multiarray_umath',
    # Pillow
    'PIL',
    'PIL.Image',
    'PIL.JpegImagePlugin',
    'PIL.PngImagePlugin',
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
    excludes=['tkinter', 'matplotlib', 'scipy'],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=None,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=None)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='FBNeoRageX',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=['fbneo_libretro.dll', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll'],
    runtime_tmpdir=None,
    console=False,          # 콘솔 창 숨김
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon='assets\\Neo.ico' if os.path.exists('assets\\Neo.ico') else None,
)
