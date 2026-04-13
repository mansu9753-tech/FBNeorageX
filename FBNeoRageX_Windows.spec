# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for FBNeoRageX v1.7 (Windows)
# --onefile: single exe, fbneo_libretro.dll bundled inside

import os
datas_list = []
if os.path.exists('config.json'):
    datas_list.append(('config.json', '.'))
for folder in ['assets', 'previews', 'roms']:
    if os.path.exists(folder):
        datas_list.append((folder, folder))

a = Analysis(
    ['FBNeoRageX_v1.7.py'],
    pathex=[],
    binaries=[],  # dll은 exe 옆에 별도 파일로 배포 (번들 안함)
    datas=datas_list,
    hiddenimports=[
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
    ],
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

# --onefile: COLLECT 없음, a.binaries/a.datas 를 EXE 에 직접 포함
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
    icon='assets\\Neo.ico',
)
