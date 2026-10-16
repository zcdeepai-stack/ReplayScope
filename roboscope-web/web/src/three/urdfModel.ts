import * as THREE from 'three';
import { ColladaLoader } from 'three/addons/loaders/ColladaLoader.js';

/**
 * 极简 URDF 加载器（只覆盖 X30 实际用到的特性）。
 *
 * 之所以不引第三方 urdf-loader：X30 的 URDF 里所有 origin 的 rpy 都是 "0 0 0"，
 * 关节全是绕单一轴的 revolute，没有 mesh scale。手写一份反而更可控，
 * 也便于和 PyBullet 的正运动学逐点比对。
 */

export interface UrdfJoint {
  name: string;
  type: string;
  /** 关节节点：位置为 URDF origin，自身旋转表示关节角 */
  node: THREE.Object3D;
  axis: THREE.Vector3;
  lower: number;
  upper: number;
}

export interface UrdfRobot {
  /** 根节点（base link），世界坐标下 Z 轴朝上 */
  root: THREE.Object3D;
  links: Map<string, THREE.Object3D>;
  joints: UrdfJoint[];
  /** 按 URDF 声明顺序排列的 revolute 关节，与 PyBullet 关节索引顺序一致 */
  movableJoints: UrdfJoint[];
  /** 各 link 上的网格，用于改颜色 */
  linkMeshes: Map<string, THREE.Mesh[]>;
  /** 加载诊断：每个 link 实际挂上的网格数、失败的网格 URL */
  diagnostics: {
    meshCountByLink: Record<string, number>;
    failedUrls: string[];
    uniqueMeshes: number;
  };
  /** 足端 link 的世界 Z 减去整机最低点的差值，用于把四足机器狗放到地面上 */
  footClearance: number;
  /** 四个足端 link（若 URDF 中存在） */
  footLinks: THREE.Object3D[];
}

function parseVec3(s: string | null, fallback: [number, number, number]): THREE.Vector3 {
  if (!s) return new THREE.Vector3(...fallback);
  const p = s.trim().split(/\s+/).map(Number);
  if (p.length !== 3 || p.some((n) => !isFinite(n))) return new THREE.Vector3(...fallback);
  return new THREE.Vector3(p[0], p[1], p[2]);
}

/** URDF 的 rpy 是固定轴 roll-pitch-yaw，等价于 R = Rz(y)·Ry(p)·Rx(r) */
function applyRpy(obj: THREE.Object3D, rpy: THREE.Vector3) {
  obj.quaternion.setFromEuler(new THREE.Euler(rpy.x, rpy.y, rpy.z, 'ZYX'));
}

/**
 * 解析 URDF 并构建 three.js 层级。
 * @param urdfUrl   URDF 文件地址
 * @param meshBase  网格解析基准（URDF 内是 ../meshes/xxx.dae 这种相对路径）
 */
export async function loadUrdf(urdfUrl: string): Promise<UrdfRobot> {
  const res = await fetch(urdfUrl);
  if (!res.ok) {
    throw new Error(`URDF 加载失败 (HTTP ${res.status}) ${urdfUrl}\n` +
      `${(await res.text()).slice(0, 200)}`);
  }
  const text = await res.text();

  // 服务端若没能注册模型静态路由，请求会落到 SPA 兜底返回 index.html。
  // 直接拿去当 XML 解析，只会得到 "Specification mandates value for attribute
  // crossorigin" 这类与真实原因毫不相干的报错，排查成本极高。这里先识别出来。
  if (/^\s*<!doctype html/i.test(text) || /<html[\s>]/i.test(text)) {
    throw new Error(
      `服务端返回的是网页而不是模型文件：${urdfUrl}\n` +
      `说明后端没有找到 X30 模型目录（应位于 x30PybusSimlu\\_internal\\X30 或 x30PybusSimlu\\X30），` +
      `请确认安装完整。`);
  }

  const xml = new DOMParser().parseFromString(text, 'application/xml');

  const perr = xml.querySelector('parsererror');
  if (perr) throw new Error('URDF 解析失败: ' + perr.textContent);

  const links = new Map<string, THREE.Object3D>();
  const linkMeshes = new Map<string, THREE.Mesh[]>();
  const joints: UrdfJoint[] = [];

  // ---- 1. 建立所有 link 节点 ----
  const linkEls = Array.from(xml.querySelectorAll('robot > link'));
  for (const el of linkEls) {
    const name = el.getAttribute('name') || '';
    const obj = new THREE.Object3D();
    obj.name = name;
    links.set(name, obj);
    linkMeshes.set(name, []);
  }

  // ---- 2. 建立关节，串起父子关系 ----
  const jointEls = Array.from(xml.querySelectorAll('robot > joint'));
  const childLinks = new Set<string>();

  for (const el of jointEls) {
    const name = el.getAttribute('name') || '';
    const type = el.getAttribute('type') || 'fixed';
    const parentName = el.querySelector(':scope > parent')?.getAttribute('link') || '';
    const childName = el.querySelector(':scope > child')?.getAttribute('link') || '';
    const parent = links.get(parentName);
    const child = links.get(childName);
    if (!parent || !child) continue;

    const originEl = el.querySelector(':scope > origin');
    const xyz = parseVec3(originEl?.getAttribute('xyz') ?? null, [0, 0, 0]);
    const rpy = parseVec3(originEl?.getAttribute('rpy') ?? null, [0, 0, 0]);

    const node = new THREE.Object3D();
    node.name = name;
    node.position.copy(xyz);
    applyRpy(node, rpy);
    // 关节角在 node 之下再套一层，避免和 origin 的旋转混在一起
    const rotator = new THREE.Object3D();
    rotator.name = name + '__rot';
    node.add(rotator);
    rotator.add(child);

    parent.add(node);
    childLinks.add(childName);

    const axis = parseVec3(el.querySelector(':scope > axis')?.getAttribute('xyz') ?? null, [1, 0, 0]);
    if (axis.lengthSq() > 0) axis.normalize();
    const limitEl = el.querySelector(':scope > limit');

    joints.push({
      name,
      type,
      node: rotator,
      axis,
      lower: Number(limitEl?.getAttribute('lower') ?? -Math.PI),
      upper: Number(limitEl?.getAttribute('upper') ?? Math.PI),
    });
  }

  // ---- 3. 找根 link ----
  let rootName = '';
  for (const [name] of links) {
    if (!childLinks.has(name)) { rootName = name; break; }
  }
  const root = links.get(rootName);
  if (!root) throw new Error('URDF 中找不到根 link');

  // ---- 4. 收集视觉引用 ----
  const urdfDir = urdfUrl.substring(0, urdfUrl.lastIndexOf('/'));
  interface VisualSpec {
    linkName: string;
    url: string;
    xyz: THREE.Vector3;
    rpy: THREE.Vector3;
  }
  const visuals: VisualSpec[] = [];
  const uniqueUrls = new Set<string>();

  for (const el of linkEls) {
    const linkName = el.getAttribute('name') || '';
    if (!links.has(linkName)) continue;

    for (const vis of Array.from(el.querySelectorAll(':scope > visual'))) {
      const file = vis.querySelector(':scope > geometry > mesh')?.getAttribute('filename');
      if (!file) continue;
      const originEl = vis.querySelector(':scope > origin');
      const url = new URL(file, window.location.origin + urdfDir + '/').pathname;
      uniqueUrls.add(url);
      visuals.push({
        linkName,
        url,
        xyz: parseVec3(originEl?.getAttribute('xyz') ?? null, [0, 0, 0]),
        rpy: parseVec3(originEl?.getAttribute('rpy') ?? null, [0, 0, 0]),
      });
    }
  }

  // ---- 5. 每个网格只加载一次，再按需克隆 ----
  // X30 里 hip.dae 被 4 条腿共用、shank.dae 也是，若按 visual 逐个并发加载，
  // 同一 URL 会被同时请求 4 次，既浪费又容易出问题。这里先去重加载。
  const loader = new ColladaLoader();
  const loaded = new Map<string, THREE.Object3D>();
  const failed: string[] = [];

  await Promise.all(
    Array.from(uniqueUrls).map(
      (url) =>
        new Promise<void>((resolve) => {
          loader.load(
            url,
            (collada) => {
              if (collada && collada.scene) {
                loaded.set(url, collada.scene as unknown as THREE.Object3D);
              } else {
                failed.push(url);
              }
              resolve();
            },
            undefined,
            () => { failed.push(url); resolve(); }   // 单个失败不拖垮整体
          );
        })
    )
  );

  // ---- 6. 挂到各 link ----
  for (const spec of visuals) {
    const src = loaded.get(spec.url);
    const linkObj = links.get(spec.linkName);
    if (!src || !linkObj) continue;

    const holder = new THREE.Object3D();
    holder.position.copy(spec.xyz);
    applyRpy(holder, spec.rpy);

    // 注意：这些 DAE 头部声明 up_axis=Y_UP，但顶点数据实际是 Z-up
    // （实测 shank.dae 长轴沿 Z=0.409，与 URDF「小腿沿 -Z 延伸 0.31」吻合）。
    // ColladaLoader 对 Y_UP 不做旋转，正好保持原样，这里不能再补坐标轴转换，
    // 否则模型会整体侧躺 90°。
    const obj = src.clone(true);
    holder.add(obj);
    linkObj.add(holder);

    const arr = linkMeshes.get(spec.linkName)!;
    obj.traverse((o) => {
      const m = o as THREE.Mesh;
      if ((m as any).isMesh) {
        // 克隆材质，保证按 link 改色时互不影响
        m.material = Array.isArray(m.material)
          ? m.material.map((x) => x.clone())
          : (m.material as THREE.Material).clone();
        m.castShadow = true;
        m.receiveShadow = true;
        arr.push(m);
      }
    });
  }

  const movableJoints = joints.filter((j) => j.type === 'revolute' || j.type === 'continuous');

  const meshCountByLink: Record<string, number> = {};
  for (const [name, arr] of linkMeshes) {
    if (arr.length > 0) meshCountByLink[name] = arr.length;
  }

  // 足端 link：URDF 里叫 *_FOOT
  const footLinks: THREE.Object3D[] = [];
  for (const [name, obj] of links) {
    if (name.endsWith('_FOOT')) footLinks.push(obj);
  }

  // 量一次「足端 link 原点」比「整机最低点」高多少，之后每帧靠它把四足机器狗放到地面上
  root.updateMatrixWorld(true);
  let footClearance = 0;
  if (footLinks.length > 0) {
    const box = new THREE.Box3().setFromObject(root);
    let minFoot = Infinity;
    const v = new THREE.Vector3();
    for (const f of footLinks) minFoot = Math.min(minFoot, f.getWorldPosition(v).z);
    if (isFinite(minFoot) && isFinite(box.min.z)) footClearance = minFoot - box.min.z;
  }

  return {
    root, links, joints, movableJoints, linkMeshes,
    diagnostics: { meshCountByLink, failedUrls: failed, uniqueMeshes: uniqueUrls.size },
    footClearance,
    footLinks,
  };
}

/**
 * 把机身抬到「当前姿态下最低足端刚好踩在 z=0 地面」的高度。
 *
 * 注意：CSV 里没有机身高度这一列（只有 12 个关节角 + IMU 三个姿态角），
 * 所以机身的上下位置无法来自数据，只能由程序推算。这里按足端触地推算，
 * 保证运动过程中腿始终在网格之上。关节角与机身姿态仍然完全取自原始数据。
 *
 * 只做一次 updateMatrixWorld：root 在 Z 上的平移会等量作用于所有足端世界坐标，
 * 因此先按 z=0 量一次，再直接设置 root.z 即可，渲染前 three.js 会自动刷新矩阵。
 */
export function disposeRobot(robot: UrdfRobot) {
  const geometries = new Set<THREE.BufferGeometry>();
  const materials = new Set<THREE.Material>();

  robot.root.traverse((o) => {
    const m = o as THREE.Mesh;
    if (!(m as any).isMesh) return;
    if (m.geometry) geometries.add(m.geometry);
    if (Array.isArray(m.material)) m.material.forEach((x) => materials.add(x));
    else if (m.material) materials.add(m.material);
  });

  for (const g of geometries) g.dispose();
  for (const mat of materials) {
    // 材质上挂的贴图也要一起释放
    for (const key of Object.keys(mat)) {
      const val = (mat as any)[key];
      if (val && val.isTexture) (val as THREE.Texture).dispose();
    }
    mat.dispose();
  }

  robot.root.removeFromParent();
  robot.links.clear();
  robot.linkMeshes.clear();
  robot.joints.length = 0;
  robot.movableJoints.length = 0;
  robot.footLinks.length = 0;
}

const _tmpVec = new THREE.Vector3();

export function groundRobot(robot: UrdfRobot) {
  if (robot.footLinks.length === 0) return;
  robot.root.position.z = 0;
  robot.root.updateMatrixWorld(true);
  let minFoot = Infinity;
  for (const f of robot.footLinks) {
    const z = f.getWorldPosition(_tmpVec).z;
    if (z < minFoot) minFoot = z;
  }
  if (!isFinite(minFoot)) return;
  robot.root.position.z = robot.footClearance - minFoot;
}

/**
 * 按 URDF 声明顺序设置各可动关节的角度（弧度）。
 *
 * 角度原样使用，不做任何限位钳制——桌面端 PyBullet 的 resetJointState 同样不检查
 * 限位（其默认站姿膝关节 2.72085 本身就超出 URDF 的 2.531 上限），钳制会改变真实姿态。
 * 只挡掉非有限值，因为 NaN 四元数会让整条腿的矩阵失效、在画面上凭空消失。
 */
export function setJointAngles(robot: UrdfRobot, angles: ArrayLike<number>) {
  const n = Math.min(robot.movableJoints.length, angles.length);
  for (let i = 0; i < n; i++) {
    const j = robot.movableJoints[i];
    const a = angles[i];
    j.node.quaternion.setFromAxisAngle(j.axis, Number.isFinite(a) ? a : 0);
  }
}

/** 用 IMU 的 roll/pitch/yaw（弧度）设置机身姿态，与 PyBullet getQuaternionFromEuler 一致 */
export function setBaseOrientation(robot: UrdfRobot, roll: number, pitch: number, yaw: number) {
  // 同样要防非有限值，否则整机矩阵会变 NaN，模型直接消失
  const r = Number.isFinite(roll) ? roll : 0;
  const p = Number.isFinite(pitch) ? pitch : 0;
  const y = Number.isFinite(yaw) ? yaw : 0;
  robot.root.quaternion.setFromEuler(new THREE.Euler(r, p, y, 'ZYX'));
}
