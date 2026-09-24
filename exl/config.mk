# exlaunch 配置（构建时拷进 build/sshud/，覆盖 vendor/exlaunch 的模板）。
# 由 exl/Makefile 驱动，REPO_ROOT 由它传入。

# 作为 subsdk9 由游戏自己的 rtld 加载
LOAD_KIND := Module

# Silksong（小写：exlaunch 用它拼 atmosphere/contents/ 路径）
PROGRAM_ID := 010013c00e930000

ELF_EXTRACT :=
PYTHON := python3

# 只用来让 exlaunch 的构建流程走通；真正部署的 main.npdm 由
# tools/40-patch-npdm.py 从游戏原版 npdm 生成，见 exl/Makefile。
NPDM_JSON := application.json

# offsets.h 只有一份：plugin/source/offsets.h（tools/30-gen-offsets.py 生成，不入库）
C_FLAGS := -I$(REPO_ROOT)/plugin/source
# GCC 16 新增的告警，exlaunch 的 hook_impl.cpp 会触发（它开了 -Werror）
CXX_FLAGS := -Wno-non-c-typedef-for-linkage

# 以下是 exlaunch 部署脚本用的变量；本项目不用它们部署（不往仓库外写文件）
MOUNT_PATH :=
FTP_IP :=
FTP_PORT :=
FTP_USERNAME :=
FTP_PASSWORD :=
RYU_PATH :=
