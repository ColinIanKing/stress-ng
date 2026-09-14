# stress-ng Kunpeng 950 (ARMv9/SVE2/SMT) SDC 压测移植方案

日期：2026-09-14
分支：`port/kunpeng950-sdc-stress`
动机：CP1 出现 CPU 核隔离、自检失败，疑 SDC 故障核。需让 stress-ng 在 Kunpeng 950 7592C（ARMv9 级特性：SVE2/svei8mm/svebf16/i8mm/bf16/ls64/SMT2）上把负载真正打到 SVE2/64B 访存通路，并提供 SMT 感知绑定与逐核扫描能力，配合 sdcshield 检出故障核。

已核实基线（见 ../../task_plan.md Phase 1b / findings.md §0-§6）：
- Makefile 无 ARM -march，TARGET_CLONES 在 ARM 为空宏 → 当前二进制零 SVE 指令（objdump 实测：182 fmla 全 NEON v 寄存器，ld1d/ptrue/whilelo = 0）
- gcc 12.3.1 拒绝 `svebf16` 修饰符；正确拼写 `-march=armv8.6-a+sve2+bf16+i8mm`（降级链 `-march=armv8.2-a+sve2` 可用）
- `--taskset` 无 `physical` 关键字（仅 even/odd/all/random/package/cluster/die/core）
- 本开发机 Kunpeng 920：无 SMT（thread_siblings_list 单值）、无 sve/dit/ls64 → SVE/SMT/ls64 相关 patch 本机只能验证"构建干净 + 诚实跳过 + 不破坏现有测试"，真功能验证在目标机

## 单元分解（one patch per unit，每个 = 一个 commit）

### Patch 1: config: 探测 aarch64 SVE2 march 支持，注入 CONFIG_CFLAGS — DONE
- [x] 新增 `test/test-march-aarch64-sve2.c`：编译期检查 `__aarch64__ + __ARM_FEATURE_SVE2 + __ARM_FEATURE_BF16_VECTOR_ARITHMETIC + __ARM_FEATURE_MATMUL_INT8`；运行期检查 `getauxval(AT_HWCAP) & (1UL<<22)` (HWCAP_SVE)
- [x] `Makefile.config`：`MARCH_AARCH64_SVE2` 自定义探测目标（编译+运行双条件，非 check_tmp 宏——它只编译不运行）；`MARCH_AARCH64_SVE2=1/0` 可强制开/关（跨编译用）；注入 `CONFIG_CFLAGS += -O3 -march=armv8.6-a+sve2+bf16+i8mm`
- [x] `Makefile.config` `all:` 聚合：CONFIG_CFLAGS 行追加进 `config` 文件（原来只聚合 CONFIG_LDFLAGS）
- [x] `Makefile`：`%.o` 规则加 `$(CFLAGS_CONFIG_EXTRACT)`（recipe 时求值，解决 config 文件先生成后编译的鸡蛋问题）
- [x] 验证实测（本机 920 / gcc 12.3.1 / **无 SVE 硬件**）：
  - 自动路径：`using aarch64 sve2 march ... no`，config 无 CONFIG_CFLAGS，二进制 SVE 指令数 0，`--fma 2 --verify -t 10` → passed: 2（修复前曾 SIGILL，已通过 hwcap 门禁修复）
  - 强制开（`MARCH_AARCH64_SVE2=1`，模拟 950 本机构建）：`... yes`，config 有 flag，**objdump SVE 指令 904 条**（ld1d/ptrue/whilelo/fmul z/fmla z）
  - 强制关（`MARCH_AARCH64_SVE2=0`）：`... no`，0 条
  - 警告：`make -j128` 全程 0 warning 0 error
- [x] 回归：`--fma 2 --verify` passed、`--zombie 1 -t 5` passed、`--cpu 2 --cpu-method all -t 30` passed
- [x] 发现并修正的两个关键问题（诚实记录）：
  1. **-O2 不生成 SVE**：march flag 加了但 GCC 在 -O2 下仍用 NEON（实测 fma.o 0 条）→ CONFIG_CFLAGS 里必须带 -O3（已注释说明）
  2. **构建主机硬件门禁缺失会 SIGILL**：第一版只有编译期探测，本机 920（无 SVE）构建出的二进制 `--fma --verify` 直接 Illegal instruction (core dumped) → 探测必须"编译+运行"双条件（probe 程序运行检查 HWCAP_SVE）
- [x] x86 无回归论证：探测程序 `#if defined(__aarch64__)` 守卫，x86 上编译失败 → no 路径，不注入任何 flag；Makefile 改动仅 `%.o` 规则追加（可能为空的）`$(CFLAGS_CONFIG_EXTRACT)`，x86 上 config 无 CONFIG_CFLAGS 行 → 展开为空 → 编译命令与改动前完全一致

### Patch 2: core-affinity: 新增 --taskset physical 关键字（SMT 感知）
- [ ] `core-affinity.c`：`stress_affinity_cpu_set()` token 解析加 `physical` 分支——遍历 `/sys/devices/system/cpu/cpu*/topology/thread_siblings_list`，每对 sibling 只保留最小 CPU 号（无 SMT 机器 = 全部在线核，行为等同 all）
- [ ] `stress-ng.1` man 手册：taskset 关键字表加 physical 条目
- [ ] bash-completion 同步（如有关键字列表）
- [ ] 验证（本机无 SMT）：`./stress-ng --taskset physical --cpu 1 -t 2` passed 且绑定的 CPU 集 = 全部核（-v 观察或对比 `--taskset all` 行为一致）；单元级：构造 mock 无法做，靠本机"无 SMT = all"语义 + 目标机验证计划
- [ ] 回归：`--taskset all/odd/even/package0/core0` 各跑 2s 全 passed

### Patch 3: 逐物理核 sweep 编排脚本 scripts/sdc-scan.sh
- [ ] 新增 `scripts/sdc-scan.sh`：阶段 0 取证（topology/isolated/offline/EDAC/dmesg 快照）→ 跳过 isolated 核 → 遍历 thread_siblings_list 物理核代表线程 → 每核 `--taskset <pair> --cpu 2 --cpu-method all --fma 2 --verify` N 秒 → 汇总 verify fail / bogo 离群核清单
- [ ] `bash -n` 语法校验；本机小规模实测（`--cpus 0-3 --secs 5`）
- [ ] 验证：本机跑通全流程且输出 suspects 汇总（应为空 = 无嫌疑）

### Patch 4: stress-fma: verify 失败路径位级诊断（CORE179 对齐）
- [ ] `stress-fma.c`：verify memcmp 失败时，找出首个不一致下标，输出 expected/actual 十六进制 + xor + popcount（翻转 bit 数），逐 double/float 段
- [ ] 验证：临时注入错误（本地改一份数据副本比对）观察输出格式（或用 -Werror 干净构建+代码走查论证 + 目标机计划）；正常路径输出不变：`--fma 1 --verify -t 5` passed
- [ ] 回归：`--fma 1 --verify -t 5` passed、`--vecfp 1 --verify -t 5` passed

### Patch 5: stress-vecfp/stress-matrix 同款位级诊断
- [ ] 同 Patch 4 模式扩展到 stress-vecfp.c、stress-matrix.c
- [ ] 验证 + 回归同上

### Patch 6: 新 stressor stress-sve2.c（SVE2 数据通路）
- [ ] 新文件：`__ARM_FEATURE_SVE2` guard；使用 arm_sve.h intrinsics（svmla/svld1_f64/svptrue_b64/svwhilelt、svebitperm BEXT/BGRP、bf16 SVMMLA、i8mm SUDOT/USDOT）；软件 golden 自校验（标量参考实现比对）；无 SVE2 硬件 `.supported` 返回 -1 + unimplemented_reason 诚实跳过
- [ ] 5 点注册：stress-sve2.c / Makefile STRESS_SRC / core-stressors.h MACRO / core-opts.h OPT_ / core-opts.c long_options（--sve2, --sve2-ops, --sve2-method）
- [ ] 验证（本机 920 无 SVE2）：`./stress-ng --sve2 1 -t 5` → skipped with reason；构建零新警告
- [ ] 回归：`--zombie 1 -t 5` passed

### Patch 7: 新 stressor stress-ls64.c（64B 原子访存 LD64B/ST64B）
- [ ] 新文件：`__ARM_FEATURE_LS64` guard + HWCAP 检查；LD64B/ST64B 内联汇编压 LSU+一致性；无 ls64 诚实跳过
- [ ] 5 点注册同上（--ls64, --ls64-ops）
- [ ] 验证（本机无 ls64）：skipped with reason；回归 passed

### Patch 8: cpu-method crc32（硬件 CRC32 vs 软件双路径）
- [ ] `stress-cpu.c`：新 method `crc32`，用 `__ARM_FEATURE_CRC32` 的 `__crc32cd` 算大块数据，与逐字节软件参考实现比对（本机 920 有 crc32 flag，可真验证！）
- [ ] 验证：`./stress-ng --cpu 1 --cpu-method crc32 --verify -t 10` passed
- [ ] 回归：`--cpu-method all -t 30` passed

### Patch 9: README.md / 文档同步（大颗粒度修改的文档纪律）
- [ ] README.md 构建章节补 aarch64 SVE2 构建说明；stress-ng.1 已在 Patch 2/6/7/8 就地更新，此处查漏
- [ ] 验证：man 渲染 `man ./stress-ng.1 | grep -A3 physical` 正确

## 执行纪律
- 每单元：plan 勾选 → 编码 → 自验证（引用真实输出）→ commit → push 到 `port/kunpeng950-sdc-stress`
- 不动 x86 路径；ARM guard 一律 `#if defined(__aarch64__)` + 编译期宏
- 本机无法真跑的特性（SVE2/SMT/ls64 硬件行为）必须诚实跳过并在 commit message 写明"功能验证需目标机"
