# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for FBNeoRageX (Linux / Steam Deck, onedir)
# Build: pyinstaller FBNeoRageX_Linux.spec --noconfirm
#
# Distribution structure:
#   dist/FBNeoRageX/
#     FBNeoRageX          <- executable
#     run.sh              <- launcher script (set by CI or manually)
#     fbneo_libretro.so   <- user places this here
#     _internal/          <- Python/Qt/OpenGL bundles (do not touch)
#
# fbneo_libretro.so is NOT bundled - user places it next to the executable

import os
from PyInstaller.utils.hooks import collect_dynamic_libs, collect_submodules

# ── Read-only bundled resources ───────────────────────────
datas_list = []
if os.path.exists('assets'):
    datas_list.append(('assets', 'assets'))
if os.path.exists('game_names_db.json'):
    datas_list.append(('game_names_db.json', '.'))

# ── Dynamic libraries (sounddevice PortAudio, etc.) ───────
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
    'OpenGL.platform.egl',
    'OpenGL.platform.glx',
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
    cipher=None,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=None)

# ── onedir mode: EXE + _internal folder ──────────────────
exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='FBNeoRageX',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon='assets/Neo.ico' if os.path.exists('assets/Neo.ico') else None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=['libQt6Core.so.6', 'libQt6Gui.so.6', 'libQt6Widgets.so.6'],
    name='FBNeoRageX',
)
