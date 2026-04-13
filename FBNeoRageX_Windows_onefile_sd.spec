# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for FBNeoRageX v1.7 (Windows)
# 목표: 단일 exe(--onefile) + fbneo_libretro.dll + sounddevice(PortAudio)까지 번들

import os
from PyInstaller.utils.hooks import collect_dynamic_libs, collect_submodules

datas_list = []

# 런타임에서 exe 옆의 config.json을 쓰지만, 기본 config가 있으면 함께 포함
if os.path.exists('config.json'):
    datas_list.append(('config.json', '.'))

# 실행에 필요한 리소스(최소)
if os.path.exists('assets'):
    datas_list.append(('assets', 'assets'))
if os.path.exists('game_names_db.json'):
    datas_list.append(('game_names_db.json', '.'))

# 코어 DLL은 번들 내부에 포함 (코드에서 BUNDLE_PATH에서 로드하도록 수정됨)
binaries_list = []
if os.path.exists('fbneo_libretro.dll'):
    binaries_list.append(('fbneo_libretro.dll', '.'))

# sounddevice는 PortAudio dll이 필요할 수 있으므로 동적 라이브러리를 함께 수집
try:
    binaries_list += collect_dynamic_libs('sounddevice')
except Exception:
    pass

a = Analysis(
    ['FBNeoRageX_v1.7.py'],
    pathex=[],
    binaries=binaries_list,
    datas=datas_list,
    hiddenimports=(
        collect_submodules('sounddevice') +
        [
            'PySide6.QtCore',
            'PySide6.QtGui',
            'PySide6.QtWidgets',
            'PySide6.QtOpenGLWidgets',
            'PySide6.QtOpenGL',
            'PySide6.QtMultimedia',
            'PySide6.QtMultimediaWidgets',
            'OpenGL',
            'OpenGL.GL',
            'numpy',
            'numpy.core',
            'PIL',
            'PIL.Image',
        ]
    ),
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
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
    upx_exclude=['fbneo_libretro.dll'],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon='assets\\Neo.ico' if os.path.exists('assets\\Neo.ico') else None,
)

