package config

import (
	"log"
	"os"
	"path/filepath"
)

// AppConfig holds all application configuration.
type AppConfig struct {
	Port           string
	DataDir        string
	UploadDir      string
	DefaultSSHPort int
	DefaultLogPath string
	PageSize       int // lines per page for file preview
}

var Cfg AppConfig

func Init() {
	exe, _ := os.Executable()
	baseDir := filepath.Dir(exe)

	Cfg = AppConfig{
		Port:           ":9090",
		DataDir:        filepath.Join(baseDir, "data"),
		UploadDir:      filepath.Join(baseDir, "data", "uploads"),
		DefaultSSHPort: 22,
		DefaultLogPath: "/var/opt/robot/log/",
		PageSize:       1000,
	}

	os.MkdirAll(Cfg.DataDir, 0755)
	os.MkdirAll(Cfg.UploadDir, 0755)

	purgeUploads()
}

// purgeUploads 清空上传暂存目录。
//
// 上传的文件只在「当前会话」有意义，而会话状态(sessionFiles 等)只存在于内存里：
// 进程一重启，磁盘上这些文件就再也没有任何代码会去删它们。实测运行几个月后
// 该目录残留了 31 个文件共 101MB，属于无界磁盘泄漏，因此启动时统一清理。
func purgeUploads() {
	entries, err := os.ReadDir(Cfg.UploadDir)
	if err != nil {
		return
	}
	var removed int
	var freed int64
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		if info, err := e.Info(); err == nil {
			freed += info.Size()
		}
		if os.Remove(filepath.Join(Cfg.UploadDir, e.Name())) == nil {
			removed++
		}
	}
	if removed > 0 {
		log.Printf("清理上传暂存目录: 删除 %d 个残留文件, 释放 %.1f MB",
			removed, float64(freed)/(1<<20))
	}
}
