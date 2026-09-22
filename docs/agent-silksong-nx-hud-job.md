# Agent Team 分工 — silksong-nx-hud

## 团队组成

| Agent | 模型 | 职责 | 状态 |
|-------|------|------|------|
| `saltynx-research` | Opus | SaltyNX 插件 API、注入机制、SharedMemory、构建工具链 | running |
| `overlay-research` | Opus | libtesla 常驻层、输入穿透、坐标空间、dmnt:cht 读内存 | running |
| `il2cpp-research` | Opus | Il2CppDumper on macOS、NSO RVA 语义、Silksong 类结构 | running |
| 主会话 | Opus | 架构设计、协议、追踪器、测试、集成 | — |

## 分工依据

三项研究彼此无依赖，且都需要抓取并交叉验证多个一手仓库源码，适合并行。
主会话同时推进不依赖研究结论的部分（协议、追踪器、偏移生成器、测试），
避免串行等待。

## 交付物归属

- 研究结论 → `docs/tasks/*.md`
- 代码 → 主会话统一集成，保证风格与协议一致
