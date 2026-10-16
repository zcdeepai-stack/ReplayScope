# -*- mode: python ; coding: utf-8 -*-
# DR02 PyBullet 服务打包配置

datas = [
    ('../DR02/DR02_pro-complete-20261009', 'DR02_model'),
]
binaries = []
hiddenimports = ['win32gui', 'win32process', 'win32api']

a = Analysis(
    ['pybullet_server.py'],
    pathex=[],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='pybullet_server',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    # Qt 以 CREATE_NO_WINDOW 启动服务，保留标准输出管道用于诊断，但不会弹黑色窗口。
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='dr02_server',
)
