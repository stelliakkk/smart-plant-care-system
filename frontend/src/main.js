// -*- coding: utf-8 -*-
/**
 * 前端入口
 * ---------------------------------------------------------------------
 * 三件事: 装 Pinia(状态管理) -> 装 Router(页面切换) -> 挂载到 #app
 */
import { createApp } from 'vue'
import { createPinia } from 'pinia'

import App from './App.vue'
import router from './router'
import './assets/main.css'

const app = createApp(App)

app.use(createPinia())
app.use(router)

app.mount('#app')
