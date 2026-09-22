# 任务：怪物血条 HUD v1

## 目标（可验证）

1. 在 Switch 上打敌人时，屏幕固定位置出现血条 + `当前HP/最大HP` + 伤害数字
2. Boss（maxHp ≥ 105）用顶部独立样式
3. 4 秒无受击后血条淡出

**验证方式**：实机打一个已知血量的敌人，overlay 显示的数值与金手指
`Infinite HP` 关闭时的实际掉血一致。

## 范围决策：v1 不做世界坐标跟随

跟随怪物头顶需要每帧拿到敌人的屏幕坐标，路径是
`HealthManager → Component.get_gameObject → get_transform → get_position`，
其中 `Transform.get_position` 在 IL2CPP 里是 icall，要正确调用需要
`get_position_Injected(void* native, Vector3* out)` 的地址与 Unity 原生对象布局，
脆弱且随 Unity 小版本变化。

PC 端三个血条 mod 里有两个（`Enemy Health Bars`、`BestHealthBar`）本来就是
屏幕固定位置。v1 对齐它们，把世界坐标留给 v2。

协议已预留 `screenX/screenY` + `SSHUD_FLAG_POS_VALID`，v2 不需要改协议。

## 步骤 → 验证

| # | 步骤 | 验证 |
|---|------|------|
| 1 | 共享内存协议 | ✅ 结构体大小 < 4096，seqlock 测试通过 |
| 2 | 追踪器 | ✅ `make -C tests test` 10/10 |
| 3 | 偏移生成器 | ✅ fixture 提取 13 个符号正确 |
| 4 | 用户侧转储 + IL2CPP dump | `dump.cs` 中存在 `class HealthManager` 且有 `hp` 字段 |
| 5 | SaltyNX 插件骨架 | 插件加载后 `hookStatus` 的 `INIT_OK` 位被置起 |
| 6 | TakeDamage hook | 打敌人时共享内存 `entityCount > 0` |
| 7 | Tesla overlay 渲染 | 屏幕上出现血条 |
| 8 | 常驻 + 输入穿透 | 血条常显且手柄操作不受影响 |

当前进度：步骤 1–3 完成，步骤 4 需要用户提供转储。

## 风险

- **Build ID 绑定**：游戏每次更新所有 RVA 全变。插件启动时校验 Build ID，
  不匹配直接 bail，不得盲目 patch。
- **Switch 2 不适用**：目前无公开 CFW。
- **联机封号**：装任何 CFW 插件后不要联网。
