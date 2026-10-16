import React from 'react';
import { Row, Col } from 'antd';
import {
  CloudServerOutlined,
  FileSearchOutlined,
  ClusterOutlined,
} from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';

const cards = [
  {
    title: '日志收集',
    desc: 'SSH 远程连接主机，浏览、预览和下载日志文件',
    icon: <CloudServerOutlined />,
    color: '#58a6ff',
    path: '/log-collect',
  },
  {
    title: '日志分析',
    desc: '上传日志文件，按等级/时间/关键字智能筛选分析',
    icon: <FileSearchOutlined />,
    color: '#238636',
    path: '/log-analysis',
  },
  {
    title: '三维复现',
    desc: '加载关节数据 CSV，在浏览器中回放 X30 三维运动',
    icon: <ClusterOutlined />,
    color: '#d29922',
    path: '/replay-3d',
  },
];

const Home: React.FC = () => {
  const navigate = useNavigate();

  return (
    <div className="page-enter" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', minHeight: 'calc(100vh - 120px)', padding: '0 40px' }}>
      {/* Title */}
      <div style={{ textAlign: 'center', marginBottom: 48 }}>
        <h1 style={{ color: '#58a6ff', fontSize: 32, fontWeight: 700, marginBottom: 8, letterSpacing: 2 }}>
          RobotReplay
        </h1>
        <p style={{ color: '#8b949e', fontSize: 15, margin: 0 }}>
          机器人三维复现与数据分析工具
        </p>
      </div>

      {/* Feature Cards */}
      <Row gutter={[24, 24]} style={{ width: '100%', maxWidth: 960 }}>
        {cards.map((c, i) => (
          <Col span={8} key={i}>
            <div
              onClick={() => navigate(c.path)}
              style={{
                background: '#161b22',
                border: '1px solid #21262d',
                borderRadius: 12,
                padding: '40px 24px',
                textAlign: 'center',
                cursor: 'pointer',
                transition: 'all 0.35s ease',
                position: 'relative',
                overflow: 'hidden',
              }}
              onMouseEnter={e => {
                const el = e.currentTarget;
                el.style.borderColor = c.color;
                el.style.boxShadow = `0 8px 30px ${c.color}22`;
                el.style.transform = 'translateY(-4px)';
                const icon = el.querySelector('.card-icon') as HTMLElement;
                if (icon) icon.style.transform = 'scale(1.2)';
              }}
              onMouseLeave={e => {
                const el = e.currentTarget;
                el.style.borderColor = '#21262d';
                el.style.boxShadow = 'none';
                el.style.transform = 'translateY(0)';
                const icon = el.querySelector('.card-icon') as HTMLElement;
                if (icon) icon.style.transform = 'scale(1)';
              }}
            >
              <div
                className="card-icon"
                style={{
                  fontSize: 42,
                  color: c.color,
                  marginBottom: 16,
                  transition: 'transform 0.35s ease',
                }}
              >
                {c.icon}
              </div>
              <h3 style={{ color: '#c9d1d9', fontSize: 18, fontWeight: 600, marginBottom: 10 }}>
                {c.title}
              </h3>
              <p style={{ color: '#8b949e', fontSize: 13, margin: 0, lineHeight: 1.6 }}>
                {c.desc}
              </p>
            </div>
          </Col>
        ))}
      </Row>

      {/* Footer */}
      <div style={{ marginTop: 56, textAlign: 'center', color: '#30363d', fontSize: 12 }}>
        RobotReplay  v2.2.1
      </div>
    </div>
  );
};

export default Home;
