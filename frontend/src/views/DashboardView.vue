<script setup>
// -*- coding: utf-8 -*-
/**
 * 实时总览页
 * ---------------------------------------------------------------------
 * 一眼看清: 环境数据 + 系统各环节通不通。
 * 没有硬件时点"模拟上报"就能让页面动起来, 方便调样式。
 */
import { computed, ref } from 'vue'

import SensorCard from '@/components/SensorCard.vue'
import { simulate } from '@/api/telemetry'
import { useDeviceStore } from '@/store/device'

const store = useDeviceStore()
const busy = ref(false)

// 没数据时给个空对象, 模板里就不用到处写 ?.
const d = computed(() => store.latest || {})

const updatedAt = computed(() =>
  store.latest?.ts ? new Date(store.latest.ts).toLocaleString('zh-CN') : '暂无数据',
)

/** 手动塞一条假数据, 用来验证前后端链路是否通了 */
async function mockReport() {
  busy.value = true
  try {
    await simulate({
      device_id: store.health?.device_id || 'edge01',
      soil_moisture: +(30 + Math.random() * 25).toFixed(1),
      soil_ec: +(1 + Math.random()).toFixed(2),
      temperature: +(22 + Math.random() * 6).toFixed(1),
      light_lux: Math.round(Math.random() * 8000),
    })
    await store.refreshAll()
  } catch (e) {
    // 拦截器已经在控制台打过日志了
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <h1 class="page-title">实时总览</h1>
  <p class="page-desc">
    数据来自边缘端（ESP32）经 MQTT 上报 · 最后更新：{{ updatedAt }}
  </p>

  <div class="grid grid-cards" style="margin-bottom: 16px">
    <SensorCard
      icon="💧"
      label="土壤湿度"
      :value="d.soil_moisture"
      unit="%"
      hint="低于 25% 应补水"
    />
    <SensorCard
      icon="🧪"
      label="土壤盐分"
      :value="d.soil_ec"
      unit="mS/cm"
      :digits="2"
      hint="反映肥力高低"
    />
    <SensorCard
      icon="🌡️"
      label="环境温度"
      :value="d.temperature"
      unit="°C"
      hint="过高需通风遮阳"
    />
    <SensorCard
      icon="☀️"
      label="光照强度"
      :value="d.light_lux"
      unit="lux"
      :digits="0"
      hint="低于 3000 lux 需补光"
    />
  </div>

  <div class="grid grid-2">
    <div class="card">
      <h2 class="card-title">🔌 链路状态</h2>
      <table>
        <tbody>
          <tr>
            <td>后端服务</td>
            <td>
              <i class="dot" :class="{ on: store.backendOnline }"></i>
              {{ store.backendOnline ? '正常' : '未连接' }}
            </td>
          </tr>
          <tr>
            <td>MQTT 通道</td>
            <td>
              <i class="dot" :class="{ on: store.mqttConnected }"></i>
              {{ store.mqttConnected ? '已连接 broker' : '未连接' }}
            </td>
          </tr>
          <tr>
            <td>边缘端设备</td>
            <td>
              <i class="dot" :class="{ on: store.deviceOnline }"></i>
              {{ store.deviceOnline ? '在线' : '离线' }}
              <span v-if="store.status?.rssi" class="hint">
                （信号 {{ store.status.rssi }} dBm）
              </span>
            </td>
          </tr>
          <tr>
            <td>实时推送</td>
            <td>
              <i class="dot" :class="{ on: store.wsConnected }"></i>
              {{ store.wsConnected ? 'WebSocket 已连接' : '未连接（暂用轮询兜底）' }}
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <div class="card">
      <h2 class="card-title">🛠️ 调试工具</h2>
      <p class="hint" style="margin-bottom: 12px">
        没有硬件时，点下面的按钮会往后端塞一条随机数据，
        用来验证「前端 → 后端 → 数据库 → 前端」这条链路。
      </p>
      <button class="primary" :disabled="busy || !store.backendOnline" @click="mockReport">
        {{ busy ? '写入中...' : '模拟上报一条数据' }}
      </button>

      <p class="hint" style="margin-top: 14px">
        要模拟真实设备持续上报，请在 backend 目录运行：<br />
        <span class="mono">python scripts\sim_device.py --interval 3</span>
      </p>
    </div>
  </div>
</template>
