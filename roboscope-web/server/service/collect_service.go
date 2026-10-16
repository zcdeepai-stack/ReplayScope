package service

import (
	"fmt"
	"strconv"
	"strings"

	"roboscope-web/server/config"
	"roboscope-web/server/model"
)

// BrowseRemote lists files in a remote directory.
func BrowseRemote(hostID, path string) ([]model.FileEntry, error) {
	if path == "" {
		path = config.Cfg.DefaultLogPath
	}
	// ls -la with machine-readable format
	cmd := fmt.Sprintf(`ls -la --time-style=long-iso '%s' 2>/dev/null`, path)
	out, err := RunCommand(hostID, cmd)
	if err != nil {
		return nil, fmt.Errorf("browse failed: %w", err)
	}

	var entries []model.FileEntry
	for _, line := range strings.Split(strings.TrimSpace(out), "\n") {
		if strings.HasPrefix(line, "total") || line == "" {
			continue
		}
		entry := parseLsLine(line, path)
		if entry != nil {
			entries = append(entries, *entry)
		}
	}
	return entries, nil
}

func parseLsLine(line, parentPath string) *model.FileEntry {
	fields := strings.Fields(line)
	if len(fields) < 8 {
		return nil
	}
	perms := fields[0]
	sizeStr := fields[4]
	date := fields[5]
	timeStr := fields[6]
	name := strings.Join(fields[7:], " ")

	if name == "." || name == ".." {
		return nil
	}

	size, _ := strconv.ParseInt(sizeStr, 10, 64)
	isDir := strings.HasPrefix(perms, "d")

	fullPath := parentPath
	if !strings.HasSuffix(fullPath, "/") {
		fullPath += "/"
	}
	fullPath += name

	return &model.FileEntry{
		Name:    name,
		Size:    size,
		IsDir:   isDir,
		ModTime: date + " " + timeStr,
		Path:    fullPath,
	}
}

// PreviewFile returns paginated file content from a remote host.
func PreviewFile(hostID, path string, page int) (*model.PreviewResult, error) {
	pageSize := config.Cfg.PageSize

	// get total line count
	cmd := fmt.Sprintf(`wc -l < '%s'`, path)
	out, err := RunCommand(hostID, cmd)
	if err != nil {
		return nil, fmt.Errorf("wc failed: %w", err)
	}
	totalLines, _ := strconv.Atoi(strings.TrimSpace(out))
	totalPages := (totalLines + pageSize - 1) / pageSize
	if totalPages == 0 {
		totalPages = 1
	}
	if page < 0 {
		page = 0
	}
	if page >= totalPages {
		page = totalPages - 1
	}

	start := page*pageSize + 1
	end := start + pageSize - 1

	cmd = fmt.Sprintf(`sed -n '%d,%dp' '%s'`, start, end, path)
	out, err = RunCommand(hostID, cmd)
	if err != nil {
		return nil, fmt.Errorf("sed failed: %w", err)
	}

	var lines []model.PreviewLine
	for i, l := range strings.Split(out, "\n") {
		if i == pageSize {
			break
		}
		lines = append(lines, model.PreviewLine{
			Num:     start + i,
			Content: l,
		})
	}
	// trim trailing empty lines
	for len(lines) > 0 && lines[len(lines)-1].Content == "" {
		lines = lines[:len(lines)-1]
	}

	return &model.PreviewResult{
		Lines:      lines,
		Page:       page,
		TotalPages: totalPages,
		TotalLines: totalLines,
	}, nil
}

// SearchFile searches for keyword in remote file, returns paginated matching lines.
func SearchFile(hostID, path, keyword string, page int) (*model.PreviewResult, error) {
	pageSize := config.Cfg.PageSize

	// count matches
	cmd := fmt.Sprintf(`grep -c -i '%s' '%s' 2>/dev/null || echo 0`, keyword, path)
	out, err := RunCommand(hostID, cmd)
	if err != nil && strings.TrimSpace(out) == "" {
		return nil, err
	}
	matchCount, _ := strconv.Atoi(strings.TrimSpace(out))
	totalPages := (matchCount + pageSize - 1) / pageSize
	if totalPages == 0 {
		totalPages = 1
	}
	if page < 0 {
		page = 0
	}
	if page >= totalPages {
		page = totalPages - 1
	}

	start := page*pageSize + 1
	end := start + pageSize - 1

	cmd = fmt.Sprintf(`grep -n -i '%s' '%s' | sed -n '%d,%dp'`, keyword, path, start, end)
	out, _ = RunCommand(hostID, cmd)

	var lines []model.PreviewLine
	for _, l := range strings.Split(strings.TrimSpace(out), "\n") {
		if l == "" {
			continue
		}
		parts := strings.SplitN(l, ":", 2)
		if len(parts) == 2 {
			num, _ := strconv.Atoi(parts[0])
			lines = append(lines, model.PreviewLine{
				Num:     num,
				Content: parts[1],
			})
		}
	}

	return &model.PreviewResult{
		Lines:      lines,
		Page:       page,
		TotalPages: totalPages,
		TotalLines: matchCount,
	}, nil
}

// GetDeviceInfo reads MFI.toml and release_note.json from the 103 host.
func GetDeviceInfo(hostID string) (*model.DeviceInfo, error) {
	info := &model.DeviceInfo{}

	// read SN from MFI.toml
	out, _ := RunCommand(hostID, `cat /opt/robot/mfi/MFI.toml 2>/dev/null | grep -i 'sn' | head -1`)
	if parts := strings.SplitN(out, "=", 2); len(parts) == 2 {
		info.SN = strings.Trim(strings.TrimSpace(parts[1]), `"'`)
	}

	// read VERSION and DEVICE_TYPE from release_note.json
	out, _ = RunCommand(hostID, `cat /opt/robot/release_note.json 2>/dev/null`)
	for _, line := range strings.Split(out, "\n") {
		line = strings.TrimSpace(line)
		if strings.Contains(line, "VERSION") {
			info.Version = extractJSONValue(line)
		}
		if strings.Contains(line, "DEVICE_TYPE") {
			info.DeviceType = extractJSONValue(line)
		}
	}

	return info, nil
}

func extractJSONValue(line string) string {
	parts := strings.SplitN(line, ":", 2)
	if len(parts) < 2 {
		return ""
	}
	val := strings.TrimSpace(parts[1])
	val = strings.Trim(val, `",`)
	return val
}
