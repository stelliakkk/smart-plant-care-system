<script setup>
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { useDeviceStore } from '@/store/device'
import { fetchControlLog, stopAllOutputs, fetchAutomation, setAutomation } from '@/api/device'
const store = useDeviceStore()
const actuators = [
  {key:'pump',name:'补水泵',icon:'💧',preset:3000,max:30000},
  {key:'fertilizer',name:'盐水/施肥泵',icon:'🧪',preset:2000,max:30000},
  {key:'grow_light',name:'补光灯',icon:'💡',preset:5000,max:600000},
  {key:'spray',name:'农药泵',icon:'🌫️',preset:1500,max:30000},
]
const durations=ref(Object.fromEntries(actuators.map(a=>[a.key,a.preset])))
const busy=ref('')
const message=ref('')
const records=ref([])
const automation=ref({events:[]})
async function changeMode(actuator, enabled) {
  busy.value=actuator+"-mode"
  try { automation.value=await setAutomation(actuator, enabled);message.value="模式已保存，等待设备同步";await refresh() }
  catch {message.value="模式修改失败，请检查后端"}
  finally {busy.value=""}
}
const labels={pending:'等待板子确认',running:'输出已开启',done:'输出已关闭',rejected:'板子拒绝',failed:'执行失败',timeout:'回执超时，状态未知'}
const reasons={duration_elapsed:'运行到时，已关闭',stopped_by_command:'收到关闭指令',output_on:'输出设置成功',output_off:'输出关闭成功',mqtt_disconnected:'网络断开，已请求关闭',actuator_busy:'该输出正在运行，请先关闭',invalid_duration:'运行时长不合法',command_expired:'指令已过期',dedupe_capacity:'指令处理繁忙，请稍后重试',output_start_failed:'设置输出失败',output_stop_failed:'关闭输出失败'}
let timer
let fetching=false
let disposed=false
async function refresh() {
  if(fetching) return
  fetching=true
  try {
    await store.refreshAll()
    const policy=await fetchAutomation()
    if(!disposed) automation.value=policy
    const res=await fetchControlLog()
    if(!disposed) records.value=res.items
  } catch { if(!disposed) message.value='操作记录加载失败，请检查后端' }
  finally {fetching=false}
}
async function fire(a,action) {
  const duration=action==='on'?durations.value[a.key]:0
  if(!Number.isInteger(duration)||duration<0||duration>a.max||(action==='on'&&a.key!=='grow_light'&&duration===0)) {
    message.value=a.key==='grow_light'?'补光灯时长必须为 0～600000 的整数':'泵时长必须为 1～30000 的整数';return
  }
  busy.value=a.key+'-'+action
  try {
    const res=await store.control(a.key,action,duration)
    message.value=res.sent?'已发送，等待 S3 回执，请查看操作记录':(res.reason||'指令未发出')
    await refresh()
  } catch(e) {message.value=e.response?.data?.detail||'请求失败，请检查后端'}
  finally {busy.value=''}
}
async function stopAll() {
  busy.value='all-off'
  try {
    const res=await stopAllOutputs()
    message.value=res.sent?'四路关闭指令已发送，等待板子确认':'部分或全部关闭指令未发出，请检查设备连接'
    await refresh()
  } catch {message.value='关闭请求失败，请检查设备连接'}
  finally {busy.value=''}
}
function outputState(a) {
  if(!store.deviceOnline) return '设备离线，实际状态未知'
  const state=store.status?.actuators?.[a.key]
  return state===true?'板子报告：开启':state===false?'板子报告：关闭':'等待板子状态'
}
onMounted(async()=>{await refresh();if(!disposed)timer=setInterval(refresh,2000)})
onBeforeUnmount(()=>{disposed=true;clearInterval(timer)})
</script>

<template>
  <h1 class="page-title">手动与自动控制</h1>
  <div class="card">
    <h2 class="card-title">自动补光与喷洒</h2>
    <p v-if="!automation.automation_ready" class="alert">S3 离线或尚未烧录自动控制固件；设置会保存，设备恢复后同步。</p>
    <div v-for="a in ['grow_light','spray']" :key="a" class="controls">
      <strong>{{ a==='grow_light'?'补光灯':'农药泵' }}</strong>
      <span>设置：{{ automation[a==='grow_light'?'light_auto':'spray_auto']?'自动':'暂停 / 手动' }}</span>
      <span>板子：{{ !automation.automation_ready?'未知':automation.board_modes?.[a]?'自动':'手动' }}</span>
      <button :disabled="busy!=='' || !store.backendOnline" @click="changeMode(a,true)">恢复自动</button>
      <button :disabled="busy!=='' || !store.backendOnline" @click="changeMode(a,false)">暂停自动</button>
    </div>
    <p>光照：{{ automation.light_lux ?? '—' }} lux · 低于 3000 lux 持续 10 秒开灯；高于 4000 lux 持续 30 秒关灯。</p>
    <p>补光判断：{{ !automation.light_auto?'自动暂停':automation.light_fault?'传感器异常':automation.light_lux<3000?'低光照，累计 10 秒后开启':automation.light_lux>4000?'高光照，累计 30 秒后关闭':'光照处于保持区间' }}</p>
    <p v-if="automation.light_fault" class="alert">{{ automation.light_fault }}</p>
    <p>虫害连续检测：{{ automation.count || 0 }}/2 · 自动喷洒 2 秒</p>
    <p>冷却：{{ automation.cooldown_remaining_sec>0?Math.ceil(automation.cooldown_remaining_sec/60)+' 分钟剩余':'无冷却' }} <span v-if="automation.cooldown_remaining_sec>0">（截止 {{ new Date(automation.cooldown_until).toLocaleString() }}）</span></p>
    <p>{{ automation.last_reason }}</p>
    <p v-if="automation.spray_command_status">最近喷洒回执：{{ labels[automation.spray_command_status.result] || automation.spray_command_status.result }}</p>
    <p class="hint">手动操作暂停对应自动模式；手动喷洒也开始冷却。恢复自动不会清除冷却。冷却期间每 5 分钟拍照分析继续。</p>
  </div>
  <p class="page-desc">当前设备：{{ store.health?.device_id || 'edge01' }} · {{ store.deviceOnline?'在线':'离线' }}</p>
  <p v-if="store.deviceOnline && !store.status?.control_ready" class="alert">当前板上固件尚未启用网页控制，请先烧录新版 S3 固件。</p>
  <p v-if="message" class="alert">{{ message }}</p>
  <button style="margin-bottom:20px" :disabled="busy!=='' || !store.backendOnline" @click="stopAll">关闭全部输出</button>
  <div class="grid grid-2">
    <div v-for="a in actuators" :key="a.key" class="card">
      <h2 class="card-title">{{ a.icon }} {{ a.name }}</h2>
      <p>{{ outputState(a) }}</p>
      <div class="controls">
        <button class="primary" :disabled="busy!=='' || !store.deviceOnline || !store.mqttConnected || !store.status?.control_ready" @click="fire(a,'on')">开启</button>
        <button :disabled="busy!=='' || !store.backendOnline" @click="fire(a,'off')">关闭</button>
        <label>运行 <input v-model.number="durations[a.key]" type="number" :min="a.key==='grow_light'?0:1" :max="a.max" step="500" /> ms</label>
      </div>
      <p class="hint">{{ a.key==='grow_light'?'0 表示持续开启；定时最多 10 分钟。':'单次最多 30 秒，必须填写正整数时长。' }}</p>
    </div>
  </div>
  <div class="card">
    <h2 class="card-title">自动判断记录</h2>
    <div class="log-scroll"><table><thead><tr><th>时间</th><th>判断 / 未执行原因</th><th>照片</th></tr></thead><tbody>
    <tr v-for="(event,i) in automation.events" :key="i"><td>{{ new Date(event.ts).toLocaleString() }}</td><td>{{ event.reason }}</td><td><RouterLink v-if="event.record_id" :to="{path:'/vision',query:{record:event.record_id}}">记录 #{{ event.record_id }}</RouterLink></td></tr>
    </tbody></table></div>
  </div>
  <div class="card">
    <h2 class="card-title">操作记录</h2>
    <p class="hint">“已发送”只表示提交到 MQTT；结果由 S3 回执确认。输出状态来自驱动设置，尚未测量水泵流量或灯具亮度。断开 MQTT 后关闭泵和手动灯光；自动补光仍按本地传感器运行。</p>
    <div class="log-scroll">
      <table><thead><tr><th>时间</th><th>设备输出</th><th>操作</th><th>结果</th><th>说明</th></tr></thead>
        <tbody><tr v-for="row in records" :key="row.command_id">
          <td>{{ new Date(row.ts).toLocaleString() }}</td>
          <td>{{ actuators.find(a=>a.key===row.actuator)?.name || row.actuator }}</td>
          <td>{{ row.action==='on'?`开启 ${row.duration_ms} ms`:row.action==='auto_on'?'恢复自动':row.action==='auto_off'?'暂停自动':'关闭' }}</td>
          <td>{{ labels[row.result] || row.result }}</td>
          <td>{{ reasons[row.reason] || row.reason }}</td>
        </tr></tbody>
      </table>
      <p v-if="!records.length" class="hint">暂无操作记录</p>
    </div>
  </div>
</template>
<style scoped>
.controls {display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:10px}
.controls label {display:flex;align-items:center;gap:6px}
.log-scroll {overflow-x:auto}
</style>
