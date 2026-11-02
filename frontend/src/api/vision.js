// -*- coding: utf-8 -*-
/**
 * 图像分析接口
 * ---------------------------------------------------------------------
 * 对应后端的 app/api/routes_vision.py
 * 后端走的是 DeepSeek 视觉模型, 所以前端只管触发和展示。
 */
import http from './http'

/** 最近一次 AI 诊断结果 */
export function fetchLatestVision() {
  return http.get('/vision/latest')
}

/** 提交一次异步抓拍分析；任务进度由最新记录接口查询。 */
export function analyzeNow() {
  return http.post('/vision/analyze')
}

/** ESP32-CAM 在线状态与最近一次仅元数据抓帧记录。 */
export function fetchCameraStatus() {
  return http.get('/vision/camera/status')
}

export function fetchVisionHistory(beforeId = null) {
  return http.get('/vision/history', { params: { limit: 30, before_id: beforeId } })
}



/** 把后端返回的相对图片路径补成可直接放进 <img> 的地址 */
export function imageUrl(path) {
  if (!path) return ''
  if (/^https?:\/\//.test(path)) return path
  const base = import.meta.env.VITE_API_BASE || ''
  return `${base}${path.startsWith('/') ? '' : '/'}${path}`
}


export function fetchVisionRecord(id) { return http.get('/vision/record/' + id) }
