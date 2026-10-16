package service

import (
	"fmt"
	"net"
	"sync"
	"time"

	"golang.org/x/crypto/ssh"

	"roboscope-web/server/model"
	"roboscope-web/server/store"
)

// SSHPool manages reusable SSH connections.
type SSHPool struct {
	mu      sync.RWMutex
	clients map[string]*ssh.Client
}

var Pool = &SSHPool{clients: make(map[string]*ssh.Client)}

func sshConfig(host model.Host) *ssh.ClientConfig {
	return &ssh.ClientConfig{
		User: host.User,
		Auth: []ssh.AuthMethod{
			ssh.Password(host.Password),
		},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
		Timeout:         10 * time.Second,
	}
}

// GetClient returns a cached or new SSH client for the host.
func (p *SSHPool) GetClient(hostID string) (*ssh.Client, error) {
	p.mu.RLock()
	if c, ok := p.clients[hostID]; ok {
		// check if still alive
		_, _, err := c.SendRequest("keepalive", true, nil)
		if err == nil {
			p.mu.RUnlock()
			return c, nil
		}
		// dead connection, will reconnect
	}
	p.mu.RUnlock()

	host, ok := store.Hosts.GetByID(hostID)
	if !ok {
		return nil, fmt.Errorf("host %s not found", hostID)
	}

	addr := fmt.Sprintf("%s:%d", host.IP, host.Port)
	client, err := ssh.Dial("tcp", addr, sshConfig(host))
	if err != nil {
		return nil, fmt.Errorf("SSH connect failed: %w", err)
	}

	p.mu.Lock()
	p.clients[hostID] = client
	p.mu.Unlock()

	return client, nil
}

// RunCommand executes a command on the remote host and returns output.
func RunCommand(hostID string, cmd string) (string, error) {
	client, err := Pool.GetClient(hostID)
	if err != nil {
		return "", err
	}

	session, err := client.NewSession()
	if err != nil {
		return "", fmt.Errorf("session create failed: %w", err)
	}
	defer session.Close()

	out, err := session.CombinedOutput(cmd)
	if err != nil {
		return string(out), err
	}
	return string(out), nil
}

// TestConnection tests SSH connectivity to a host.
func TestConnection(host model.Host) error {
	addr := fmt.Sprintf("%s:%d", host.IP, host.Port)
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		return fmt.Errorf("TCP connect failed: %w", err)
	}
	conn.Close()

	client, err := ssh.Dial("tcp", addr, sshConfig(host))
	if err != nil {
		return fmt.Errorf("SSH auth failed: %w", err)
	}
	client.Close()
	return nil
}

// CloseAll closes all cached SSH connections.
func (p *SSHPool) CloseAll() {
	p.mu.Lock()
	defer p.mu.Unlock()
	for id, c := range p.clients {
		c.Close()
		delete(p.clients, id)
	}
}
