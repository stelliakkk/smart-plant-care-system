<script setup>
import { useRoute } from 'vue-router'
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { analyzeNow, fetchCameraStatus, fetchLatestVision, fetchVisionHistory, fetchVisionRecord, imageUrl } from '@/api/vision'
const route=useRoute()
const camera = ref(null)
const latest = ref(null)
const records = ref([])
const selected = ref(null)
const cursor = ref(null)
const busy = ref(false)
const submitting = ref(false)
const loadingHistory = ref(false)
const message = ref('')
const shown = computed(() => selected.value?.id === latest.value?.id ? latest.value : selected.value || latest.value)
const result = computed(() => shown.value?.result)
const states = { capturing: '正在拍照', analyzing: '正在分析', done: '分析完成', error: '任务失败' }
let timer
let refreshing = false
let disposed = false
async function refresh() {
  if (refreshing) return
  refreshing = true
  try {
    const [status, recent] = await Promise.all([fetchCameraStatus(), fetchLatestVision()])
    if (disposed) return
    camera.value = status
    busy.value = recent.busy
    latest.value = recent.data
    if (recent.data && (!records.value.length || recent.data.id !== records.value[0].id)) {
      const page = await fetchVisionHistory()
      if (!disposed) { records.value = page.items; cursor.value = page.next_cursor }
    } else if (recent.data && records.value.length) {
      records.value[0] = recent.data
    }
  } catch {
    if (!disposed) { camera.value = null; message.value = '无法连接后端，请检查服务' }
  } finally { refreshing = false }
}
async function run() {
  submitting.value = true
  message.value = ''
  selected.value = null
  try {
    await analyzeNow()
    busy.value = true
    message.value = '已开始拍照，照片和分析结果会自动更新'
    await refresh()
  } catch (error) { message.value = error.response?.data?.detail || '请求失败，请检查后端服务' }
  finally { submitting.value = false }
}
async function more() {
  if (loadingHistory.value || !cursor.value) return
  loadingHistory.value = true
  try {
    const page = await fetchVisionHistory(cursor.value)
    records.value = [...records.value, ...page.items.filter(item => !records.value.some(old => old.id === item.id))]
    cursor.value = page.next_cursor
  } catch { message.value = '历史记录加载失败，请重试' }
  finally { loadingHistory.value = false }
}
onMounted(async () => {
  await refresh()
  if(route.query.record) {
    const target=Number(route.query.record)
    try { selected.value=await fetchVisionRecord(target) }
    catch {message.value="关联照片不存在或已过期"}
  }
  if (!disposed) timer = setInterval(refresh, 3000)
})
onBeforeUnmount(() => { disposed = true; clearInterval(timer) })
</script>

<template>
  <h1 class="page-title">植物照片与 AI 建议</h1>
  <p class="page-desc">每 {{ Math.round((camera?.capture_interval_sec || 300) / 60) }} 分钟拍摄一张照片并分析，照片与结果保留 {{ camera?.retention_days || 7 }} 天。</p>
  <p v-if="message" class="alert">{{ message }}</p>
  <p v-if="camera && !camera.ai_configured" class="alert">尚未配置 DeepSeek API Key。拍照仍会保存；请在后端 .env 中填写密钥并重启后端。</p>
  <div class="grid grid-2">
    <div class="card">
      <h2 class="card-title">📷 {{ selected ? '历史照片' : '最近一次拍摄' }}</h2>
      <a v-if="shown?.image_url" :href="imageUrl(shown.image_url)" target="_blank" rel="noopener">
        <img class="still-photo" :src="imageUrl(shown.image_url)" alt="植物抓拍照片，点击查看原图" />
      </a>
      <div v-else class="placeholder">{{ shown ? (states[shown.status] || '暂无照片') : '暂无照片，可点击下方按钮拍摄' }}</div>
      <p class="hint">相机：{{ camera?.online ? '在线' : '离线' }} · {{ busy ? '任务执行中' : '等待拍摄' }}</p>
      <p v-if="shown" class="hint">{{ new Date(shown.ts).toLocaleString() }}<template v-if="shown.image_url"> · {{ shown.width }} × {{ shown.height }} · {{ Math.round(shown.byte_size / 1024) }} KB</template></p>
      <button class="primary" :disabled="submitting || busy || !camera?.online" @click="run">{{ busy || submitting ? '拍摄分析中…' : '立即抓拍并分析' }}</button>
      <button v-if="selected" style="margin-left:12px" @click="selected = null">返回最新照片</button>
    </div>
    <div class="card">
      <h2 class="card-title">🔍 分析建议</h2>
      <p v-if="shown?.error" class="alert">{{ shown.error }}</p>
      <template v-if="result">
        <p><strong>{{ result.diagnosis }}</strong></p>
        <p>是否发现异常：{{ result.has_issue === null ? '无法判断' : result.has_issue ? '有可疑异常' : '未见明显异常' }}</p>
        <p>虫害判断：{{ {detected:'明确检测到虫害',suspected:'疑似虫害',none:'未见虫害',unknown:'无法判断'}[result.pest_status] || '旧结果，无专门虫害判定' }}</p>
        <p v-if="result.pest_evidence">虫害依据：{{ result.pest_evidence }}</p>
        <p>可见特征：{{ result.visible_signs }}</p>
        <p>可能原因：{{ result.possible_causes }}</p>
        <p>养护建议：{{ result.advice }}</p>
        <p>不确定性：{{ result.uncertainty }}</p>
        <p class="hint">模型：{{ shown.model }} · 建议供观察参考；自动喷洒是否执行请查看控制页的模式与记录。</p>
      </template>
      <div v-else class="placeholder">{{ shown ? (states[shown.status] || '暂无分析结果') : '尚无分析结果' }}</div>
    </div>
  </div>
  <div class="card">
    <h2 class="card-title">最近 7 天的拍摄记录</h2>
    <p v-if="!records.length" class="hint">暂无记录</p>
    <div class="photo-history">
      <button v-for="record in records" :key="record.id" class="photo-record" @click="selected = record">
        <img v-if="record.image_url" :src="imageUrl(record.image_url)" alt="历史植物照片" loading="lazy" />
        <span>{{ new Date(record.ts).toLocaleString() }}</span>
        <span>{{ states[record.status] || '历史记录' }}</span>
      </button>
    </div>
    <button v-if="cursor" :disabled="loadingHistory" @click="more">{{ loadingHistory ? '加载中…' : '加载更早记录' }}</button>
  </div>
</template>

<style scoped>
.still-photo { display:block; width:100%; height:auto; max-height:480px; object-fit:contain; border-radius:12px; background:#f4f7f5; }
.photo-history { display:grid; grid-template-columns:repeat(auto-fill,minmax(160px,1fr)); gap:12px; margin-bottom:16px; }
.photo-record { display:flex; flex-direction:column; gap:8px; text-align:left; padding:10px; background:#f5f8f6; border:1px solid #dce5df; border-radius:10px; cursor:pointer; }
.photo-record img { width:100%; height:120px; object-fit:cover; border-radius:6px; }
</style>

