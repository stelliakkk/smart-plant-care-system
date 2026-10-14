// -*- coding: utf-8 -*-
/**
 * 传感器数据接口
 * ---------------------------------------------------------------------
 * 对应后端的 app/api/routes_telemetry.py
 */
import http from './http'

/**
 * 最新一条读数
 * @returns {Promise<{available: boolean, data: object|null}>}
 */
export function fetchLatest(deviceId) {
  return http.get('/telemetry/latest', { params: { device_id: deviceId } })
}

/**
 * 历史读数(时间正序)
 * @returns {Promise<{count: number, data: object[]}>}
 */
export function fetchHistory(deviceId, limit = 200) {
  return http.get('/telemetry/history', {
    params: { device_id: deviceId, limit },
  })
}

/**
 * 手动写入一条数据 —— 调试用。
 * 没有硬件时可以在总览页点"模拟上报"看看页面有没有反应。
 */
export function simulate(payload) {
  return http.post('/telemetry/simulate', payload)
}
