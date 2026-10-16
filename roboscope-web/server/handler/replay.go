package handler

import (
	"net/http"
	"os"
	"path/filepath"
	"runtime/debug"
	"sync"

	"github.com/gin-gonic/gin"
	"github.com/google/uuid"

	"roboscope-web/server/config"
	"roboscope-web/server/model"
	"roboscope-web/server/service"
)

// 当前会话装载的三维回放数据。与日志分析一致，服务本机单会话使用。
var (
	replayMu   sync.RWMutex
	replayInfo *model.ReplayInfo
	replayData []byte
)

// UploadReplayCSV 接收关节数据 CSV，解析成逐帧紧凑数组暂存在内存。
func UploadReplayCSV(c *gin.Context) {
	fh, err := c.FormFile("file")
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "缺少上传文件"})
		return
	}

	dest := filepath.Join(config.Cfg.UploadDir, uuid.New().String()+".csv")
	if err := c.SaveUploadedFile(fh, dest); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	// 解析完就不再需要原始文件
	defer os.Remove(dest)

	info, data, err := service.ParseReplayCSV(dest)
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	info.FileName = fh.Filename

	replayMu.Lock()
	replayInfo, replayData = info, data
	replayMu.Unlock()

	c.JSON(http.StatusOK, gin.H{"data": info})
}

// GetReplayInfo 返回当前已装载数据的信息。
func GetReplayInfo(c *gin.Context) {
	replayMu.RLock()
	defer replayMu.RUnlock()
	if replayInfo == nil {
		c.JSON(http.StatusOK, gin.H{"data": nil})
		return
	}
	c.JSON(http.StatusOK, gin.H{"data": replayInfo})
}

// GetReplayData 以二进制返回全部帧数据（float32 小端），供前端一次性取走。
func GetReplayData(c *gin.Context) {
	replayMu.RLock()
	data := replayData
	replayMu.RUnlock()

	if len(data) == 0 {
		c.JSON(http.StatusNotFound, gin.H{"error": "尚未加载回放数据"})
		return
	}
	c.Header("Cache-Control", "no-store")
	c.Data(http.StatusOK, "application/octet-stream", data)
}

// ClearReplay 释放已装载的数据。前端离开三维复现页时会调用。
func ClearReplay(c *gin.Context) {
	replayMu.Lock()
	replayInfo, replayData = nil, nil
	replayMu.Unlock()
	// Go 的 GC 默认不急于把空闲堆还给操作系统，这里主动归还，
	// 否则任务管理器里会一直挂着上百 MB，容易被当成泄漏。
	debug.FreeOSMemory()
	c.JSON(http.StatusOK, gin.H{"message": "cleared"})
}
