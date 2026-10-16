import React, { useCallback, useEffect, useRef, useState } from 'react';
import { Card, Button, Upload, Space, Slider, Select, message, Tag, Descriptions } from 'antd';
import {
  UploadOutlined, PlayCircleOutlined, PauseCircleOutlined, BorderOutlined,
  RetweetOutlined, CameraOutlined,
} from '@ant-design/icons';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

import { uploadReplayCsv, getReplayData, clearReplay } from '../api';
import {
  loadUrdf, setJointAngles, setBaseOrientation, groundRobot, disposeRobot, UrdfRobot,
} from '../three/urdfModel';

const URDF_URL = '/models/X30/urdf/X30.urdf';

/** 每帧浮点数：12 关节 + 3 姿态 + 4 膝关节力矩，与后端 ReplayFloatsPerFrame 一致 */
const FLOATS_PER_FRAME = 19;
const FRAME_INTERVAL = 0.005;          // 200Hz

/** 倍速档位：值是每帧的名义时间间隔（秒），与桌面端一致 */
const SPEEDS = [
  { label: '0.5x', interval: 0.010 },
  { label: '1x', interval: 0.005 },
  { label: '1.5x', interval: 0.003 },
  { label: '2x', interval: 0.001 },
];

/** 桌面端默认站姿（FL/FR/HL/HR 各 HipX,HipY,Knee） */
const INIT_POSE = [
  -0.367596, -1.48719, 2.72085,
  0.267604, -1.4947, 2.72902,
  -0.314182, -1.49791, 2.75625,
  0.260833, -1.49793, 2.72576,
];

const LEGS = [
  { key: 'FL', label: 'FL (前左)' },
  { key: 'FR', label: 'FR (前右)' },
  { key: 'HL', label: 'HL (后左)' },
  { key: 'HR', label: 'HR (后右)' },
];
const JOINT_NAMES = ['HipX', 'HipY', 'Knee'];
const SHANK_LINKS = ['FL_SHANK', 'FR_SHANK', 'HL_SHANK', 'HR_SHANK'];

/** 复刻桌面端 _set_joint_color：0~59 青色渐暗，60~100 红色渐亮（HSV，非 HSL） */
const _c = new THREE.Color();
function torqueColor(val: number): THREE.Color {
  const v = Math.max(0, Math.min(100, Math.abs(Math.trunc(val))));
  let h: number, vv: number;
  if (v <= 59) { h = 0.5; vv = 1.0 - 0.5 * (v / 59); }
  else { h = 0.0; vv = 0.6 + 0.4 * (1 - (v - 60) / 40); }
  const i = Math.floor(h * 6), f = h * 6 - i;
  const q = vv * (1 - f), t2 = vv * f;      // s = 1 时 p = 0
  switch (i % 6) {
    case 0: return _c.setRGB(vv, t2, 0);
    case 1: return _c.setRGB(q, vv, 0);
    case 2: return _c.setRGB(0, vv, t2);
    case 3: return _c.setRGB(0, q, vv);
    case 4: return _c.setRGB(t2, 0, vv);
    default: return _c.setRGB(vv, 0, q);
  }
}

function fmtTime(sec: number) {
  const s = Math.max(0, sec);
  const m = Math.floor(s / 60);
  return `${String(m).padStart(2, '0')}:${(s - m * 60).toFixed(1).padStart(4, '0')}`;
}

const Replay3D: React.FC = () => {
  const mountRef = useRef<HTMLDivElement>(null);
  const rendererRef = useRef<THREE.WebGLRenderer | null>(null);
  const sceneRef = useRef<THREE.Scene | null>(null);
  const cameraRef = useRef<THREE.PerspectiveCamera | null>(null);
  const controlsRef = useRef<OrbitControls | null>(null);
  const robotRef = useRef<UrdfRobot | null>(null);
  const rafRef = useRef(0);
  const needsRenderRef = useRef(true);

  // 播放状态一律放 ref：每帧 setState 会让整页 React 重渲染，60fps 下开销无法接受
  const dataRef = useRef<Float64Array | null>(null);
  const framesRef = useRef(0);
  const frameRef = useRef(0);
  const playingRef = useRef(false);
  const loopRef = useRef(true);
  const intervalRef = useRef(SPEEDS[1].interval);
  const lastTsRef = useRef(0);
  const lastUiRef = useRef(0);

  // 关节面板与时间显示直接写 DOM，绕开 React 重渲染
  const barRefs = useRef<(HTMLDivElement | null)[]>([]);
  const valRefs = useRef<(HTMLSpanElement | null)[]>([]);
  const timeRef = useRef<HTMLSpanElement | null>(null);

  const [modelReady, setModelReady] = useState(false);
  const [modelError, setModelError] = useState('');
  const [fileName, setFileName] = useState('');
  const [totalFrames, setTotalFrames] = useState(0);
  const [duration, setDuration] = useState(0);
  const [curFrame, setCurFrame] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [loop, setLoop] = useState(true);
  const [speedIdx, setSpeedIdx] = useState(1);
  const [loading, setLoading] = useState(false);

  /** 把 12 个关节角写进左侧面板（直接操作 DOM） */
  const updateJointPanel = useCallback((angles: ArrayLike<number>) => {
    const robot = robotRef.current;
    if (!robot) return;
    for (let i = 0; i < 12 && i < angles.length; i++) {
      const j = robot.movableJoints[i];
      if (!j) continue;
      const a = angles[i];
      const span = j.upper - j.lower;
      // 条形位置按 URDF 限位裁剪显示（数据可能超限位），数值文本仍显示真实值
      const ratio = span > 0 ? Math.min(1, Math.max(0, (a - j.lower) / span)) : 0;
      const bar = barRefs.current[i];
      const val = valRefs.current[i];
      if (bar) bar.style.width = `${(ratio * 100).toFixed(1)}%`;
      if (val) val.textContent = Number.isFinite(a) ? a.toFixed(3) : String(a);
    }
  }, []);

  /** 套用某一帧原始数据（不插值、不平滑、不钳制） */
  const applyFrame = useCallback((idx: number) => {
    const robot = robotRef.current;
    const data = dataRef.current;
    if (!robot || !data || framesRef.current === 0) return;
    const i = Math.max(0, Math.min(framesRef.current - 1, idx));
    const base = i * FLOATS_PER_FRAME;

    setJointAngles(robot, data.subarray(base, base + 12));
    setBaseOrientation(robot, data[base + 12], data[base + 13], data[base + 14]);
    groundRobot(robot);      // 机身高度非数据，按足端触地推算，保证腿在网格之上

    for (let k = 0; k < 4; k++) {
      const meshes = robot.linkMeshes.get(SHANK_LINKS[k]);
      if (!meshes?.length) continue;
      const c = torqueColor(data[base + 15 + k]);
      for (const m of meshes) {
        const mat = m.material as THREE.MeshStandardMaterial;
        if (mat?.color) mat.color.copy(c);
      }
    }
    needsRenderRef.current = true;
  }, []);

  // ---------- 场景 ----------
  useEffect(() => {
    const mount = mountRef.current;
    if (!mount) return;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x0d1117);
    sceneRef.current = scene;

    const camera = new THREE.PerspectiveCamera(45, 1, 0.05, 100);
    camera.up.set(0, 0, 1);                       // URDF / PyBullet 是 Z 轴朝上
    camera.position.set(1.4, -1.4, 0.9);
    cameraRef.current = camera;

    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    renderer.domElement.style.display = 'block';
    // 右键拖动是 OrbitControls 的平移，必须挡掉浏览器右键菜单
    renderer.domElement.addEventListener('contextmenu', (e) => e.preventDefault());
    mount.appendChild(renderer.domElement);
    rendererRef.current = renderer;

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;
    controls.screenSpacePanning = true;
    controls.minDistance = 0.4;
    controls.maxDistance = 20;
    controls.target.set(0, 0, 0.35);
    controlsRef.current = controls;

    scene.add(new THREE.AmbientLight(0xffffff, 0.55));
    const key = new THREE.DirectionalLight(0xffffff, 1.6);
    key.position.set(2, -2, 4);
    key.castShadow = true;
    key.shadow.mapSize.set(1024, 1024);
    scene.add(key);
    const fill = new THREE.DirectionalLight(0x88bbff, 0.5);
    fill.position.set(-2, 2, 1.5);
    scene.add(fill);

    const grid = new THREE.GridHelper(6, 30, 0x1f6feb, 0x21262d);
    grid.rotation.x = Math.PI / 2;                // GridHelper 默认在 XZ 面，转到 XY
    scene.add(grid);

    const onResize = () => {
      const w = mount.clientWidth, h = mount.clientHeight;
      if (!w || !h) return;
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      renderer.setSize(w, h);                     // 必须同时设 CSS 尺寸，否则画面发虚
      needsRenderRef.current = true;
    };
    onResize();
    const ro = new ResizeObserver(onResize);
    ro.observe(mount);

    const tick = (ts: number) => {
      rafRef.current = requestAnimationFrame(tick);
      const dt = lastTsRef.current ? (ts - lastTsRef.current) / 1000 : 0;
      lastTsRef.current = ts;

      if (playingRef.current && dataRef.current && framesRef.current > 0) {
        // 1x = 真实时间：按墙钟推进，与渲染帧率无关
        let next = frameRef.current + dt / intervalRef.current;
        if (next >= framesRef.current - 1) {
          if (loopRef.current) next = 0;
          else { next = framesRef.current - 1; playingRef.current = false; setPlaying(false); }
        }
        frameRef.current = next;
        const fi = Math.floor(next);
        applyFrame(fi);

        const d = dataRef.current;
        updateJointPanel(d.subarray(fi * FLOATS_PER_FRAME, fi * FLOATS_PER_FRAME + 12));
        if (timeRef.current) timeRef.current.textContent = fmtTime(fi * FRAME_INTERVAL);
        // React 状态每 120ms 才动一次，避免 60fps 触发整页重渲染
        if (ts - lastUiRef.current > 120) { lastUiRef.current = ts; setCurFrame(fi); }
      }

      // 按需渲染：空闲时不出图，省 GPU
      const moved = controls.update();
      if (playingRef.current || moved || needsRenderRef.current) {
        renderer.render(scene, camera);
        needsRenderRef.current = false;
      }
    };
    rafRef.current = requestAnimationFrame(tick);

    return () => {
      cancelAnimationFrame(rafRef.current);
      ro.disconnect();
      // 显式释放模型的 geometry / material / texture，否则反复进出页面会累积显存
      if (robotRef.current) { disposeRobot(robotRef.current); robotRef.current = null; }
      grid.geometry.dispose();
      (grid.material as THREE.Material).dispose();
      controls.dispose();
      renderer.dispose();
      renderer.forceContextLoss();
      if (renderer.domElement.parentNode === mount) mount.removeChild(renderer.domElement);
      // 同时通知后端释放这份回放数据
      clearReplay().catch(() => {});
      dataRef.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // ---------- 加载模型 ----------
  useEffect(() => {
    let cancelled = false;
    loadUrdf(URDF_URL)
      .then((robot) => {
        if (cancelled) { disposeRobot(robot); return; }
        robotRef.current = robot;
        sceneRef.current?.add(robot.root);
        setJointAngles(robot, INIT_POSE);
        groundRobot(robot);
        updateJointPanel(INIT_POSE);

        robot.root.updateMatrixWorld(true);
        const c = new THREE.Box3().setFromObject(robot.root).getCenter(new THREE.Vector3());
        controlsRef.current?.target.set(0, 0, isFinite(c.z) ? c.z : 0.35);

        if (robot.diagnostics.failedUrls.length) {
          setModelError('网格加载失败: ' + robot.diagnostics.failedUrls.join(', '));
        }
        needsRenderRef.current = true;
        setModelReady(true);
      })
      .catch((e) => !cancelled && setModelError(String(e?.message || e)));
    return () => { cancelled = true; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // ---------- 加载 CSV ----------
  const handleFile = async (file: File) => {
    setLoading(true);
    try {
      const res = await uploadReplayCsv(file);
      const info = res.data.data;
      const buf = await getReplayData();
      // Float64Array 直接视图化 ArrayBuffer，不额外拷贝一份
      const arr = new Float64Array(buf.data as ArrayBuffer);

      dataRef.current = arr;                 // 旧数组失去引用，交给 GC
      framesRef.current = info.frames;
      frameRef.current = 0;
      playingRef.current = false;
      setPlaying(false);
      setTotalFrames(info.frames);
      setDuration(info.duration);
      setFileName(info.fileName || file.name);
      setCurFrame(0);
      if (timeRef.current) timeRef.current.textContent = fmtTime(0);
      // 与桌面端一致：加载后保持站姿，点「播放」才开始套用数据
      message.success(`已加载 ${info.frames.toLocaleString()} 帧，${info.duration.toFixed(1)} 秒`);
    } catch (e: any) {
      message.error(e?.response?.data?.error || e?.message || '加载失败');
    } finally {
      setLoading(false);
    }
    return false;
  };

  // ---------- 控制 ----------
  const togglePlay = () => {
    if (!dataRef.current) { message.warning('请先选择关节数据 CSV 文件'); return; }
    const next = !playingRef.current;
    playingRef.current = next;
    lastTsRef.current = 0;              // 重新计时，避免暂停期间累积的 dt 一次跳帧
    setPlaying(next);
  };

  const stop = () => {
    playingRef.current = false;
    setPlaying(false);
    frameRef.current = 0;
    setCurFrame(0);
    seekTo(0);
  };

  const seekTo = (v: number) => {
    frameRef.current = v;
    applyFrame(v);
    const d = dataRef.current;
    if (d) updateJointPanel(d.subarray(v * FLOATS_PER_FRAME, v * FLOATS_PER_FRAME + 12));
    if (timeRef.current) timeRef.current.textContent = fmtTime(v * FRAME_INTERVAL);
  };

  const seek = (v: number) => { setCurFrame(v); seekTo(v); };

  const applyView = (preset: string) => {
    const cam = cameraRef.current, ctl = controlsRef.current;
    if (!cam || !ctl) return;
    const t = ctl.target, d = 2.2;
    const set = (x: number, y: number, z: number) => {
      cam.position.set(t.x + x, t.y + y, t.z + z);
      ctl.update();
      needsRenderRef.current = true;
    };
    if (preset === 'default') set(d * 0.6, -d * 0.7, d * 0.45);
    else if (preset === 'front') set(0, -d, 0);
    else if (preset === 'side') set(d, 0, 0);
    else set(0, -0.001, d);
  };

  const snapshot = () => {
    const r = rendererRef.current, s = sceneRef.current, c = cameraRef.current;
    if (!r || !s || !c) return;
    // 不开 preserveDrawingBuffer（有持续性能损耗），改为渲染后同步取像素
    r.render(s, c);
    const url = r.domElement.toDataURL('image/png');
    const a = document.createElement('a');
    a.href = url;
    a.download = `X30_${new Date().toISOString().replace(/[:.]/g, '-')}.png`;
    a.click();
  };

  const jointRow = (legIdx: number, jIdx: number) => {
    const i = legIdx * 3 + jIdx;
    return (
      <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 6, height: 20 }}>
        <span style={{ width: 34, color: '#c9d1d9', fontSize: 11, fontWeight: 600 }}>
          {JOINT_NAMES[jIdx]}
        </span>
        <div style={{
          flex: 1, height: 4, background: '#21262d', borderRadius: 2, overflow: 'hidden',
        }}>
          <div
            ref={(el) => { barRefs.current[i] = el; }}
            style={{ height: '100%', width: '0%', background: '#58a6ff', borderRadius: 2 }}
          />
        </div>
        <span
          ref={(el) => { valRefs.current[i] = el; }}
          style={{
            width: 46, textAlign: 'right', color: '#8b949e',
            fontSize: 10, fontFamily: 'Consolas, monospace',
          }}
        >0.000</span>
      </div>
    );
  };

  return (
    <div className="page-enter" style={{ display: 'flex', gap: 12, height: 'calc(100vh - 120px)' }}>
      {/* 左栏 */}
      <div style={{
        width: 290, flexShrink: 0, display: 'flex', flexDirection: 'column',
        gap: 10, overflowY: 'auto',
      }}>
        <Card size="small" title="数据文件">
          <Space direction="vertical" style={{ width: '100%' }}>
            <div style={{
              padding: '7px 10px', background: '#0d1117', border: '1px solid #21262d',
              borderRadius: 4, fontSize: 12, color: fileName ? '#3fb950' : '#8b949e',
              wordBreak: 'break-all',
            }}>
              {fileName || '未选择关节数据文件'}
            </div>
            <Upload beforeUpload={handleFile as any} showUploadList={false} accept=".csv">
              <Button icon={<UploadOutlined />} type="primary" block loading={loading}>
                选择关节 CSV 文件
              </Button>
            </Upload>
          </Space>
        </Card>

        <Card size="small" title="数据信息">
          <Descriptions column={1} size="small" colon={false}
            labelStyle={{ color: '#8b949e', fontSize: 12 }}
            contentStyle={{ color: '#c9d1d9', fontSize: 12 }}>
            <Descriptions.Item label="总帧数">
              {totalFrames ? totalFrames.toLocaleString() : '--'}
            </Descriptions.Item>
            <Descriptions.Item label="总时长">
              {duration ? fmtTime(duration) : '--'}
            </Descriptions.Item>
            <Descriptions.Item label="状态">
              {modelError ? <Tag color="red">异常</Tag>
                : !modelReady ? <Tag color="blue">模型加载中…</Tag>
                : playing ? <Tag color="green">播放中</Tag> : <Tag>就绪</Tag>}
            </Descriptions.Item>
          </Descriptions>
          {modelError && (
            <div style={{ color: '#f85149', fontSize: 11, marginTop: 6 }}>{modelError}</div>
          )}
        </Card>

        {/* 12 个关节实时位置 */}
        <Card size="small" title="X30 关节位置 (rad)" bodyStyle={{ padding: '8px 12px' }}>
          {LEGS.map((leg, li) => (
            <div key={leg.key} style={{ marginBottom: li === 3 ? 0 : 8 }}>
              <div style={{
                color: '#58a6ff', fontSize: 11, fontWeight: 600, marginBottom: 2,
              }}>{leg.label}</div>
              {JOINT_NAMES.map((_, ji) => jointRow(li, ji))}
            </div>
          ))}
        </Card>

        <Card size="small" title="视角">
          <Space wrap>
            <Button size="small" onClick={() => applyView('default')}>默认</Button>
            <Button size="small" onClick={() => applyView('front')}>前视</Button>
            <Button size="small" onClick={() => applyView('side')}>侧视</Button>
            <Button size="small" onClick={() => applyView('top')}>俯视</Button>
          </Space>
          <Button size="small" icon={<CameraOutlined />} onClick={snapshot} block
            style={{ marginTop: 10 }}>截图保存</Button>
        </Card>
      </div>

      {/* 右栏 */}
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 8, minWidth: 0 }}>
        <div ref={mountRef} style={{
          flex: 1, minHeight: 0, background: '#0d1117',
          border: '1px solid #21262d', borderRadius: 6, overflow: 'hidden',
        }} />
        <Card size="small" bodyStyle={{ padding: '10px 14px' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
            <span ref={timeRef} style={{
              color: '#58a6ff', fontFamily: 'Consolas, monospace', fontSize: 12, width: 56,
            }}>00:00.0</span>
            <Slider
              style={{ flex: 1, margin: 0 }}
              min={0} max={Math.max(0, totalFrames - 1)} value={curFrame}
              tooltip={{ formatter: (v) => fmtTime((v || 0) * FRAME_INTERVAL) }}
              onChange={seek} disabled={!totalFrames}
            />
            <span style={{
              color: '#8b949e', fontFamily: 'Consolas, monospace', fontSize: 12, width: 56,
            }}>{fmtTime(duration)}</span>
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginTop: 8 }}>
            <Button type="primary" onClick={togglePlay} disabled={!modelReady}
              icon={playing ? <PauseCircleOutlined /> : <PlayCircleOutlined />}>
              {playing ? '暂停' : '播放'}
            </Button>
            <Button icon={<BorderOutlined />} onClick={stop} disabled={!totalFrames}>停止</Button>
            <span style={{ color: '#8b949e', fontSize: 12 }}>速度</span>
            <Select size="small" style={{ width: 80 }} value={speedIdx}
              onChange={(i) => { setSpeedIdx(i); intervalRef.current = SPEEDS[i].interval; }}
              options={SPEEDS.map((s, i) => ({ value: i, label: s.label }))} />
            <Button icon={<RetweetOutlined />} type={loop ? 'primary' : 'default'} ghost={loop}
              onClick={() => { loopRef.current = !loopRef.current; setLoop(loopRef.current); }}>
              循环
            </Button>
            <span style={{
              marginLeft: 'auto', color: '#8b949e',
              fontFamily: 'Consolas, monospace', fontSize: 12,
            }}>
              帧: {curFrame.toLocaleString()} / {totalFrames.toLocaleString()}
            </span>
          </div>
        </Card>
      </div>
    </div>
  );
};

export default Replay3D;
