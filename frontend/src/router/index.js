// -*- coding: utf-8 -*-
/**
 * 路由表
 * ---------------------------------------------------------------------
 * 每个页面对应一个功能, 与 docs/architecture.md 里的"四个页面"一一对应。
 * 用 () => import(...) 懒加载: 进哪个页面才下载哪个页面的代码, 首页更快。
 */
import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  { path: '/', redirect: '/dashboard' },

  {
    path: '/dashboard',
    name: 'dashboard',
    component: () => import('@/views/DashboardView.vue'),
    meta: { title: '实时总览', icon: '📊' },
  },
  {
    path: '/control',
    name: 'control',
    component: () => import('@/views/ControlView.vue'),
    meta: { title: '手动控制', icon: '🎛️' },
  },
  {
    path: '/vision',
    name: 'vision',
    component: () => import('@/views/VisionView.vue'),
    meta: { title: 'AI 诊断', icon: '🔍' },
  },
  {
    path: '/history',
    name: 'history',
    component: () => import('@/views/HistoryView.vue'),
    meta: { title: '历史数据', icon: '📈' },
  },

  // 兜底: 访问不存在的地址就回总览
  { path: '/:pathMatch(.*)*', redirect: '/dashboard' },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
})

// 切换页面时顺便改浏览器标签标题
router.afterEach((to) => {
  document.title = to.meta?.title ? `${to.meta.title} · 植物养护系统` : '植物养护系统'
})

export default router
