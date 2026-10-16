import React from 'react';
import { Layout, Menu } from 'antd';
import { Outlet, useNavigate, useLocation } from 'react-router-dom';
import {
  HomeOutlined,
  CloudDownloadOutlined,
  FileSearchOutlined,
  DeploymentUnitOutlined,
} from '@ant-design/icons';

const { Sider, Content, Header } = Layout;

const menuItems = [
  { key: '/', icon: <HomeOutlined />, label: '首页' },
  { key: '/replay-3d', icon: <DeploymentUnitOutlined />, label: '三维复现' },
  { key: '/log-collect', icon: <CloudDownloadOutlined />, label: '日志收集' },
  { key: '/log-analysis', icon: <FileSearchOutlined />, label: '日志分析' },
];

const MainLayout: React.FC = () => {
  const navigate = useNavigate();
  const location = useLocation();

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <Sider width={200} theme="dark" collapsible>
        <div style={{
          height: 48, display: 'flex', alignItems: 'center', justifyContent: 'center',
          color: '#58a6ff', fontWeight: 700, fontSize: 18, borderBottom: '1px solid #21262d',
        }}>
          RobotReplay
        </div>
        <Menu
          theme="dark"
          mode="inline"
          selectedKeys={[location.pathname]}
          items={menuItems}
          onClick={({ key }) => navigate(key)}
        />
      </Sider>
      <Layout>
        <Header style={{
          background: '#161b22', padding: '0 24px', display: 'flex',
          alignItems: 'center', borderBottom: '1px solid #21262d',
        }}>
          <span style={{ color: '#8b949e', fontSize: 14 }}>
            机器人三维复现与数据分析工具
          </span>
        </Header>
        <Content style={{ padding: 16, overflow: 'auto' }}>
          <Outlet />
        </Content>
      </Layout>
    </Layout>
  );
};

export default MainLayout;
