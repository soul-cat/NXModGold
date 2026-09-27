// BOSS 挑战的 BOSS 表（纯数据，不依赖 Unity / nn）。
//
// 每一项：从哪个场景的哪个门进、进之前把哪些 PlayerData 标记改成什么、到场后做哪些修正
// （激活物体 / 给 BOSS 状态机发事件 / 让状态机跳到某个状态）。
// 标记来自场景资源包（tools/80-scan-boss-scenes.py 列出场景检查的字段），对照 100% 存档（4 号位）的值：
//   - defeated* 为 true 时 BOSS 战场景被停用 → 改成 false；
//   - encountered* 保持 true：跳过初见演出，进门直接开打；
//   - blackThreadWorld：100% 存档已在第三幕（true），第一、二幕的 BOSS 在「普通世界」布景里 → 改成 false，
//     第三幕的 BOSS 改成 true。
// 场景持久标记（SceneData.persistentBools，按 场景 + 物体名 记）也要改：例如 Tut_03 的「Battle Scene」为 true
// 表示这场战斗已打完，战斗场景不会再开打（苔藓之母实测）。
// 只改内存，4 号位不存档（boss_rush_save_guard.hpp），重新读档即恢复。
// 名字取猎人日志的本地化键（Journal 表），取不到才用 fallback。

#pragma once

#include <cstddef>
#include <cstdint>

namespace sshud::bossrush {

constexpr size_t MAX_BOSS_FLAGS = 4;
constexpr size_t MAX_SCENE_BOOLS = 4;
constexpr size_t MAX_FIXUPS = 3;
constexpr int ACT_COUNT = 3;

struct BossFlag {
  const char* name;  // PlayerData 的 bool 字段名
  bool value;
};

struct SceneBool {
  const char* scene;  // nullptr = BOSS 所在场景；附加分块场景（如 Bone_05_boss）里的标记写分块名
  const char* id;     // 带 PersistentBoolItem 的物体名
  bool value;
};

enum class FixupKind { Activate, FsmEvent, FsmState };

/// 到场后的修正：path 是 "根/子/…"（根须激活，子可以停用）；FsmEvent / FsmState 找 path 上名为 fsm 的状态机
struct Fixup {
  FixupKind kind;
  const char* path;
  const char* fsm;   // Activate 时为 nullptr
  const char* name;  // 事件名 / 状态名；Activate 时为 nullptr
};

struct BossEntry {
  const char* nameKey;        // 猎人日志本地化键（表 Journal）
  const char16_t* fallbackZh;
  const char16_t* fallbackEn;
  int act;                    // 1..ACT_COUNT，菜单按幕分页
  const char* scene;
  const char* gate;
  BossFlag flags[MAX_BOSS_FLAGS];
  size_t flagCount;
  SceneBool sceneBools[MAX_SCENE_BOOLS];
  size_t sceneBoolCount;
  Fixup fixups[MAX_FIXUPS];
  size_t fixupCount;
};

constexpr const char* JOURNAL_SHEET = "Journal";

constexpr BossEntry BOSSES[] = {
    {"NAME_MOSSBONE_MOTHER", u"苔藓之母", u"Moss Mother", 1, "Tut_03", "right1",
     {{"defeatedMossMother", false}, {"blackThreadWorld", false}}, 2, {{nullptr, "Battle Scene", false}}, 1, {}, 0},
    {"NAME_SPLINTER_QUEEN", u"碎裂者修女", u"Sister Splinter", 1, "Shellwood_18", "right1",
     {{"defeatedSplinterQueen", false}, {"blackThreadWorld", false}}, 2, {}, 0, {}, 0},
    {"NAME_SKULL_KING", u"骷髅暴君", u"Skull Tyrant", 1, "Bone_15", "left1",
     {{"skullKingDefeated", false}, {"blackThreadWorld", false}}, 2, {}, 0, {}, 0},
};
constexpr size_t BOSS_COUNT = sizeof(BOSSES) / sizeof(BOSSES[0]);

/// 休息点：100% 存档自己的存档长椅（钟心镇空房间）
constexpr const char* REST_SCENE = "Belltown_Room_Spare";
constexpr const char* REST_GATE = "left1";

}  // namespace sshud::bossrush
