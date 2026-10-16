package model

import "time"

// LogEntry represents a single parsed log line.
type LogEntry struct {
	Timestamp time.Time `json:"timestamp"`
	Level     string    `json:"level"`
	Module    string    `json:"module"`
	Message   string    `json:"message"`
	Line      int       `json:"line"`
	File      string    `json:"file"`
}

// LogFilter defines filter criteria for log analysis.
type LogFilter struct {
	Levels    []string `json:"levels"`
	Keyword   string   `json:"keyword"`
	Module    string   `json:"module"`
	StartTime string   `json:"startTime"`
	EndTime   string   `json:"endTime"`
	Files     []string `json:"files"`
}

// ReplayInfo describes a loaded 3D replay dataset.
type ReplayInfo struct {
	Frames         int     `json:"frames"`
	FrameInterval  float64 `json:"frameInterval"`  // seconds per frame (200Hz -> 0.005)
	Duration       float64 `json:"duration"`       // seconds
	HasTorque      bool    `json:"hasTorque"`      // 是否含膝关节力矩（用于小腿着色）
	FloatsPerFrame int     `json:"floatsPerFrame"` // 每帧浮点数个数
	FileName       string  `json:"fileName"`
}

// LogStats contains aggregated log statistics.
type LogStats struct {
	Total      int            `json:"total"`
	ByLevel    map[string]int `json:"byLevel"`
	ByModule   map[string]int `json:"byModule"`
	TimeRange  [2]string      `json:"timeRange"`
	ParseFailed int           `json:"parseFailed"`
}
