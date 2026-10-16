package handler

import (
	"archive/zip"
	"fmt"
	"io"
	"net/http"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/gin-gonic/gin"

	"roboscope-web/server/service"
	"roboscope-web/server/store"
)

// BrowseRemote lists files in a remote directory.
func BrowseRemote(c *gin.Context) {
	hostID := c.Query("hostId")
	path := c.Query("path")
	if hostID == "" {
		c.JSON(http.StatusBadRequest, gin.H{"error": "hostId required"})
		return
	}
	entries, err := service.BrowseRemote(hostID, path)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"data": entries, "path": path})
}

// PreviewFile returns paginated file content.
func PreviewFile(c *gin.Context) {
	hostID := c.Query("hostId")
	path := c.Query("path")
	pageStr := c.DefaultQuery("page", "0")
	keyword := c.Query("keyword")

	if hostID == "" || path == "" {
		c.JSON(http.StatusBadRequest, gin.H{"error": "hostId and path required"})
		return
	}
	page, _ := strconv.Atoi(pageStr)

	if keyword != "" {
		result, err := service.SearchFile(hostID, path, keyword, page)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
		c.JSON(http.StatusOK, gin.H{"data": result})
		return
	}

	result, err := service.PreviewFile(hostID, path, page)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"data": result})
}

// GetDeviceInfo reads device info from remote host.
func GetDeviceInfo(c *gin.Context) {
	hostID := c.Query("hostId")
	if hostID == "" {
		c.JSON(http.StatusBadRequest, gin.H{"error": "hostId required"})
		return
	}
	info, err := service.GetDeviceInfo(hostID)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"data": info})
}

// DownloadFiles downloads one or more remote files. If multiple, streams a zip.
func DownloadFiles(c *gin.Context) {
	var req struct {
		HostID string   `json:"hostId"`
		Files  []string `json:"files"`
	}
	if err := c.ShouldBindJSON(&req); err != nil || req.HostID == "" || len(req.Files) == 0 {
		c.JSON(http.StatusBadRequest, gin.H{"error": "hostId and files required"})
		return
	}

	host, ok := store.Hosts.GetByID(req.HostID)
	if !ok {
		c.JSON(http.StatusNotFound, gin.H{"error": "host not found"})
		return
	}
	_ = host // used for context

	if len(req.Files) == 1 {
		// Single file download
		path := req.Files[0]
		name := filepath.Base(path)
		out, err := service.RunCommand(req.HostID, fmt.Sprintf("cat %s", path))
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
		c.Header("Content-Disposition", fmt.Sprintf("attachment; filename=%s", name))
		c.Data(http.StatusOK, "application/octet-stream", []byte(out))
		return
	}

	// Multiple files: stream as zip
	c.Header("Content-Disposition", "attachment; filename=logs.zip")
	c.Header("Content-Type", "application/zip")
	c.Status(http.StatusOK)

	zw := zip.NewWriter(c.Writer)
	defer zw.Close()

	for _, path := range req.Files {
		name := filepath.Base(path)
		out, err := service.RunCommand(req.HostID, fmt.Sprintf("cat %s", path))
		if err != nil {
			continue
		}
		w, err := zw.Create(name)
		if err != nil {
			continue
		}
		io.Copy(w, strings.NewReader(out))
	}
}
