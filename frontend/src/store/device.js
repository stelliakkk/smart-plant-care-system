// -*- coding: utf-8 -*-
/**
 * 全局状态仓库(Pinia)
 * ---------------------------------------------------------------------
 * 只保存"多个页面都要看"的东西, 页面自己的临时状态不要放这里。
 *
 * 这样设计的好处: 在总览页拿到的数据, 切到控制页不用重新请求,
 * 页面不会先空一下再刷出来。
 */
import { computed, ref } from 'vue'
import { defineStore } from 'pinia'

import { fetchHealth, fetchStatus, sendCommand } from '@/api/device'
import { fetchLatest } from '@/api/telemetry'
import { createSocket } from '@/utils/ws'

export const useDeviceStore = defineStore('device', () => {
  // ---------------- 状态 ----------------
  const health = ref(null)        // 后端 /api/health 的返回
  const status = ref(null)        // 边缘端在线状态
  const latest = ref(null)        // 最新一条传感器读数
  const wsConnected = ref(false)  // WebSocket 是否连着
  const lastError = ref('')       // 最近一次错误提示

  // ---------------- 派生状态 ----------------
  /** 后端服务通不通 */
  const backendOnline = computed(() => health.value !== null)
  /** 板子在线吗 */
  const deviceOnline = computed(() => status.value?.online === true)
  /** 后端有没有连上 MQTT broker */
  const mqttConnected = computed(() => health.value?.mqtt_connected === true)

  // ---------------- 动作 ----------------
  /** 拉一遍全部状态(首次进页面 / 手动刷新) */
  async function refreshAll() {
    try {
      health.value = await fetchHealth()
      status.value = await fetchStatus()
      const res = await fetchLatest()
      latest.value = res.available ? res.data : null
      lastError.value = ''
    } catch (e) {
      health.value = null
      status.value = null
      lastError.value = '连接不上后端服务, 请确认 uvicorn 已启动'
    }
  }

  // ---------------- 实时推送 ----------------
  let socket = null

  function startRealtime() {
    if (socket) return
    socket = createSocket({
      onOpen: () => {
        wsConnected.value = true
      },
      onClose: () => {
        wsConnected.value = false
      },
      // 后端主动推来的消息。
      // 目前后端还没实现推送(见 app/main.py 里的 TODO), 这里先把接口留好 ——
      // 将来后端一补上, 前端不用改任何代码就能实时刷新。
      onMessage: (msg) => {
        if (msg.type === 'telemetry' && msg.data) latest.value = msg.data
        else if (msg.type === 'status' && msg.data) status.value = msg.data
      },
    })
    socket.connect()
  }

  function stopRealtime() {
    socket?.close()
    socket = null
    wsConnected.value = false
  }

  /**
   * 手动下发指令
   * @param {string} actuator 执行器
   * @param {'on'|'off'} action
   * @param {number} durationMs 计划运行毫秒数, 0 表示不自动关
   */
  function control(actuator, action, durationMs = 0, source = 'manual') {
    return sendCommand({
      actuator,
      action,
      duration_ms: durationMs,
      source,
    })
  }

  return {
    // 状态
    health,
    status,
    latest,
    wsConnected,
    lastError,
    // 派生
    backendOnline,
    deviceOnline,
    mqttConnected,
    // 动作
    refreshAll,
    startRealtime,
    stopRealtime,
    control,
  }
})
