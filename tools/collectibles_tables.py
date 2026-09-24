"""Completionist Map 的硬编码表（移植自 PersistentBoolItemMapIcons.cs / ModMapData.cs，v1.0.3）。

只放数据，逻辑在 collectibles_rules.py。顺序与原代码一致：分类规则是"后匹配覆盖先匹配"。
"""

from __future__ import annotations

# PersistentBoolItemMapIcons.Inaccessible：(场景, GameObject 名)，原样照搬
_INACCESSIBLE_RAW = """
Library_11b|Secret Mask New;Bellway_03|Secret Mask New (1);Coral_28|Coral_gate_large;Coral_28|Coral_gate_large (1)
Under_(07?)|Battle Scene;Coral_23|Breakable Wall;Tut_01b|remask right;Under_02|Barrel Plat Lift DROPPER Bottom
Under_20|Silk Grub Large Cocoon;Under_20|Silk Grub Small Cocoon 1;Under_20|Silk Grub Small Cocoon 1 (1)
Under_20|Silk Grub Small Cocoon 2;Song_19_entrance|Song_lever_side;Slab_22|Collapser Small (1)
Shellwood_02|Plank Solid 2;Bellshrine_05|Song_lever_side old;Bone_01|Rock Roller;Bone_11b|Plate
Dock_03b|Breakable Wall;Dock_03c|Breakable Wall_Silhouette;Dock_03d|Breakable Wall_Silhouette;Dust_01|Secret Mask New
Organ_01|Hornet_cage_rect_lift_organ (1);Room_Forge|dock_sealed_gate;Song_10|Collapser Small;Slab_16|Start Inspect
Slab_21|Slab Lock;Bone_East_20|explode_wall_norck;Slab_03|Slab Lock (3);Slab_14|Slab Lock R;Slab_15|Slab Lock (2)
Slab_16|Slab Lock (2);Slab_03|Slab Lock (2);Coral_10|Coral10rightsidegate;Bone_04|Bone Gate
Cog_09_Destroyed|Weakness Cog Drop Scene;Cog_10_Destroyed|Wake Scene;Arborium_02|Rhino;Clover_05c|Clover Gate (1)
Slab_16|Collectable Item Pickup Return;Slab_16|Gate Return Inspect;Slab_16|Jail Gate Door - Persistent
Slab_16|slab_item_chain;Slab_16|slab_item_chain (1);Memory_Coral_Tower|Coral Warrior (1);Memory_Coral_Tower|Coral Warrior
Dock_12|Diving Bell Door;Bone_East_03|Bone Hunter Child Alerter;Bone_East_10_Room|Toll Door Breakable Cog
Bone_East_10|Pilgrim 02 (Unique female);Clover_04b|Clover Gate;Clover_04b|Clover Gate (1);Bone_12|Pilgrim StaffWielder
Aqueduct_05|Collectable Item Pickup Reward 1;Aqueduct_05|Collectable Item Pickup Reward 2
Coral_41|Coral Crust Wall Mid (15);Coral_41|Coral Crust Wall Tall (8);Coral_41|Coral Crust Wall Mid (37)
Coral_41|Coral Crust Wall Mid (38);Coral_41|Coral Crust Wall Mid (21);Coral_41|Coral Crust Wall Tall (6)
Coral_41|Coral Crust Wall Mid (20);Coral_41|Coral Crust Wall Mid (23);Coral_41|Coral Crust Wall Mid (27)
Coral_41|Coral Crust Wall Mid (26);Coral_41|Coral Crust Wall Mid (24);Coral_24|Coral Crust Wall Tall (2)
Bone_East_24|DropBomb Rock (2);Ant_21|ant_item_string;Library_02|Collectable Item Pickup;Mosstown_02|Bone Lever 2
Shellwood_03|Hornet_pressure_plate;Bone_East_24|ant_lever_persistent;Clover_04b|Hornet_pressure_plate_small_persistent
Room_CrowCourt|Bone Lever;Greymoor_02|greymoor_lever_simple;Cog_Bench|cog_lever (2);Slab_Cell_Quiet|One Way Wall
Under_05|Fall_platform_barrel wide;Bone_04|Fall_platform_barrels;Bone_16|Collapser Small (1);Ant_19|Collapser Small (6)
Greymoor_02|Collapser Small;Dust_03|Battle Scene;Ant_21|Ant Chest;Bone_East_18|Ant Chest;Ant_04|Secret Mask New
Ant_04_left|Secret Mask New;Ant_04_mid|Secret Mask New;Coral_24|Coral Crust Mask (1) - not used;Bone_10|Secret Mask New
Bone_10|Collectable Item Pickup Locket;Clover_16|Breakable Wall (1);Bone_12|Lady Bug Large
Library_13b|Collectable Item Pickup - Quill Red;Library_13b|Collectable Item Pickup - Quill Purple;Ward_02|Pipe_Vent_Hatch
Cog_10_Destroyed|Pipe_Vent_Hatch Broken;Bone_East_01|Collectable Item Pickup;Ant_21|Battle Scene Extra
Library_02|Battle Scene Acolyte;Bone_01|Secret Mask;Bone_East_07|Secret Mask New (1);Slab_03|Secret Mask New
Pilgrims Rest|Toll Door;Bone_07|tool_metal_deposit;Abyss_05|Abyss Dive Cutscene;Slab_14|Thief NPC Slab Hopaway
Under_07c|Battle Scene;Dust_Shack|Collectable Item Extractor Pins;Room_Witch|vine_4;Room_Witch|vine_3;Room_Witch|vine_2
Room_Witch|vine_1;Room_Witch|body lower;Room_Witch|body top;Room_Witch|head;Bone_East_10_Room|Rosary Thief
Song_25|Weakness Scene Act3 Final;Song_25|Hatch;Song_04|Wake Scene;Bone_East_02b|Rhino
"""

INACCESSIBLE: frozenset[tuple[str, str]] = frozenset(
    (scene, name)
    for chunk in _INACCESSIBLE_RAW.strip().splitlines()
    for scene, name in (pair.split("|", 1) for pair in chunk.split(";"))
)

# PersistentBoolItemMapIcons.IdTypoCorrections（按 CleanScenePath 纠正）
SCENE_TYPO_CORRECTIONS: dict[str, str] = {"Aueduct_04": "Aqueduct_04"}

# UndesiredEntriesByDetails + AssignUseless 里其余按 Details 精确匹配的条目
USELESS_DETAILS: frozenset[str] = frozenset({
    "Deep Memory Pre Enter Effect", "_", "Hiding Audio", "Dialogue", "sherma_anim", "Wake Roaches", "Tink Effects",
    "Taunt Detect", "Remove", "Animation", "Wake Up", "Conversation Control", "Behaviour", "Set Z", "States",
    "Teleport Arrive", "Chime Sfx", "Remind", "unmasker", "Shake", "Re-Encounter", "Fall Tele",
    "Break for Courier", "Destroy", "Splash Audio", "Dock Demo Key",
})

# AssignUseless：Id 包含（忽略大小写）即无用
USELESS_ID_SUBSTRINGS: tuple[str, ...] = (
    "Caravan", "Remasker", "Reminder", "Needolin Memory", "Garmond", "Silk Grub", "Spine Floater", "Gate_Small",
    "Crystal Break Chunk", "black_thread_impacter", "Breakable Window", "Cog_Choir_Cylinder", "song_bridge_pole",
    "SC_", "Chef_Meat_React", "cradle_plat", "cradle_spike_plat", "Memory Orb Challenge", "Swamp Muckman",
    "Tension Tone", "Slab_vent_mask", "Memory Orb Group", "Memory Group", "bell_toll_machine", "Breakable Hang Sack",
    "bell_necklace", "Organ Webbed Bench", "Grasshopper Child", "Pustule_set_small", "pustule_set_large",
    "Rosary Pilgrim", "Breakable Restaurant Sack", "Bone Hunter Throw", "Bone Carriage",
)
USELESS_ID_EXACT: frozenset[str] = frozenset({"charm_break_wall", "lamp_hang_top"})

# AssignUseless.CullControl：Details 以 "Control" 结尾且 Id 含以下任一
CULL_CONTROL_ID_SUBSTRINGS: tuple[str, ...] = (
    "Plat", "Flyaway", "Understore Thrower", "Bone Thumper", "Rock Roller", "Weaver Sign", "Silkfly",
)

# Details == "FSM" 时仍保留的 Id（其余一律无用）
FSM_DETAIL_KEEP_IDS: frozenset[str] = frozenset({"Heart Piece", "Moss Vine Cluster", "Architect Shrine Door"})

# UselessSelf（HidesSelf 恒真）：按 Id / Details 精确匹配
HIDDEN_SELF_IDS: frozenset[str] = frozenset({"tool_metal_deposit", "Junk Hatch", "slab_item_chain"})
HIDDEN_SELF_DETAILS: frozenset[str] = frozenset({"Spawn Item On Land"})

# AssignStyles：(分类, 字段, 方式, 模式)，按原顺序，后匹配覆盖先匹配。
# 字段 "Id" / "Details"；方式 "prefix" / "contains"（均忽略大小写）/ "eq"（区分大小写）/
# "wall_control"（Id 含 Wall 且 Details 含 Control）/ "cog10"（Id 含模式且场景为 Cog_10）。
# "Pickup" 表示按物品类型再细分（GetFilterFromItemType）。
STYLE_RULES: tuple[tuple[str, str, str, str], ...] = (
    ("Geo", "Id", "prefix", "Geo"), ("Geo", "Id", "prefix", "Craw_home"), ("Geo", "Id", "prefix", "churchkeeper_rosary"),
    ("MossBerry", "Id", "contains", "moss_berry_fruit"),
    ("Pickup", "Id", "contains", "Collectable Item"), ("Pickup", "Id", "contains", "Rosary Cannon Pickup"),
    ("Pickup", "Id", "contains", "Entry Pickup"), ("Pickup", "Id", "contains", "Corpse Pickup"),
    ("Pickup", "Id", "contains", "Return Pickup"), ("Pickup", "Id", "contains", "Ladybug Craft Pickup"),
    ("Pickup", "Id", "contains", "Lantern Item"),
    ("Secret", "Id", "contains", "Secret"), ("Secret", "Id", "contains", "moss_bone_plaque"),
    ("Secret", "Id", "contains", "Sound Control"), ("Secret", "Id", "contains", "Coral Crust Mask"),
    ("HeartPiece", "Id", "contains", "Heart Piece"), ("VoidMasses", "Id", "contains", "Black_Thread_Core"),
    ("HeartPiece", "Id", "contains", "library_glass_heart_piece"),
    ("BreakableWall", "Id", "wall_control", "Wall"), ("BreakableWall", "Id", "contains", "Breakable Blocker"),
    ("BreakableWall", "Id", "contains", "Breakable Wall"), ("BreakableWall", "Id", "contains", "loom_room_jar"),
    ("BreakableWall", "Id", "contains", "multi_breakable"), ("BreakableWall", "Id", "contains", "Plate Gate"),
    ("BreakableWall", "Id", "contains", "BreakableWall"), ("BreakableWall", "Id", "contains", "breakable floor"),
    ("Lock", "Id", "contains", "lock_machine"), ("Lock", "Id", "contains", "Thick Silk Vines"),
    ("BreakableWall", "Id", "contains", "GG_Breakable_Junk"), ("Lock", "Id", "contains", "Architect Shrine Door"),
    ("BreakableWall", "Id", "contains", "drop_planks"), ("BreakableWall", "Id", "contains", "One Way"),
    ("BreakableWall", "Id", "contains", "jail_door"), ("BreakableWall", "Id", "contains", "Twig Wall"),
    ("BreakableWall", "Id", "contains", "Cluster_Vine"), ("BreakableWall", "Id", "contains", "crest_shrine_break_tube"),
    ("BreakableWall", "Id", "contains", "Vine Platform"), ("BreakableWall", "Id", "contains", "Collapse_Wall"),
    ("BreakableWall", "Id", "contains", "diving_bell_door_breakable"), ("BreakableWall", "Id", "cog10", "Active"),
    ("Battle", "Id", "contains", "Battle Scene"), ("Battle", "Id", "contains", "Battle Cloaked Scene"),
    ("Battle", "Id", "contains", "Boss Scene"), ("BreakableWall", "Details", "contains", "breakable_wall_v2"),
    ("BreakableWall", "Id", "contains", "Vine Cluster"), ("BreakableWall", "Id", "contains", "wall_cluster"),
    ("BreakableWall", "Id", "contains", "organ_lift_broken_drop"), ("ExplodeWall", "Id", "contains", "Explode_Wall"),
    ("ExplodeWall", "Id", "contains", "Coral Crust Wall"), ("ExplodeWall", "Id", "contains", "ExplodeRock"),
    ("ExplodeWall", "Id", "contains", "Wind Rock"), ("ExplodeWall", "Id", "contains", "stake_trap_spikeball_loose"),
    ("ExplodeWall", "Id", "contains", "DropBomb"), ("Collapser", "Id", "contains", "Collapser"),
    ("BreakableWall", "Id", "contains", "ward_junk_pile_break"), ("Collapser", "Id", "contains", "thin_fall"),
    ("Collapser", "Id", "contains", "sway drop"), ("Collapser", "Details", "contains", "break_floor"),
    ("Lock", "Id", "contains", "Slab Lock"), ("Lock", "Id", "contains", "aspid_sealed_gate"),
    ("Lock", "Id", "contains", "weaver_gate_ring"), ("Lock", "Id", "contains", "Thief NPC Slab Hopaway"),
    ("Lock", "Id", "contains", "bank_door"), ("Lock", "Details", "contains", "Cell Door"),
    ("BreakableWall", "Id", "contains", "shortcut"), ("Misc", "Id", "contains", "Song Knight Tube"),
    ("BreakableWall", "Id", "contains", "junk_pile_break"), ("SpoolPiece", "Id", "contains", "Silk Spool"),
    ("Lever", "Id", "contains", "Lever"), ("Lever", "Id", "contains", "Bone East 11 Cross Over Group"),
    ("Lever", "Id", "contains", "pressure_plate"), ("Lever", "Id", "contains", "Pressure Plate"),
    ("Lever", "Id", "eq", "Plate"), ("Lever", "Id", "contains", "Trap Bridge"),
    ("Lever", "Id", "contains", "cage_rect_lift"), ("Misc", "Id", "contains", "lamp_pivot"),
    ("Misc", "Id", "contains", "Confessional NPC"), ("Lever", "Details", "contains", "Lift Control"),
    ("Lever", "Id", "contains", "Dust_02Gate"), ("Chest", "Details", "contains", "Chest Control"),
    ("PollipHeart", "Id", "contains", "Nectar Pickup"), ("RingLineSwitch", "Id", "contains", "slide_gate_ring"),
    ("RingLineSwitch", "Id", "contains", "Ring Pull Switch"), ("ShellShard", "Id", "contains", "Shell Shard"),
    ("Propeller", "Id", "contains", "Propeller"), ("PillarDrop", "Id", "contains", "drop pillar"),
    ("PillarDrop", "Id", "contains", "stalactite group"), ("PillarDrop", "Id", "contains", "fall platform support pole"),
    ("PillarDrop", "Id", "contains", "lift dropper"), ("OneWayUp", "Id", "contains", "bot blocker"),
    ("Misc", "Details", "contains", "Conversation"), ("OneWayUp", "Id", "contains", "attic_ladder"),
    ("Collapser", "Details", "contains", "Detect Landing"), ("Collapser", "Details", "contains", "Rope Plat"),
    ("Lever", "Id", "contains", "Shrine Pressure Plate"), ("Lever", "Id", "contains", "under_lift_large"),
    ("Lever", "Id", "contains", "Cradle_pipe_trapdoor"), ("Lever", "Id", "contains", "Dial Door Bridge"),
    ("Secret", "Id", "contains", "weaver_lift_power_chamber"),
)

# Completionist 分类 → 本项目 CollectibleKind；不在表里的分类（墙、锁、战斗……）不是收集物
FILTER_TO_KIND: dict[str, str] = {
    "HeartPiece": "MaskShard",
    "SpoolPiece": "SpoolFragment",
    "MossBerry": "MossBerry",
    "ShellShard": "ShellShard",
    "Geo": "Rosary",
}

# CreateItemFsmActionIcons 里整组移除的 SavedItemId
FSM_REMOVED_ITEMS: frozenset[str] = frozenset({
    "Shell Flower", "Craw Summons", "White Flower", "FleasCollected Target", "Needle Upgrade", "Attunement Increase",
    "Citadel Ascent Melody Librarian Return", "Citadel Ascent Melody Conductor", "Citadel Ascent Melody Architect",
    "Dock Key", "Extractor", "Belltown House Key", "Rosary_Set_Small", "Rosary_Set_Large",
})
FSM_TAKE_ACTIONS: frozenset[str] = frozenset({"CollectableItemTake", "CollectableItemTakeV2"})

# FindItemsByFlags：(显示名, playmakers 里的 GameObject, 场景, PlayerData bool, 分类)。
# 纹章（completion 为 null，靠 ToolCrest.IsUnlocked）和按 alternateUnlockedTest 找的工具不在此表。
FLAG_SPOTS: tuple[tuple[str, str, str, str, str], ...] = (
    ("Silk Spear", "Shrine Weaver Ability", "Mosstown_02", "hasNeedleThrow", "Tool"),
    ("Thread Sphere", "Shrine Weaver Ability", "Greymoor_22", "hasThreadSphere", "Tool"),
    ("Silk Charge", "Shrine Weaver Ability", "Crawl_05", "hasSilkCharge", "Tool"),
    ("Swift Step", "Shrine Weaver Ability", "Bone_East_05", "hasDash", "Ability"),
    ("Drifter's Cloak", "Shrine Weaver Ability", "Greymoor_22", "hasBrolly", "Ability"),
    ("Cling Grip", "Shrine Weaver Ability", "Shellwood_10", "hasWalljump", "Ability"),
    ("Needolin", "Boss Scene", "Belltown_Shrine", "hasNeedolin", "Ability"),
    ("Clawline", "Shrine Weaver Ability", "Under_18", "hasHarpoonDash", "Ability"),
    ("Faydown Cloak", "DJ Get Sequence", "Peak_08b", "hasDoubleJump", "Ability"),
    ("Silk Soar", "Shrine Weaver Ability", "Abyss_08", "hasSuperJump", "Ability"),
    ("Sylphsong", "Crest Upgrade Shrine", "Weave_10", "HasBoundCrestUpgrader", "Ability"),
    ("Silk Spool", "Caravan Troupe Leader Judge NPC", "Coral_Judge_Arena", "MetCaravanTroupeLeaderJudge",
     "SpoolFragment"),
)

# AddCustomIcons：tracker_markers 里的丝之心 (场景, PlayerData bool)
SILK_HEART_SPOTS: tuple[tuple[str, str], ...] = (
    ("Ward_02", "wardBossDefeated"), ("Bone_05", "defeatedBellBeast"), ("Song_Tower_01", "defeatedLaceTower"),
)

# 跳蚤：item_fsm_actions 里 SavedItemId == "FleasCollected Target" 的条目。默认字段 SavedFlea_<场景>，
# 这三只不按场景命名（字段名见 dump.cs）。
FLEA_ITEM_ID = "FleasCollected Target"
FLEA_SPECIAL_FIELDS: dict[str, str] = {
    "Giant Flea": "tamedGiantFlea",
    "Caravan Lech Strung Up": "CaravanLechReturnedToCaravan",
    "Caravan Troupe Hunter Wild": "MetTroupeHunterWild",
}

# 地图图标（MapStyles / MapFilters）。缩放 = MapStylingParameters.IconScale_Items(0.3) × 各样式倍数，
# 没设倍数的样式（CraftMetal）是 1。IconSource 与 map_markers_data.hpp 同名。
ENTRY_ICON_SCALE = 0.3  # 数据自带贴图：AssetBundleBasedIconStyle(name, 1f)
ICON_OVERRIDE_KINDS: frozenset[str] = frozenset({"MaskShard", "SpoolFragment", "MossBerry", "Crest"})  # OverridesTexture
KIND_ICONS: dict[str, tuple[str, str, float]] = {
    "MaskShard": ("SpriteName", "Inv_0030_inv_health_backboard_mask_full", 0.3 * 0.35),
    "SpoolFragment": ("SpriteName", "Hornet_Spool_Upgrade_Shop_Icon_full", 0.3 * 0.66),
    "Craftmetal": ("SpriteName", "Hornet_Tool_Metal", 1.0),
    "Tool": ("SpriteName", "_0006_I_magnetite_dice", 0.3 * 1.5),
    "ToolkitUpgrade": ("SpriteName", "Inv_0015_tool_pouch_original_with_kit", 0.3 * 1.5),
    "Materium": ("SpriteName", "icon_slab_key_ring_0000_4", 0.3 * 1.5),
    "Relic": ("SpriteName", "Hornet_icon_0005_R_saint_locket", 0.3 * 1.5),
    "Memento": ("SpriteName", "flea_memento", 0.3 * 1.5),
    "MemoryLocket": ("SpriteName", "Charm_Notch", 0.3 * 1.5),
    "Collectable": ("SpriteName", "icon_slab_key_ring_0000_4", 0.3 * 1.5),
    "Rosary": ("SmallGeo", "", 0.25),
    "ShellShard": ("SpriteName", "Shell_shard_icon", 0.3 * 1.5),
    "MossBerry": ("CollectableItem", "Mossberry", 0.3 * 0.6),
    "SilkEater": ("SpriteName", "Silk_Grub_idle0000", 0.3 * 0.5),
    "Crest": ("SpriteName", "Hornet_Inv_pane_icons_0001_crest", 0.3 * 2.5),
    "SilkHeart": ("SpriteName", "silk_heart_inv_icon", 0.3 * 0.5),
    "Ability": ("SpriteName", "UI_tool_slot_lock_weave0001", 0.3 * 1.5),
    "Flea": ("FleaPin", "", 1.0),  # 缩放由运行时乘上游戏跳蚤标记自身的 localScale
}
