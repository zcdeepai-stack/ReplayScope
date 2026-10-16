import pybullet as p
import pybullet_data
import numpy as np
import pandas as pd
import time


class TerrainMap:
    """
    地形可视化类：封装地形数据加载、3D可视化、帧更新、中心区域高度计算等功能
    核心修改：仅将每列前2后2行高度替换为该列中间7行平均值，其余功能完全不变
    """

    def __init__(self, csv_file_path, physics_client_id, sim_sleep_time=2,
                 relative_base=1.2, grid_rows=21, grid_cols=33,
                 cube_half_x=0.02,  # 宽半尺寸（总宽0.08）
                 cube_half_y=0.02,  # 长半尺寸（总长0.08）
                 cube_half_z=0.05,  # 高半尺寸（总高0.02）
                 grid_spacing=0.11,
                 row_height_threshold=0.02, center_region_size=7, log_callback=None):
        # 核心参数赋值
        self.CSV_FILE_PATH = csv_file_path
        self.physics_client_id = physics_client_id
        self.SIM_SLEEP_TIME = sim_sleep_time
        self.RELATIVE_BASE = relative_base
        self.GRID_ROWS = grid_rows
        self.GRID_COLS = grid_cols
        self.CUBE_HALF_X = cube_half_x
        self.CUBE_HALF_Y = cube_half_y
        self.CUBE_HALF_Z = cube_half_z
        self.GRID_SPACING = grid_spacing
        self.ROW_HEIGHT_THRESHOLD = row_height_threshold
        self.CENTER_REGION_SIZE = center_region_size
        self.log_callback = log_callback

        # 颜色配置 - 科技感蓝色系渐变
        self.COL_COLOR_LIST = [
            [0.1, 0.4, 0.8, 0.85],   # 深蓝色
            [0.0, 0.7, 0.9, 0.85],   # 青蓝色
            [0.2, 0.9, 0.95, 0.85]   # 亮青色
        ]

        # 全局状态变量
        self.max_center_height = 0.0
        self.cube_id_map = None
        self.last_terrain_data = None
        self.total_frames = 0
        self.df = None
        # 缓存每帧的列颜色和颜色索引
        self.current_frame_colors = None
        self.current_frame_color_indices = None

        # 已下发到 PyBullet 的状态。changeVisualShape / resetBasePositionAndOrientation
        # 都是同步往返调用，一帧全量刷新要 693×2 次；相邻帧绝大多数方块并无变化，
        # 记录"当前实际生效的值"后只对真正变化的方块下发指令。
        self._applied_z = None             # (GRID_ROWS, GRID_COLS)
        self._applied_col_color_idx = None # (GRID_COLS,)
        self._cube_x = None                # 每列的 x（固定不变）
        self._cube_y = None                # 每行的 y（固定不变）

        # 初始化流程
        self._load_and_validate_csv()
        self._print_init_info()

    def _load_and_validate_csv(self):
        self.df = pd.read_csv(self.CSV_FILE_PATH, header=None)
        self.total_frames = len(self.df)
        expected_cols = 1 + self.GRID_ROWS * self.GRID_COLS
        actual_cols = self.df.shape[1]
        if actual_cols != expected_cols:
            raise ValueError(
                f"CSV数据格式错误！\n"
                f"预期每行列数：{expected_cols}列（1列时间戳 + {self.GRID_ROWS}×{self.GRID_COLS}地形数据） | 实际：{actual_cols}列"
                f"\n请检查CSV数据是否匹配{self.GRID_ROWS}行×{self.GRID_COLS}列的地形维度"
            )

    def _print_init_info(self):
        log_message = f"""
                    ✅ CSV数据加载完成！总帧数：{self.total_frames}帧 | 地形尺寸：{self.GRID_ROWS}×{self.GRID_COLS}
                    📌 坐标基准：{self.RELATIVE_BASE} | 帧率：{self.SIM_SLEEP_TIME}s | 行误差阈值：{self.ROW_HEIGHT_THRESHOLD}
                    📌 列颜色规则：按列中间7行平均值差值>0.12切换（红→青绿→草绿） | 运算精度：2位小数
                    📌 数据还原：CSV原始高度值已扩大2倍，增加视觉效果
                    📌 中心区域：提取{self.CENTER_REGION_SIZE}×{self.CENTER_REGION_SIZE}网格的最大高度（用于机器狗放置）
                    📌 绘制规则：按列遍历更新长方体 | 长方体尺寸：宽{self.CUBE_HALF_X * 2}×长{self.CUBE_HALF_Y * 2}×高{self.CUBE_HALF_Z * 2}"""
        print("log info : ", log_message)
        if self.log_callback:
            self.log_callback(log_message.strip())  # 使用回调

    def _get_col_mid7_avg(self, terrain_earth_2d):
        """计算每列中间7行的高度平均值"""
        mid7_row_start = (self.GRID_ROWS // 2) - (7 // 2)
        mid7_row_end = mid7_row_start + 7
        col_avg_list = []
        for col_idx in range(self.GRID_COLS):
            col_data = terrain_earth_2d[mid7_row_start:mid7_row_end, col_idx]
            col_avg = np.mean(col_data)
            col_avg_list.append(col_avg)
        return np.array(col_avg_list)

    def _get_col_color(self, col_idx, col_avg_list):
        """按列间差值动态获取颜色，保证列内颜色一致"""
        if col_idx == 0:
            current_color_idx = 0
        else:
            avg_diff = abs(col_avg_list[col_idx] - col_avg_list[col_idx - 1])
            if avg_diff > 0.12:
                current_color_idx = (self.current_frame_color_indices[col_idx - 1] + 1) % len(self.COL_COLOR_LIST)
            else:
                current_color_idx = self.current_frame_color_indices[col_idx - 1]

        self.current_frame_color_indices.append(current_color_idx)
        return self.COL_COLOR_LIST[current_color_idx]

    def calculate_center_max_height(self, terrain_earth_2d):
        row_start = (self.GRID_ROWS // 2) - (self.CENTER_REGION_SIZE // 2)
        row_end = row_start + self.CENTER_REGION_SIZE
        col_start = (self.GRID_COLS // 2) - (self.CENTER_REGION_SIZE // 2)
        col_end = col_start + self.CENTER_REGION_SIZE
        center_region = terrain_earth_2d[row_start:row_end, col_start:col_end]
        max_height = np.max(center_region)
        self.max_center_height = max_height + self.CUBE_HALF_Z
        return self.max_center_height

    def get_frame_terrain(self, target_frame):
        if target_frame > self.total_frames:
            target_frame = self.total_frames - 1
        if not (1 <= target_frame <= self.total_frames):
            raise ValueError(f"帧号{target_frame}超出范围！合法范围：1~{self.total_frames}")
        frame_idx = target_frame - 1
        frame_data = self.df.iloc[frame_idx]
        time_stamp = frame_data[0]
        terrain_1d = np.round(frame_data[1:].values, 2) * 2
        terrain_raw_2d = terrain_1d.reshape((self.GRID_ROWS, self.GRID_COLS))
        terrain_earth_2d = np.zeros_like(terrain_raw_2d, dtype=np.float64)
        for i in range(self.GRID_ROWS):
            for j in range(self.GRID_COLS):
                raw_val = terrain_raw_2d[i, j]
                earth_val = self.RELATIVE_BASE - abs(raw_val) if raw_val < 0 else self.RELATIVE_BASE + raw_val
                terrain_earth_2d[i, j] = np.round(earth_val, 2)
        return terrain_earth_2d, time_stamp

    def _init_cubes(self):
        """初始化创建长方体（仅执行一次）"""
        self.cube_id_map = np.full((self.GRID_ROWS, self.GRID_COLS), -1, dtype=int)
        shared_coll_shape = p.createCollisionShape(
            shapeType=p.GEOM_BOX,
            halfExtents=[self.CUBE_HALF_X, self.CUBE_HALF_Y, self.CUBE_HALF_Z],
            physicsClientId=self.physics_client_id
        )

        CENTER_ROW = self.GRID_ROWS // 2
        CENTER_COL = self.GRID_COLS // 2

        for col_idx in range(self.GRID_COLS):
            for row_idx in range(self.GRID_ROWS):
                x = (col_idx - CENTER_COL) * self.GRID_SPACING
                y = (row_idx - CENTER_ROW) * self.GRID_SPACING
                z = self.RELATIVE_BASE + self.CUBE_HALF_Z
                color = self.COL_COLOR_LIST[0]

                vis_shape = p.createVisualShape(
                    shapeType=p.GEOM_BOX,
                    halfExtents=[self.CUBE_HALF_X, self.CUBE_HALF_Y, self.CUBE_HALF_Z],
                    rgbaColor=color,
                    physicsClientId=self.physics_client_id
                )

                cube_id = p.createMultiBody(
                    baseMass=0,
                    baseCollisionShapeIndex=shared_coll_shape,
                    baseVisualShapeIndex=vis_shape,
                    basePosition=[x, y, z],
                    physicsClientId=self.physics_client_id
                )
                self.cube_id_map[row_idx, col_idx] = cube_id

        # 记录初始状态：全部方块 z = RELATIVE_BASE + CUBE_HALF_Z，颜色为 COL_COLOR_LIST[0]
        self._applied_z = np.full((self.GRID_ROWS, self.GRID_COLS),
                                  self.RELATIVE_BASE + self.CUBE_HALF_Z, dtype=np.float64)
        self._applied_col_color_idx = np.zeros(self.GRID_COLS, dtype=np.int32)
        self._cube_x = (np.arange(self.GRID_COLS) - CENTER_COL) * self.GRID_SPACING
        self._cube_y = (np.arange(self.GRID_ROWS) - CENTER_ROW) * self.GRID_SPACING

        print(f"✅ 初始化完成：全量创建{self.GRID_ROWS * self.GRID_COLS}个长方体 | 初始化顺序：按列创建")

    def remove_cubes(self):
        """移除本实例创建的全部长方体。
        用于关闭地形显示或切换地形文件时清场；必须在回放线程中调用，
        避免与回放中的 PyBullet 调用并发。"""
        if self.cube_id_map is None:
            return
        removed = 0
        for cube_id in self.cube_id_map.flatten().tolist():
            if cube_id is None or int(cube_id) < 0:
                continue
            try:
                p.removeBody(int(cube_id), physicsClientId=self.physics_client_id)
                removed += 1
            except Exception:
                pass
        self.cube_id_map = None
        self._applied_z = None
        self._applied_col_color_idx = None
        return removed

    def update_terrain_frame(self, frame_num):
        terrain_earth_2d, time_stamp = self.get_frame_terrain(frame_num)
        self.calculate_center_max_height(terrain_earth_2d)

        col_avg_list = self._get_col_mid7_avg(terrain_earth_2d)
        self.current_frame_colors = []
        self.current_frame_color_indices = []
        for col_idx in range(self.GRID_COLS):
            col_color = self._get_col_color(col_idx, col_avg_list)
            self.current_frame_colors.append(col_color)

        if self._cube_x is None:   # 未经 _init_cubes 直接调用时的兜底
            self._cube_x = (np.arange(self.GRID_COLS) - self.GRID_COLS // 2) * self.GRID_SPACING
            self._cube_y = (np.arange(self.GRID_ROWS) - self.GRID_ROWS // 2) * self.GRID_SPACING

        # 目标高度矩阵：与原逐格分支完全等价（第 0/1/18/19 行取该列均值，其余取真实高度）
        use_h = terrain_earth_2d.astype(np.float64, copy=True)
        for edge_row in (0, 1, 18, 19):
            if 0 <= edge_row < self.GRID_ROWS:
                use_h[edge_row, :] = col_avg_list
        target_z = use_h + self.CUBE_HALF_Z

        color_idx_arr = np.asarray(self.current_frame_color_indices, dtype=np.int32)

        update_count = 0

        # ---- 只对颜色变化的列下发 changeVisualShape ----
        if self._applied_col_color_idx is None or \
                self._applied_col_color_idx.shape != color_idx_arr.shape:
            changed_cols = np.arange(self.GRID_COLS)
        else:
            changed_cols = np.nonzero(color_idx_arr != self._applied_col_color_idx)[0]

        for col_idx in changed_cols.tolist():
            col_color = self.COL_COLOR_LIST[int(color_idx_arr[col_idx])]
            for row_idx in range(self.GRID_ROWS):
                p.changeVisualShape(
                    objectUniqueId=int(self.cube_id_map[row_idx, col_idx]),
                    linkIndex=-1,
                    rgbaColor=col_color,
                    physicsClientId=self.physics_client_id
                )
                update_count += 1

        # ---- 只对高度变化的方块下发 resetBasePositionAndOrientation ----
        if self._applied_z is None or self._applied_z.shape != target_z.shape:
            rows_i, cols_i = np.nonzero(np.ones(target_z.shape, dtype=bool))
        else:
            rows_i, cols_i = np.nonzero(target_z != self._applied_z)

        for row_idx, col_idx in zip(rows_i.tolist(), cols_i.tolist()):
            p.resetBasePositionAndOrientation(
                bodyUniqueId=int(self.cube_id_map[row_idx, col_idx]),
                posObj=[float(self._cube_x[col_idx]),
                        float(self._cube_y[row_idx]),
                        float(target_z[row_idx, col_idx])],
                ornObj=[0, 0, 0, 1],
                physicsClientId=self.physics_client_id
            )
            update_count += 1

        self._applied_col_color_idx = color_idx_arr
        self._applied_z = target_z

        self.last_terrain_data = terrain_earth_2d.copy()
        progress_msg = f"▶️ 正在播放：第{frame_num}/{self.total_frames}帧 | 时间戳：{time_stamp:.6f}s | 下发更新：{update_count}次 | 中心高度：{self.max_center_height:.2f}m"
        print(f"\r{progress_msg}", end="")
        if self.log_callback:
            self.log_callback(progress_msg)
        return update_count, self.max_center_height

    def run_simulation(self):
        p.setAdditionalSearchPath(pybullet_data.getDataPath(), physicsClientId=self.physics_client_id)
        p.setGravity(0, 0, 0, physicsClientId=self.physics_client_id)
        p.configureDebugVisualizer(p.COV_ENABLE_GUI, 0, physicsClientId=self.physics_client_id)
        p.configureDebugVisualizer(p.COV_ENABLE_SHADOWS, 0, physicsClientId=self.physics_client_id)

        ground_id = p.loadURDF("plane.urdf", [0, 0, 0.0], physicsClientId=self.physics_client_id)
        p.changeVisualShape(ground_id, -1, rgbaColor=[0.3, 0.3, 0.3, 1.0], physicsClientId=self.physics_client_id)

        p.resetDebugVisualizerCamera(
            cameraDistance=1,
            cameraYaw=35.0,
            cameraPitch=-35.0,
            cameraTargetPosition=[1, 0, 1.5],
            physicsClientId=self.physics_client_id
        )

        self._init_cubes()

        try:
            while True:
                for frame_num in range(1, self.total_frames + 1):
                    self.update_terrain_frame(frame_num)
                    time.sleep(self.SIM_SLEEP_TIME)
        finally:
            if p.isConnected(self.physics_client_id):
                p.disconnect(self.physics_client_id)


if __name__ == "__main__":
    physics_client_id = p.connect(p.GUI)
    terrain_map = TerrainMap(
        csv_file_path='gridmap_20261212_195732.csv',
        physics_client_id=physics_client_id,
        sim_sleep_time=0.2,
        relative_base=1.8,
        grid_rows=21,
        grid_cols=33,
        cube_half_x=0.05,
        cube_half_y=0.05,
        cube_half_z=0.03,
        grid_spacing=0.105
    )
    terrain_map.run_simulation()
