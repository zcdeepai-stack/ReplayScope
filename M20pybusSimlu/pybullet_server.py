"""
M20 PyBullet GPU直渲服务器
- PyBullet以GUI模式运行，GPU直接渲染到窗口
- 窗口句柄发送给Qt进行嵌入（单窗口体验）
- TCP只传控制命令和进度状态，不传图像
- 连接建立后立即加载URDF显示模型
"""

import socket
import struct
import json
import threading
import time
import sys
import os
import io
import math

if sys.platform == 'win32':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

import numpy as np
import pandas as pd
import pybullet as p
import pybullet_data
import colorsys


def get_resource_path(relative_path):
    if hasattr(sys, '_MEIPASS'):
        base_path = sys._MEIPASS
    else:
        base_path = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base_path, relative_path)


MSG_LOG    = 0x4C
MSG_STATUS = 0x53

M20_FULL_JOINT_COLUMNS = [f"Joint Pos {i}" for i in range(16)]
M20_LEGACY_JOINT_COLUMNS = [f"Joint Pos {i}" for i in range(12)]
M20_FULL_JOINT_INDICES = list(range(16))
M20_LEGACY_JOINT_INDICES = [0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14]
M20_FULL_KNEE_TORQUE_COLUMNS = [f"Joint Tor {i}" for i in (2, 6, 10, 14)]
M20_LEGACY_KNEE_TORQUE_COLUMNS = [f"Joint Tor {i}" for i in (2, 5, 8, 11)]
IMU_RPY_COLUMNS = ["IMU roll", "IMU pitch", "IMU yaw"]
IMU_QUAT_COLUMNS = ["IMU quat_x", "IMU quat_y", "IMU quat_z", "IMU quat_w"]

# M20 对外发布角 -> 模型内部原始角：raw = published * direction + offset。
# 参数来自 DeepRobotics 官方 M20 仿真接口；轮关节只换方向，没有零位偏移。
M20_JOINT_DIRECTION = np.array(
    [1, 1, -1, 1, 1, -1, 1, -1,
     -1, 1, -1, 1, -1, -1, 1, -1],
    dtype=np.float64,
)
M20_POSITION_OFFSET_RAD = np.radians(np.array(
    [-25, 229, 160, 0, 25, -131, -200, 0,
     -25, -229, -160, 0, 25, 131, 200, 0],
    dtype=np.float64,
))


class M20Server:
    def __init__(self, host='127.0.0.1', port=0):
        self.host = host
        self.port = port
        self.conn = None
        self.server_socket = None
        self._send_lock = threading.Lock()

        self.physics_client_id = -1
        self.robot_id = None
        # link -> 上一次实际下发的色阶值，避免每帧重复 changeVisualShape（同步往返调用）
        self._link_color_cache = {}
        self._opacity = 1.0

        self.csv_path = ""
        self.joint_data = None
        self.imu_data = None
        self.joint_tor = None
        self.joint_indices = M20_FULL_JOINT_INDICES
        self.orientation_is_quaternion = False
        self.frame_interval = 0.004

        self._lock = threading.Lock()
        self._paused = False
        self._stop_playback = True
        self._speed = self.frame_interval
        self._loop = True
        self._seek_frame = -1
        self._current_frame = 0
        self._total_frames = 0
        self._start_frame = 0
        self._end_frame = 0
        self._playback_thread = None

    # ---- 启动服务器 ----
    def start(self):
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(1)
        actual_port = self.server_socket.getsockname()[1]
        print(f"READY {actual_port}", flush=True)

        try:
            self.conn, addr = self.server_socket.accept()
            self.conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

            # 连接建立后立即初始化 PyBullet 并显示模型
            self._init_pybullet()

            self._handle_client()
        except Exception as e:
            print(f"Server error: {e}", file=sys.stderr, flush=True)
        finally:
            self._do_stop_playback()
            self._cleanup_pybullet()
            if self.conn:
                try: self.conn.close()
                except: pass
            self.server_socket.close()

    # ---- 网络收发 ----
    def _recv_exact(self, n):
        data = b''
        while len(data) < n:
            chunk = self.conn.recv(n - len(data))
            if not chunk:
                return None
            data += chunk
        return data

    def _recv_message(self):
        header = self._recv_exact(4)
        if header is None:
            return None
        msg_len = struct.unpack('>I', header)[0]
        return self._recv_exact(msg_len)

    def _send_raw(self, data):
        with self._send_lock:
            try:
                self.conn.sendall(data)
                return True
            except (BrokenPipeError, ConnectionResetError, OSError):
                return False

    def _send_message(self, msg_type, payload):
        msg = struct.pack('B', msg_type) + payload
        header = struct.pack('>I', len(msg))
        return self._send_raw(header + msg)

    def _send_log(self, level, message):
        data = json.dumps({"level": level, "msg": message}, ensure_ascii=False).encode('utf-8')
        self._send_message(MSG_LOG, data)

    def _send_status(self, d):
        data = json.dumps(d, ensure_ascii=False).encode('utf-8')
        self._send_message(MSG_STATUS, data)

    # ---- 命令处理 ----
    def _handle_client(self):
        while True:
            try:
                data = self._recv_message()
                if data is None:
                    break
                cmd = json.loads(data.decode('utf-8'))
                self._process_command(cmd)
            except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError):
                break
            except json.JSONDecodeError as e:
                self._send_log("error", f"JSON解析错误: {e}")
            except Exception as e:
                self._send_log("error", f"命令错误: {e}")

    def _process_command(self, cmd):
        action = cmd.get("cmd", "")
        if action == "load_csv":
            self._cmd_load_csv(cmd.get("path", ""))
        elif action == "play":
            self._cmd_play(cmd.get("start_frame", 0), cmd.get("end_frame", -1))
        elif action == "pause":
            with self._lock: self._paused = True
            self._send_status({"status": "paused"})
        elif action == "resume":
            with self._lock: self._paused = False
            self._send_status({"status": "playing"})
        elif action == "stop":
            self._do_stop_playback()
            self._send_status({"status": "stopped"})
        elif action == "seek":
            with self._lock: self._seek_frame = cmd.get("frame", 0)
        elif action == "set_speed":
            with self._lock:
                requested_speed = float(cmd.get("speed", self.frame_interval))
                self._speed = max(0.0001, requested_speed)
        elif action == "set_loop":
            with self._lock: self._loop = cmd.get("enabled", True)
        elif action == "ping":
            self._send_status({"status": "pong"})
        elif action == "set_joint":
            self._cmd_set_joint(cmd.get("joint", 0), cmd.get("value", 0.0))
        elif action == "set_camera":
            self._cmd_set_camera(cmd)
        elif action == "pan_camera":
            self._cmd_pan_camera(cmd)
        elif action == "set_opacity":
            self._cmd_set_opacity(cmd)

    def _cmd_load_csv(self, path):
        try:
            if not path or not os.path.isfile(path):
                raise ValueError("文件不存在或路径为空")

            columns = list(pd.read_csv(path, nrows=0).columns)
            column_set = set(columns)
            if set(M20_FULL_JOINT_COLUMNS).issubset(column_set):
                joint_columns = M20_FULL_JOINT_COLUMNS
                torque_columns = M20_FULL_KNEE_TORQUE_COLUMNS
                self.joint_indices = M20_FULL_JOINT_INDICES
                data_format = "16关节（含4个轮关节）"
            elif set(M20_LEGACY_JOINT_COLUMNS).issubset(column_set):
                joint_columns = M20_LEGACY_JOINT_COLUMNS
                torque_columns = M20_LEGACY_KNEE_TORQUE_COLUMNS
                self.joint_indices = M20_LEGACY_JOINT_INDICES
                data_format = "12关节（旧格式，不含轮关节）"
            else:
                missing = [name for name in M20_LEGACY_JOINT_COLUMNS if name not in column_set]
                raise ValueError("缺少M20关节列: " + ", ".join(missing))

            if not set(torque_columns).issubset(column_set):
                missing = [name for name in torque_columns if name not in column_set]
                raise ValueError("缺少膝关节力矩列: " + ", ".join(missing))

            has_quaternion = set(IMU_QUAT_COLUMNS).issubset(column_set)
            has_rpy = set(IMU_RPY_COLUMNS).issubset(column_set)
            if not has_quaternion and not has_rpy:
                raise ValueError("缺少IMU姿态列（需要四元数或roll/pitch/yaw）")

            use_columns = joint_columns + torque_columns
            use_columns += IMU_QUAT_COLUMNS if has_quaternion else IMU_RPY_COLUMNS
            if "Time s" in column_set:
                use_columns.append("Time s")

            # 只读取三维回放需要的列，避免把实测文件的两百多列全部装入内存。
            df = pd.read_csv(path, usecols=use_columns)
            if df.empty:
                raise ValueError("CSV没有数据行")

            numeric_columns = joint_columns + torque_columns
            numeric_columns += IMU_QUAT_COLUMNS if has_quaternion else IMU_RPY_COLUMNS
            if "Time s" in df.columns:
                numeric_columns.append("Time s")
            numeric = df[numeric_columns].apply(pd.to_numeric, errors="coerce")
            invalid = ~np.isfinite(numeric.to_numpy(dtype=float))
            if invalid.any():
                row, col = np.argwhere(invalid)[0]
                raise ValueError(f"第{row + 2}行的 {numeric_columns[col]} 不是有效数字")

            self.csv_path = path
            published_positions = numeric[joint_columns].to_numpy(dtype=np.float64, copy=True)
            model_indices = np.asarray(self.joint_indices, dtype=np.int64)
            self.joint_data = (
                published_positions * M20_JOINT_DIRECTION[model_indices]
                + M20_POSITION_OFFSET_RAD[model_indices]
            )
            # CSV 中的发布角按 [-pi, pi] 记录。加零位后可能跨过 2*pi；
            # 有限位的腿关节取等价角，轮关节保留连续转角。
            for data_column, model_index in enumerate(self.joint_indices):
                if model_index % 4 != 3:
                    values = self.joint_data[:, data_column]
                    self.joint_data[:, data_column] = (
                        (values + math.pi) % (2.0 * math.pi) - math.pi
                    )
            self.joint_tor = numeric[torque_columns].to_numpy(dtype=np.float64, copy=True)

            if has_quaternion:
                quaternions = numeric[IMU_QUAT_COLUMNS].to_numpy(dtype=np.float64, copy=True)
                norms = np.linalg.norm(quaternions, axis=1)
                if np.any(norms < 1e-9):
                    bad_row = int(np.argwhere(norms < 1e-9)[0][0])
                    raise ValueError(f"第{bad_row + 2}行的IMU四元数无效")
                self.imu_data = quaternions / norms[:, None]
                self.orientation_is_quaternion = True
                orientation_format = "IMU四元数"
            else:
                rpy = numeric[IMU_RPY_COLUMNS].to_numpy(dtype=np.float64, copy=True)
                # 实测M20文件为弧度；兼容数值范围明显为角度的旧文件。
                if float(np.max(np.abs(rpy))) > (2.0 * math.pi + 0.1):
                    rpy = np.radians(rpy)
                    orientation_format = "IMU欧拉角（度转弧度）"
                else:
                    orientation_format = "IMU欧拉角（弧度）"
                self.imu_data = rpy
                self.orientation_is_quaternion = False

            if "Time s" in numeric.columns:
                time_values = numeric["Time s"].to_numpy(dtype=np.float64, copy=False)
                positive_steps = np.diff(time_values)
                positive_steps = positive_steps[positive_steps > 0]
                if positive_steps.size:
                    self.frame_interval = float(np.median(positive_steps))
                else:
                    self.frame_interval = 0.004
                duration = float(time_values[-1] - time_values[0] + self.frame_interval)
            else:
                self.frame_interval = 0.004
                duration = len(self.joint_data) * self.frame_interval

            if not 0.0005 <= self.frame_interval <= 0.1:
                raise ValueError(f"Time s时间间隔异常: {self.frame_interval:.6f}秒")

            self._total_frames = len(self.joint_data)
            with self._lock:
                self._speed = self.frame_interval
                self._current_frame = 0
            self._send_status({
                "status": "csv_loaded",
                "frames": int(self._total_frames),
                "duration": round(duration, 3),
                "frame_interval": self.frame_interval,
                "sample_rate": round(1.0 / self.frame_interval, 3),
                "joint_count": len(joint_columns),
                "filename": os.path.basename(path),
            })
            self._send_log(
                "info",
                f"关节数据加载成功: {self._total_frames:,}帧, {duration:.1f}秒, "
                f"{1.0 / self.frame_interval:.1f}Hz, {data_format}, {orientation_format}, "
                "已应用M20官方关节方向与零位转换")
        except Exception as e:
            self.csv_path = ""
            self.joint_data = None
            self.joint_tor = None
            self.imu_data = None
            self._total_frames = 0
            self._send_log("error", f"CSV加载错误: {e}")
            self._send_status({"status": "error", "reason": str(e)})

    def _cmd_set_joint(self, joint_index, value):
        """直接设置单个关节位置（手动交互控制）"""
        pid = self.physics_client_id
        if pid < 0 or self.robot_id is None:
            return
        try:
            p.resetJointState(self.robot_id, int(joint_index),
                              targetValue=float(value),
                              targetVelocity=0, physicsClientId=pid)
        except Exception as e:
            self._send_log("warn", f"设置关节 {joint_index} 失败: {e}")

    def _cmd_set_camera(self, cmd):
        pid = self.physics_client_id
        if pid < 0:
            return
        try:
            p.resetDebugVisualizerCamera(
                cameraDistance=cmd.get("distance", 1.0),
                cameraYaw=cmd.get("yaw", 35),
                cameraPitch=cmd.get("pitch", -25),
                cameraTargetPosition=[cmd.get("tx", 0), cmd.get("ty", 0), cmd.get("tz", 0)],
                physicsClientId=pid)
        except Exception as e:
            self._send_log("warn", f"设置相机失败: {e}")

    def _cmd_pan_camera(self, cmd):
        pid = self.physics_client_id
        if pid < 0:
            return
        try:
            camera = p.getDebugVisualizerCamera(physicsClientId=pid)
            width, height = max(int(camera[0]), 1), max(int(camera[1]), 1)
            right, up = list(camera[6]), list(camera[7])

            def unit(vector):
                length = math.sqrt(sum(value * value for value in vector)) or 1.0
                return [value / length for value in vector]

            right, up = unit(right), unit(up)
            yaw, pitch, distance = float(camera[8]), float(camera[9]), float(camera[10])
            target = list(camera[11])
            dx = max(-100.0, min(100.0, float(cmd.get("dx", 0.0))))
            dy = max(-100.0, min(100.0, float(cmd.get("dy", 0.0))))
            scale = max(distance, 0.1) * 1.6 / min(width, height)
            target = [target[i] - dx * right[i] * scale + dy * up[i] * scale
                      for i in range(3)]
            p.resetDebugVisualizerCamera(distance, yaw, pitch, target, physicsClientId=pid)
        except Exception as e:
            self._send_log("warn", f"平移视野失败: {e}")

    def _cmd_set_opacity(self, cmd):
        if self.physics_client_id < 0 or self.robot_id is None:
            return
        try:
            self._opacity = max(0.2, min(1.0, float(cmd.get("opacity", 1.0))))
            seen = set()
            for shape in p.getVisualShapeData(self.robot_id, physicsClientId=self.physics_client_id):
                link = int(shape[1])
                if link in seen:
                    continue
                seen.add(link)
                rgba = shape[7]
                p.changeVisualShape(
                    self.robot_id, link,
                    rgbaColor=[float(rgba[0]), float(rgba[1]), float(rgba[2]), self._opacity],
                    physicsClientId=self.physics_client_id)
            self._link_color_cache.clear()
        except Exception as e:
            self._send_log("warn", f"设置模型透明度失败: {e}")

    def _cmd_play(self, start_frame, end_frame):
        if self.joint_data is None:
            self._send_log("error", "请先加载关节数据CSV文件")
            return
        try:
            start_frame = max(0, min(int(start_frame), self._total_frames - 1))
            requested_end = int(end_frame)
        except (TypeError, ValueError):
            start_frame = 0
            requested_end = -1
        end_frame = (self._total_frames if requested_end <= 0
                     else max(start_frame + 1, min(requested_end, self._total_frames)))
        self._do_stop_playback()
        with self._lock:
            self._start_frame = start_frame
            self._end_frame = end_frame
            self._stop_playback = False
            self._paused = False
            self._seek_frame = -1
        self._playback_thread = threading.Thread(target=self._playback_loop, daemon=True)
        self._playback_thread.start()

    def _do_stop_playback(self):
        with self._lock:
            self._stop_playback = True
            self._paused = False
        if self._playback_thread and self._playback_thread.is_alive():
            self._playback_thread.join(timeout=5.0)
        self._playback_thread = None

    # ---- PyBullet初始化 (GPU直渲模式) ----
    def _init_pybullet(self):
        self._cleanup_pybullet()

        pid = p.connect(p.GUI,
            options="--background_color_red=0.05 "
                    "--background_color_green=0.08 "
                    "--background_color_blue=0.15")
        self.physics_client_id = pid

        # 立即禁用实时模拟！GUI模式connect瞬间就开始物理模拟，必须第一时间关闭
        p.setRealTimeSimulation(0, physicsClientId=pid)

        # 禁用PyBullet窗口GUI控件
        p.configureDebugVisualizer(p.COV_ENABLE_GUI, 0, physicsClientId=pid)
        p.configureDebugVisualizer(p.COV_ENABLE_RGB_BUFFER_PREVIEW, 0, physicsClientId=pid)
        p.configureDebugVisualizer(p.COV_ENABLE_DEPTH_BUFFER_PREVIEW, 0, physicsClientId=pid)
        p.configureDebugVisualizer(p.COV_ENABLE_SEGMENTATION_MARK_PREVIEW, 0, physicsClientId=pid)
        p.configureDebugVisualizer(p.COV_ENABLE_SHADOWS, 1, physicsClientId=pid)

        # 设置初始相机
        p.resetDebugVisualizerCamera(
            cameraDistance=1.0, cameraYaw=35, cameraPitch=-25,
            cameraTargetPosition=[0, 0, 0.4], physicsClientId=pid)

        p.setAdditionalSearchPath(pybullet_data.getDataPath(), physicsClientId=pid)
        p.setGravity(0, 0, 0, physicsClientId=pid)
        p.setPhysicsEngineParameter(
            numSolverIterations=20, fixedTimeStep=1/240,
            enableFileCaching=0, physicsClientId=pid)

        # 加载地面
        plane_id = p.loadURDF("plane.urdf", [0, 0, 0], physicsClientId=pid)
        p.changeVisualShape(plane_id, -1, rgbaColor=[0.08, 0.12, 0.2, 1.0], physicsClientId=pid)

        # 加载 M20 URDF
        # 腿长: thigh(0.25) + shank(0.25) + wheel_radius(0.09) ≈ 0.59m
        robot_pos = [0, 0, 0.62]
        robot_rot = p.getQuaternionFromEuler([0, 0, 0])
        urdf_path = get_resource_path("M20_urdf/urdf/M20.urdf")
        self.robot_id = p.loadURDF(
            urdf_path, robot_pos, robot_rot,
            useFixedBase=False, flags=p.URDF_USE_INERTIA_FROM_FILE,
            physicsClientId=pid)

        # --- 设置外观颜色（接近实物配色） ---
        # base_link (-1): 中灰色机体
        p.changeVisualShape(self.robot_id, -1,
            rgbaColor=[0.45, 0.45, 0.48, 1.0], physicsClientId=pid)

        # M20 关节索引: 0-3(FL), 4-7(FR), 8-11(HL), 12-15(HR)
        # 每条腿: hipx, hipy(thigh), knee(shank), wheel
        for leg_base in [0, 4, 8, 12]:
            # hipx: 灰色
            p.changeVisualShape(self.robot_id, leg_base,
                rgbaColor=[0.50, 0.50, 0.52, 1.0], physicsClientId=pid)
            # hipy (thigh): 银白色
            p.changeVisualShape(self.robot_id, leg_base + 1,
                rgbaColor=[0.75, 0.75, 0.77, 1.0], physicsClientId=pid)
            # knee (shank): 浅灰色
            p.changeVisualShape(self.robot_id, leg_base + 2,
                rgbaColor=[0.70, 0.70, 0.72, 1.0], physicsClientId=pid)
            # wheel: 深灰色 (轮胎)
            p.changeVisualShape(self.robot_id, leg_base + 3,
                rgbaColor=[0.28, 0.28, 0.30, 1.0], physicsClientId=pid)

        # 设置关节限位
        # M20: 12 revolute + 4 continuous(wheel)
        joint_cfg = {
            0: (-0.611, 0.436),   # fl_hipx
            1: (-2.583, 2.286),   # fl_hipy
            2: (-2.792, 2.809),   # fl_knee
            4: (-0.436, 0.611),   # fr_hipx
            5: (-2.583, 2.286),   # fr_hipy
            6: (-2.792, 2.809),   # fr_knee
            8: (-0.611, 0.436),   # hl_hipx
            9: (-2.286, 2.583),   # hl_hipy
            10: (-2.809, 2.792),  # hl_knee
            12: (-0.436, 0.611),  # hr_hipx
            13: (-2.286, 2.583),  # hr_hipy
            14: (-2.809, 2.792),  # hr_knee
        }
        for jid, (lo, hi) in joint_cfg.items():
            p.changeDynamics(self.robot_id, jid, jointDamping=0.01,
                             jointLowerLimit=lo, jointUpperLimit=hi, physicsClientId=pid)

        self._send_log("info", "M20 模型加载完成，PyBullet GPU渲染就绪")

        # 发送窗口句柄给Qt
        hwnd = self._get_pybullet_hwnd()
        if hwnd:
            self._send_status({"status": "window_ready", "hwnd": hwnd})
            self._send_log("info", f"渲染窗口句柄: {hwnd}")
        else:
            self._send_log("warn", "未能获取PyBullet窗口句柄")

    def _get_pybullet_hwnd(self):
        """获取PyBullet窗口句柄"""
        try:
            import win32gui
            import win32process
            time.sleep(0.5)
            current_pid = os.getpid()
            result = []
            def callback(hwnd, _):
                _, pid = win32process.GetWindowThreadProcessId(hwnd)
                if pid == current_pid and win32gui.IsWindowVisible(hwnd):
                    title = win32gui.GetWindowText(hwnd)
                    if title:
                        result.append(hwnd)
            win32gui.EnumWindows(callback, None)
            return result[0] if result else 0
        except Exception as e:
            self._send_log("warn", f"获取窗口句柄失败: {e}")
            return 0

    def _cleanup_pybullet(self):
        if self.physics_client_id >= 0:
            try:
                p.disconnect(self.physics_client_id)
            except:
                pass
            self.physics_client_id = -1
            self.robot_id = None
        self._link_color_cache = {}   # 新的 physics client 会重置模型颜色，缓存必须失效

    # ---- 播放主循环 (GPU直渲) ----
    def _playback_loop(self):
        pid = self.physics_client_id
        if pid < 0 or self.robot_id is None:
            self._send_log("error", "PyBullet 未初始化")
            self._send_status({"status": "error"})
            return

        # 新实测格式16列与URDF关节0～15一一对应；旧12列格式仍跳过4个轮关节。
        j_idx = self.joint_indices

        with self._lock:
            start, end = self._start_frame, self._end_frame

        jd = self.joint_data[start:end]
        imu = self.imu_data[start:end]
        jt = self.joint_tor[start:end]
        total = len(jd)
        zbase = 0.62

        self._send_log(
            "info",
            f"开始运动回放（共 {total} 帧, {total * self.frame_interval:.1f}秒）")
        self._send_status({"status": "playing"})

        while True:
            with self._lock:
                if self._stop_playback:
                    break

            fi = 0
            next_tick = time.perf_counter()
            while fi < total:
                while True:
                    with self._lock:
                        if self._stop_playback or not self._paused:
                            break
                    time.sleep(0.05)
                with self._lock:
                    if self._stop_playback:
                        break

                with self._lock:
                    if self._seek_frame >= 0:
                        sf = self._seek_frame - start
                        if 0 <= sf < total:
                            fi = sf
                            next_tick = time.perf_counter()
                        self._seek_frame = -1

                pos = jd[fi]
                ori = (imu[fi].tolist() if self.orientation_is_quaternion
                       else p.getQuaternionFromEuler(imu[fi].tolist()))

                for i, ji in enumerate(j_idx):
                    p.resetJointState(self.robot_id, ji,
                                      targetValue=float(pos[i]),
                                      targetVelocity=0, physicsClientId=pid)

                p.resetBasePositionAndOrientation(
                    self.robot_id, [0, 0, zbase], ori, physicsClientId=pid)

                # 膝关节扭矩着色 (knee joints: PyBullet 2,6,10,14)
                for link, col in [(2, 0), (6, 1), (10, 2), (14, 3)]:
                    self._set_joint_color(self.robot_id, link, abs(int(jt[fi][col])))

                # 每20帧发一次进度
                if fi % 20 == 0:
                    self._send_status({
                        "status": "progress",
                        "frame": fi + start,
                        "total": self._total_frames,
                        "joints": [round(float(v), 4) for v in pos]
                    })

                with self._lock:
                    self._current_frame = fi + start
                    spd = self._speed

                fi += 1
                # 绝对时钟调度可抵消每帧关节更新耗时，250Hz回放不会逐帧累积变慢。
                next_tick += max(0.0001, float(spd))
                wait = next_tick - time.perf_counter()
                if wait > 0:
                    time.sleep(wait)
                elif wait < -0.25:
                    next_tick = time.perf_counter()

            with self._lock:
                if self._stop_playback or not self._loop:
                    break
            self._send_log("info", "循环: 回到起始帧")

        self._send_status({"status": "finished"})

    def _set_joint_color(self, body_id, link_idx, val):
        v = max(0, min(100, val))
        if self._link_color_cache.get(link_idx) == v:
            return
        self._link_color_cache[link_idx] = v
        if v <= 59:
            t = v / 59.0
            r, g, b = colorsys.hsv_to_rgb(0.5, 1.0, 1.0 - 0.5 * t)
        else:
            t = (v - 60) / 40.0
            r, g, b = colorsys.hsv_to_rgb(0.0, 1.0, 0.6 + 0.4 * (1 - t))
        p.changeVisualShape(body_id, link_idx, rgbaColor=[r, g, b, self._opacity],
                            physicsClientId=self.physics_client_id)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    server = M20Server(port=port)
    server.start()
