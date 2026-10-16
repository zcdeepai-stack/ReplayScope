# DR02 人形三维复现 CSV 格式

DR02 页面只使用 DR02 人形模型和下列 31 个可动关节，不使用 X30 或 M20 的关节编号。

推荐直接把关节名称作为 CSV 列名。列的前后顺序可以改变，程序会按名称对应：

| 身体部位 | 关节列名 |
| --- | --- |
| 躯干 | `waist_z_joint`、`waist_x_joint`、`waist_y_joint` |
| 左臂 | `left_shoulder_y_joint`、`left_shoulder_x_joint`、`left_shoulder_z_joint`、`left_elbow_joint`、`left_wrist_z_joint`、`left_wrist_y_joint`、`left_wrist_x_joint` |
| 右臂 | `right_shoulder_y_joint`、`right_shoulder_x_joint`、`right_shoulder_z_joint`、`right_elbow_joint`、`right_wrist_z_joint`、`right_wrist_y_joint`、`right_wrist_x_joint` |
| 头部 | `neck_z_joint`、`neck_y_joint` |
| 左腿 | `left_hip_y_joint`、`left_hip_x_joint`、`left_hip_z_joint`、`left_knee_joint`、`left_ankle_y_joint`、`left_ankle_x_joint` |
| 右腿 | `right_hip_y_joint`、`right_hip_x_joint`、`right_hip_z_joint`、`right_knee_joint`、`right_ankle_y_joint`、`right_ankle_x_joint` |

也兼容按顺序命名的 `Joint Pos 0` 至 `Joint Pos 30`。使用这种格式时，顺序必须与上表从上到下完全一致。

关节角单位为弧度。每行是一帧，当前按 0.005 秒一帧计算时间。

机身姿态是可选项：

- `IMU roll`
- `IMU pitch`
- `IMU yaw`

这三列按度读取。三列都没有时，程序仍会播放 31 个关节，机身保持直立；只提供其中一部分时按未提供姿态处理。

如果数据包含整体移动，还可以同时提供 `Base Pos X`、`Base Pos Y`、`Base Pos Z`。其中 Z 按相对默认站立高度处理，防止不同数据坐标原点让脚部落入地面。

程序读取后会检查 31 个关节列是否完整、每个值是否为有效数字。检查失败时页面显示具体行号和列名，不开始播放，也不改写原 CSV。
