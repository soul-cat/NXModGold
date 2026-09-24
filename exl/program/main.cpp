// exlaunch 入口 —— Silksong 血条 HUD 的 exefs 注入版（Ryujinx 与 Atmosphère 通用）。
//
// 与 plugin/（SaltyNX 版）的区别：
//   - 由游戏自己的 rtld 以 subsdk9 加载，不依赖 SaltyNX / Tesla，模拟器能跑。
//   - hook 用 exlaunch 的 trampoline（svcMapProcessMemory 给 .text 建 RW 别名）。
//   - 绘制在游戏进程内用 Unity IMGUI 完成（hud_hooks.hpp / hud_draw.hpp），
//     日志走 svcOutputDebugString（Ryujinx 按 ASCII 解码，所以日志文本用英文）。
//
// 时机：rtld 在 main 的入口之前初始化 subsdk，所以 exl_main 跑的时候
// IL2CPP 尚未初始化 —— 这里只装 hook，所有 Unity 调用都发生在 hook 体里（主线程）。
//
// 地址约定与 plugin/ 相同：运行时地址 = main 模块基址 + RVA（offsets.h）。

#include "lib.hpp"
#include "offsets.h"
#include "hud_hooks.hpp"

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

extern "C" void exl_main(void* x0, void* x1) {
  (void)x0;
  (void)x1;
  exl::hook::Initialize();

  const auto& mainModule = exl::util::GetMainModuleInfo();
  Logging.Log("[sshud] loaded, main @ 0x%lx (.text 0x%lx bytes)", mainModule.m_Total.m_Start,
              mainModule.m_Text.m_Size);
  if (!verifyBuildId()) return;

  sshud::hud::installHooks();
}

extern "C" NORETURN void exl_exception_entry() {
  // 只有 applet/sysmodule 场景会走到这里；应用里不会被调用。
  EXL_ABORT("sshud: 未预期的异常入口");
}
