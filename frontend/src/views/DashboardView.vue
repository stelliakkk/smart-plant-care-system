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
    最新一组传感器读数 · 最后更新：{{ updatedAt }}
  </p>

  <div class="grid grid-cards" style="margin-bottom: 16px">
    <SensorCard
      icon="💧"
      label="土壤相对湿度"
      :value="d.soil_moisture"
      unit="%"
      :data-source="d.data_source"
      hint="探头干湿参考值映射；补水当前按定时运行"
    />
    <SensorCard
      icon="🧪"
      label="土壤电导率（EC）"
      :value="d.soil_ec"
      unit="mS/cm"
      :digits="2"
      :data-source="d.data_source"
      hint="设备尚未上传标定后的 EC；模拟值仅供演示"
    />
    <SensorCard
      icon="🌡️"
      label="探头温度"
      :value="d.temperature"
      unit="°C"
      :data-source="d.data_source"
      hint="DS18B20 当前仅本地读取，设备上传值为空"
    />
    <SensorCard
      icon="☀️"
      label="光照强度"
      :value="d.light_lux"
      unit="lux"
      :digits="0"
      :data-source="d.data_source"
      hint="低于 100 lux 持续 10 秒补光"
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
              {{ store.wsConnected ? '连接正常，数据当前每 3 秒轮询更新' : '连接未建立，数据当前每 3 秒轮询更新' }}
            </td>
          </tr>
        </tbody>
      </table>
    </div>

    <div class="card">
      <h2 class="card-title">🛠️ 调试工具</h2>
      <p class="hint" style="margin-bottom: 12px">
        生成一组随机演示数据，数值旁会显示“模拟”角标。
        历史记录会保留来源；旧记录无法追溯时显示“来源未记录”。
      </p>
      <button class="primary" :disabled="busy || !store.backendOnline" @click="mockReport">
        {{ busy ? '写入中...' : '模拟上报一条数据' }}
      </button>

      <p class="hint" style="margin-top: 14px">
        实物调试时请先停止模拟上报。持续模拟可在 backend 目录运行：<br />
        <span class="mono">python scripts\sim_device.py --interval 3</span>
      </p>
    </div>
  </div>
</template>
