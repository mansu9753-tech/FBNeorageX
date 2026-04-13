# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec for FBNeoRageX v1.7 (Linux / Steam Deck)

a = Analysis(
    ['FBNeoRageX_v1.7.py'],
    pathex=[],
    binaries=[
        ('fbneo_libretro.so', '.'),
    ],
    datas=[
        ('config.json', '.'),
        ('assets',   'assets'),
        ('previews', 'previews'),
        ('roms',     'roms'),
    ],
    hiddenimports=[
        'PySide6.QtCore',
        'PySide6.QtGui',
        'PySide6.QtWidgets',
        'PySide6.QtOpenGLWidgets',
        'PySide6.QtMultimedia',
        'PySide6.QtMultimediaWidgets',
        'OpenGL',
        'OpenGL.GL',
        'PIL',
        'PIL.Image',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=['cv2', 'imageio', 'numpy'],
    cipher=None,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=None)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='FBNeoRageX',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name='FBNeoRageX',
)
