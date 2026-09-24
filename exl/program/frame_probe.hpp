// 探测：哪些候选回调在游戏里每帧被调用。
//
// 画血条需要一个每帧都在主线程上跑的回调——IMGUI 绘制只能在 OnGUI 里做，
// 世界坐标跟随至少要一个 LateUpdate。候选来自 dump.cs，但哪个在实际场景里
// 挂着、是否每帧触发，只能跑起来看。每个探针只计数，首次与每 PROBE_LOG_EVERY
// 次打一行日志，开销可以忽略。确定方案后删掉本文件。

#pragma once

#include "lib.hpp"
#include "offsets.h"

#include <cstdint>

namespace sshud::probe {

constexpr uint32_t PROBE_LOG_EVERY = 600;

inline void countTick(const char* label, uint32_t& counter, const void* self) {
  ++counter;
  if (counter == 1 || counter % PROBE_LOG_EVERY == 0) {
    Logging.Log("[sshud] probe %s calls=%u self=%p", label, counter, self);
  }
}

}  // namespace sshud::probe

// 每个探针一个 trampoline hook；签名都是 void (this, MethodInfo*)
#define SSHUD_FRAME_PROBE(Name, Label)                                  \
  HOOK_DEFINE_TRAMPOLINE(Name) {                                        \
    static inline uint32_t s_count = 0;                                 \
    static void Callback(void* self, const void* method) {              \
      ::sshud::probe::countTick(Label, s_count, self);                  \
      Orig(self, method);                                               \
    }                                                                   \
  }

#ifdef PMGUI_ONGUI
SSHUD_FRAME_PROBE(ProbePlayMakerGuiOnGui, "PlayMakerGUI.OnGUI");
#endif
#ifdef GUIDRAWER_ONGUI
SSHUD_FRAME_PROBE(ProbeGuiDrawerOnGui, "GUIDrawer.OnGUI");
#endif
#ifdef CAMCTRL_LATEUPDATE
SSHUD_FRAME_PROBE(ProbeCameraControllerLateUpdate, "CameraController.LateUpdate");
#endif
#ifdef HERO_LATEUPDATE
SSHUD_FRAME_PROBE(ProbeHeroLateUpdate, "HeroController.LateUpdate");
#endif

namespace sshud::probe {

inline void installFrameProbes() {
#ifdef PMGUI_ONGUI
  ProbePlayMakerGuiOnGui::InstallAtOffset(PMGUI_ONGUI);
#endif
#ifdef GUIDRAWER_ONGUI
  ProbeGuiDrawerOnGui::InstallAtOffset(GUIDRAWER_ONGUI);
#endif
#ifdef CAMCTRL_LATEUPDATE
  ProbeCameraControllerLateUpdate::InstallAtOffset(CAMCTRL_LATEUPDATE);
#endif
#ifdef HERO_LATEUPDATE
  ProbeHeroLateUpdate::InstallAtOffset(HERO_LATEUPDATE);
#endif
  Logging.Log("[sshud] frame probes installed");
}

}  // namespace sshud::probe
