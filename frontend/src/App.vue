<script setup>
// -*- coding: utf-8 -*-
/**
 * 根组件: 顶栏 + 页面容器
 * ---------------------------------------------------------------------
 * 顶栏上那三个小圆点是整个系统最直观的"体检报告":
 *   后端服务 / MQTT 通道 / 边缘端设备, 哪个断了立刻能看出来。
 */
import { computed, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'

import { useDeviceStore } from '@/store/device'

const store = useDeviceStore()
const router = useRouter()

// 菜单直接从路由表生成, 免得两处维护(加了页面忘了加菜单)
const menus = computed(() =>
  router
    .getRoutes()
    .filter((r) => r.meta?.title && r.name !== 'home')
    .map((r) => ({
      path: r.path,
      title: r.meta.title,
      icon: r.meta.icon,
    })),
)

// 后端 WebSocket 推送还没实现(见 app/main.py 的 TODO), 所以先用 HTTP 轮询兜底。
//
// 为什么是 3 秒:
//   传感器每秒采一次、板子每 3 秒上报一次, 页面跟着 3 秒刷一格,
//   肉眼看就是"数字在跳", 而请求量对后端毫无压力。
//   等后端把 WebSocket 推起来后, 这里可以改成 30 秒甚至去掉。
const REFRESH_INTERVAL_MS = 3000

let timer = null

onMounted(async () => {
  await store.refreshAll()
  store.startRealtime()
  timer = setInterval(() => store.refreshAll(), REFRESH_INTERVAL_MS)
})

onUnmounted(() => {
  clearInterval(timer)
  store.stopRealtime()
})
</script>

<template>
  <div class="topbar">
    <div class="brand">🌱 植物养护系统</div>

    <nav class="nav">
      <RouterLink v-for="m in menus" :key="m.path" :to="m.path">
        {{ m.icon }} {{ m.title }}
      </RouterLink>
    </nav>

    <div class="indicators">
      <span class="indicator">
        <i class="dot" :class="{ on: store.backendOnline }"></i>
        后端
      </span>
      <span class="indicator">
        <i class="dot" :class="{ on: store.mqttConnected }"></i>
        MQTT
      </span>
      <span class="indicator">
        <i class="dot" :class="{ on: store.deviceOnline }"></i>
        设备
      </span>
    </div>
  </div>

  <main class="content">
    <!-- 后端连不上时, 每个页面顶部都给一条明显提示, 免得对着空页面发懵 -->
    <p v-if="!store.backendOnline" class="alert" style="margin-bottom: 16px">
      ⚠️ {{ store.lastError || '连接不上后端服务' }} ——
      请在 backend 目录执行
      <span class="mono">uvicorn app.main:app --reload</span>
    </p>

    <RouterView />
  </main>
</template>
