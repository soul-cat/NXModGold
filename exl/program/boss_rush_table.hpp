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

constexpr size_t MAX_BOSS_FLAGS = 8;
constexpr size_t MAX_SCENE_BOOLS = 4;
constexpr size_t MAX_FIXUPS = 3;
constexpr int ACT_COUNT = 3;

struct BossFlag {
  const char* name;  // PlayerData 的 bool 字段名
  bool value;
};

struct SceneBool {
  const char* scene;  // nullptr = BOSS 所在场景。附加分块（如 Ward_02_boss）里的物体也记在主场景名下（存档核实）
  const char* id;     // 带 PersistentBoolItem 的物体名
  bool value;
};

enum class FixupKind { Activate, Deactivate, FsmEvent, FsmState };  // Deactivate：如拦下结局演出

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
    // 第一幕：苔藓之母 / 碎裂者修女 / 骷髅暴君已实机验证
    {"NAME_MOSSBONE_MOTHER", u"苔藓之母", u"Moss Mother", 1, "Tut_03", "right1",
     {{"defeatedMossMother", false}, {"blackThreadWorld", false}}, 2, {{nullptr, "Battle Scene", false}}, 1, {}, 0},
    {"NAME_SPLINTER_QUEEN", u"碎裂者修女", u"Sister Splinter", 1, "Shellwood_18", "right1",
     {{"defeatedSplinterQueen", false}, {"blackThreadWorld", false}}, 2, {}, 0, {}, 0},
    {"NAME_SKULL_KING", u"骷髅暴君", u"Skull Tyrant", 1, "Bone_15", "left1",
     {{"skullKingDefeated", false}, {"blackThreadWorld", false}}, 2, {}, 0, {}, 0},
    // moss_mother_duo (high)
    {"NAME_MOSSBONE_MOTHER", u"苔藓之母（双）", u"Moss Mother (duo)", 1, "Weave_03", "right1",
     {{"blackThreadWorld", false}}, 1,
     {{nullptr, "Boss Scene", false}}, 1,
     {}, 0},
    // bell_beast (medium)
    {"NAME_BONE_BEAST", u"钟兽", u"Bell Beast", 1, "Bone_05", "left1",
     {{"defeatedBellBeast", false}, {"encounteredBellBeast", true}, {"blackThreadWorld", false}}, 3,
     {}, 0,
     {}, 0},
    // lace_1 (high)
    {"NAME_LACE", u"蕾丝", u"Lace", 1, "Bone_East_12", "bot1",
     {{"defeatedLace1", false}, {"encounteredLace1", true}, {"laceLeftDocks", false}, {"visitedCitadel", false}, {"encounteredLace1Grotto", false}, {"blackThreadWorld", false}}, 6,
     {}, 0,
     {}, 0},
    // fourth_chorus (high)
    {"NAME_SONG_GOLEM", u"第四圣咏", u"Fourth Chorus", 1, "Bone_East_08", "left1",
     {{"defeatedSongGolem", false}, {"encounteredSongGolem", true}, {"hasBrolly", true}, {"blackThreadWorld", false}}, 4,
     {}, 0,
     {}, 0},
    // savage_beastfly_1 (high)
    {"NAME_BONE_FLYER_GIANT", u"野蛮兽蝇", u"Savage Beastfly", 1, "Ant_19", "left1",
     {{"defeatedBoneFlyerGiant", false}, {"blackThreadWorld", false}}, 2,
     {}, 0,
     {}, 0},
    // widow (high)
    {"NAME_SPINNER_BOSS", u"寡妇", u"Widow", 1, "Belltown_Shrine", "top1",
     {{"spinnerDefeated", false}, {"encounteredSpinner", true}, {"blackThreadWorld", false}}, 3,
     {}, 0,
     {}, 0},
    // moorwing (high)
    {"NAME_VAMPIRE_GNAT", u"沼翼", u"Moorwing", 1, "Greymoor_05", "left2",
     {{"defeatedVampireGnatBoss", false}, {"greymoor05_clearedOut", false}, {"allowVampireGnatInAltLoc", true}, {"visitedCitadel", true}, {"visitedBellhart", true}, {"encounteredVampireGnatBoss", true}, {"blackThreadWorld", false}}, 7,
     {}, 0,
     {}, 0},
    // great_conchflies (high)
    {"NAME_CORAL_CONCH_DRILLER_GIANT", u"巨型螺蝇（双）", u"Great Conchflies", 1, "Coral_11", "left1",
     {{"defeatedCoralDrillers", false}, {"encounteredCoralDrillers", true}, {"defeatedCoralDrillerSolo", true}, {"blackThreadWorld", false}}, 4,
     {{nullptr, "Driller A", false}, {nullptr, "Driller B", false}}, 2,
     {}, 0},
    // last_judge (high)
    {"NAME_LAST_JUDGE", u"最后审判者", u"Last Judge", 1, "Coral_Judge_Arena", "left1",
     {{"defeatedLastJudge", false}, {"encounteredLastJudge", true}, {"visitedCitadel", true}, {"blackThreadWorld", false}}, 4,
     {{nullptr, "Last Judge", false}}, 1,
     {}, 0},
    // phantom (medium)
    {"NAME_PHANTOM", u"幻影", u"Phantom", 1, "Organ_01", "left1",
     {{"defeatedPhantom", false}, {"blackThreadWorld", false}}, 2,
     {}, 0,
     {}, 0},
    // forebrothers (high)
    {"NAME_DOCK_GUARD_THROWER", u"先驱兄弟 西格尼斯与格隆", u"Forebrothers Signis & Gron", 1, "Dock_09", "right1",
     {{"defeatedDockForemen", false}, {"encounteredDockForemen", true}, {"blackThreadWorld", false}}, 3,
     {{nullptr, "Dock Guard Thrower", false}}, 1,
     {}, 0},
    // raging_conchfly (high)
    {"NAME_CORAL_CONCH_DRILLER_GIANT", u"狂暴螺蝇", u"Raging Conchfly", 2, "Coral_27", "left1",
     {{"defeatedCoralDrillerSolo", false}, {"blackThreadWorld", false}}, 2,
     {{nullptr, "Battle Scene", false}, {nullptr, "Coral Conch Driller Giant Solo", false}}, 2,
     {}, 0},
    // cogwork_dancers (high)
    {"NAME_CLOCKWORK_DANCER", u"齿轮舞者", u"Cogwork Dancers", 2, "Cog_Dancers", "left1",
     {{"defeatedCogworkDancers", false}, {"encounteredCogworkDancers", true}, {"blackThreadWorld", false}}, 3,
     {{nullptr, "Dancer A", false}, {nullptr, "Dancer B", false}}, 2,
     {}, 0},
    // trobbio (high)
    {"NAME_TROBBIO", u"特罗比奥", u"Trobbio", 2, "Library_13", "left1",
     {{"defeatedTrobbio", false}, {"encounteredTrobbio", true}, {"blackThreadWorld", false}}, 3,
     {}, 0,
     {}, 0},
    // garmond_zaza (medium)
    {"NAME_GARMOND_ZAZA", u"加蒙德与扎扎", u"Garmond & Zaza", 2, "Library_09", "left1",
     {{"blackThreadWorld", false}, {"garmondInLibrary", true}, {"garmondWillAidInForumBattle", false}, {"garmondInEnclave", false}, {"garmondLibrarySeen", true}, {"garmondLibraryMet", true}, {"garmondLibraryOffered", true}, {"garmondPurposeConvo", true}}, 8,
     {}, 0,
     {}, 0},
    // unravelled (medium)
    {"NAME_CONDUCTOR_BOSS", u"解离者", u"The Unravelled", 2, "Ward_02", "right1",
     {{"wardBossDefeated", false}, {"wardBossEncountered", true}, {"collectedWardBossKey", true}, {"wardBossHatchOpened", false}, {"blackThreadWorld", false}}, 5,
     {{nullptr, "Slasher 1", false}, {nullptr, "Slasher 2", false}}, 2,
     {}, 0},
    // chef_lugoli (medium)
    {"NAME_ROACHKEEPER_CHEF", u"主厨卢戈利", u"Chef Lugoli", 2, "Dust_Chef", "left1",
     {{"defeatedRoachkeeperChef", false}, {"roachkeeperChefCorpsePrepared", false}, {"blackThreadWorld", false}}, 3,
     {{nullptr, "Battle Scene", false}}, 1,
     {}, 0},
    // father_of_flame (high)
    {"NAME_WISP_PYRE_EFFIGY", u"焰之父", u"Father of the Flame", 2, "Belltown_08", "right1",
     {{"defeatedWispPyreEffigy", false}, {"blackThreadWorld", false}}, 2,
     {}, 0,
     {}, 0},
    // groal (high)
    {"NAME_SWAMP_SHAMAN", u"伟大的格罗尔", u"Groal the Great", 2, "Shadow_18", "right1",
     {{"DefeatedSwampShaman", false}, {"blackThreadWorld", false}}, 2,
     {}, 0,
     {}, 0},
    // broodmother (high)
    {"NAME_SLAB_FLY_BROODMOTHER", u"育母", u"Broodmother", 2, "Slab_16b", "left1",
     {{"defeatedBroodMother", false}, {"blackThreadWorld", false}}, 2,
     {{nullptr, "Battle Scene Broodmother", false}, {nullptr, "Slab Fly Mid", false}}, 2,
     {}, 0},
    // first_sinner (high)
    {"NAME_FIRST_WEAVER", u"初罪者", u"First Sinner", 2, "Slab_10b", "left1",
     {{"hasSilkBomb", false}, {"encounteredFirstWeaver", true}, {"blackThreadWorld", false}}, 3,
     {}, 0,
     {}, 0},
    // voltvyrm (high)
    {"NAME_ZAP_CORE_ENEMY", u"伏特维姆", u"Voltvyrm", 2, "Coral_29", "left1",
     {{"defeatedZapCoreEnemy", false}, {"blackThreadWorld", true}}, 2,
     {}, 0,
     {}, 0},
    // lace_2 (high)
    {"NAME_LACE", u"蕾丝（二）", u"Lace (Song Tower)", 2, "Song_Tower_01", "door_cutsceneEndLaceTower",
     {{"encounteredLaceTower", true}, {"defeatedLaceTower", false}, {"blackThreadWorld", false}}, 3,
     {}, 0,
     {}, 0},
    // grand_mother_silk (medium)
    {"NAME_SILK_BOSS", u"丝之母", u"Grand Mother Silk", 2, "Cradle_03", "right2",
     {{"soulSnareReady", false}, {"blackThreadWorld", false}}, 2,
     {}, 0,
     {{FixupKind::Deactivate, "Boss Scene/Death Sequence", nullptr, nullptr}, {FixupKind::Deactivate, "Boss Scene/Death Sequence Cursed", nullptr, nullptr}}, 2},
    // shakra (medium)
    {"NAME_SHAKRA", u"沙克拉", u"Shakra", 2, "Greymoor_08", "left2",
     {{"blackThreadWorld", false}, {"mapperSparIntro", true}}, 2,
     {}, 0,
     {{FixupKind::FsmEvent, "Mapper Spar NPC", "Dialogue", "MAPPER CALL"}}, 1},
    // giant_flea (high)
    {"NAME_GIANT_FLEA", u"巨型跳蚤", u"Giant Flea", 2, "Arborium_08", "left1",
     {{"tamedGiantFlea", false}, {"uncagedGiantFlea", true}, {"blackThreadWorld", false}}, 3,
     {}, 0,
     {}, 0},
    // tormented_trobbio (medium)
    {"NAME_TORMENTED_TROBBIO", u"受难的特罗比奥", u"Tormented Trobbio", 3, "Library_13", "left1",
     {{"blackThreadWorld", true}, {"encounteredTormentedTrobbio", true}}, 2,
     {}, 0,
     {{FixupKind::FsmState, "Grand Stage Scene", "Control", "Trobbio Ready 2"}}, 1},
    // second_sentinel (high)
    {"NAME_SONG_KNIGHT", u"第二哨兵", u"Second Sentinel", 3, "Hang_17b", "left1",
     {{"defeatedSongChevalierBoss", false}, {"encounteredSongChevalierBoss", true}, {"blackThreadWorld", true}}, 3,
     {}, 0,
     {}, 0},
    // bell_eater (medium)
    {"NAME_GIANT_CENTIPEDE", u"噬钟者", u"Bell Eater", 3, "Bellway_Centipede_Arena", "top1",
     {{"blackThreadWorld", true}, {"UnlockedFastTravelTeleport", false}}, 2,
     {}, 0,
     {}, 0},
    // lost_garmond (high)
    {"NAME_GARMOND", u"迷途加蒙德", u"Lost Garmond", 3, "Coral_33", "right1",
     {{"garmondBlackThreadDefeated", false}, {"blackThreadWorld", true}}, 2,
     {}, 0,
     {}, 0},
    // crawfather (high)
    {"NAME_CRAWFATHER", u"鸦父", u"Crawfather", 3, "Room_CrowCourt_02", "top1",
     {{"defeatedCrowCourt", false}, {"blackThreadWorld", true}}, 2,
     {}, 0,
     {}, 0},
    // plasmified_zango (high)
    {"NAME_BLUE_ASSISTANT", u"浆化赞戈", u"Plasmified Zango", 3, "Crawl_10", "right1",
     {{"blackThreadWorld", true}, {"BlueAssistantEnemyEncountered", false}}, 2,
     {{nullptr, "Blue Assistant", false}}, 1,
     {}, 0},
    // watcher_at_the_edge (high)
    {"NAME_CORAL_WARRIOR_GREY", u"边缘守望者", u"Watcher at the Edge", 3, "Coral_39", "right1",
     {{"defeatedGreyWarrior", false}, {"wokeGreyWarrior", true}, {"blackThreadWorld", true}}, 3,
     {}, 0,
     {}, 0},
    // gurr_the_outcast (high)
    {"NAME_BONE_HUNTER_TRAPPER", u"放逐者古尔", u"Gurr the Outcast", 3, "Bone_East_18b", "top1",
     {{"defeatedAntTrapper", false}, {"encounteredAntTrapper", true}, {"blackThreadWorld", true}}, 3,
     {}, 0,
     {}, 0},
    // pinstress (medium)
    {"NAME_PINSTRESS_BOSS", u"针线女士", u"Pinstress", 3, "Peak_07", "top2",
     {{"blackThreadWorld", true}, {"visitedIceCore", false}}, 2,
     {}, 0,
     {{FixupKind::FsmState, "Pinstress Control/Pinstress Scene/Pinstress Boss/NPC", "NPC Control", "Wake 2"}, {FixupKind::FsmState, "Pinstress Control/Pinstress Scene/Pinstress Boss/NPC", "NPC Control", "Battle Start"}}, 2},
    // palestag (high)
    {"NAME_CLOVERSTAG_WHITE", u"苍白牡鹿", u"Palestag", 3, "Clover_19", "left1",
     {{"defeatedWhiteCloverstag", false}, {"blackThreadWorld", true}}, 2,
     {}, 0,
     {}, 0},
    // clover_dancers (high)
    {"NAME_CLOVER_DANCER", u"三叶草舞者", u"Clover Dancers", 3, "Clover_10", "left1",
     {{"encounteredCloverDancers", true}, {"defeatedCloverDancers", false}, {"blackThreadWorld", true}}, 3,
     {{nullptr, "Green Prince Boss NPC", false}, {nullptr, "Dancer A", false}, {nullptr, "Dancer B", false}}, 3,
     {}, 0},
    // seth (high)
    {"NAME_SETH", u"赛斯", u"Seth", 3, "Shellwood_22", "right1",
     {{"defeatedSeth", false}, {"encounteredSeth", true}, {"blackThreadWorld", true}}, 3,
     {}, 0,
     {}, 0},
    // nyleth (high)
    {"NAME_FLOWER_QUEEN", u"奈莱丝", u"Nyleth", 3, "Shellwood_11b_Memory", "door_wakeInMemory",
     {{"defeatedFlowerQueen", false}, {"encounteredFlowerQueen", true}, {"blackThreadWorld", true}}, 3,
     {}, 0,
     {}, 0},
    // karmelita (high)
    {"NAME_HUNTER_QUEEN", u"卡梅莉塔", u"Karmelita", 3, "Memory_Ant_Queen", "door_wakeInMemory",
     {{"blackThreadWorld", true}}, 1,
     {}, 0,
     {}, 0},
    // crust_king_khann (high)
    {"NAME_CORAL_KING", u"甲壳王坎恩", u"Crust King Khann", 3, "Memory_Coral_Tower", "door_wakeInMemory",
     {{"encounteredCoralKing", true}, {"blackThreadWorld", true}}, 2,
     {}, 0,
     {}, 0},
    // lost_lace (high)
    {"NAME_LOST_LACE", u"迷失蕾丝", u"Lost Lace", 3, "Abyss_Cocoon", "door_entry",
     {{"EncounteredLostLace", true}, {"blackThreadWorld", true}}, 2,
     {}, 0,
     {{FixupKind::Deactivate, "Boss Control/Superjump Sequence", nullptr, nullptr}}, 1},
};
constexpr size_t BOSS_COUNT = sizeof(BOSSES) / sizeof(BOSSES[0]);

/// 休息点：100% 存档自己的存档长椅（钟心镇空房间）
constexpr const char* REST_SCENE = "Belltown_Room_Spare";
constexpr const char* REST_GATE = "left1";

}  // namespace sshud::bossrush
