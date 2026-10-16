import axios from 'axios';

const api = axios.create({
  baseURL: '/api/v1',
  timeout: 30000,
});

// SSH Hosts
export const getHosts = () => api.get('/ssh/hosts');
export const addHost = (data: any) => api.post('/ssh/hosts', data);
export const updateHost = (id: string, data: any) => api.put(`/ssh/hosts/${id}`, data);
export const deleteHost = (id: string) => api.delete(`/ssh/hosts/${id}`);
export const testHost = (id: string) => api.post(`/ssh/hosts/${id}/test`);

// Log Collection
export const browseRemote = (hostId: string, path: string) =>
  api.get('/collect/browse', { params: { hostId, path } });
export const previewFile = (hostId: string, path: string, page: number, keyword?: string) =>
  api.get('/collect/preview', { params: { hostId, path, page, keyword } });
export const getDeviceInfo = (hostId: string) =>
  api.get('/collect/device-info', { params: { hostId } });
export const downloadFiles = (hostId: string, files: string[]) =>
  api.post('/collect/download', { hostId, files }, { responseType: 'blob' });

// 3D Replay（本地 CSV，与 SSH 无关）
export const uploadReplayCsv = (file: File) => {
  const form = new FormData();
  form.append('file', file);
  return api.post('/replay/upload', form);
};
export const getReplayInfo = () => api.get('/replay/info');
export const getReplayData = () =>
  api.get('/replay/data', { responseType: 'arraybuffer' });
export const clearReplay = () => api.post('/replay/clear');

// Log Analysis
export const uploadFiles = (files: File[]) => {
  const form = new FormData();
  files.forEach(f => form.append('files', f));
  return api.post('/analysis/upload', form);
};
export const parseLogs = () => api.post('/analysis/parse');
export const filterLogs = (filter: any) => api.post('/analysis/filter', filter);
export const getLogStats = () => api.get('/analysis/stats');
export const clearSession = () => api.post('/analysis/clear');
export const exportLogs = (filter: any) =>
  api.post('/analysis/export', filter, { responseType: 'blob' });
export const deleteFile = (name: string) => api.delete(`/analysis/file/${encodeURIComponent(name)}`);
export const getSessionFiles = () => api.get('/analysis/files');

export default api;
