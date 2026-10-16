package main

import (
	"fmt"
	"log"
	"os/exec"
	"runtime"

	"roboscope-web/server/config"
	"roboscope-web/server/router"
	"roboscope-web/server/service"
	"roboscope-web/server/store"
)

func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "windows":
		cmd = exec.Command("cmd", "/c", "start", url)
	case "darwin":
		cmd = exec.Command("open", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	cmd.Start()
}

func main() {
	// Initialize
	config.Init()
	store.InitHostStore()

	// Setup router
	r := router.Setup()

	addr := config.Cfg.Port
	url := fmt.Sprintf("http://localhost%s", addr)

	fmt.Println("=========================================")
	fmt.Println("  RoboScope - Log Management Platform")
	fmt.Println("=========================================")
	fmt.Printf("  Server: %s\n", url)
	fmt.Println("  Press Ctrl+C to stop")
	fmt.Println("=========================================")

	// Auto open browser
	openBrowser(url)

	// Graceful shutdown hook for SSH pool
	defer service.Pool.CloseAll()

	if err := r.Run(addr); err != nil {
		log.Fatalf("Server failed: %v", err)
	}
}
