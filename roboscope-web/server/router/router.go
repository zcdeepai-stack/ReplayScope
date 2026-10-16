package router

import (
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	"github.com/gin-contrib/cors"
	"github.com/gin-gonic/gin"

	"roboscope-web/server/handler"
)

// Setup creates and configures the Gin engine.
func Setup() *gin.Engine {
	r := gin.Default()

	// CORS for development (React dev server on port 5173)
	r.Use(cors.New(cors.Config{
		AllowOrigins:     []string{"http://localhost:5173", "http://127.0.0.1:5173"},
		AllowMethods:     []string{"GET", "POST", "PUT", "DELETE", "OPTIONS"},
		AllowHeaders:     []string{"Origin", "Content-Type", "Authorization"},
		AllowCredentials: true,
	}))

	v1 := r.Group("/api/v1")
	{
		// 探活：上位机据此判断本机是否已有后端在跑，
		// 有就直接开浏览器，不再重复拉起进程。
		v1.GET("/health", func(c *gin.Context) {
			c.JSON(http.StatusOK, gin.H{
				"service": "roboscope",
				"pid":     os.Getpid(),
			})
		})

		// SSH host management
		ssh := v1.Group("/ssh")
		{
			ssh.GET("/hosts", handler.ListHosts)
			ssh.POST("/hosts", handler.AddHost)
			ssh.PUT("/hosts/:id", handler.UpdateHost)
			ssh.DELETE("/hosts/:id", handler.DeleteHost)
			ssh.POST("/hosts/:id/test", handler.TestHost)
		}

		// Log collection (remote)
		collect := v1.Group("/collect")
		{
			collect.GET("/browse", handler.BrowseRemote)
			collect.GET("/preview", handler.PreviewFile)
			collect.GET("/device-info", handler.GetDeviceInfo)
			collect.POST("/download", handler.DownloadFiles)
		}

		// 三维回放（本地 CSV，与 SSH 无关）
		replay := v1.Group("/replay")
		{
			replay.POST("/upload", handler.UploadReplayCSV)
			replay.GET("/info", handler.GetReplayInfo)
			replay.GET("/data", handler.GetReplayData)
			replay.POST("/clear", handler.ClearReplay)
		}

		// Log analysis (local upload)
		analysis := v1.Group("/analysis")
		{
			analysis.POST("/upload", handler.UploadLogFile)
			analysis.POST("/parse", handler.ParseLogs)
			analysis.POST("/filter", handler.FilterLogs)
			analysis.GET("/stats", handler.GetLogStats)
			analysis.POST("/clear", handler.ClearSession)
			analysis.POST("/export", handler.ExportLogs)
			analysis.DELETE("/file/:name", handler.DeleteFile)
			analysis.GET("/files", handler.GetSessionFiles)
		}
	}

	// --- 三维模型静态资源（URDF + 网格），直接复用桌面端那一份 ---
	//
	// 注意两种布局下模型位置不同：
	//   源码树:   x30PybusSimlu\X30\...
	//   安装之后: x30PybusSimlu\_internal\X30\...   （PyInstaller 把数据文件放进 _internal）
	// 少列一种就会导致路由不注册，请求落到下面的 SPA 兜底返回 index.html，
	// 前端把 HTML 当 URDF 解析后报 "Specification mandates value for attribute crossorigin"，
	// 极难定位。因此这里必须把两种布局都覆盖，且找不到时要明确报错而非静默放过。
	modelDir := findModelDir()
	if modelDir != "" {
		r.StaticFS("/models/X30", http.Dir(modelDir))
		log.Printf("3D model assets: %s", modelDir)
	} else {
		log.Printf("WARNING: X30 model dir not found, 3D replay will not work")
	}

	// --- Serve React frontend static files ---
	exe, _ := os.Executable()
	webDist := filepath.Join(filepath.Dir(exe), "..", "web", "dist")
	hasWeb := false
	if info, err := os.Stat(webDist); err == nil && info.IsDir() {
		hasWeb = true
		r.StaticFS("/assets", http.Dir(filepath.Join(webDist, "assets")))
	}

	// 只能注册一个 NoRoute：Gin 里后注册的会覆盖先注册的。
	indexFile := filepath.Join(webDist, "index.html")
	r.NoRoute(func(c *gin.Context) {
		path := c.Request.URL.Path

		// 模型与接口请求绝不能回落到 index.html。
		// 否则前端会把 HTML 当 XML 解析，报出与真实原因毫不相干的错误。
		if strings.HasPrefix(path, "/models/") || strings.HasPrefix(path, "/api/") {
			c.String(http.StatusNotFound,
				"not found: %s\n(3D model dir resolved to: %q)", path, modelDir)
			return
		}

		if !hasWeb {
			c.String(http.StatusNotFound, "web assets not found: %s", webDist)
			return
		}
		// dist 里有同名实体文件就直接返回，否则交给前端路由
		if fi, err := os.Stat(filepath.Join(webDist, path)); err == nil && !fi.IsDir() {
			c.File(filepath.Join(webDist, path))
			return
		}
		c.File(indexFile)
	})

	return r
}

// findModelDir 依次尝试各种布局，返回第一个存在的 X30 模型目录。
func findModelDir() string {
	exePath, err := os.Executable()
	if err != nil {
		return ""
	}
	base := filepath.Dir(exePath)
	candidates := []string{
		// 安装后：{app}\roboscope-web\server\roboscope.exe
		// PyInstaller 把数据文件放在 _internal 下
		filepath.Join(base, "..", "..", "x30PybusSimlu", "_internal", "X30"),
		// 源码树：roboscope-web\server\roboscope.exe
		filepath.Join(base, "..", "..", "x30PybusSimlu", "X30"),
		// exe 与模型同级摆放的情形
		filepath.Join(base, "x30PybusSimlu", "_internal", "X30"),
		filepath.Join(base, "x30PybusSimlu", "X30"),
	}
	for _, dir := range candidates {
		if info, err := os.Stat(filepath.Join(dir, "urdf", "X30.urdf")); err == nil && !info.IsDir() {
			abs, _ := filepath.Abs(dir)
			return abs
		}
	}
	return ""
}
