import React, { useState, useEffect, useCallback, useRef } from 'react';
import {
  Row, Col, Card, Input, Button, Table, Space, Modal, Form, InputNumber,
  message, Tag, Popconfirm, Progress, Collapse, Tooltip, Select,
} from 'antd';
import {
  PlusOutlined, DeleteOutlined, ApiOutlined, FolderOutlined, FileTextOutlined,
  EyeOutlined, EditOutlined, DownloadOutlined, ReloadOutlined, CopyOutlined,
  MinusCircleOutlined, FolderOpenOutlined,
} from '@ant-design/icons';
import {
  getHosts, addHost, updateHost, deleteHost, testHost,
  browseRemote, previewFile, downloadFiles,
} from '../api';

interface Host {
  id: string; name: string; ip: string; port: number;
  user: string; password: string; logPaths?: string[];
}
interface FileItem {
  name: string; size: number; isDir: boolean; modTime: string; path: string;
}
interface PreviewLine { num: number; content: string; }
interface OpLog { time: string; level: 'info' | 'success' | 'error' | 'warn'; msg: string; }

const DEFAULT_PATH = '/var/opt/robot/log/';
type HostStatus = 'online' | 'offline' | 'unknown' | 'testing';

const LogCollect: React.FC = () => {
  const [hosts, setHosts] = useState<Host[]>([]);
  const [hostStatus, setHostStatus] = useState<Record<string, HostStatus>>({});
  const [selectedHost, setSelectedHost] = useState<Host | null>(null);
  const [files, setFiles] = useState<FileItem[]>([]);
  const [currentPath, setCurrentPath] = useState(DEFAULT_PATH);
  const [modalOpen, setModalOpen] = useState(false);
  const [editingHost, setEditingHost] = useState<Host | null>(null);
  const [previewOpen, setPreviewOpen] = useState(false);
  const [previewLines, setPreviewLines] = useState<PreviewLine[]>([]);
  const [previewInfo, setPreviewInfo] = useState({ page: 0, totalPages: 0, totalLines: 0, fileName: '', filePath: '' });
  const [previewKeyword, setPreviewKeyword] = useState('');
  const [selectedFiles, setSelectedFiles] = useState<string[]>([]);
  const [downloading, setDownloading] = useState(false);
  const [downloadProgress, setDownloadProgress] = useState(0);
  const [opLogs, setOpLogs] = useState<OpLog[]>([]);
  const [form] = Form.useForm();
  const opLogRef = useRef<HTMLDivElement>(null);

  const addOpLog = useCallback((level: OpLog['level'], msg: string) => {
    const time = new Date().toLocaleTimeString('zh-CN', { hour12: false });
    // 上限保护：这个数组原本无界增长，长时间使用会持续吃内存
    setOpLogs((prev: OpLog[]) => {
      const next = [...prev, { time, level, msg }];
      return next.length > 500 ? next.slice(next.length - 500) : next;
    });
    setTimeout(() => opLogRef.current?.scrollTo(0, opLogRef.current.scrollHeight), 50);
  }, []);

  const loadHosts = async () => {
    try {
      const res = await getHosts();
      setHosts(res.data.data || []);
    } catch { /* ignore */ }
  };
  useEffect(() => { loadHosts(); }, []);

  // Get configured paths for selected host
  const getHostPaths = (h: Host | null): string[] => {
    if (!h) return [DEFAULT_PATH];
    const paths = h.logPaths && h.logPaths.length > 0 ? h.logPaths : [DEFAULT_PATH];
    return paths;
  };

  // Host CRUD
  const openAddModal = () => {
    setEditingHost(null);
    form.resetFields();
    form.setFieldsValue({ logPaths: [DEFAULT_PATH] });
    setModalOpen(true);
  };
  const openEditModal = (h: Host) => {
    setEditingHost(h);
    form.setFieldsValue({
      name: h.name, ip: h.ip, port: h.port, user: h.user, password: h.password,
      logPaths: h.logPaths && h.logPaths.length > 0 ? h.logPaths : [DEFAULT_PATH],
    });
    setModalOpen(true);
  };

  const handleSubmitHost = async (values: any) => {
    const paths = (values.logPaths || []).filter((p: string) => p && p.trim());
    const data = { ...values, port: values.port || 22, logPaths: paths.length > 0 ? paths : [DEFAULT_PATH] };
    delete data.logPath; // remove old field
    if (editingHost) {
      await updateHost(editingHost.id, data);
      message.success('修改成功');
      addOpLog('success', `修改主机 ${data.name} (${data.ip})`);
    } else {
      await addHost(data);
      message.success('添加成功');
      addOpLog('success', `添加主机 ${data.name} (${data.ip})`);
    }
    setModalOpen(false);
    form.resetFields();
    loadHosts();
  };

  const handleDeleteHost = async (h: Host) => {
    await deleteHost(h.id);
    message.success('已删除');
    addOpLog('warn', `删除主机 ${h.name}`);
    if (selectedHost?.id === h.id) { setSelectedHost(null); setFiles([]); }
    loadHosts();
  };

  const handleTestHost = async (h: Host) => {
    setHostStatus((prev: Record<string, HostStatus>) => ({ ...prev, [h.id]: 'testing' as HostStatus }));
    addOpLog('info', `测试连接 ${h.name} (${h.ip})...`);
    try {
      const res = await testHost(h.id);
      if (res.data.success) {
        setHostStatus((prev: Record<string, HostStatus>) => ({ ...prev, [h.id]: 'online' as HostStatus }));
        message.success('连接成功');
        addOpLog('success', `${h.name} 连接成功`);
      } else {
        setHostStatus((prev: Record<string, HostStatus>) => ({ ...prev, [h.id]: 'offline' as HostStatus }));
        message.error('连接失败: ' + res.data.error);
        addOpLog('error', `${h.name} 连接失败: ${res.data.error}`);
      }
    } catch {
      setHostStatus((prev: Record<string, HostStatus>) => ({ ...prev, [h.id]: 'offline' as HostStatus }));
      addOpLog('error', `${h.name} 连接异常`);
    }
  };

  // File browsing
  const handleBrowse = async (host: Host, path: string) => {
    setSelectedHost(host);
    setCurrentPath(path);
    setSelectedFiles([]);
    addOpLog('info', `浏览 ${host.name}:${path}`);
    try {
      const res = await browseRemote(host.id, path);
      setFiles(res.data.data || []);
      if (!hostStatus[host.id] || hostStatus[host.id] === 'unknown') {
        setHostStatus((prev: Record<string, HostStatus>) => ({ ...prev, [host.id]: 'online' as HostStatus }));
      }
    } catch (err: any) {
      setFiles([]);
      setHostStatus((prev: Record<string, HostStatus>) => ({ ...prev, [host.id]: 'offline' as HostStatus }));
      addOpLog('error', `浏览失败: ${err.message}`);
    }
  };

  const handleSelectHost = (h: Host) => {
    const paths = getHostPaths(h);
    handleBrowse(h, paths[0]);
  };

  // Path dropdown change
  const handlePathChange = (path: string) => {
    if (selectedHost) handleBrowse(selectedHost, path);
  };

  // Preview
  const handlePreview = async (fileName: string, filePath: string, page = 0, keyword = '') => {
    if (!selectedHost) return;
    const path = filePath || previewInfo.filePath;
    try {
      const res = await previewFile(selectedHost.id, path, page, keyword || undefined);
      const data = res.data.data;
      setPreviewLines(data.lines || []);
      setPreviewInfo({ page: data.page, totalPages: data.totalPages, totalLines: data.totalLines, fileName, filePath: path });
      setPreviewOpen(true);
    } catch (err: any) {
      message.error('预览失败: ' + err.message);
    }
  };

  // Download
  const handleDownload = async () => {
    if (!selectedHost || selectedFiles.length === 0) return;
    setDownloading(true);
    setDownloadProgress(10);
    addOpLog('info', `开始下载 ${selectedFiles.length} 个文件...`);
    try {
      setDownloadProgress(40);
      const res = await downloadFiles(selectedHost.id, selectedFiles);
      setDownloadProgress(80);
      const blob = new Blob([res.data]);
      const url = window.URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = selectedFiles.length === 1 ? selectedFiles[0].split('/').pop()! : 'logs.zip';
      a.click();
      window.URL.revokeObjectURL(url);
      setDownloadProgress(100);
      addOpLog('success', `下载完成: ${selectedFiles.length} 个文件`);
      message.success('下载完成');
    } catch (err: any) {
      addOpLog('error', `下载失败: ${err.message}`);
      message.error('下载失败');
    } finally {
      setTimeout(() => { setDownloading(false); setDownloadProgress(0); }, 1000);
    }
  };

  const getLogLevelColor = (text: string) => {
    const u = text.toUpperCase();
    if (u.includes('ERROR') || u.includes('FATAL')) return '#f85149';
    if (u.includes('WARN')) return '#d29922';
    if (u.includes('INFO')) return '#58a6ff';
    if (u.includes('DEBUG')) return '#8b949e';
    return '#c9d1d9';
  };

  const formatSize = (bytes: number) => {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    if (bytes < 1024 ** 3) return `${(bytes / 1024 ** 2).toFixed(1)} MB`;
    return `${(bytes / 1024 ** 3).toFixed(2)} GB`;
  };

  const statusDot = (id: string) => {
    const s = hostStatus[id] || 'unknown';
    return <span className={`status-dot ${s}`} />;
  };

  // Build path segments for navigation
  const buildPathSegments = () => {
    const parts = currentPath.split('/').filter(Boolean);
    return parts.map((part: string, i: number) => ({
      label: part,
      path: '/' + parts.slice(0, i + 1).join('/') + '/',
    }));
  };

  return (
    <div className="page-enter" style={{ display: 'flex', flexDirection: 'column', height: 'calc(100vh - 112px)', gap: 0 }}>
      <Row gutter={12} style={{ flex: 1, minHeight: 0 }}>
        {/* Left: Host Management */}
        <Col span={6} style={{ height: '100%' }}>
          <Card title="主机管理" size="small"
            style={{ background: '#161b22', borderColor: '#21262d', height: '100%', display: 'flex', flexDirection: 'column' }}
            bodyStyle={{ flex: 1, overflow: 'auto', padding: '8px 12px' }}
            extra={<Button type="primary" size="small" icon={<PlusOutlined />} onClick={openAddModal}>添加</Button>}>
            {hosts.length === 0 && <div style={{ color: '#484f58', textAlign: 'center', padding: 20 }}>暂无主机，请添加</div>}
            {hosts.map((h: Host) => (
              <div key={h.id}
                style={{
                  marginBottom: 8, padding: '10px 12px', borderRadius: 8, cursor: 'pointer',
                  background: selectedHost?.id === h.id ? '#21262d' : '#0d1117',
                  border: `1px solid ${selectedHost?.id === h.id ? '#58a6ff' : '#1a2332'}`,
                  transition: 'all 0.25s ease',
                }}
                onClick={() => handleSelectHost(h)}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <div style={{ minWidth: 0, flex: 1 }}>
                    <div style={{ color: '#c9d1d9', fontWeight: 600, fontSize: 13 }}>
                      {statusDot(h.id)}{h.name}
                    </div>
                    <div style={{ color: '#8b949e', fontSize: 11, marginTop: 2 }}>{h.ip}:{h.port}</div>
                    {h.logPaths && h.logPaths.length > 0 && (
                      <div style={{ color: '#484f58', fontSize: 10, marginTop: 2 }}>
                        {h.logPaths.length} 个日志路径
                      </div>
                    )}
                  </div>
                  <Space size={2}>
                    <Tooltip title="编辑"><Button size="small" type="text" icon={<EditOutlined style={{ color: '#8b949e' }} />}
                      onClick={(e: React.MouseEvent) => { e.stopPropagation(); openEditModal(h); }} /></Tooltip>
                    <Tooltip title="测试连接"><Button size="small" type="text" icon={<ApiOutlined style={{ color: '#58a6ff' }} />}
                      onClick={(e: React.MouseEvent) => { e.stopPropagation(); handleTestHost(h); }} /></Tooltip>
                    <Popconfirm title="确认删除？" onConfirm={() => handleDeleteHost(h)} okText="删除" cancelText="取消">
                      <Tooltip title="删除"><Button size="small" type="text" danger icon={<DeleteOutlined />}
                        onClick={(e: React.MouseEvent) => e.stopPropagation()} /></Tooltip>
                    </Popconfirm>
                  </Space>
                </div>
              </div>
            ))}
          </Card>
        </Col>

        {/* Right: File Browser */}
        <Col span={18} style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
          <Card title="远程文件浏览" size="small"
            style={{ background: '#161b22', borderColor: '#21262d', flex: 1, display: 'flex', flexDirection: 'column' }}
            bodyStyle={{ flex: 1, display: 'flex', flexDirection: 'column', overflow: 'hidden', padding: '8px 16px' }}
            extra={selectedHost && (
              <Button size="small" icon={<ReloadOutlined />} onClick={() => handleBrowse(selectedHost, currentPath)}>刷新</Button>
            )}>

            {/* Path selector: dropdown + navigation */}
            <div style={{ display: 'flex', gap: 8, alignItems: 'center', marginBottom: 8 }}>
              <FolderOpenOutlined style={{ color: '#58a6ff', fontSize: 16 }} />
              {selectedHost ? (
                <Select size="small" value={currentPath} onChange={handlePathChange}
                  style={{ minWidth: 220 }}
                  dropdownMatchSelectWidth={false}
                  options={getHostPaths(selectedHost).map((p: string) => ({ label: p, value: p }))}
                />
              ) : (
                <span style={{ color: '#484f58', fontSize: 13 }}>请先选择主机</span>
              )}
              {/* Path navigation segments */}
              {selectedHost && (
                <div style={{ display: 'flex', gap: 2, alignItems: 'center', flex: 1, overflow: 'auto' }}>
                  <a style={{ color: '#58a6ff', fontSize: 12, cursor: 'pointer', padding: '2px 4px' }}
                    onClick={() => selectedHost && handleBrowse(selectedHost, '/')}>根目录</a>
                  {buildPathSegments().map((seg: { label: string; path: string }, i: number) => (
                    <React.Fragment key={i}>
                      <span style={{ color: '#30363d', fontSize: 11 }}>/</span>
                      <a style={{ color: '#58a6ff', fontSize: 12, cursor: 'pointer', padding: '2px 4px', whiteSpace: 'nowrap' }}
                        onClick={() => selectedHost && handleBrowse(selectedHost, seg.path)}>{seg.label}</a>
                    </React.Fragment>
                  ))}
                </div>
              )}
            </div>

            {/* Toolbar */}
            {selectedFiles.length > 0 && (
              <div style={{ marginBottom: 8, display: 'flex', gap: 8, alignItems: 'center' }}>
                <Tag color="blue">已选 {selectedFiles.length} 个文件</Tag>
                <Button size="small" type="primary" icon={<DownloadOutlined />} loading={downloading}
                  onClick={handleDownload}>
                  {selectedFiles.length > 1 ? '压缩下载' : '下载'}
                </Button>
                <Button size="small" onClick={() => setSelectedFiles([])}>取消选择</Button>
              </div>
            )}
            {downloading && <Progress percent={downloadProgress} size="small" strokeColor="#58a6ff" style={{ marginBottom: 8 }} />}

            {/* File Table */}
            <div style={{ flex: 1, overflow: 'auto' }}>
              <Table dataSource={files} rowKey="path" size="small" pagination={false}
                scroll={{ y: 'calc(100vh - 380px)' }}
                rowSelection={{
                  selectedRowKeys: selectedFiles,
                  onChange: (keys: React.Key[]) => setSelectedFiles(keys as string[]),
                  getCheckboxProps: (record: FileItem) => ({ disabled: record.isDir }),
                }}
                onRow={(record: FileItem) => ({
                  onDoubleClick: () => record.isDir && selectedHost && handleBrowse(selectedHost, record.path + '/'),
                })}
                columns={[
                  { title: '', dataIndex: 'isDir', width: 32,
                    render: (v: boolean) => v ? <FolderOutlined style={{ color: '#58a6ff' }} /> : <FileTextOutlined style={{ color: '#8b949e' }} /> },
                  { title: '文件名', dataIndex: 'name',
                    render: (n: string, r: FileItem) => <span style={{ color: r.isDir ? '#58a6ff' : '#c9d1d9', cursor: r.isDir ? 'pointer' : 'default' }}>{n}</span> },
                  { title: '大小', dataIndex: 'size', width: 100,
                    render: (s: number, r: FileItem) => r.isDir ? '-' : formatSize(s) },
                  { title: '修改时间', dataIndex: 'modTime', width: 160 },
                  { title: '操作', width: 140,
                    render: (_: any, r: FileItem) => !r.isDir && (
                      <Space size={4}>
                        <Button size="small" type="link" icon={<EyeOutlined />}
                          onClick={() => handlePreview(r.name, r.path)}>预览</Button>
                        <Tooltip title="复制路径">
                          <Button size="small" type="text" icon={<CopyOutlined style={{ color: '#484f58' }} />}
                            onClick={() => { navigator.clipboard.writeText(r.path); message.success('已复制'); }} />
                        </Tooltip>
                      </Space>
                    )},
                ]}
              />
            </div>
          </Card>
        </Col>
      </Row>

      {/* Operation Log Panel */}
      <Collapse size="small" style={{ marginTop: 8 }}
        items={[{
          key: '1',
          label: <span style={{ color: '#8b949e', fontSize: 12 }}>操作日志 ({opLogs.length})</span>,
          children: (
            <div className="op-log-panel" ref={opLogRef}>
              {opLogs.length === 0 && <span style={{ color: '#484f58' }}>暂无操作记录</span>}
              {opLogs.map((log: OpLog, i: number) => (
                <div key={i}>
                  <span className="op-time">{log.time}</span>
                  <span className={`op-${log.level}`}>{log.msg}</span>
                </div>
              ))}
            </div>
          ),
        }]}
      />

      {/* Add/Edit Host Modal - narrower, vertical layout */}
      <Modal title={editingHost ? '编辑主机' : '添加主机'} open={modalOpen}
        onCancel={() => setModalOpen(false)} onOk={() => form.submit()}
        okText={editingHost ? '保存' : '添加'} cancelText="取消"
        width={420}>
        <Form form={form} onFinish={handleSubmitHost} layout="vertical" size="small">
          <Form.Item name="name" label="名称" rules={[{ required: true }]}><Input /></Form.Item>
          <Form.Item name="ip" label="IP 地址" rules={[{ required: true }]}><Input /></Form.Item>
          <Form.Item name="port" label="端口" initialValue={22}>
            <InputNumber min={1} max={65535} style={{ width: '100%' }} />
          </Form.Item>
          <Form.Item name="user" label="用户名" rules={[{ required: true }]}><Input /></Form.Item>
          <Form.Item name="password" label="密码" rules={[{ required: true }]}><Input.Password /></Form.Item>

          {/* Dynamic log paths list */}
          <Form.Item label="日志路径配置">
            <Form.List name="logPaths" initialValue={[DEFAULT_PATH]}>
              {(fields, { add, remove }) => (
                <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
                  {fields.map((field) => (
                    <div key={field.key} style={{ display: 'flex', gap: 6 }}>
                      <Form.Item {...field} noStyle rules={[{ required: true, message: '请输入路径' }]}>
                        <Input placeholder="/var/opt/robot/log/" style={{ flex: 1 }} />
                      </Form.Item>
                      {fields.length > 1 && (
                        <Button type="text" danger icon={<MinusCircleOutlined />}
                          onClick={() => remove(field.name)} />
                      )}
                    </div>
                  ))}
                  <Button type="dashed" onClick={() => add('')} icon={<PlusOutlined />} block
                    style={{ borderColor: '#30363d', color: '#8b949e' }}>
                    添加路径
                  </Button>
                </div>
              )}
            </Form.List>
          </Form.Item>
        </Form>
      </Modal>

      {/* Preview Modal */}
      <Modal title={`文件预览 - ${previewInfo.fileName}`} open={previewOpen}
        onCancel={() => setPreviewOpen(false)} width="85%" footer={null} style={{ top: 20 }}>
        <div style={{ marginBottom: 12, display: 'flex', gap: 8, alignItems: 'center', flexWrap: 'wrap' }}>
          <Input placeholder="搜索关键字" value={previewKeyword}
            onChange={(e: React.ChangeEvent<HTMLInputElement>) => setPreviewKeyword(e.target.value)}
            onPressEnter={() => handlePreview(previewInfo.fileName, previewInfo.filePath, 0, previewKeyword)}
            style={{ width: 280 }} allowClear />
          <Button type="primary" onClick={() => handlePreview(previewInfo.fileName, previewInfo.filePath, 0, previewKeyword)}>搜索</Button>
          <Tag color="blue">共 {previewInfo.totalLines} 行 | 第 {previewInfo.page + 1}/{previewInfo.totalPages} 页</Tag>
          <Space>
            <Button size="small" disabled={previewInfo.page <= 0}
              onClick={() => handlePreview(previewInfo.fileName, previewInfo.filePath, previewInfo.page - 1, previewKeyword)}>上一页</Button>
            <Button size="small" disabled={previewInfo.page >= previewInfo.totalPages - 1}
              onClick={() => handlePreview(previewInfo.fileName, previewInfo.filePath, previewInfo.page + 1, previewKeyword)}>下一页</Button>
          </Space>
        </div>
        <div style={{ maxHeight: '70vh', overflow: 'auto', background: '#0d1117', borderRadius: 6, padding: 12, fontFamily: 'Consolas, monospace', fontSize: 13 }}>
          {previewLines.map((l: PreviewLine, i: number) => (
            <div key={i} style={{ display: 'flex', borderBottom: '1px solid #161b22', padding: '2px 0' }}>
              <span style={{ width: 60, textAlign: 'right', color: '#484f58', marginRight: 12, flexShrink: 0, userSelect: 'none' }}>{l.num}</span>
              <span style={{ color: getLogLevelColor(l.content), whiteSpace: 'pre-wrap', wordBreak: 'break-all' }}>{l.content}</span>
            </div>
          ))}
        </div>
      </Modal>
    </div>
  );
};

export default LogCollect;
