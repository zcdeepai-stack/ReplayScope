"""
PyBullet GPU直渲服务器
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


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from map import TerrainMap

MSG_LOG    = 0x4C
MSG_STATUS = 0x53


class PyBulletServer:
    TERRAIN_FRAMES_PER_JOINT = 20
    TERRAIN_BASE = 1.8

    def __init__(self, host='127.0.0.1', port=0):
        self.host = host
        self.port = port
        self.conn = None
        self.server_socket = None
        self._send_lock = threading.Lock()

        self.physics_client_id = -1
        self.robot_id = None
        self.terrain_map = None
        # link -> 上一次实际下发的色阶值。changeVisualShape 是同步往返调用，
        # 而色阶只有 0~100 共 101 档，绝大多数帧颜色并未变化，缓存后可跳过。
        self._link_color_cache = {}
        self._opacity = 1.0

        self.csv_path = ""
        self.map_csv_path = ""
        # 是否在播放时加载地形图。默认关闭：未选文件 / 文件不存在一律不加载。
        self._terrain_enabled = False
        self._terrain_loaded_path = ""   # 当前已建好的地形对应的文件，用于判断是否需要重建
        self.joint_data = None
        self.imu_data = None
        self.joint_tor = None

        self._lock = threading.Lock()
        self._paused = False
        self._stop_playback = True
        self._speed = 0.005
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

            # 连接建立后立即初始化 PyBullet 并显示静态模型
            self._init_pybullet_display()

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
            try:
                chunk = self.conn.recv(n - len(data))
            except (ConnectionResetError, ConnectionAbortedError, OSError):
                return None
            if not chunk:
                return None
            data += chunk
        return data

    def _recv_message(self):
        header = self._recv_exact(4)
        if header is None:
            return None
        length = struct.unpack('>I', header)[0]
        if length == 0:
            return b'{}'
        return self._recv_exact(length)

    def _send_raw(self, data):
        with self._send_lock:
            if self.conn is None:
                return False
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
        elif action == "load_terrain":
            self._cmd_load_terrain(cmd)
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
            with self._lock: self._speed = cmd.get("speed", 0.005)
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
            df = pd.read_csv(path)
            self.csv_path = path
            self.joint_data = df[[f'Joint Pos {i}' for i in range(12)]].values
            self.joint_tor = df[[f'Joint Tor {i}' for i in (2, 5, 8, 11)]].values
            self.imu_data = np.radians(df[['IMU roll', 'IMU pitch', 'IMU yaw']].values)
            self._total_frames = len(self.joint_data)
            dur = round(self._total_frames * 0.005, 2)
            self._send_status({
                "status": "csv_loaded",
                "frames": int(self._total_frames),
                "duration": dur,
                "filename": os.path.basename(path)
            })
            self._send_log("info", f"关节数据加载成功: {self._total_frames:,}帧, {dur:.1f}秒")
        except Exception as e:
            self._send_log("error", f"CSV加载错误: {e}")

    def _cmd_load_terrain(self, cmd):
        """只记录意图，真正的建/拆在回放线程里做，避免与回放并发调用 PyBullet"""
        path = cmd.get("path", "") or ""
        self._terrain_enabled = bool(cmd.get("enabled", False))
        self.map_csv_path = path

        if not self._terrain_enabled or not path:
            self._send_status({"status": "terrain_disabled"})
            self._send_log("info", "地形图: 不加载")
            return

        fn = os.path.basename(path)
        self._send_status({"status": "terrain_loaded", "filename": fn})
        self._send_log("info", f"地形图路径已设置: {fn}")

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
                cameraDistance=cmd.get("distance", 1.5),
                cameraYaw=cmd.get("yaw", 35),
                cameraPitch=cmd.get("pitch", -30),
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
        self._do_stop_playback()
        with self._lock:
            self._start_frame = start_frame
            self._end_frame = end_frame if end_frame > 0 else self._total_frames
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

    # ---- PyBullet初始化: 静态模型显示 (GPU直渲模式) ----
    def _init_pybullet_display(self):
        """连接建立后立即调用，加载URDF显示静态模型（不依赖CSV数据）"""
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
            cameraDistance=1.5, cameraYaw=35, cameraPitch=-30,
            cameraTargetPosition=[0, 0, 1.9], physicsClientId=pid)

        p.setAdditionalSearchPath(pybullet_data.getDataPath(), physicsClientId=pid)
        p.setGravity(0, 0, 0, physicsClientId=pid)
        p.setPhysicsEngineParameter(
            numSolverIterations=20, fixedTimeStep=1/240,
            enableFileCaching=0, physicsClientId=pid)

        plane_id = p.loadURDF("plane.urdf", [0, 0, 0], physicsClientId=pid)
        p.changeVisualShape(plane_id, -1, rgbaColor=[0.08, 0.12, 0.2, 1.0], physicsClientId=pid)

        # 加载 X30 URDF（使用默认站立姿态，不依赖CSV数据）
        robot_pos = [0, 0, 2.05]
        robot_rot = p.getQuaternionFromEuler([0, 0, 0])
        urdf_path = get_resource_path("X30/urdf/X30.urdf")
        self.robot_id = p.loadURDF(
            urdf_path, robot_pos, robot_rot,
            useFixedBase=False, flags=p.URDF_USE_INERTIA_FROM_FILE,
            physicsClientId=pid)

        joint_cfg = {
            0: (-0.323, 0.585), 1: (-2.967, 0.262), 2: (0.349, 2.531),
            4: (-0.585, 0.323), 5: (-2.967, 0.262), 6: (0.349, 2.531),
            8: (-0.323, 0.585), 9: (-2.967, 0.262), 10: (0.349, 2.531),
            12: (-0.585, 0.323), 13: (-2.967, 0.262), 14: (0.349, 2.531),
        }
        for jid, (lo, hi) in joint_cfg.items():
            p.changeDynamics(self.robot_id, jid, jointDamping=0.01,
                             jointLowerLimit=lo, jointUpperLimit=hi, physicsClientId=pid)

        # 默认站立姿态
        init_pos = [-0.367596, -1.48719, 2.72085, 0.267604, -1.4947, 2.72902,
                     -0.314182, -1.49791, 2.75625, 0.260833, -1.49793, 2.72576]
        j_idx = [0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14]
        for ji, jp in zip(j_idx, init_pos):
            p.resetJointState(self.robot_id, ji, jp, physicsClientId=pid)

        p.resetBasePositionAndOrientation(self.robot_id, robot_pos, robot_rot, physicsClientId=pid)

        self._send_log("info", "X30 模型加载完成，PyBullet GPU渲染就绪")

        # 发送窗口句柄给Qt嵌入
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
                    if title:  # PyBullet窗口有标题
                        result.append(hwnd)
            win32gui.EnumWindows(callback, None)
            return result[0] if result else 0
        except Exception as e:
            self._send_log("warn", f"获取窗口句柄失败: {e}")
            return 0

    # ---- 地形的建 / 拆 ----
    def _destroy_terrain(self):
        """卸载已创建的地形方块。只应在回放线程中调用。"""
        if self.terrain_map is None:
            self._terrain_loaded_path = ""
            return
        try:
            self.terrain_map.remove_cubes()
            self._send_log("info", "已卸载地形图")
        except Exception as e:
            self._send_log("warn", f"卸载地形图失败: {e}")
        self.terrain_map = None
        self._terrain_loaded_path = ""

    def _prepare_terrain(self, pid):
        """依据开关 + 文件是否存在，决定加载 / 复用 / 重建 / 卸载"""
        path = self.map_csv_path or ""
        want = bool(self._terrain_enabled) and bool(path) and os.path.isfile(path)

        # 勾了但用不了，给出明确原因
        if self._terrain_enabled and not want:
            if not path:
                self._send_log("warn", "未选择地形图文件，本次不加载地形")
            else:
                self._send_log("warn", f"地形图文件不存在，本次不加载地形: {path}")

        if not want:
            self._destroy_terrain()
            return

        # 已经加载的就是同一个文件，直接复用，避免重复建 693 个方块
        if self.terrain_map is not None and self._terrain_loaded_path == path:
            return

        self._destroy_terrain()   # 换文件时先清掉旧的，否则新旧地形会叠加在场景里
        try:
            self.terrain_map = TerrainMap(
                csv_file_path=path, physics_client_id=pid,
                sim_sleep_time=0.01, relative_base=self.TERRAIN_BASE,
                grid_rows=21, grid_cols=33,
                cube_half_x=0.03, cube_half_y=0.03, cube_half_z=0.03,
                grid_spacing=0.07,
                log_callback=lambda msg: self._send_log("info", msg))
            self.terrain_map._init_cubes()
            self._terrain_loaded_path = path
            self._send_log("info", f"地形图初始化完成 | 地形帧:{self.terrain_map.total_frames}")
        except Exception as e:
            self._send_log("warn", f"地形图初始化失败: {e}")
            self.terrain_map = None
            self._terrain_loaded_path = ""

    # ---- 播放主循环 (GPU直渲，不捕获帧) ----
    def _playback_loop(self):
        pid = self.physics_client_id
        if pid < 0 or self.robot_id is None:
            self._send_log("error", "PyBullet 未初始化")
            self._send_status({"status": "error"})
            return

        # 按当前开关与文件状态决定加载 / 重建 / 卸载地形（在回放线程中做，无并发风险）
        self._prepare_terrain(pid)

        # 设置CSV数据的初始姿态
        j_idx = [0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14]
        init_pos = [-0.367596, -1.48719, 2.72085, 0.267604, -1.4947, 2.72902,
                     -0.314182, -1.49791, 2.75625, 0.260833, -1.49793, 2.72576]
        for ji, jp in zip(j_idx, init_pos):
            p.resetJointState(self.robot_id, ji, jp, physicsClientId=pid)

        r0, p0, y0 = self.imu_data[0]
        quat = p.getQuaternionFromEuler([r0, p0, y0])
        p.resetBasePositionAndOrientation(self.robot_id, [0, 0, 2.05], quat, physicsClientId=pid)
        TF = self.TERRAIN_FRAMES_PER_JOINT
        squat1 = [-0.326406, -1.16762, 2.72495, 0.326414, -1.16511, 2.72588,
                   -0.318728, -1.1434, 2.71686, 0.31584, -1.15776, 2.72667]
        squat2 = [-0.326998, -1.49174, 2.72751, 0.324121, -1.48969, 2.72757,
                   -0.321665, -1.49547, 2.71819, 0.324077, -1.48447, 2.72756]

        with self._lock:
            start, end = self._start_frame, self._end_frame

        jd = self.joint_data[start:end]
        imu = self.imu_data[start:end]
        jt = self.joint_tor[start:end]
        total = len(jd)
        zbase = 2.05
        mf = max(1, (start // TF) + 1)
        syn = 0

        self._send_log("info", f"开始运动回放（共 {total} 帧, {total*0.005:.1f}秒）")
        self._send_status({"status": "playing"})

        while True:
            with self._lock:
                if self._stop_playback:
                    break

            fi = 0
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
                            mf = max(1, (self._seek_frame // TF) + 1)
                            syn = 0
                        self._seek_frame = -1

                if syn % TF == 0:
                    try:
                        if self.terrain_map is not None:
                            self.terrain_map.update_terrain_frame(mf)
                    except Exception:
                        pass
                    mf += 1

                pos = jd[fi]
                ori = p.getQuaternionFromEuler(imu[fi].tolist())

                if self._check_ratio(pos, squat1) or self._check_ratio(pos, squat2):
                    zbase = 2.05
                elif self.terrain_map is not None:
                    zbase = self.terrain_map.max_center_height + 0.547
                else:
                    zbase = 2.05

                for i, ji in enumerate(j_idx):
                    p.resetJointState(self.robot_id, ji,
                                      targetValue=float(pos[i]),
                                      targetVelocity=0, physicsClientId=pid)

                p.resetBasePositionAndOrientation(
                    self.robot_id, [0, 0, zbase], ori, physicsClientId=pid)

                for link, col in [(2, 0), (6, 1), (10, 2), (14, 3)]:
                    self._set_joint_color(self.robot_id, link, abs(int(jt[fi][col])))

                syn += 1

                # 每20帧发一次进度（只传数字，极轻量）
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

                time.sleep(spd)
                fi += 1

            with self._lock:
                if self._stop_playback or not self._loop:
                    break
                mf = max(1, (start // TF) + 1)
                syn = 0
            self._send_log("info", "循环: 回到起始帧")

        self._send_status({"status": "finished"})
        # 不调用 _cleanup_pybullet()，保留 3D 场景可见可旋转
        # 下次播放时 _init_pybullet() 会自动清理旧实例

    @staticmethod
    def _check_ratio(target, base, threshold=0.04, ratio=0.80):
        ok = sum(1 for a, b in zip(target, base) if abs(a - b) <= threshold)
        return (ok / len(base)) >= ratio

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

    def _cleanup_pybullet(self):
        if self.physics_client_id >= 0:
            try:
                p.disconnect(self.physics_client_id)
            except Exception:
                pass
            self.physics_client_id = -1
        self.robot_id = None
        self.terrain_map = None       # 方块随 physics client 一起销毁，无需逐个 removeBody
        self._terrain_loaded_path = ""
        self._link_color_cache = {}   # 新的 physics client 会重置模型颜色，缓存必须失效


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    server = PyBulletServer(port=port)
    server.start()
