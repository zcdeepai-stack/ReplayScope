# -*- mode: python ; coding: utf-8 -*-
# X30 PyBullet 服务器打包配置
# 用法: cd x30PybusSimlu && pyinstaller x30_server.spec

from PyInstaller.utils.hooks import collect_all

datas = [
    ('X30', 'X30'),                                # URDF 模型目录
    ('gridmap_20261212_195732.csv', '.'),           # 默认地形数据
    # map.py 作为模块会被自动收集，无需手动添加
]
binaries = []
hiddenimports = ['win32gui', 'win32process', 'win32api']

# 收集 pybullet_data (包含 plane.urdf 等内置资源)
tmp_ret = collect_all('pybullet_data')
datas += tmp_ret[0]; binaries += tmp_ret[1]; hiddenimports += tmp_ret[2]

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
    console=False,              # 无控制台窗口（stdout 通过 QProcess 管道传输）
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
    name='x30_server',
)
