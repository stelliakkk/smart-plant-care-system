<script setup>
import { computed } from 'vue'

const props = defineProps({ source: { type: String, default: 'unknown' } })
const kind = computed(() => ['device', 'simulated'].includes(props.source) ? props.source : 'unknown')
const labels = { device: '设备', simulated: '模拟', unknown: '来源未记录' }
const descriptions = {
  device: '设备上报的数据；准确性取决于传感器与标定',
  simulated: '调试生成的数据，不代表实物测量',
  unknown: '旧记录没有保存来源，无法可靠区分设备与模拟数据',
}
</script>

<template>
  <small class="reading-source" :class="kind" :title="descriptions[kind]">{{ labels[kind] }}</small>
</template>

<style scoped>
.reading-source { display:inline-block; margin-left:5px; padding:2px 5px; border-radius:4px; font-size:10px; font-weight:500; line-height:1.3; vertical-align:sub; white-space:nowrap; }
.device { color:#35614c; background:#edf5f0; }
.simulated { color:#8a4900; background:#fff0cc; }
.unknown { color:#59636d; background:#edf0f2; }
</style>
