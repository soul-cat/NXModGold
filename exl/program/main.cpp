// exlaunch 入口 —— Silksong 血条 HUD 与地图收集物标记的 exefs 注入版（Ryujinx 与 Atmosphère 通用）。
//
//   - 由游戏自己的 rtld 以 subsdk9 加载，不依赖任何 sysmodule / overlay。
//   - hook 用 exlaunch 的 trampoline（svcMapProcessMemory 给 .text 建 RW 别名）。
//   - 绘制在游戏进程内用 Unity IMGUI 完成（hud_hooks.hpp / hud_draw.hpp），
//     地图收集物标记克隆游戏自己的标记模板（map_markers.hpp），
//     功能开关除 R3 面板外也做进了游戏原生的「选项 → 游戏」页（options_menu.hpp），
//     送货物品不损坏（delivery_guard.hpp），新存档起始纹章（start_crest.hpp），
//     日志走 svcOutputDebugString 与 SD 卡（Ryujinx 按 ASCII 解码，所以日志文本用英文）。
//
// 时机：rtld 在 main 的入口之前初始化 subsdk，所以 exl_main 跑的时候
// IL2CPP 尚未初始化 —— 这里只装 hook，所有 Unity 调用都发生在 hook 体里（主线程）。
//
// 地址约定：运行时地址 = main 模块基址 + RVA（offsets.h，由 tools/30-gen-offsets.py 生成）。

#include "lib.hpp"
#include "offsets.h"
#include "hud_hooks.hpp"
#include "map_markers.hpp"
#include "delivery_guard.hpp"
#include "options_menu.hpp"
#include "start_crest.hpp"

#include <cstdio>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// Build ID 校验
//
// NSO 头里的 ModuleId 就是链接时的 GNU build-id，它同时以 ELF note 的形式
// 留在 .rodata 里（Atmosphère creport 就是从 .rodata 尾部找 "GNU\0" 取 Build ID）。
// SS_BUILD_ID_STR 是前 8 字节的十六进制，与 nsotool.py buildid 输出一致。
// ---------------------------------------------------------------------------
constexpr uint32_t NOTE_NAME_SIZE = 4;
constexpr uint32_t NOTE_TYPE_GNU_BUILD_ID = 3;
constexpr uint32_t NOTE_DESC_MAX = 0x20;
constexpr size_t NOTE_HEADER_SIZE = 16;  // namesz + descsz + type + "GNU\0"
constexpr size_t BUILD_ID_PREFIX_BYTES = 8;
constexpr size_t NOTE_ALIGN = 4;

bool isBuildIdNote(const uint8_t* at) {
  uint32_t header[3] = {};
  std::memcpy(header, at, sizeof(header));
  return header[0] == NOTE_NAME_SIZE && header[1] >= BUILD_ID_PREFIX_BYTES &&
         header[1] <= NOTE_DESC_MAX && header[2] == NOTE_TYPE_GNU_BUILD_ID &&
         std::memcmp(at + 12, "GNU", NOTE_NAME_SIZE) == 0;
}

/// 返回 build-id 描述符起点；找不到返回 nullptr。从尾部往前扫（note 通常在末尾）。
const uint8_t* findBuildId() {
  const auto& rodata = exl::util::GetMainModuleInfo().m_Rodata;
  const auto* begin = reinterpret_cast<const uint8_t*>(rodata.m_Start);
  const size_t span = NOTE_HEADER_SIZE + BUILD_ID_PREFIX_BYTES;
  if (rodata.m_Size < span) return nullptr;

  for (size_t off = (rodata.m_Size - span) & ~(NOTE_ALIGN - 1);; off -= NOTE_ALIGN) {
    if (isBuildIdNote(begin + off)) return begin + off + NOTE_HEADER_SIZE;
    if (off == 0) return nullptr;
  }
}

/// Build ID 一变，所有 RVA 全部作废。不匹配就什么都不做。
bool verifyBuildId() {
  const uint8_t* buildId = findBuildId();
  if (buildId == nullptr) {
    Logging.Log("[sshud] GNU build-id note not found in .rodata, giving up");
    return false;
  }
  char actual[BUILD_ID_PREFIX_BYTES * 2 + 1] = {};
  for (size_t i = 0; i < BUILD_ID_PREFIX_BYTES; ++i) {
    std::snprintf(actual + i * 2, 3, "%02X", buildId[i]);
  }
  const bool isMatch = std::strncmp(actual, SS_BUILD_ID_STR, sizeof(actual) - 1) == 0;
  Logging.Log("[sshud] build id %s, expected %s -> %s", actual, SS_BUILD_ID_STR,
              isMatch ? "match" : "MISMATCH, hooks not installed");
  return isMatch;
}

}  // namespace

// 打印 rtld 的模块表。皮肤 Mod 的预启动 IPS 把游戏调用点写死跳到
// main + 0x8A3A000 + hook_offset；两个 exefs 模块共存时用它确认皮肤模块
// 仍然落在原来的地址上。日志先进内存缓冲，首个 LateUpdate 挂上 SD 卡后落盘。
void logModuleLayout() {
  using exl::util::ModuleIndex;
  for (int i = static_cast<int>(ModuleIndex::Start); i < static_cast<int>(ModuleIndex::End); ++i) {
    const auto index = static_cast<ModuleIndex>(i);
    if (!exl::util::HasModule(index)) continue;
    const auto& module = exl::util::GetModuleInfo(index);
    const auto name = module.GetModuleName();
    Logging.Log("[sshud] module idx=%d base=0x%lx text=0x%lx/%lu rodata=0x%lx data=0x%lx name=%.*s",
                i, static_cast<unsigned long>(module.m_Total.m_Start),
                static_cast<unsigned long>(module.m_Text.m_Start),
                static_cast<unsigned long>(module.m_Text.m_Size),
                static_cast<unsigned long>(module.m_Rodata.m_Start),
                static_cast<unsigned long>(module.m_Data.m_Start),
                static_cast<int>(name.size()), name.data());
  }
}

extern "C" void exl_main(void* x0, void* x1) {
  (void)x0;
  (void)x1;
  exl::hook::Initialize();

  const auto& mainModule = exl::util::GetMainModuleInfo();
  Logging.Log("[sshud] loaded, main @ 0x%lx (.text 0x%lx bytes)", mainModule.m_Total.m_Start,
              mainModule.m_Text.m_Size);
  if (!verifyBuildId()) return;
  logModuleLayout();

  sshud::hud::installHooks();
  sshud::map::installHooks();
  sshud::options::installHooks();
  sshud::delivery::installHooks();
  sshud::crest::installHooks();
  sshud::bossrush::installHooks();
}

extern "C" NORETURN void exl_exception_entry() {
  // 只有 applet/sysmodule 场景会走到这里；应用里不会被调用。
  EXL_ABORT("sshud: 未预期的异常入口");
}
