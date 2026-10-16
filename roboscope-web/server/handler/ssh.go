package handler

import (
	"net/http"

	"github.com/gin-gonic/gin"
	"github.com/google/uuid"

	"roboscope-web/server/model"
	"roboscope-web/server/service"
	"roboscope-web/server/store"
)

// ListHosts returns all saved hosts.
func ListHosts(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{"data": store.Hosts.GetAll()})
}

// AddHost creates a new host.
func AddHost(c *gin.Context) {
	var h model.Host
	if err := c.ShouldBindJSON(&h); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	h.ID = uuid.New().String()
	if h.Port == 0 {
		h.Port = 22
	}
	if err := store.Hosts.Add(h); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"data": h})
}

// UpdateHost updates an existing host.
func UpdateHost(c *gin.Context) {
	id := c.Param("id")
	var h model.Host
	if err := c.ShouldBindJSON(&h); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}
	h.ID = id
	if err := store.Hosts.Update(h); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"data": h})
}

// DeleteHost removes a host.
func DeleteHost(c *gin.Context) {
	id := c.Param("id")
	if err := store.Hosts.Delete(id); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"message": "deleted"})
}

// TestHost tests SSH connection to a host.
func TestHost(c *gin.Context) {
	id := c.Param("id")
	host, ok := store.Hosts.GetByID(id)
	if !ok {
		c.JSON(http.StatusNotFound, gin.H{"error": "host not found"})
		return
	}
	if err := service.TestConnection(host); err != nil {
		c.JSON(http.StatusOK, gin.H{"success": false, "error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"success": true})
}
