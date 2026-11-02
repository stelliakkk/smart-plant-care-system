// -*- coding: utf-8 -*-
/**
 * 设备状态与指令接口
 * ---------------------------------------------------------------------
 * 对应后端的 app/api/routes_device.py 和 routes_health.py
 */
import http from './http'

/** 后端健康检查(前端顶栏那个绿点用它) */
export function fetchHealth() {
  return http.get('/health')
}

/** 边缘端在线状态 */
export function fetchStatus(deviceId) {
  return http.get('/device/status', { params: { device_id: deviceId } })
}

/**
 * 下发执行器指令
 *
 * 注意: 后端返回 sent=true 只表示"报文发出去了"。
 *       边缘端可能因为本地安全策略(比如两泵互锁或指令过期)拒绝执行,
 *       真正的结果要看 cmd_ack —— 这也是为什么安全逻辑必须放在板子上。
 *
 * @param {{actuator: 'pump'|'fertilizer'|'grow_light'|'spray',
 *          action: 'on'|'off', duration_ms?: number,
 *          source?: 'manual'|'auto'|'ai'}} cmd
 */
export function sendCommand(cmd, deviceId) {
  return http.post('/device/command', cmd, { params: { device_id: deviceId } })
}

export function fetchControlLog() { return http.get('/device/control-log') }
export function stopAllOutputs() { return http.post('/device/emergency-stop') }

export function fetchAutomation() { return http.get('/device/automation') }
export function setAutomation(actuator, enabled) { return http.post('/device/automation', {actuator, enabled}) }

export function savePumpSchedule(actuator, interval_sec, duration_ms) { return http.post('/device/pump-schedule', {actuator, interval_sec, duration_ms}) }
