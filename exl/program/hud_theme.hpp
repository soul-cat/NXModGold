// 启动时判断皮肤 Mod 是否已加载，并选择血条主题。
//
// 皮肤 Mod 通过自己的 subsdk 模块 SilkModLoader 注入；rtld 在 main 入口之前就把
// 所有 exefs 模块映射好了，所以 exl_main 里扫模块表即可。这个 title 的实测布局：
//   idx3 Subsdk1 = SilkModLoader，idx4 Subsdk2 = sshud（有皮肤时）
//   idx?  Subsdk1 = sshud（没有皮肤时）
// 模块名通常可用（GetModuleName()）；拿不到时用「Subsdk1 不是自己」兜底。

#pragma once

#include "hud_theme_model.hpp"
#include "lib.hpp"

#include <lib/util/module_index.hpp>
#include <lib/util/sys/modules.hpp>

#include <string_view>

namespace sshud::hud {

inline bool hasSkinLoaderModule() {
  using exl::util::ModuleIndex;

  for (int value = static_cast<int>(ModuleIndex::Start);
       value < static_cast<int>(ModuleIndex::End); ++value) {
    const auto index = static_cast<ModuleIndex>(value);
    if (!exl::util::HasModule(index)) continue;
    const auto name = exl::util::GetModuleInfo(index).GetModuleName();
    if (name.find("SilkModLoader") != std::string_view::npos) return true;
  }

  // RTLD 的模块名偶尔为空。这个 title 里注入的第一个 subsdk 固定落在 Subsdk1；
  // 如果它不是本模块，就是皮肤运行时。
  if (exl::util::HasModule(ModuleIndex::Subsdk1)) {
    const auto& other = exl::util::GetModuleInfo(ModuleIndex::Subsdk1);
    const auto& self = exl::util::GetSelfModuleInfo();
    if (other.m_Total.m_Start != self.m_Total.m_Start) return true;
  }
  return false;
}

inline void selectThemeAtStartup() {
  const bool skin = hasSkinLoaderModule();
  setTheme(selectTheme(skin));
  Logging.Log("[sshud] bar theme: %s (SilkModLoader %s)", skin ? "skin" : "default",
              skin ? "detected" : "not found");
}

}  // namespace sshud::hud
