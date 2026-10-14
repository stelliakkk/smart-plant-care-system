// -*- coding: utf-8 -*-
/**
 * axios 统一封装
 * ---------------------------------------------------------------------
 * 为什么不在页面里直接调 axios:
 *   地址、超时、错误处理都集中在这里, 将来要加 token 也只改这一个文件。
 *
 * 注意 VITE_API_BASE 留空时 baseURL 就是 '/api',
 * 由 vite.config.js 的代理转发到后端 8000 端口(同源, 无跨域问题)。
 */
import axios from 'axios'

const BASE = import.meta.env.VITE_API_BASE || ''

const http = axios.create({
  baseURL: `${BASE}/api`,
  timeout: 10000,
})

// 响应拦截器: 直接把 data 交出去, 顺便把常见错误在控制台说清楚。
// 返回 Promise.reject 让调用方还能自己 catch(比如显示"后端未启动")
http.interceptors.response.use(
  (res) => res.data,
  (err) => {
    const url = err.config?.url ?? ''
    if (err.code === 'ECONNABORTED') {
      console.warn(`[API] 请求超时: ${url}`)
    } else if (!err.response) {
      console.warn(`[API] 后端无响应(uvicorn 启动了吗?): ${url}`)
    } else {
      console.warn(`[API] HTTP ${err.response.status} ${url}`, err.response.data)
    }
    return Promise.reject(err)
  },
)

export default http
