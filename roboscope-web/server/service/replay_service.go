package service

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"math"
	"os"
	"strconv"
	"strings"

	"roboscope-web/server/model"
)

// 三维回放所需的列，与桌面端 x30PybusSimlu/pybullet_server.py 完全一致：
//   关节角  Joint Pos 0..11        (rad)
//   膝关节力矩 Joint Tor 2/5/8/11  (Nm，用于小腿着色)
//   机身姿态 IMU roll/pitch/yaw    (deg，读入后转弧度)
const (
	ReplayJointCount  = 12
	ReplayTorqueCount = 4
	// 每帧浮点数：12 关节 + 3 姿态 + 4 力矩
	ReplayFloatsPerFrame = ReplayJointCount + 3 + ReplayTorqueCount
)

var replayTorqueCols = [ReplayTorqueCount]int{2, 5, 8, 11}

// 预算常数，与 numpy.radians 的舍入方式一致
const degToRad = math.Pi / 180.0

// ParseReplayCSV 解析三维回放 CSV，返回逐帧的紧凑数组（float64，小端）。
//
// 必须用 float64：实测数据里存在 6.12e+257 这类异常采样值，它在 float64 范围内
// （桌面端 Python 就是 float64，所以能正常显示），但转成 float32 会变成 Infinity，
// 前端据此算出的四元数是 NaN，会导致整条腿在 WebGL 里直接消失。
func ParseReplayCSV(path string) (*model.ReplayInfo, []byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, nil, err
	}
	defer f.Close()

	rd := bufio.NewReaderSize(f, 1<<20)

	header, err := rd.ReadString('\n')
	if err != nil && header == "" {
		return nil, nil, fmt.Errorf("文件为空")
	}
	cols := splitTrim(header)

	idx := make(map[string]int, len(cols))
	for i, c := range cols {
		idx[c] = i
	}

	jointIdx := make([]int, ReplayJointCount)
	for i := 0; i < ReplayJointCount; i++ {
		name := fmt.Sprintf("Joint Pos %d", i)
		p, ok := idx[name]
		if !ok {
			return nil, nil, fmt.Errorf("缺少列 %q，这不是三维回放数据文件", name)
		}
		jointIdx[i] = p
	}

	var torIdx [ReplayTorqueCount]int
	hasTorque := true
	for i, c := range replayTorqueCols {
		p, ok := idx[fmt.Sprintf("Joint Tor %d", c)]
		if !ok {
			hasTorque = false
			break
		}
		torIdx[i] = p
	}

	imuNames := [3]string{"IMU roll", "IMU pitch", "IMU yaw"}
	var imuIdx [3]int
	for i, n := range imuNames {
		p, ok := idx[n]
		if !ok {
			return nil, nil, fmt.Errorf("缺少列 %q，这不是三维回放数据文件", n)
		}
		imuIdx[i] = p
	}

	maxIdx := 0
	for _, p := range jointIdx {
		maxIdx = maxInt(maxIdx, p)
	}
	for _, p := range imuIdx {
		maxIdx = maxInt(maxIdx, p)
	}
	if hasTorque {
		for _, p := range torIdx {
			maxIdx = maxInt(maxIdx, p)
		}
	}

	// 按文件大小与首行长度预估容量，一次分配到位。
	// 否则 append 增长到 8MB 要经历多次翻倍拷贝，峰值内存约为结果的 1.5 倍。
	bufCap := 1 << 20
	if st, err := f.Stat(); err == nil && len(header) > 0 {
		estRows := st.Size()/int64(len(header)) + 64
		if est := estRows * int64(ReplayFloatsPerFrame) * 8; est > int64(bufCap) {
			if est > 512<<20 { // 上限保护，防止行长估算失真时吃爆内存
				est = 512 << 20
			}
			bufCap = int(est)
		}
	}
	buf := make([]byte, 0, bufCap)
	scratch := make([]byte, 8)
	frames := 0

	for {
		line, rerr := rd.ReadString('\n')
		if line != "" {
			fields := splitTrim(line)
			if len(fields) > maxIdx {
				put := func(v float64) {
					binary.LittleEndian.PutUint64(scratch, math.Float64bits(v))
					buf = append(buf, scratch...)
				}
				for _, p := range jointIdx {
					put(parseFloat(fields[p]))
				}
				// 角度转弧度。必须写成「乘以预算常数」，与 numpy 的 radians 同一种
				// 舍入顺序；写成 x*Pi/180 会有末位 1 ULP 的差异。
				for _, p := range imuIdx {
					put(parseFloat(fields[p]) * degToRad)
				}
				for i := 0; i < ReplayTorqueCount; i++ {
					if hasTorque {
						put(parseFloat(fields[torIdx[i]]))
					} else {
						put(0)
					}
				}
				frames++
			}
		}
		if rerr != nil {
			break
		}
	}

	if frames == 0 {
		return nil, nil, fmt.Errorf("没有解析到任何数据行")
	}

	info := &model.ReplayInfo{
		Frames:        frames,
		FrameInterval: 0.005, // 桌面端同样按 200Hz 处理
		Duration:      float64(frames) * 0.005,
		HasTorque:     hasTorque,
		FloatsPerFrame: ReplayFloatsPerFrame,
	}
	return info, buf, nil
}

func splitTrim(line string) []string {
	line = strings.TrimRight(line, "\r\n")
	parts := strings.Split(line, ",")
	for i := range parts {
		parts[i] = strings.TrimSpace(parts[i])
	}
	return parts
}

func parseFloat(s string) float64 {
	v, err := strconv.ParseFloat(s, 64)
	if err != nil {
		return 0
	}
	return v
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}
