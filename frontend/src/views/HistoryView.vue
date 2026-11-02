<script setup>
// -*- coding: utf-8 -*-
/**
 * 历史数据页
 * ---------------------------------------------------------------------
 * 按时间展示读数与逐条数据来源；当前采用表格，尚无历史曲线。
 */
import { onMounted, ref } from 'vue'
import ReadingSourceBadge from '@/components/ReadingSourceBadge.vue'

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
    共显示最近 {{ rows.length }} 条记录 · 每条记录单独标记来源
  </p>

  <div class="card">
    <div style="display: flex; align-items: center; flex-wrap: wrap; gap: 12px; margin-bottom: 14px">
      <label class="hint" style="display: flex; align-items: center; gap: 6px">
        取最近
        <input v-model.number="limit" type="number" min="1" max="2000" step="50" />
        条
      </label>
      <button :disabled="loading || !store.backendOnline" @click="load">
        {{ loading ? '加载中...' : '刷新' }}
      </button>
      <span class="hint">时间为上报时间；未提供时使用后端接收时间</span>
    </div>

    <div v-if="!rows.length" class="placeholder">
      还没有历史数据<br />
      <span class="hint">
        启动虚拟设备即可产生数据：
        <span class="mono">python scripts\sim_device.py</span>
      </span>
    </div>

    <div v-else class="table-scroll"><table>
      <thead>
        <tr>
          <th>#</th>
          <th>记录时间</th>
          <th>来源</th>
          <th>相对湿度 %</th>
          <th>EC mS/cm</th>
          <th>探头温度 °C</th>
          <th>光照 lux</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="r in rows" :key="r.id">
          <td class="hint">{{ r.id }}</td>
          <td>{{ fmt(r.ts) }}</td>
          <td><ReadingSourceBadge :source="r.data_source" /></td>
          <td>{{ num(r.soil_moisture) }}</td>
          <td>{{ num(r.soil_ec, 2) }}</td>
          <td>{{ num(r.temperature) }}</td>
          <td>{{ num(r.light_lux, 0) }}</td>
        </tr>
      </tbody>
    </table></div>
  </div>

  <div class="card">
    <h2 class="card-title">读数说明</h2>
    <p class="hint">
      “模拟”表示调试生成的数据；“来源未记录”表示旧记录没有保存来源。
      相对湿度不是绝对含水率；设备当前只上传湿度和光照，EC 与温度留空。
    </p>
  </div>
</template>
