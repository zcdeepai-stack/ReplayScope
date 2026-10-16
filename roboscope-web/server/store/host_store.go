package store

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"

	"roboscope-web/server/config"
	"roboscope-web/server/model"
)

// HostStore manages host configuration persistence.
type HostStore struct {
	mu    sync.RWMutex
	hosts []model.Host
	path  string
}

var Hosts *HostStore

func InitHostStore() {
	Hosts = &HostStore{
		path: filepath.Join(config.Cfg.DataDir, "hosts.json"),
	}
	Hosts.load()
}

func (s *HostStore) load() {
	data, err := os.ReadFile(s.path)
	if err != nil {
		s.hosts = []model.Host{}
		return
	}
	if json.Unmarshal(data, &s.hosts) != nil {
		s.hosts = []model.Host{}
		return
	}
	// 兼容并清理旧版本曾明文保存的密码。密码只在当前进程内使用。
	hadStoredPassword := false
	for i := range s.hosts {
		hadStoredPassword = hadStoredPassword || s.hosts[i].Password != ""
		s.hosts[i].Password = ""
	}
	if hadStoredPassword {
		_ = s.save()
	}
}

func (s *HostStore) save() error {
	persistent := make([]model.Host, len(s.hosts))
	copy(persistent, s.hosts)
	for i := range persistent {
		persistent[i].Password = ""
	}
	data, err := json.MarshalIndent(persistent, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(s.path, data, 0600)
}

func (s *HostStore) GetAll() []model.Host {
	s.mu.RLock()
	defer s.mu.RUnlock()
	result := make([]model.Host, len(s.hosts))
	copy(result, s.hosts)
	for i := range result {
		result[i].Password = ""
	}
	return result
}

func (s *HostStore) GetByID(id string) (model.Host, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	for _, h := range s.hosts {
		if h.ID == id {
			return h, true
		}
	}
	return model.Host{}, false
}

func (s *HostStore) Add(h model.Host) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.hosts = append(s.hosts, h)
	return s.save()
}

func (s *HostStore) Update(h model.Host) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	for i, old := range s.hosts {
		if old.ID == h.ID {
			s.hosts[i] = h
			return s.save()
		}
	}
	return nil
}

func (s *HostStore) Delete(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	for i, h := range s.hosts {
		if h.ID == id {
			s.hosts = append(s.hosts[:i], s.hosts[i+1:]...)
			return s.save()
		}
	}
	return nil
}
