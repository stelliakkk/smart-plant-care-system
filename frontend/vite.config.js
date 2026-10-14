// -*- coding: utf-8 -*-
/**
 * Vite 配置
 * ---------------------------------------------------------------------
 * 这里的 proxy 是前端开发最省事的一招:
 *   浏览器只跟 5173 端口打交道, /api、/ws、/images 都由 Vite 转发给后端 8000。
 *   于是前端代码里直接写 '/api/health' 就行, 既不用关心跨域,
 *   也不依赖后端配 CORS(手机真机调试时才可能用得上 CORS)。
 */
import { fileURLToPath, URL } from 'node:url'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

const BACKEND = 'http://127.0.0.1:8000'

export default defineConfig({
  plugins: [vue()],

  resolve: {
    alias: {
      // 用 @/xxx 代替 ../../xxx, 层级深了也不会绕晕
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },

  server: {
    port: 5173,
    // 方便用局域网 IP 访问(手机测试)
    host: true,
    proxy: {
      '/api': { target: BACKEND, changeOrigin: true },
      '/images': { target: BACKEND, changeOrigin: true },
      // WebSocket 要单独写 ws: true
      '/ws': { target: BACKEND, ws: true, changeOrigin: true },
    },
  },
})
