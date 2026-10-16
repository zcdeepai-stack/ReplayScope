package model

// Host represents an SSH host configuration.
type Host struct {
	ID       string `json:"id"`
	Name     string `json:"name"`
	IP       string `json:"ip"`
	Port     int    `json:"port"`
	User     string `json:"user"`
	Password string `json:"password"`
	LogPaths []string `json:"logPaths,omitempty"`
}

// FileEntry represents a remote file/directory.
type FileEntry struct {
	Name    string `json:"name"`
	Size    int64  `json:"size"`
	IsDir   bool   `json:"isDir"`
	ModTime string `json:"modTime"`
	Path    string `json:"path"`
}

// PreviewResult represents paginated file preview.
type PreviewResult struct {
	Lines      []PreviewLine `json:"lines"`
	Page       int           `json:"page"`
	TotalPages int           `json:"totalPages"`
	TotalLines int           `json:"totalLines"`
}

// PreviewLine represents a single line with line number.
type PreviewLine struct {
	Num     int    `json:"num"`
	Content string `json:"content"`
}

// DeviceInfo contains MFI and version info from remote host.
type DeviceInfo struct {
	SN         string `json:"sn"`
	Version    string `json:"version"`
	DeviceType string `json:"deviceType"`
}
