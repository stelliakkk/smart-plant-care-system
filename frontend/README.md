# 前端（Vue 3 + Vite）

基于大模型的植物养护系统的**用户界面**：实时看数据、手动控制执行器、查看 AI 诊断。

## 环境要求

**必须用 Node 20 以上**（推荐 22）。你机器上 PATH 里的 `node` 是 v18.7.0，太老，
Vite 6 跑不起来。机器里已经有一个现成的 Node 22：

```
D:\比赛\物联网设计\.tools\node-v22.23.1-win-x64
```

### 方案 A：临时用（当前这个终端窗口有效）

```powershell
$env:Path = "D:\比赛\物联网设计\.tools\node-v22.23.1-win-x64;" + $env:Path
node --version    # 应该显示 v22.23.1
```

### 方案 B：永久生效（推荐，一次搞定）

把 `D:\比赛\物联网设计\.tools\node-v22.23.1-win-x64` 加到系统的
"Path" 环境变量里，并且**排在旧 Node 前面**。改完重开终端，`node --version` 就是 v22 了。

## 快速开始

```powershell
cd "D:\Lab\Embedded Curriculum Design\pro\frontend"

# 1. 切到 Node 22（方案 A）
$env:Path = "D:\比赛\物联网设计\.tools\node-v22.23.1-win-x64;" + $env:Path

# 2. 装依赖（用国内镜像快很多）
npm install --registry=https://registry.npmmirror.com

# 3. 启动
npm run dev
```

然后打开 <http://localhost:5173>。

⚠️ 前端依赖后端，请先把后端跑起来（`backend` 目录下 `uvicorn app.main:app --reload`），
否则页面顶部会显示一条黄色的"连接不上后端服务"提示。

## 目录结构

```
frontend/
├── index.html           页面外壳（Vue 是挂到 #app 上的）
├── vite.config.js       构建配置 + 开发代理（关键文件，见下文）
├── .env.development     环境变量（一般不用改）
└── src/
    ├── main.js          入口：装 Pinia、装 Router、挂载
    ├── App.vue          顶栏 + 三个状态灯 + 页面容器
    ├── router/          路由表（4 个页面）
    ├── api/             接口调用层
    │   ├── http.js      ★ axios 统一封装，改地址/加 token 只改这里
    │   ├── telemetry.js 传感器数据
    │   ├── device.js    设备状态与指令
    │   └── vision.js    AI 诊断
    ├── store/device.js  全局状态（Pinia）：链路状态、最新读数、WebSocket
    ├── utils/ws.js      WebSocket 封装（带自动重连）
    ├── components/      可复用小组件
    ├── views/           4 个页面
    └── assets/main.css  全局样式（配色用 CSS 变量，改主题只改 :root）
```

## 四个页面

| 路由 | 页面 | 作用 |
|---|---|---|
| `/dashboard` | 实时总览 | 5 个环境指标 + 链路状态 + 调试按钮 |
| `/control` | 手动控制 | 开关补水泵 / 施肥泵 / 补光灯 / 喷雾 |
| `/vision` | AI 诊断 | 抓拍图片与模型诊断结果 |
| `/history` | 历史数据 | 表格形式的历史读数 |

## 关于开发代理（`vite.config.js`）

这是前端开发最省事的一招：

```
浏览器 ──▶ 5173 (Vite)  ──/api、/ws、/images──▶  8000 (后端)
```

好处是**浏览器眼里只有一个源**，于是：

- 前端代码里直接写 `http.get('/api/health')`，不用管后端在哪
- 不用配 CORS，也不会遇到跨域报错
- 换后端地址只改 `vite.config.js` 一处

将来手机连同一个 WiFi 访问时，把地址换成电脑的局域网 IP 即可
（`vite.config.js` 里已经写了 `host: true`）。

## 当前进度

页面骨架、接口封装、状态管理、WebSocket 客户端都已完成，可以正常跑通
"后端有数据 → 页面显示出来"。

还差（代码里都标了 TODO 或提示文字）：

- 历史曲线图（阶段 3 接 ECharts）
- WebSocket 实时推送（后端 `app/main.py` 里还没实现推送，前端先用 10 秒轮询兜底）
- AI 诊断结果展示的真实数据（阶段 5）
