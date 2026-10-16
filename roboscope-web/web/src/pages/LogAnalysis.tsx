import React, { useState, useCallback, useRef, useEffect } from 'react';
import {
  Row, Col, Card, Button, Upload, Table, Space, Select, Input, DatePicker,
  Tag, message, Statistic, Modal, Popconfirm, Tooltip, Progress, Collapse,
} from 'antd';
import {
  UploadOutlined, SearchOutlined, DeleteOutlined, ClearOutlined,
  BarChartOutlined, ExportOutlined, FileTextOutlined, CloseCircleOutlined,
  MergeCellsOutlined, ReloadOutlined, PlusOutlined,
  VerticalAlignTopOutlined, VerticalAlignBottomOutlined,
} from '@ant-design/icons';
import {
  uploadFiles, parseLogs, filterLogs, getLogStats, clearSession,
  exportLogs, deleteFile, getSessionFiles,
} from '../api';

const { RangePicker } = DatePicker;

interface FileInfo {
  name: string;
  size: number;
  lines?: number;
}
interface LogEntry {
  timestamp: string; level: string; module: string; message: string;
  line: number; file: string;
}
interface Stats {
  total: number; byLevel: Record<string, number>; byModule: Record<string, number>;
  timeRange: [string, string]; parseFailed: number;
}
interface OpLog { time: string; level: 'info' | 'success' | 'error' | 'warn'; msg: string; }

const levelColors: Record<string, string> = {
  FATAL: '#ff4d4f', ERROR: '#f85149', WARN: '#d29922', WARNING: '#d29922',
  INFO: '#58a6ff', DEBUG: '#8b949e', TRACE: '#6e7681',
};

const LogAnalysis: React.FC = () => {
  const [sessionFiles, setSessionFiles] = useState<FileInfo[]>([]);
  const [entries, setEntries] = useState<LogEntry[]>([]);
  const [stats, setStats] = useState<Stats | null>(null);
  const [loading, setLoading] = useState(false);
  const [uploading, setUploading] = useState(false);

  // Filter state
  const [levels, setLevels] = useState<string[]>([]);
  const [availableLevels, setAvailableLevels] = useState<string[]>(['ERROR', 'WARN', 'INFO', 'DEBUG']);
  const [availableModules, setAvailableModules] = useState<string[]>([]);
  const [keyword, setKeyword] = useState('');
  const [module, setModule] = useState('');
  const [timeRange, setTimeRange] = useState<[any, any] | null>(null);
  const [fileFilter, setFileFilter] = useState<string[]>([]);

  // Export & Progress
  const [exporting, setExporting] = useState(false);
  const [parseProgress, setParseProgress] = useState(0);
  const [opLogs, setOpLogs] = useState<OpLog[]>([]);
  const opLogRef = useRef<HTMLDivElement>(null);
  const tableWrapRef = useRef<HTMLDivElement>(null);
  const tableRef = useRef<any>(null);
  const [tableHeight, setTableHeight] = useState(400);

  // Dynamically compute virtual table height based on container size
  useEffect(() => {
    const calcHeight = () => {
      if (!tableWrapRef.current) return;
      const rect = tableWrapRef.current.getBoundingClientRect();
      const h = Math.max(200, window.innerHeight - rect.top - 90);
      setTableHeight(h);
    };
    calcHeight();
    window.addEventListener('resize', calcHeight);
    return () => window.removeEventListener('resize', calcHeight);
  }, [entries.length, stats]);

  // 离开页面时通知后端释放这次会话的解析结果与临时文件。
  // 实测 9.7MB 日志解析后服务端常驻 198MB，不释放会一直挂着。
  useEffect(() => () => { clearSession().catch(() => {}); }, []);

  const scrollTable = (to: 'top' | 'bottom') => {
    if (entries.length === 0) return;
    // Use Ant Design Table ref scrollTo API for virtual table
    if (tableRef.current?.scrollTo) {
      tableRef.current.scrollTo({ index: to === 'top' ? 0 : entries.length - 1 });
    }
  };

  // custom level input
  const [newLevel, setNewLevel] = useState('');

  const addOpLog = useCallback((level: OpLog['level'], msg: string) => {
    const time = new Date().toLocaleTimeString('zh-CN', { hour12: false });
    // 上限保护：这个数组原本无界增长，长时间使用会持续吃内存
    setOpLogs((prev: OpLog[]) => {
      const next = [...prev, { time, level, msg }];
      return next.length > 500 ? next.slice(next.length - 500) : next;
    });
    setTimeout(() => opLogRef.current?.scrollTo(0, opLogRef.current.scrollHeight), 50);
  }, []);

  const refreshFiles = async () => {
    try {
      const res = await getSessionFiles();
      setSessionFiles(res.data.data || []);
    } catch { /* ignore */ }
  };

  const handleUpload = async (file: File) => {
    setUploading(true);
    addOpLog('info', `上传文件: ${file.name}`);
    try {
      await uploadFiles([file]);
      addOpLog('success', `上传成功: ${file.name}`);
      await refreshFiles();
    } catch (err: any) {
      addOpLog('error', `上传失败: ${err.message}`);
      message.error('上传失败');
    } finally {
      setUploading(false);
    }
    return false;
  };

  const handleDeleteFile = async (name: string) => {
    addOpLog('warn', `删除文件: ${name}`);
    try {
      await deleteFile(name);
      message.success('已删除');
      await refreshFiles();
    } catch (err: any) {
      addOpLog('error', `删除失败: ${err.message}`);
    }
  };

  const handleParse = async () => {
    if (sessionFiles.length === 0) {
      message.warning('请先上传日志文件');
      return;
    }
    setLoading(true);
    setParseProgress(20);
    addOpLog('info', `开始解析 ${sessionFiles.length} 个文件...`);
    try {
      setParseProgress(50);
      const parseRes = await parseLogs();
      setParseProgress(70);
      message.success(`解析完成: ${parseRes.data.total} 条日志, ${parseRes.data.files} 个文件`);
      addOpLog('success', `解析完成: ${parseRes.data.total} 条, ${parseRes.data.files} 个文件`);

      // Load entries
      const filterRes = await filterLogs({});
      setEntries(filterRes.data.data || []);
      setParseProgress(85);

      // Load stats & dynamic levels + modules
      const statsRes = await getLogStats();
      const statsData = statsRes.data.data as Stats;
      setStats(statsData);
      if (statsData?.byLevel) {
        const dynamicLevels = Object.keys(statsData.byLevel);
        setAvailableLevels(dynamicLevels.length > 0 ? dynamicLevels : ['ERROR', 'WARN', 'INFO', 'DEBUG']);
      }
      if (statsData?.byModule) {
        setAvailableModules(Object.keys(statsData.byModule));
      }
      setParseProgress(100);
    } catch (err: any) {
      addOpLog('error', `解析失败: ${err.message}`);
      message.error('解析失败');
    } finally {
      setTimeout(() => { setLoading(false); setParseProgress(0); }, 600);
    }
  };

  const handleFilter = async () => {
    setLoading(true);
    addOpLog('info', '筛选日志...');
    try {
      const params: any = { levels, keyword, module };
      if (timeRange && timeRange[0] && timeRange[1]) {
        params.startTime = timeRange[0].toISOString();
        params.endTime = timeRange[1].toISOString();
      }
      if (fileFilter.length > 0) {
        params.files = fileFilter;
      }
      const res = await filterLogs(params);
      setEntries(res.data.data || []);
      message.info(`筛选结果: ${res.data.total} 条`);
      addOpLog('success', `筛选完成: ${res.data.total} 条`);
    } catch (err: any) {
      addOpLog('error', `筛选失败: ${err.message}`);
    } finally {
      setLoading(false);
    }
  };

  const handleExport = async () => {
    setExporting(true);
    addOpLog('info', '导出日志结果...');
    try {
      const params: any = { levels, keyword, module };
      if (timeRange && timeRange[0] && timeRange[1]) {
        params.startTime = timeRange[0].toISOString();
        params.endTime = timeRange[1].toISOString();
      }
      const res = await exportLogs(params);
      const blob = new Blob([res.data], { type: 'text/plain;charset=utf-8' });
      const url = window.URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `merged_${new Date().toISOString().slice(0, 10)}.log`;
      a.click();
      window.URL.revokeObjectURL(url);
      addOpLog('success', '导出完成');
      message.success('导出完成');
    } catch (err: any) {
      addOpLog('error', `导出失败: ${err.message}`);
      message.error('导出失败');
    } finally {
      setExporting(false);
    }
  };

  const handleClear = async () => {
    await clearSession();
    setSessionFiles([]);
    setEntries([]);
    setStats(null);
    setLevels([]);
    addOpLog('warn', '已清空所有数据');
    message.success('已清空');
  };

  const handleAddLevel = () => {
    const v = newLevel.trim().toUpperCase();
    if (v && !availableLevels.includes(v)) {
      setAvailableLevels((prev: string[]) => [...prev, v]);
      setNewLevel('');
      message.success(`已添加等级: ${v}`);
    }
  };

  const formatSize = (bytes: number) => {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / 1024 ** 2).toFixed(1)} MB`;
  };

  const columns = [
    { title: '#', width: 70, fixed: 'left' as const,
      render: (_: any, __: LogEntry, index: number) => <span style={{ color: '#484f58' }}>{index + 1}</span> },
    {
      title: '时间', dataIndex: 'timestamp', width: 190,
      render: (t: string) => <span style={{ fontSize: 12, fontFamily: 'Consolas, monospace' }}>
        {t?.replace('T', ' ').replace('Z', '').slice(0, 23)}
      </span>,
    },
    {
      title: '等级', dataIndex: 'level', width: 80,
      render: (l: string) => <Tag color={levelColors[l] || '#8b949e'} style={{ margin: 0 }}>{l}</Tag>,
    },
    { title: '模块', dataIndex: 'module', width: 140, ellipsis: true,
      render: (m: string) => m ? <Tag color="#1f6feb" style={{ margin: 0, fontSize: 11 }}>{m}</Tag> : <span style={{ color: '#484f58' }}>-</span> },
    {
      title: '来源文件', dataIndex: 'file', width: 150, ellipsis: true,
      render: (f: string) => <span style={{ color: '#8b949e', fontSize: 11 }}>{f}</span>,
    },
    {
      title: '消息', dataIndex: 'message', ellipsis: true,
      render: (m: string, r: LogEntry) => (
        <span style={{ color: levelColors[r.level] ? levelColors[r.level] + 'cc' : '#c9d1d9' }}>{m}</span>
      ),
    },
  ];

  return (
    <div className="page-enter" style={{ display: 'flex', flexDirection: 'column', gap: 10, height: 'calc(100vh - 112px)' }}>

      {/* Top: File Management */}
      <Card size="small" title="文件管理" style={{ background: '#161b22', borderColor: '#21262d' }}
        extra={
          <Space size={8}>
            <Upload beforeUpload={handleUpload} showUploadList={false} multiple accept=".log,.txt,.csv">
              <Button icon={<UploadOutlined />} loading={uploading} size="small">添加文件</Button>
            </Upload>
            <Button type="primary" icon={<MergeCellsOutlined />} size="small"
              onClick={handleParse} loading={loading} disabled={sessionFiles.length === 0}>
              解析日志
            </Button>
            <Popconfirm title="确认清空所有数据？" onConfirm={handleClear} okText="清空" cancelText="取消">
              <Button danger icon={<ClearOutlined />} size="small">清空</Button>
            </Popconfirm>
          </Space>
        }>
        {parseProgress > 0 && <Progress percent={parseProgress} size="small" strokeColor="#58a6ff" style={{ marginBottom: 8 }} />}
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8, minHeight: 32 }}>
          {sessionFiles.length === 0 && (
            <div style={{ color: '#484f58', fontSize: 13 }}>
              拖拽或点击"添加文件"上传日志文件 (.log / .txt)
            </div>
          )}
          {sessionFiles.map((f: FileInfo) => (
            <div key={f.name} className="file-chip">
              <FileTextOutlined style={{ color: '#58a6ff', marginRight: 4 }} />
              <span style={{ color: '#c9d1d9', fontSize: 12 }}>{f.name}</span>
              <span style={{ color: '#484f58', fontSize: 11, marginLeft: 4 }}>({formatSize(f.size)})</span>
              <Tooltip title="删除此文件">
                <CloseCircleOutlined
                  style={{ color: '#f85149', marginLeft: 6, cursor: 'pointer', fontSize: 12 }}
                  onClick={() => handleDeleteFile(f.name)}
                />
              </Tooltip>
            </div>
          ))}
        </div>
      </Card>

      {/* Filter Bar */}
      <Card size="small" title="筛选条件" style={{ background: '#161b22', borderColor: '#21262d' }}
        extra={
          <Space size={8}>
            <Button type="primary" icon={<SearchOutlined />} size="small" onClick={handleFilter} loading={loading}>筛选</Button>
            <Button icon={<ExportOutlined />} size="small" onClick={handleExport} loading={exporting}
              disabled={entries.length === 0}>导出合并日志</Button>
          </Space>
        }>
        <Row gutter={[12, 8]} align="middle">
          <Col flex="auto">
            <Space wrap size={8}>
              <Select mode="multiple" placeholder="日志等级" value={levels} onChange={setLevels}
                style={{ minWidth: 200 }} allowClear
                options={availableLevels.map((l: string) => ({ label: l, value: l }))}
                dropdownRender={(menu: React.ReactNode) => (
                  <>
                    {menu}
                    <div style={{ padding: '4px 8px', borderTop: '1px solid #21262d', display: 'flex', gap: 4 }}>
                      <Input size="small" placeholder="自定义等级" value={newLevel}
                        onChange={(e: React.ChangeEvent<HTMLInputElement>) => setNewLevel(e.target.value)}
                        onPressEnter={handleAddLevel} style={{ flex: 1 }} />
                      <Button size="small" type="link" icon={<PlusOutlined />} onClick={handleAddLevel}>添加</Button>
                    </div>
                  </>
                )}
              />
              <Input placeholder="关键字 (支持正则)" value={keyword}
                onChange={(e: React.ChangeEvent<HTMLInputElement>) => setKeyword(e.target.value)}
                style={{ width: 180 }} allowClear />
              <Select allowClear placeholder="模块名" value={module || undefined}
                onChange={(v: string) => setModule(v || '')}
                style={{ minWidth: 140 }}
                options={availableModules.map((m: string) => ({ label: m, value: m }))} />
              <RangePicker size="small" showTime format="YYYY-MM-DD HH:mm:ss"
                onChange={(v: any) => setTimeRange(v)} style={{ width: 340 }} />
              <Select mode="multiple" placeholder="来源文件" value={fileFilter}
                onChange={setFileFilter} style={{ minWidth: 180 }} allowClear
                options={sessionFiles.map((f: FileInfo) => ({ label: f.name, value: f.name }))} />
            </Space>
          </Col>
        </Row>
      </Card>

      {/* Stats Panel */}
      {stats && (
        <Card size="small" style={{ background: '#161b22', borderColor: '#21262d' }}>
          <Row gutter={16} align="middle">
            <Col><Statistic title="总条数" value={stats.total} valueStyle={{ color: '#c9d1d9', fontSize: 20 }} /></Col>
            {Object.entries(stats.byLevel).map(([level, count]) => (
              <Col key={level}>
                <Statistic title={level} value={count as number}
                  valueStyle={{ color: levelColors[level] || '#c9d1d9', fontSize: 20 }} />
              </Col>
            ))}
            <Col><Statistic title="未识别" value={stats.parseFailed}
              valueStyle={{ color: '#f0883e', fontSize: 20 }} /></Col>
            {stats.timeRange && stats.timeRange[0] && (
              <Col>
                <div style={{ color: '#484f58', fontSize: 11 }}>时间范围</div>
                <div style={{ color: '#8b949e', fontSize: 12 }}>
                  {stats.timeRange[0]?.slice(0, 19).replace('T', ' ')} ~ {stats.timeRange[1]?.slice(0, 19).replace('T', ' ')}
                </div>
              </Col>
            )}
            {stats.byModule && Object.keys(stats.byModule).length > 0 && (
              <Col>
                <div style={{ color: '#484f58', fontSize: 11 }}>模块数</div>
                <div style={{ color: '#58a6ff', fontSize: 18, fontWeight: 600 }}>{Object.keys(stats.byModule).length}</div>
              </Col>
            )}
          </Row>
        </Card>
      )}

      {/* Log Table */}
      <Card size="small" style={{ background: '#161b22', borderColor: '#21262d', flex: 1, overflow: 'hidden' }}
        bodyStyle={{ padding: 0, height: '100%' }}
        title={<span style={{ fontSize: 13 }}>日志记录 <Tag style={{ marginLeft: 8 }}>{entries.length} 条</Tag></span>}
        extra={
          <Space size={6}>
            <Button size="small" icon={<VerticalAlignTopOutlined />}
              onClick={() => scrollTable('top')} disabled={entries.length === 0}
              style={{ background: '#21262d', borderColor: '#30363d', color: '#c9d1d9', borderRadius: 4, fontSize: 12, height: 28 }}>顶部</Button>
            <Button size="small" icon={<VerticalAlignBottomOutlined />}
              onClick={() => scrollTable('bottom')} disabled={entries.length === 0}
              style={{ background: '#21262d', borderColor: '#30363d', color: '#c9d1d9', borderRadius: 4, fontSize: 12, height: 28 }}>底部</Button>
          </Space>
        }>
        <div ref={tableWrapRef}>
          <Table ref={tableRef} dataSource={entries} columns={columns}
            rowKey={(_: LogEntry, i?: number) => String(i)}
            size="small"
            pagination={false}
            virtual
            scroll={{ y: tableHeight, x: 1000 }}
            rowClassName={(r: LogEntry) => `log-${r.level?.toLowerCase()}`}
          />
        </div>
      </Card>

      {/* Operation Log */}
      <Collapse size="small"
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
    </div>
  );
};

export default LogAnalysis;
