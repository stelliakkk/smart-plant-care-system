<script setup>
// -*- coding: utf-8 -*-
/**
 * 单个传感器数值卡片
 * ---------------------------------------------------------------------
 * 只负责显示。数值为空时显示 "--" 而不是 0,
 * 因为"传感器没接"和"读数是 0"是两回事, 不能混淆。
 */
import { computed } from 'vue'

const props = defineProps({
  label: { type: String, required: true },
  value: { type: [Number, String], default: null },
  unit: { type: String, default: '' },
  icon: { type: String, default: '📟' },
  hint: { type: String, default: '' },
  digits: { type: Number, default: 1 },
})

const hasValue = computed(
  () => props.value !== null && props.value !== undefined && props.value !== '',
)

const text = computed(() => {
  if (!hasValue.value) return '--'
  return typeof props.value === 'number' ? props.value.toFixed(props.digits) : props.value
})
</script>

<template>
  <div class="sensor card">
    <div class="head">
      <span class="icon">{{ icon }}</span>
      <span class="label">{{ label }}</span>
    </div>
    <div class="value" :class="{ empty: !hasValue }">
      {{ text }}<span v-if="hasValue" class="unit">{{ unit }}</span>
    </div>
    <div class="hint">{{ hint }}</div>
  </div>
</template>

<style scoped>
.sensor {
  padding: 14px 16px;
}

.head {
  display: flex;
  align-items: center;
  gap: 6px;
  color: var(--muted);
  font-size: 13px;
}

.icon {
  font-size: 15px;
}

.value {
  font-size: 26px;
  font-weight: 700;
  margin: 6px 0 2px;
  color: var(--primary-dark);
  line-height: 1.2;
}

.value.empty {
  color: #cbd5d1;
}

.unit {
  font-size: 13px;
  font-weight: 500;
  color: var(--muted);
  margin-left: 3px;
}

.hint {
  font-size: 12px;
  color: #9ca3af;
}
</style>
