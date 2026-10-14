// -*- coding: utf-8 -*-
/**
 * WebSocket 封装(带自动重连)
 * ---------------------------------------------------------------------
 * 为什么必须自动重连:
 *   用户合上笔记本、切个 WiFi、或者后端重启一下, 连接就断了。
 *   没有重连的话页面会永远"静止"在那里, 看起来像坏了。
 *
 * 重连用退避策略 1s -> 2s -> 4s -> 8s -> 15s(封顶):
 *   断网时不要疯狂重试, 那只会让情况更糟。
 */
export function createSocket({ onOpen, onClose, onMessage } = {}) {
  // 留空时自动用当前页面地址(开发时由 vite 代理转发到后端)
  const configured = import.meta.env.VITE_WS_URL
  const origin =
    configured ||
    `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}`
  const url = `${origin}/ws`

  let ws = null
  let timer = null
  let retry = 1000
  let manualClose = false

  function connect() {
    manualClose = false
    try {
      ws = new WebSocket(url)
    } catch (e) {
      console.warn('[WS] 创建失败:', e)
      scheduleRetry()
      return
    }

    ws.onopen = () => {
      retry = 1000 // 连上了就把退避重置
      console.info('[WS] 已连接', url)
      onOpen?.()
    }

    ws.onmessage = (ev) => {
      try {
        onMessage?.(JSON.parse(ev.data))
      } catch (e) {
        console.warn('[WS] 收到非 JSON 报文, 已忽略:', ev.data)
      }
    }

    ws.onclose = () => {
      onClose?.()
      if (!manualClose) scheduleRetry()
    }

    ws.onerror = () => {
      // 出错后浏览器一定会触发 onclose, 重连统一在 onclose 里做
    }
  }

  function scheduleRetry() {
    clearTimeout(timer)
    timer = setTimeout(() => {
      console.info(`[WS] ${retry / 1000}s 后重连...`)
      connect()
    }, retry)
    retry = Math.min(retry * 2, 15000)
  }

  function close() {
    manualClose = true
    clearTimeout(timer)
    ws?.close()
    ws = null
  }

  function send(data) {
    if (ws?.readyState === WebSocket.OPEN) {
      ws.send(typeof data === 'string' ? data : JSON.stringify(data))
      return true
    }
    return false
  }

  return { connect, close, send }
}
