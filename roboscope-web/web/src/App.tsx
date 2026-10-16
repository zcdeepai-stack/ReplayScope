import { BrowserRouter, Routes, Route } from 'react-router-dom';
import { ConfigProvider, theme } from 'antd';
import MainLayout from './layouts/MainLayout';
import Home from './pages/Home';
import LogCollect from './pages/LogCollect';
import LogAnalysis from './pages/LogAnalysis';
import Replay3D from './pages/Replay3D';

function App() {
  return (
    <ConfigProvider theme={{
      algorithm: theme.darkAlgorithm,
      token: {
        colorPrimary: '#58a6ff',
        colorBgContainer: '#161b22',
        colorBgElevated: '#1c2128',
        colorBorder: '#30363d',
        colorText: '#c9d1d9',
        colorTextSecondary: '#8b949e',
        borderRadius: 6,
      },
    }}>
      <BrowserRouter>
        <Routes>
          <Route path="/" element={<MainLayout />}>
            <Route index element={<Home />} />
            <Route path="replay-3d" element={<Replay3D />} />
            <Route path="log-collect" element={<LogCollect />} />
            <Route path="log-analysis" element={<LogAnalysis />} />
          </Route>
        </Routes>
      </BrowserRouter>
    </ConfigProvider>
  );
}

export default App;
