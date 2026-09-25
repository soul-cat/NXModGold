// 血条 HUD 的三个 hook：
//   1. HealthManager.TakeDamage —— 打前/打后快照算实际掉血（与 PC 版一致），
//      同时处理 sendDamageTo 伤害重定向；只记玩家造成的伤害。
//   2. CameraController.LateUpdate —— 每帧确保 PlayMakerGUI 存在（它的 OnGUI 是绘制入口），
//      读开关面板输入；首帧打开 SD 卡日志，按需导出整个存档，再导入 SD 上待导入的存档（此时游戏的 fs 与 save 挂载都已就绪）。
//   3. PlayMakerGUI.OnGUI —— 原函数之后画 HUD。
// 三者都跑在 Unity 主线程，共享的 HudModel 无需加锁。

#pragma once

#include "boss_detect.hpp"
#include "hud_draw.hpp"
#include "hud_model.hpp"
#include "lib.hpp"
#include "save_export.hpp"
#include "save_import.hpp"
#include "sd_logger.hpp"
#include "toggles.hpp"
#include "unity_api.hpp"

namespace sshud::hud {

inline HudModel g_model;

// 逐次命中日志：仅调研用，每行都是一次 SD 写入 + 额外的托管读取，战斗中会卡，平时关闭
constexpr bool LOG_HITS = false;

struct HpSnapshot {
  const void* manager = nullptr;
  int32_t hp = 0;
};

inline HpSnapshot snapshotOf(const void* manager) {
  if (!unity::isAlive(manager)) return {};
  return {manager, unity::readField<int32_t>(manager, HM_HP)};
}

/// size / big / kills：调研"怎样才算 BOSS"用（PC 版只看血量 > 105，大怪也会被当成 BOSS）
inline void logHit(const HpSnapshot& before, int32_t after, int32_t maxHp) {
  const uint8_t* record = journalRecordOf(before.manager);
  Logging.Log("[sshud] hit hm=%p hp %d -> %d / %d dmg %d boss=%d size=%d big=%d kills=%d", before.manager,
              before.hp, after, maxHp, before.hp - after, isBoss(maxHp) ? 1 : 0,
              unity::readField<int32_t>(before.manager, HM_ENEMYSIZE),
              unity::readField<bool>(before.manager, HM_BIGENEMYDEATH) ? 1 : 0,
              record != nullptr ? unity::readField<int32_t>(record, EJR_KILLSREQUIRED) : -1);
}

/// 原函数执行后调用：血量确有下降才记账
inline void recordDamage(const HpSnapshot& before, float now) {
  if (before.manager == nullptr || !unity::isAlive(before.manager)) return;
  const int32_t after = unity::readField<int32_t>(before.manager, HM_HP);
  const int32_t damage = before.hp - after;
  if (damage <= 0) return;
  const int32_t maxHp = std::max(unity::readField<int32_t>(before.manager, HM_INITHP), before.hp);
  g_model.onHit(before.manager, maxHp, now);
  const unity::Vector3 world = unity::worldPositionOf(before.manager);
  g_model.addPopup({world.x, world.y + POPUP_START_OFFSET_UNITS, world.z}, damage, now);
  if constexpr (LOG_HITS) logHit(before, after, maxHp);
}

}  // namespace sshud::hud

// IL2CPP 签名：void (HealthManager* self, HitInstance* hit, const MethodInfo* method)
//   HitInstance 是大号值类型，经隐藏指针传（x1）；x2 的 MethodInfo* 原样转发。
HOOK_DEFINE_TRAMPOLINE(TakeDamageHook) {
  static void Callback(void* self, void* hit, const void* method) {
    namespace hud = sshud::hud;
    const bool isHeroDamage = hit != nullptr && sshud::unity::readField<bool>(hit, HIT_ISHERODAMAGE);
    const void* target = self != nullptr ? sshud::unity::readField<const void*>(self, HM_SENDDAMAGETO) : nullptr;
    const hud::HpSnapshot selfBefore = hud::snapshotOf(self);
    const hud::HpSnapshot targetBefore = target != self ? hud::snapshotOf(target) : hud::HpSnapshot{};
    Orig(self, hit, method);
    if (!isHeroDamage) return;
    const float now = sshud::unity::unscaledTime();
    hud::recordDamage(selfBefore, now);
    hud::recordDamage(targetBefore, now);
  }
};

HOOK_DEFINE_TRAMPOLINE(CameraLateUpdateHook) {
  static inline bool s_announced = false;
  static void Callback(void* self, const void* method) {
    Orig(self, method);
    sshud::sdlog::start();
    sshud::sdlog::flushIfDue(sshud::unity::unscaledTime());
    sshud::saveexport::runOnce();  // 先导出再导入：导出的是导入前的原样存档
    sshud::saveimport::runOnce();
    sshud::toggles::pollInput();
    sshud::hud::scanForBosses(sshud::hud::g_model, sshud::unity::unscaledTime());
    void* gui = sshud::unity::ensurePlayMakerGui();
    if (!s_announced && gui != nullptr) {
      s_announced = true;
      Logging.Log("[sshud] PlayMakerGUI ready @ %p", gui);
    }
  }
};

HOOK_DEFINE_TRAMPOLINE(PlayMakerOnGuiHook) {
  static inline bool s_announced = false;
  static void Callback(void* self, const void* method) {
    Orig(self, method);
    if (!s_announced) {
      s_announced = true;
      Logging.Log("[sshud] first OnGUI, drawing enabled");
    }
    sshud::hud::drawHud(sshud::hud::g_model, sshud::toggles::g_settings, sshud::toggles::g_isMenuOpen);
  }
};

namespace sshud::hud {

inline void installHooks() {
  TakeDamageHook::InstallAtOffset(HM_TAKEDAMAGE);
  CameraLateUpdateHook::InstallAtOffset(CAMCTRL_LATEUPDATE);
  PlayMakerOnGuiHook::InstallAtOffset(PMGUI_ONGUI);
  BossTitleHook::InstallAtOffset(DBT_ONENTER);
  Logging.Log("[sshud] hooks installed: TakeDamage, CameraController.LateUpdate, PlayMakerGUI.OnGUI, DisplayBossTitle");
}

}  // namespace sshud::hud
