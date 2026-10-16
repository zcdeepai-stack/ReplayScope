package service

import (
	"bufio"
	"os"
	"regexp"
	"sort"
	"strings"
	"time"

	"roboscope-web/server/model"
)

// Standard log format: [2026-04-15 10:30:45.123.456] [INFO  ] [ModuleName] Message text
var logRegex = regexp.MustCompile(`\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\.\d{3}\]\s*\[(\w+)\s*\]\s*\[([^\]]+)\]\s*(.*)`)

const timeLayout = "2006-01-02 15:04:05.000"

// ParseLogFile parses a log file into structured entries.
func ParseLogFile(filePath string) ([]model.LogEntry, int, error) {
	f, err := os.Open(filePath)
	if err != nil {
		return nil, 0, err
	}
	defer f.Close()

	var entries []model.LogEntry
	failCount := 0
	scanner := bufio.NewScanner(f)
	scanner.Buffer(make([]byte, 0, 1024*1024), 1024*1024)
	lineNum := 0

	for scanner.Scan() {
		lineNum++
		line := scanner.Text()
		matches := logRegex.FindStringSubmatch(line)
		if matches == nil {
			failCount++
			continue
		}
		ts, err := time.Parse(timeLayout, matches[1])
		if err != nil {
			failCount++
			continue
		}
		entries = append(entries, model.LogEntry{
			Timestamp: ts,
			Level:     strings.TrimSpace(matches[2]),
			Module:    strings.TrimSpace(matches[3]),
			Message:   matches[4],
			Line:      lineNum,
			File:      filePath,
		})
	}
	return entries, failCount, scanner.Err()
}

// MergeEntries merges multiple entry slices sorted by timestamp.
func MergeEntries(sources [][]model.LogEntry) []model.LogEntry {
	var all []model.LogEntry
	for _, src := range sources {
		all = append(all, src...)
	}
	sort.Slice(all, func(i, j int) bool {
		return all[i].Timestamp.Before(all[j].Timestamp)
	})
	return all
}

// FilterEntries applies filter criteria to entries.
func FilterEntries(entries []model.LogEntry, filter model.LogFilter) []model.LogEntry {
	var result []model.LogEntry

	var startTime, endTime time.Time
	if filter.StartTime != "" {
		startTime, _ = time.Parse(time.RFC3339, filter.StartTime)
		if startTime.IsZero() {
			startTime, _ = time.Parse(timeLayout, filter.StartTime)
		}
	}
	if filter.EndTime != "" {
		endTime, _ = time.Parse(time.RFC3339, filter.EndTime)
		if endTime.IsZero() {
			endTime, _ = time.Parse(timeLayout, filter.EndTime)
		}
	}

	levelSet := make(map[string]bool)
	for _, l := range filter.Levels {
		levelSet[strings.ToUpper(l)] = true
	}

	fileSet := make(map[string]bool)
	for _, f := range filter.Files {
		fileSet[f] = true
	}

	var keywordRegex *regexp.Regexp
	if filter.Keyword != "" {
		keywordRegex, _ = regexp.Compile("(?i)" + filter.Keyword)
	}

	for _, e := range entries {
		if len(levelSet) > 0 && !levelSet[strings.ToUpper(e.Level)] {
			continue
		}
		if filter.Module != "" && !strings.EqualFold(e.Module, filter.Module) {
			continue
		}
		if !startTime.IsZero() && e.Timestamp.Before(startTime) {
			continue
		}
		if !endTime.IsZero() && e.Timestamp.After(endTime) {
			continue
		}
		if keywordRegex != nil && !keywordRegex.MatchString(e.Message) {
			continue
		}
		if len(fileSet) > 0 && !fileSet[e.File] {
			continue
		}
		result = append(result, e)
	}
	return result
}

// ComputeStats computes aggregated stats from entries.
func ComputeStats(entries []model.LogEntry) model.LogStats {
	stats := model.LogStats{
		Total:    len(entries),
		ByLevel:  make(map[string]int),
		ByModule: make(map[string]int),
	}
	for _, e := range entries {
		stats.ByLevel[e.Level]++
		stats.ByModule[e.Module]++
	}
	if len(entries) > 0 {
		stats.TimeRange[0] = entries[0].Timestamp.Format(timeLayout)
		stats.TimeRange[1] = entries[len(entries)-1].Timestamp.Format(timeLayout)
	}
	return stats
}
