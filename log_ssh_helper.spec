# -*- mode: python ; coding: utf-8 -*-
# SSH 日志收集助手打包配置
# 用法: pyinstaller --clean --noconfirm log_ssh_helper.spec

from PyInstaller.utils.hooks import collect_all

datas = []
binaries = []
hiddenimports = ['paramiko', 'nacl', 'bcrypt', 'cryptography']

a = Analysis(
    ['log_ssh_helper.py'],
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
    name='log_ssh_helper',
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
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='log_ssh_helper',
)
