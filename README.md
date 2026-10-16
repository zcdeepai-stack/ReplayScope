# ReplayScope

ReplayScope 是一套 Windows 桌面端机器人三维复现与数据分析工具。它可以读取离线 CSV 数据，复现 X30、M20 和 DR02 人形机器人的关节运动，并提供波形分析、日志采集、日志分析、截图和画面录制功能。

仓库只包含源码、机器人模型和构建文件。实测 CSV、日志、安装包、编译产物及账号配置不在仓库中。

## 主要功能

- X30、M20、DR02 三个互相独立的三维复现页面。
- CSV 播放、暂停、跳转、循环和倍速控制。
- 关节角度显示、视角旋转/平移/缩放、模型透明度、截图和录制。
- CSV 波形查看、窗口配置保存和数据图表导出。
- 通过 SSH 采集设备日志，并对本地日志进行筛选、统计和导出。
- React + Go 实现的浏览器端对应功能。

## 目录说明

| 路径 | 内容 |
| --- | --- |
| 根目录的 `.cpp/.h` | Qt/C++ 桌面端源码 |
| `x30PybusSimlu/` | X30 PyBullet 服务和模型 |
| `M20pybusSimlu/` | M20 PyBullet 服务和模型 |
| `DR02pybusSimlu/`、`DR02/` | DR02 服务和模型 |
| `roboscope-web/` | React 前端和 Go 后端 |
| `docs/` | 需求、设计和数据格式说明 |
| `deploy.bat` | Windows 完整构建与打包脚本 |

## 开发环境

建议使用以下版本：

- Windows 10/11 x64
- Qt 6.8 或更高版本，使用 MinGW 64-bit 套件
- Python 3.12 x64
- Node.js 20 LTS 或更高版本
- Go 1.21 或更高版本
- Inno Setup 6（只有生成安装包时需要）

### 1. 获取源码

```powershell
git clone https://github.com/yscrobot/ReplayScope.git
cd ReplayScope
```

### 2. 安装 Python 环境

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

运行桌面程序时应保持该虚拟环境已激活，使程序能够找到 `python.exe` 和 PyBullet 依赖。

### 3. 编译并运行桌面端

可以直接用 Qt Creator 打开 `Player3D.pro`，选择 Qt MinGW 64-bit Release 套件后构建运行。

也可以在已配置 Qt/MinGW PATH 的终端中执行：

```powershell
New-Item -ItemType Directory -Force build\dev | Out-Null
Set-Location build\dev
qmake ..\..\Player3D.pro -spec win32-g++ "CONFIG+=release"
mingw32-make -j4
.\release\Player3D.exe
```

开发模式下，程序会从源码目录启动三个 Python 三维服务，因此不需要先运行 PyInstaller。

### 4. 运行浏览器端

终端一：

```powershell
cd roboscope-web\web
npm ci
npm run dev
```

终端二：

```powershell
cd roboscope-web\server
go run .
```

前端默认地址为 `http://localhost:5173`，后端默认地址为 `http://localhost:9090`。

## 生成免安装目录和安装包

先在 PowerShell 中把路径改成自己的 Qt、MinGW 和 Inno Setup 安装位置：

```powershell
$env:QTDIR = 'C:\Qt\6.8.3\mingw_64'
$env:MINGW = 'C:\Qt\Tools\mingw1310_64'
$env:ISCC = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
$env:PYTHON = (Resolve-Path '.\.venv\Scripts\python.exe').Path
cmd /c deploy.bat
```

脚本会依次编译 Qt 桌面端、三个 PyBullet 服务、SSH 辅助程序、React 前端和 Go 后端，并生成：

- `deploy/`：免安装运行目录
- `installer_output/ReplayScope-<版本>-x64-Setup.exe`：安装包

## CSV 数据

- X30 使用 `Joint Pos 0` 至 `Joint Pos 11`、对应力矩列以及 `IMU roll/pitch/yaw`。
- M20 使用 `Joint Pos 0` 至 `Joint Pos 15`，顺序为左前、右前、左后、右后；每条腿依次为 HipX、HipY、Knee、Wheel。数据为发布角弧度，程序会按 M20 官方方向和零位转换后驱动模型。
- DR02 优先按 URDF 关节名称匹配，也兼容 `Joint Pos 0` 至 `Joint Pos 30`。详细格式见 `docs/DR02关节数据CSV格式.md`。

实测数据可能包含设备信息和业务信息，请不要直接提交到公开仓库。`.gitignore` 默认排除所有 CSV 和日志文件。

## 安全说明

SSH 密码只在当前进程内使用，不写入仓库或本地明文配置。旧版本配置中的密码字段会在读取后自动清除。

## 许可证

项目按 [GNU GPL v3](LICENSE) 发布。机器人模型和其他第三方组件仍遵循各自许可证，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

