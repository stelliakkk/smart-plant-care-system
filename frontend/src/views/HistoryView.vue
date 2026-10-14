<script setup>
// -*- coding: utf-8 -*-
/**
 * 历史数据页
 * ---------------------------------------------------------------------
 * 目前先用表格把数据列出来(能验证接口就够)。
 * 阶段 3 再换成 ECharts 折线图 —— 画图表的活留给专门做图表的那一步。
 */
import { onMounted, ref } from 'vue'

import { fetchHistory } from '@/api/telemetry'
import { useDeviceStore } from '@/store/device'

const store = useDeviceStore()
const rows = ref([])
const loading = ref(false)
const limit = ref(50)

async function load() {
  loading.value = true
  try {
    const res = await fetchHistory(undefined, limit.value)
    // 后端给的是时间正序, 这里倒过来让最新数据在最上面
    rows.value = [...res.data].reverse()
  } catch (e) {
    rows.value = []
  } finally {
    loading.value = false
  }
}

onMounted(load)

function fmt(ts) {
  return ts ? new Date(ts).toLocaleString('zh-CN') : '—'
}

function num(v, digits = 1) {
  return v === null || v === undefined ? '—' : Number(v).toFixed(digits)
}
</script>

<template>
  <h1 class="page-title">历史数据</h1>
  <p class="page-desc">
    共显示最近 {{ rows.length }} 条记录 · 数据存储在
    <span class="mono">backend/data/plant.db</span>
  </p>

  <div class="card">
    <div style="display: flex; align-items: center; gap: 12px; margin-bottom: 14px">
      <label class="hint" style="display: flex; align-items: center; gap: 6px">
        取最近
        <input v-model.number="limit" type="number" min="1" max="2000" step="50" />
        条
      </label>
      <button :disabled="loading || !store.backendOnline" @click="load">
        {{ loading ? '加载中...' : '刷新' }}
      </button>
      <span class="hint">时间列显示的是采集时间</span>
    </div>

    <div v-if="!rows.length" class="placeholder">
      还没有历史数据<br />
      <span class="hint">
        启动虚拟设备即可产生数据：
        <span class="mono">python scripts\sim_device.py</span>
      </span>
    </div>

    <table v-else>
      <thead>
        <tr>
          <th>#</th>
          <th>采集时间</th>
          <th>土壤湿度 %</th>
          <th>盐分 mS/cm</th>
          <th>温度 °C</th>
          <th>光照 lux</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="r in rows" :key="r.id">
          <td class="hint">{{ r.id }}</td>
          <td>{{ fmt(r.ts) }}</td>
          <td>{{ num(r.soil_moisture) }}</td>
          <td>{{ num(r.soil_ec, 2) }}</td>
          <td>{{ num(r.temperature) }}</td>
          <td>{{ num(r.light_lux, 0) }}</td>
        </tr>
      </tbody>
    </table>
  </div>

  <div class="card">
    <h2 class="card-title">📈 下一步</h2>
    <p class="hint">
      阶段 3 会把这张表换成曲线图。为此后端已经准备好接口：
      <span class="mono">GET /api/telemetry/history?limit=200</span>
      （按时间正序返回，正好可以直接喂给 ECharts）。
    </p>
  </div>
</template>
