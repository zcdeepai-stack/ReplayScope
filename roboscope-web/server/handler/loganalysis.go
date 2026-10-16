package handler

import (
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"runtime/debug"
	"strings"
	"sync"

	"github.com/gin-gonic/gin"
	"github.com/google/uuid"

	"roboscope-web/server/config"
	"roboscope-web/server/model"
	"roboscope-web/server/service"
)

// In-memory store for current analysis session.
var (
	sessionMu      sync.RWMutex
	sessionFiles   []string          // uploaded file paths
	sessionNames   []string          // original file names
	sessionEntries []model.LogEntry  // merged parsed entries
	sessionFailed  int
)

// UploadLogFile handles log file upload.
func UploadLogFile(c *gin.Context) {
	form, err := c.MultipartForm()
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	files := form.File["files"]
	var saved []string

	sessionMu.Lock()
	for _, f := range files {
		// Dedup: if a file with the same name already exists, remove the old one
		for i, existing := range sessionNames {
			if existing == f.Filename && i < len(sessionFiles) {
				os.Remove(sessionFiles[i])
				sessionFiles = append(sessionFiles[:i], sessionFiles[i+1:]...)
				sessionNames = append(sessionNames[:i], sessionNames[i+1:]...)
				break
			}
		}

		id := uuid.New().String()
		ext := filepath.Ext(f.Filename)
		dest := filepath.Join(config.Cfg.UploadDir, id+ext)
		if err := c.SaveUploadedFile(f, dest); err != nil {
			sessionMu.Unlock()
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
		sessionFiles = append(sessionFiles, dest)
		sessionNames = append(sessionNames, f.Filename)
		saved = append(saved, dest)
	}
	sessionMu.Unlock()

	// return file info list
	type fileInfo struct {
		Name string `json:"name"`
		Path string `json:"path"`
		Size int64  `json:"size"`
	}
	var infos []fileInfo
	for i, s := range saved {
		fi, _ := os.Stat(s)
		name := files[i].Filename
		infos = append(infos, fileInfo{Name: name, Path: s, Size: fi.Size()})
	}

	c.JSON(http.StatusOK, gin.H{"data": infos})
}

// ParseLogs parses all uploaded files and merges them.
func ParseLogs(c *gin.Context) {
	sessionMu.Lock()
	defer sessionMu.Unlock()

	var allSources [][]model.LogEntry
	totalFailed := 0
	for i, path := range sessionFiles {
		entries, failed, err := service.ParseLogFile(path)
		if err != nil {
			continue
		}
		// Replace internal path with original file name for display
		origName := "unknown"
		if i < len(sessionNames) {
			origName = sessionNames[i]
		}
		for j := range entries {
			entries[j].File = origName
		}
		totalFailed += failed
		allSources = append(allSources, entries)
	}

	sessionEntries = service.MergeEntries(allSources)
	sessionFailed = totalFailed

	c.JSON(http.StatusOK, gin.H{
		"total":       len(sessionEntries),
		"files":       len(sessionFiles),
		"parseFailed": sessionFailed,
	})
}

// FilterLogs applies filter to parsed entries.
func FilterLogs(c *gin.Context) {
	var filter model.LogFilter
	if err := c.ShouldBindJSON(&filter); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	sessionMu.RLock()
	entries := sessionEntries
	sessionMu.RUnlock()

	filtered := service.FilterEntries(entries, filter)

	c.JSON(http.StatusOK, gin.H{
		"data":  filtered,
		"total": len(filtered),
	})
}

// GetLogStats returns statistics of current parsed entries.
func GetLogStats(c *gin.Context) {
	sessionMu.RLock()
	entries := sessionEntries
	failed := sessionFailed
	sessionMu.RUnlock()

	stats := service.ComputeStats(entries)
	stats.ParseFailed = failed
	c.JSON(http.StatusOK, gin.H{"data": stats})
}

// ClearSession clears uploaded files and parsed data.
func ClearSession(c *gin.Context) {
	sessionMu.Lock()
	for _, f := range sessionFiles {
		os.Remove(f)
	}
	sessionFiles = nil
	sessionNames = nil
	sessionEntries = nil
	sessionFailed = 0
	sessionMu.Unlock()

	// 仅置 nil 不会把堆还给操作系统：实测 13.6 万条日志清空后常驻仍是 198MB。
	// 主动归还，避免被误判为泄漏。
	debug.FreeOSMemory()
	c.JSON(http.StatusOK, gin.H{"message": "cleared"})
}

// DeleteFile removes a single uploaded file by name.
func DeleteFile(c *gin.Context) {
	name := c.Param("name")
	if name == "" {
		c.JSON(http.StatusBadRequest, gin.H{"error": "file name required"})
		return
	}

	sessionMu.Lock()
	defer sessionMu.Unlock()

	for i, n := range sessionNames {
		if n == name && i < len(sessionFiles) {
			os.Remove(sessionFiles[i])
			sessionFiles = append(sessionFiles[:i], sessionFiles[i+1:]...)
			sessionNames = append(sessionNames[:i], sessionNames[i+1:]...)
			c.JSON(http.StatusOK, gin.H{"message": "deleted"})
			return
		}
	}
	c.JSON(http.StatusNotFound, gin.H{"error": "file not found"})
}

// ExportLogs exports filtered log entries as a merged .log file.
func ExportLogs(c *gin.Context) {
	var filter model.LogFilter
	if err := c.ShouldBindJSON(&filter); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	sessionMu.RLock()
	entries := sessionEntries
	sessionMu.RUnlock()

	filtered := service.FilterEntries(entries, filter)

	// Build merged log content: original log format
	var sb strings.Builder
	for _, e := range filtered {
		ts := e.Timestamp.Format("2006-01-02 15:04:05.000")
		sb.WriteString(fmt.Sprintf("[%s] [%-5s] [%s] %s\n",
			ts, e.Level, e.Module, e.Message))
	}

	c.Header("Content-Disposition", "attachment; filename=merged_logs.log")
	c.Data(http.StatusOK, "text/plain; charset=utf-8", []byte(sb.String()))
}

// GetSessionFiles returns the list of currently uploaded files.
func GetSessionFiles(c *gin.Context) {
	sessionMu.RLock()
	defer sessionMu.RUnlock()

	type fileInfo struct {
		Name string `json:"name"`
		Path string `json:"path"`
		Size int64  `json:"size"`
	}
	var infos []fileInfo
	for i, s := range sessionFiles {
		name := "unknown"
		if i < len(sessionNames) {
			name = sessionNames[i]
		}
		fi, err := os.Stat(s)
		var size int64
		if err == nil {
			size = fi.Size()
		}
		infos = append(infos, fileInfo{Name: name, Path: s, Size: size})
	}
	c.JSON(http.StatusOK, gin.H{"data": infos})
}
