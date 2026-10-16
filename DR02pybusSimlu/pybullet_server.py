"""DR02 人形机器人三维复现服务。

Qt 只发送控制命令；PyBullet 在独立窗口中直接渲染，窗口句柄交给 Qt 嵌入。
CSV 优先按关节名匹配，也兼容 Joint Pos 0..30 顺序列。
"""

import io
import csv
import json
import math
import os
import re
import socket
import struct
import sys
import tempfile
import threading
import time

if sys.platform == "win32":
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace")

import pybullet as p


MSG_LOG = 0x4C
MSG_STATUS = 0x53
FRAME_INTERVAL = 0.005


def resource_path(relative_path):
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base, relative_path)


def model_directory():
    if hasattr(sys, "_MEIPASS"):
        return resource_path("DR02_model")
    return os.path.abspath(os.path.join(
        os.path.dirname(__file__), "..", "DR02", "DR02_pro-complete-20261009"))


def normalized(text):
    return re.sub(r"[^a-z0-9]", "", str(text).lower())


class DR02Server:
    def __init__(self, host="127.0.0.1", port=0, headless=False):
        self.host = host
        self.port = port
        self.headless = headless
        self.conn = None
        self.server_socket = None
        self._send_lock = threading.Lock()

        self.physics_client_id = -1
        self.robot_id = None
        self.runtime_urdf = ""
        self.base_position = [0.0, 0.0, 1.0]
        self.joint_indices = []
        self.joint_names = []
        self.joint_limits = []
        self._opacity = 1.0

        self.joint_data = None
        self.imu_data = None
        self.base_data = None
        self.csv_path = ""

        self._lock = threading.Lock()
        self._paused = False
        self._stop_playback = True
        self._speed = FRAME_INTERVAL
        self._loop = True
        self._seek_frame = -1
        self._current_frame = 0
        self._total_frames = 0
        self._start_frame = 0
        self._end_frame = 0
        self._playback_thread = None

    def start(self):
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(1)
        print(f"READY {self.server_socket.getsockname()[1]}", flush=True)

        try:
            self.conn, _ = self.server_socket.accept()
            self.conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self._init_pybullet()
            self._handle_client()
        except Exception as exc:
            print(f"Server error: {exc}", file=sys.stderr, flush=True)
        finally:
            self._do_stop_playback()
            self._cleanup_pybullet()
            if self.conn:
                try:
                    self.conn.close()
                except OSError:
                    pass
            if self.server_socket:
                self.server_socket.close()

    def _recv_exact(self, size):
        data = b""
        while len(data) < size:
            chunk = self.conn.recv(size - len(data))
            if not chunk:
                return None
            data += chunk
        return data

    def _recv_message(self):
        header = self._recv_exact(4)
        if header is None:
            return None
        length = struct.unpack(">I", header)[0]
        if length > 16 * 1024 * 1024:
            raise ValueError("命令长度异常")
        return self._recv_exact(length)

    def _send_message(self, message_type, payload):
        body = struct.pack("B", message_type) + payload
        packet = struct.pack(">I", len(body)) + body
        with self._send_lock:
            try:
                self.conn.sendall(packet)
                return True
            except (BrokenPipeError, ConnectionResetError, OSError):
                return False

    def _send_log(self, level, message):
        payload = json.dumps(
            {"level": level, "msg": message}, ensure_ascii=False).encode("utf-8")
        self._send_message(MSG_LOG, payload)

    def _send_status(self, status):
        payload = json.dumps(status, ensure_ascii=False).encode("utf-8")
        self._send_message(MSG_STATUS, payload)

    def _handle_client(self):
        while True:
            try:
                data = self._recv_message()
                if data is None:
                    break
                self._process_command(json.loads(data.decode("utf-8")))
            except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError):
                break
            except json.JSONDecodeError as exc:
                self._send_log("error", f"命令格式错误: {exc}")
            except Exception as exc:
                self._send_log("error", f"命令处理失败: {exc}")

    def _process_command(self, command):
        action = command.get("cmd", "")
        if action == "load_csv":
            self._cmd_load_csv(command.get("path", ""))
        elif action == "play":
            self._cmd_play(command.get("start_frame", 0), command.get("end_frame", -1))
        elif action == "pause":
            with self._lock:
                self._paused = True
            self._send_status({"status": "paused"})
        elif action == "resume":
            with self._lock:
                self._paused = False
            self._send_status({"status": "playing"})
        elif action == "stop":
            self._do_stop_playback()
            self._send_status({"status": "stopped"})
        elif action == "seek":
            with self._lock:
                self._seek_frame = int(command.get("frame", 0))
        elif action == "set_speed":
            with self._lock:
                self._speed = max(0.0001, float(command.get("speed", FRAME_INTERVAL)))
        elif action == "set_loop":
            with self._lock:
                self._loop = bool(command.get("enabled", True))
        elif action == "set_joint":
            self._cmd_set_joint(command.get("joint", 0), command.get("value", 0.0))
        elif action == "set_camera":
            self._cmd_set_camera(command)
        elif action == "pan_camera":
            self._cmd_pan_camera(command)
        elif action == "set_opacity":
            self._cmd_set_opacity(command)
        elif action == "ping":
            self._send_status({"status": "pong"})

    def _resolve_joint_columns(self, fieldnames):
        by_normalized = {normalized(column): column for column in fieldnames}

        # 第一选择：列名直接带关节名。列顺序变化时仍能正确对应。
        named_columns = []
        for name in self.joint_names:
            short_name = name.removesuffix("_joint")
            candidates = (
                name,
                short_name,
                f"Joint Pos {name}",
                f"Joint Pos {short_name}",
                f"joint_position_{name}",
                f"joint_position_{short_name}",
            )
            match = next((by_normalized.get(normalized(c)) for c in candidates
                          if normalized(c) in by_normalized), None)
            if match is None:
                named_columns = []
                break
            named_columns.append(match)
        if named_columns:
            return named_columns, "按关节名称", "name"

        # 兼容现有 X30/M20 数据的顺序命名方式。
        indexed = [f"Joint Pos {index}" for index in range(len(self.joint_names))]
        if all(column in fieldnames for column in indexed):
            return indexed, "按 Joint Pos 顺序", "index"

        q_columns = []
        for index in range(len(self.joint_names)):
            match = next((by_normalized.get(normalized(candidate)) for candidate in
                          (f"q{index}", f"joint{index}", f"joint_pos_{index}")), None)
            if match is None:
                q_columns = []
                break
            q_columns.append(match)
        if q_columns:
            return q_columns, "按 q0/q1 顺序", "index"

        examples = ", ".join(self.joint_names[:4])
        raise ValueError(
            f"没有找到完整的 {len(self.joint_names)} 个关节列。"
            f"可使用关节名（如 {examples}）或 Joint Pos 0 至 "
            f"Joint Pos {len(self.joint_names) - 1}")

    @staticmethod
    def _optional_columns(fieldnames, candidate_groups):
        by_normalized = {normalized(column): column for column in fieldnames}
        selected = []
        for candidates in candidate_groups:
            match = next((by_normalized.get(normalized(c)) for c in candidates
                          if normalized(c) in by_normalized), None)
            if match is None:
                return None
            selected.append(match)
        return selected

    def _cmd_load_csv(self, path):
        try:
            with open(path, "r", encoding="utf-8-sig", newline="") as handle:
                reader = csv.DictReader(handle)
                fieldnames = reader.fieldnames or []
                rows = list(reader)
            if not rows:
                raise ValueError("CSV 没有数据行")

            columns, mapping, mapping_type = self._resolve_joint_columns(fieldnames)

            def number(row, column, row_number):
                try:
                    value = float(row[column])
                except (KeyError, TypeError, ValueError) as exc:
                    raise ValueError(f"第 {row_number} 行的 {column} 不是有效数字") from exc
                if not math.isfinite(value):
                    raise ValueError(f"第 {row_number} 行的 {column} 不是有限数字")
                return value

            joint_data = [
                [number(row, column, row_index + 2) for column in columns]
                for row_index, row in enumerate(rows)
            ]

            imu_columns = self._optional_columns(fieldnames, (
                ("IMU roll", "imu_roll", "base_roll", "roll"),
                ("IMU pitch", "imu_pitch", "base_pitch", "pitch"),
                ("IMU yaw", "imu_yaw", "base_yaw", "yaw"),
            ))
            if imu_columns:
                # 与现有 X30/M20 CSV 保持一致：IMU 欧拉角按度读取。
                imu_data = [
                    [math.radians(number(row, column, row_index + 2))
                     for column in imu_columns]
                    for row_index, row in enumerate(rows)
                ]
                imu_message = "，已读取机身姿态"
            else:
                imu_data = [[0.0, 0.0, 0.0] for _ in rows]
                imu_message = "，未提供机身姿态（保持直立）"

            base_columns = self._optional_columns(fieldnames, (
                ("Base Pos X", "base_x", "position_x"),
                ("Base Pos Y", "base_y", "position_y"),
                ("Base Pos Z", "base_z", "position_z"),
            ))
            base_data = None
            if base_columns:
                base_data = [
                    [number(row, column, row_index + 2) for column in base_columns]
                    for row_index, row in enumerate(rows)
                ]

            self.csv_path = path
            self.joint_data = joint_data
            self.imu_data = imu_data
            self.base_data = base_data
            self._total_frames = len(joint_data)
            duration = round(self._total_frames * FRAME_INTERVAL, 3)
            self._send_status({
                "status": "csv_loaded",
                "frames": int(self._total_frames),
                "duration": duration,
                "filename": os.path.basename(path),
                "joint_count": len(columns),
                "mapping": mapping,
                "mapping_type": mapping_type,
            })
            self._send_log(
                "info",
                f"DR02 数据加载成功: {self._total_frames:,} 帧，{len(columns)} 个关节，"
                f"{mapping}{imu_message}")
        except Exception as exc:
            self.joint_data = None
            self.imu_data = None
            self.base_data = None
            self._total_frames = 0
            self._send_status({"status": "csv_error"})
            self._send_log("error", f"CSV 加载失败: {exc}")

    def _cmd_set_joint(self, joint_index, value):
        if self.physics_client_id < 0 or self.robot_id is None:
            return
        try:
            index = int(joint_index)
            if index not in self.joint_indices:
                raise ValueError("不是 DR02 的可动关节")
            position = self.joint_indices.index(index)
            lower, upper = self.joint_limits[position]
            p.resetJointState(
                self.robot_id, index, targetValue=max(lower, min(float(value), upper)),
                targetVelocity=0.0, physicsClientId=self.physics_client_id)
        except Exception as exc:
            self._send_log("warn", f"设置关节 {joint_index} 失败: {exc}")

    def _cmd_set_camera(self, command):
        if self.physics_client_id < 0 or self.headless:
            return
        try:
            p.resetDebugVisualizerCamera(
                cameraDistance=float(command.get("distance", 2.8)),
                cameraYaw=float(command.get("yaw", 35)),
                cameraPitch=float(command.get("pitch", -12)),
                cameraTargetPosition=[
                    float(command.get("tx", 0)), float(command.get("ty", 0)),
                    float(command.get("tz", 1.0))],
                physicsClientId=self.physics_client_id)
        except Exception as exc:
            self._send_log("warn", f"设置相机失败: {exc}")

    def _cmd_pan_camera(self, command):
        if self.physics_client_id < 0 or self.headless:
            return
        try:
            camera = p.getDebugVisualizerCamera(physicsClientId=self.physics_client_id)
            width, height = max(int(camera[0]), 1), max(int(camera[1]), 1)
            right, up = list(camera[6]), list(camera[7])

            def unit(vector):
                length = math.sqrt(sum(value * value for value in vector)) or 1.0
                return [value / length for value in vector]

            right, up = unit(right), unit(up)
            yaw, pitch, distance = float(camera[8]), float(camera[9]), float(camera[10])
            target = list(camera[11])
            dx = max(-100.0, min(100.0, float(command.get("dx", 0.0))))
            dy = max(-100.0, min(100.0, float(command.get("dy", 0.0))))
            scale = max(distance, 0.1) * 1.6 / min(width, height)
            target = [target[i] - dx * right[i] * scale + dy * up[i] * scale
                      for i in range(3)]
            p.resetDebugVisualizerCamera(
                distance, yaw, pitch, target, physicsClientId=self.physics_client_id)
        except Exception as exc:
            self._send_log("warn", f"平移视野失败: {exc}")

    def _cmd_set_opacity(self, command):
        if self.physics_client_id < 0 or self.robot_id is None:
            return
        try:
            self._opacity = max(0.2, min(1.0, float(command.get("opacity", 1.0))))
            seen = set()
            for shape in p.getVisualShapeData(
                    self.robot_id, physicsClientId=self.physics_client_id):
                link = int(shape[1])
                if link in seen:
                    continue
                seen.add(link)
                rgba = shape[7]
                p.changeVisualShape(
                    self.robot_id, link,
                    rgbaColor=[float(rgba[0]), float(rgba[1]), float(rgba[2]), self._opacity],
                    physicsClientId=self.physics_client_id)
        except Exception as exc:
            self._send_log("warn", f"设置模型透明度失败: {exc}")

    def _cmd_play(self, start_frame, end_frame):
        if self.joint_data is None or self._total_frames <= 0:
            self._send_log("error", "请先加载 DR02 关节数据 CSV 文件")
            return
        self._do_stop_playback()
        start = max(0, min(int(start_frame), self._total_frames - 1))
        end = self._total_frames if int(end_frame) <= 0 else min(int(end_frame), self._total_frames)
        end = max(start + 1, end)
        with self._lock:
            self._start_frame = start
            self._end_frame = end
            self._stop_playback = False
            self._paused = False
            self._seek_frame = -1
        self._playback_thread = threading.Thread(target=self._playback_loop, daemon=True)
        self._playback_thread.start()

    def _do_stop_playback(self):
        with self._lock:
            self._stop_playback = True
            self._paused = False
        if (self._playback_thread and self._playback_thread.is_alive()
                and self._playback_thread is not threading.current_thread()):
            self._playback_thread.join(timeout=3.0)
        self._playback_thread = None

    def _prepare_runtime_urdf(self):
        source = os.path.join(model_directory(), "DR02_pro.urdf")
        mesh_dir = os.path.join(model_directory(), "meshes").replace("\\", "/") + "/"
        if not os.path.isfile(source):
            raise FileNotFoundError(f"找不到 DR02_pro.urdf: {source}")
        if not os.path.isdir(os.path.join(model_directory(), "meshes")):
            raise FileNotFoundError("找不到 DR02 meshes 目录")

        with open(source, "r", encoding="utf-8") as handle:
            content = handle.read()
        # 原模型写成 ./xxx.STL，但 STL 实际位于 meshes 子目录。运行时生成带绝对路径的
        # URDF，既不改用户原始模型，也兼容开发目录与 PyInstaller 临时目录。
        content = content.replace('filename="./', f'filename="{mesh_dir}')
        handle = tempfile.NamedTemporaryFile(
            mode="w", suffix=".urdf", prefix="dr02_runtime_", encoding="utf-8", delete=False)
        handle.write(content)
        handle.close()
        self.runtime_urdf = handle.name
        return handle.name

    def _init_pybullet(self):
        self._cleanup_pybullet()
        mode = p.DIRECT if self.headless else p.GUI
        options = "--background_color_red=0.05 --background_color_green=0.08 --background_color_blue=0.15"
        self.physics_client_id = p.connect(mode, options=options if not self.headless else "")
        pid = self.physics_client_id
        if pid < 0:
            raise RuntimeError("无法启动 PyBullet")

        p.setRealTimeSimulation(0, physicsClientId=pid)
        p.setGravity(0, 0, 0, physicsClientId=pid)
        p.setPhysicsEngineParameter(
            numSolverIterations=20, fixedTimeStep=1 / 240,
            enableFileCaching=0, physicsClientId=pid)

        if not self.headless:
            p.configureDebugVisualizer(p.COV_ENABLE_GUI, 0, physicsClientId=pid)
            p.configureDebugVisualizer(p.COV_ENABLE_RGB_BUFFER_PREVIEW, 0, physicsClientId=pid)
            p.configureDebugVisualizer(p.COV_ENABLE_DEPTH_BUFFER_PREVIEW, 0, physicsClientId=pid)
            p.configureDebugVisualizer(p.COV_ENABLE_SEGMENTATION_MARK_PREVIEW, 0, physicsClientId=pid)
            p.configureDebugVisualizer(p.COV_ENABLE_SHADOWS, 1, physicsClientId=pid)

        plane_shape = p.createCollisionShape(p.GEOM_PLANE, physicsClientId=pid)
        plane = p.createMultiBody(
            baseMass=0, baseCollisionShapeIndex=plane_shape,
            basePosition=[0, 0, 0], physicsClientId=pid)
        p.changeVisualShape(
            plane, -1, rgbaColor=[0.08, 0.12, 0.20, 1.0], physicsClientId=pid)

        self.robot_id = p.loadURDF(
            self._prepare_runtime_urdf(), [0, 0, 0], [0, 0, 0, 1],
            useFixedBase=True, flags=p.URDF_USE_INERTIA_FROM_FILE,
            physicsClientId=pid)

        self.joint_indices = []
        self.joint_names = []
        self.joint_limits = []
        for index in range(p.getNumJoints(self.robot_id, physicsClientId=pid)):
            info = p.getJointInfo(self.robot_id, index, physicsClientId=pid)
            if info[2] not in (p.JOINT_REVOLUTE, p.JOINT_PRISMATIC):
                continue
            lower, upper = float(info[8]), float(info[9])
            self.joint_indices.append(index)
            self.joint_names.append(info[1].decode("utf-8"))
            self.joint_limits.append((lower, upper))

        if len(self.joint_indices) != 31:
            raise RuntimeError(f"DR02 可动关节应为 31 个，实际为 {len(self.joint_indices)} 个")

        all_aabbs = [p.getAABB(self.robot_id, link, physicsClientId=pid)
                     for link in range(-1, p.getNumJoints(self.robot_id, physicsClientId=pid))]
        minimum_z = min(bounds[0][2] for bounds in all_aabbs)
        maximum_z = max(bounds[1][2] for bounds in all_aabbs)
        height = maximum_z - minimum_z
        self.base_position = [0.0, 0.0, -minimum_z + 0.01]
        p.resetBasePositionAndOrientation(
            self.robot_id, self.base_position, [0, 0, 0, 1], physicsClientId=pid)

        # 保留 STL 的结构细节，用分区颜色帮助识别四肢动作。
        p.changeVisualShape(self.robot_id, -1, rgbaColor=[0.32, 0.36, 0.42, 1.0], physicsClientId=pid)
        for link in self.joint_indices:
            if 3 <= link <= 17:
                color = [0.68, 0.72, 0.78, 1.0]
            elif 21 <= link <= 32:
                color = [0.52, 0.58, 0.66, 1.0]
            else:
                color = [0.42, 0.48, 0.56, 1.0]
            p.changeVisualShape(self.robot_id, link, rgbaColor=color, physicsClientId=pid)

        target_z = self.base_position[2] + (minimum_z + maximum_z) * 0.5
        if not self.headless:
            p.resetDebugVisualizerCamera(
                cameraDistance=max(2.2, height * 1.35), cameraYaw=35, cameraPitch=-12,
                cameraTargetPosition=[0, 0, target_z], physicsClientId=pid)

        metadata = [
            {"index": index, "name": name, "lower": limits[0], "upper": limits[1]}
            for index, name, limits in zip(self.joint_indices, self.joint_names, self.joint_limits)
        ]
        self._send_status({"status": "model_ready", "joint_count": 31, "joints": metadata})
        self._send_log("info", f"DR02 人形模型加载完成：31 个可动关节，模型高度约 {height:.2f} m")

        if not self.headless:
            hwnd = self._get_pybullet_hwnd()
            if hwnd:
                self._send_status({"status": "window_ready", "hwnd": hwnd})
                self._send_log("info", f"DR02 渲染窗口句柄: {hwnd}")
            else:
                self._send_log("warn", "未能获取 DR02 渲染窗口句柄")

    def _get_pybullet_hwnd(self):
        try:
            import win32gui
            import win32process
            time.sleep(0.5)
            current_pid = os.getpid()
            windows = []

            def callback(hwnd, _):
                _, process_id = win32process.GetWindowThreadProcessId(hwnd)
                if (process_id == current_pid and win32gui.IsWindowVisible(hwnd)
                        and win32gui.GetWindowText(hwnd)):
                    windows.append(hwnd)

            win32gui.EnumWindows(callback, None)
            return windows[0] if windows else 0
        except Exception as exc:
            self._send_log("warn", f"获取窗口句柄失败: {exc}")
            return 0

    def _cleanup_pybullet(self):
        if self.physics_client_id >= 0:
            try:
                p.disconnect(self.physics_client_id)
            except p.error:
                pass
        self.physics_client_id = -1
        self.robot_id = None
        if self.runtime_urdf:
            try:
                os.remove(self.runtime_urdf)
            except OSError:
                pass
        self.runtime_urdf = ""

    def _apply_frame(self, frame_index):
        pid = self.physics_client_id
        positions = self.joint_data[frame_index]
        for data_index, joint_index in enumerate(self.joint_indices):
            lower, upper = self.joint_limits[data_index]
            value = max(lower, min(float(positions[data_index]), upper))
            p.resetJointState(
                self.robot_id, joint_index, targetValue=value,
                targetVelocity=0.0, physicsClientId=pid)

        orientation = p.getQuaternionFromEuler(self.imu_data[frame_index])
        base = self.base_position
        if self.base_data is not None:
            # 数据中的 Z 视作相对站立高度，避免不同记录坐标原点导致模型落入地面。
            base = [float(self.base_data[frame_index][0]),
                    float(self.base_data[frame_index][1]),
                    self.base_position[2] + float(self.base_data[frame_index][2])]
        p.resetBasePositionAndOrientation(
            self.robot_id, base, orientation, physicsClientId=pid)
        return positions

    def _playback_loop(self):
        if self.physics_client_id < 0 or self.robot_id is None:
            self._send_log("error", "DR02 PyBullet 尚未初始化")
            self._send_status({"status": "error"})
            return

        with self._lock:
            start, end = self._start_frame, self._end_frame
        total = end - start
        self._send_log("info", f"开始 DR02 动作回放：{total} 帧，约 {total * FRAME_INTERVAL:.1f} 秒")
        self._send_status({"status": "playing"})

        while True:
            with self._lock:
                if self._stop_playback:
                    break
            frame = start
            while frame < end:
                while True:
                    with self._lock:
                        stopped = self._stop_playback
                        paused = self._paused
                    if stopped or not paused:
                        break
                    time.sleep(0.03)
                if stopped:
                    break

                with self._lock:
                    if self._seek_frame >= 0:
                        frame = max(start, min(self._seek_frame, end - 1))
                        self._seek_frame = -1

                positions = self._apply_frame(frame)
                if (frame - start) % 10 == 0 or frame == end - 1:
                    self._send_status({
                        "status": "progress",
                        "frame": frame,
                        "total": self._total_frames,
                        "joints": [round(float(value), 5) for value in positions],
                    })

                with self._lock:
                    self._current_frame = frame
                    delay = self._speed
                time.sleep(delay)
                frame += 1

            with self._lock:
                if self._stop_playback or not self._loop:
                    break
            self._send_log("info", "循环播放：回到起始帧")

        self._send_status({"status": "finished"})


if __name__ == "__main__":
    arguments = sys.argv[1:]
    port = int(arguments[0]) if arguments and arguments[0].isdigit() else 0
    headless = "--headless" in arguments or os.environ.get("DR02_HEADLESS") == "1"
    DR02Server(port=port, headless=headless).start()
