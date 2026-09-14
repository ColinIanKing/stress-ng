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

### Patch 2: core-affinity: 新增 --taskset physical 关键字（SMT 感知） — DONE
- [x] `core-affinity.c`：新增 `stress_topology_physical_set()`（遍历 `/sys/devices/system/cpu/cpu*/topology/thread_siblings_list`，每对 sibling 只保留最小 CPU 号；目录项名与"最小 sibling"匹配才加入，天然处理乱序迭代与范围解析）；token 解析加 `physical` 分支（在 core 关键字之后）
- [x] `stress-ng.1`：taskset 关键字表加 physical 条目（man 渲染实测可见）
- [x] bash-completion：确认只列选项名不列关键字值，无需改动
- [x] 验证实测（本机无 SMT）：
  - `--taskset physical --cpu 2 -t 3` → passed: 2
  - 独立模拟程序验证选择逻辑：physical 选出全部 128 CPU（每 CPU 自身即最小 sibling），与 all 语义一致 ✅
  - `--taskset physical --cpu 4 --cpu-method matrixprod -t 5` → passed: 4
  - man 渲染：`man ./stress-ng.1 | grep -A3 physical` 输出正确条目
- [x] 回归实测：`--taskset all/odd/even/core0/package0 --cpu 1 -t 1` 全部 passed；构建 0 warning 0 error
- [x] 目标机待验证：950（SMT2）上 `--taskset physical` 应选 191 核（每物理核 1 线程）——需真机确认（本机无法构造 SMT 拓扑）

### Patch 3: 逐物理核 sweep 编排脚本 scripts/sdc-scan.sh — DONE
- [x] 新增 `scripts/sdc-scan.sh`（可执行）：阶段 0 取证（topology/isolated/offline/EDAC/dmesg 快照）→ SMT sibling 映射（每物理核取代表线程，sibling 对合成 `--taskset` 参数）→ 逐核 `--cpu 2 --cpu-method all --fma 2 --verify --metrics-brief -Y <yaml>` → 可选 `--sdcshield` 同核交叉验证 → suspects.txt 汇总 + EDAC 前后 delta
- [x] 参数：`--cpus LIST / --secs N / --method M / --out DIR / --sdcshield CMD`，`NG=` 环境变量指定 stress-ng
- [x] 验证实测（本机）：
  - `bash -n` 通过
  - `NG=./stress-ng ./scripts/sdc-scan.sh --cpus 0-3 --secs 3 --out /tmp/sdctest2` 全流程跑通：4 核扫描、0 suspects、每核 yaml（bogo-ops 数据在，可用于离群检测）、每核 log（`passed: 4: cpu (2) fma (2)`）
- [x] 修复的 bug（诚实记录）：`set -u` 下关联数组 `${rep_of_pair[$lowest]}` 未绑定变量崩溃 → 改 `${rep_of_pair[$lowest]:-}` 默认值
- [x] 目标机用法：`NG=./stress-ng ./scripts/sdc-scan.sh --secs 120 --sdcshield "./run-sdcshield.sh"`（950 上每核会是 SMT 对如 "192,193"）

### Patch 4: stress-fma: verify 失败路径位级诊断（CORE179 对齐） — DONE
- [x] `stress-fma.c`：新增 `stress_fma_verify_fail()`（首个不一致元素下标 + expected/actual 十六进制 + xor + popcount 翻转位数）；double/float 两段 memcmp 失败路径改调它；首行错误信息文案保持不变（`data difference between identical ... fma computations`），只追加诊断行
- [x] include `core-bitops.h`（`stress_bitops_popcount64`）
- [x] 验证实测（含故障注入）：
  - **故障注入验证**：临时在 double_a2 memcmp 前注入 `p[3] ^= 1ULL<<41`，构建后 `--fma 1 --verify -t 3` 输出：
    `first difference at element 3: expected 0x4004ddae4df2018e, actual 0x4004dfae4df2018e, 1 bit(s) flipped (xor 0x0000020000000000)` ✅（注入位=翻转位=41，xor=0x200000000000，完全对应）
  - 注入代码已还原（diff 确认），干净构建 0 warning 0 error
  - 正常路径：`--fma 2 --verify -t 5` → passed: 2（输出格式不变）
- [x] 回归：`--fma 2 --verify -t 5` passed、`--zombie 1 -t 5` passed
- [x] 修复的构建错误（诚实记录）：忘 include core-bitops.h → undefined reference to stress_bitops_popcount64

### Patch 5: stress-vecfp/stress-matrix 同款位级诊断 — DONE
- [x] `stress-matrix.c`：memcmp 失败后扫描 r/s（uint32 float 位型），输出首个不一致元素的下标 + 行/列 + expected/actual 十六进制 + popcount 翻转位数
- [x] `stress-vecfp.c`：double/float mismatch 消息增强为"元素下标 + got/expected 十进制(位型十六进制) + 翻转位数"（保留原 %f 数值，追加位型——原消息只有数值，1-bit 翻转在小数位上不直观）；memcpy 取位型避免 strict-aliasing 违规
- [x] 验证实测（含故障注入）：
  - matrix 注入 `s[7] ^= 1u<<19` → 输出 `first difference at element 7 (row 0, column 7): expected 0x479c2bd6, actual 0x47942bd6, 1 bit(s) flipped (xor 0x00080000)` ✅（注入位=翻转位=19）
  - 注入已还原（grep TEMP INJECTION = 0），干净构建 0 warning 0 error
  - 正常路径：`--vecfp 2 --verify` passed、`--matrix 2 --verify` passed、`--matrix-method all` passed
- [x] 回归：`--fma 1 --verify -t 3` passed、`--zombie 1 -t 3` passed
- [x] 修复的构建错误（诚实记录）：vecfp 忘 include core-bitops.h/core-builtin.h → undefined reference（shim_memcpy / popcount）

### Patch 6: 新 stressor stress-sve2.c（SVE2 数据通路） — DONE
- [x] 新文件：`STRESS_ARCH_ARM && __ARM_FEATURE_SVE2` guard；sve2_fmla（svmla_f64_m 谓词化 FMA 链 ×64）+ sve2_bitperm（svbext_u64 位提取，单独 `__ARM_FEATURE_SVE2_BITPERM` guard）；标量 golden 参考实现比对（fma() / BEXT 压缩语义）；CORE179 风格位级失败诊断；`.supported` 检查 HWCAP_SVE；无 SVE2 → `stress_unimplemented` + unimplemented_reason
- [x] 5 点注册完成：Makefile STRESS_SRC（strnum 与 switch 之间）/ core-stressors.h MACRO(sve2) / core-opts.h OPT_sve2+OPT_sve2_ops / core-opts.c long_options（sve2, sve2-ops）
- [x] Makefile.config 的 SVE2 march 串扩展 `+sve2-bitperm`（BEXT 需要；950 cpuinfo 有 svebitperm）
- [x] 验证实测（本机 920 无 SVE，诚实跳过 + 编译级验证）：
  - `--sve2 1 -t 3` → `sve2 stressor will be skipped, it is not implemented ... (built for non-aarch64 target or compiler without SVE2 support)`，skipped: 1 ✅
  - **编译级功能验证**（本机无法运行 SVE2）：`gcc -march=armv8.6-a+sve2+sve2-bitperm -c stress-sve2.c` → objdump 确认真实指令 `ptrue p0.b` / `ld1d {z0.d}` / `fmla z0.d, p0/m, z3.d, z1.d` / `bext z0.d, z0.d, z1.d`
  - **全链路验证**：`make MARCH_AARCH64_SVE2=1` 全量构建成功（0 warning 0 error），最终二进制含 `bext z`（1 处）与 `fmla z`（多处）
  - 回归（正常构建）：`--zombie 1` passed、`--cpu 2 --cpu-method crc32` passed、`--fma 1 --verify` passed
- [x] 发现并修正的 bug（诚实记录）：
  1. guard 宏名写错：`STRESS_ARCH_ARM64` 不存在（正确为 `STRESS_ARCH_ARM`），导致整个实现被静默跳过、编译的是 stub（objdump 0 条 z 寄存器指令暴露了问题）
  2. 缺 `#include <math.h>`（fma 隐式声明警告）
  3. `svbext_u64` 在 gcc 12 要求 `+sve2-bitperm` 扩展标志；BEXT 语义为"掩码选中的位按序压缩到底部"（非逐位条件选择），golden 参考已按 ARM ARM 语义实现
- [x] 目标机待验证：950 上 `--sve2 N -t 60` 应 passed（golden 比对通过 = SVE2 fmla/bext 数据通路正确）；如失败即 SDC 证据

### Patch 7: 新 stressor stress-ls64.c（64B 原子访存 LD64B/ST64B） — DONE
- [x] 新文件：`STRESS_ARCH_ARM && __ARM_FEATURE_LS64` guard；`__arm_ld64b`/`__arm_st64b`（ACLE intrinsics，data512_t）64B 原子读改写循环（每 64 位 lane 旋转 7 位，每轮所有位都动）；标量 golden 比对 + CORE179 位级诊断；`.supported` 检查 HWCAP2 bit15（老内核）与 HWCAP3 bit0（新内核，AT_HWCAP3=29，asm/hwcap.h 的 HWCAP3_LS64）；无 ls64 → 诚实跳过
- [x] 5 点注册：Makefile（loop 之后）/ core-stressors.h MACRO(ls64) / core-opts.h OPT_ls64+OPT_ls64_ops / core-opts.c long_options
- [x] 验证实测（本机 920 无 ls64）：
  - `--ls64 1 -t 3` → `ls64 stressor will be skipped, ... (built for non-aarch64 target or compiler without the ls64 ... extension)`，skipped: 1 ✅
  - **编译级功能验证**：`gcc -march=armv8.6-a+ls64 -c` → objdump 确认 `ld64b x10, [x8]` / `st64b x0, [x8]` 真实指令
  - 本机 `getauxval(AT_HWCAP3=29)` = 0（无 ls64，与 920 spec 一致）
  - 回归：`--zombie 1` passed、`--longjmp 1` passed、`--sve2 1` skipped（诚实）、构建 0 warning 0 error
- [x] 排查记录（诚实）：`--loop 1` passed:0 疑似回归 → git stash 干净 HEAD 验证为上游固有行为（需要 CAP_SYS_ADMIN），非本 patch 引入
- [x] 目标机待验证：950（有 ls64/ls64_v）上 `--ls64 N -t 60` 应 passed

### Patch 8: cpu-method crc32（硬件 CRC32C vs 软件双路径） — DONE（提前于 4/5/6/7 完成，因本机可全验证）
- [x] `test/test-crc32-acle.c` 探测：`__aarch64__` + `__attribute__((target("+crc")))` + `__crc32cd` 可编译（`HAVE_CRC32_ACLE`）
- [x] `Makefile.config`：`CRC32_ACLE` check 挂入 cpufeatures
- [x] `stress-cpu.c`：`sw_crc32()`（Castagnoli 多项式 0x82f63b78 表驱动软件参考）+ `hw_crc32()`（ACLE intrinsics，`target("+crc")` 局部生效不改全局 march）+ `stress_cpu_crc32()`（双路径比对，mismatch → pr_fail + EXIT_FAILURE）；方法表加 `{ "crc32", ... }`
- [x] `stress-ng.1`：cpu-method 表加 crc32 条目
- [x] 验证实测（本机 920 有 crc32 硬件）：
  - `using aarch64 acle crc32 intrinsics ... yes`，`#define HAVE_CRC32_ACLE`
  - `objdump stress-cpu.o`：真实 `crc32cx w2,w2,x3` 指令内联（非 bl 调用）
  - `--cpu 1 --cpu-method crc32 --verify -t 10` → passed: 1
  - `--cpu 4 --cpu-method crc32 -t 5` → passed: 4
  - 独立调试程序确认 hw/sw 在全零/全 FF/递增/标准串四种模式全一致
- [x] 回归：`--cpu 2 --cpu-method all -t 30` → passed: 2；构建 0 warning 0 error
- [x] 发现并修正的两个 bug（诚实记录）：
  1. **忘记 #include <arm_acle.h>**：`__crc32cd` 隐式声明 → 链接失败（undefined reference）；修正后真实指令内联
  2. **多项式用错**：`__crc32c*` 是 Castagnoli CRC-32C（0x1EDC6F41/反射 0x82f63b78），初版软件参考用了 Ethernet CRC-32（0xEDB88320）→ 四种模式全 mismatch（`hardware crc32 0x1f0b5b06 does not match software crc32 0xcbaacf84`）；修正后全一致
- [x] x86 无回归论证：`HAVE_CRC32_ACLE` 只在 aarch64 上被定义（probe `#if defined(__aarch64__)` 守卫），x86 上无 crc32 方法表项以外改动；`sw_crc32` 无害（纯 C）

### Patch 9: README.md / 文档同步 — DONE
- [x] `stress-ng.1`：新增 SVE2 vector stressor（Symlink 前插入）与 64 byte atomic load/store stressor（lsearch 前插入）完整条目（--sve2/--sve2-ops/--ls64/--ls64-ops + SDC 检测语义说明）；crc32/physical 已在 Patch 2/8 就地更新
- [x] `README.md`：构建章节补 aarch64 SVE2 自动探测说明（-O3 必要性、MARCH_AARCH64_SVE2=0/1 跨编译控制、sve2/ls64 stressor 提示）
- [x] 验证：`man ./stress-ng.1` 渲染 sve2/ls64 条目正常（实测输出确认）；`--zombie 1`、`--cpu-method crc32` 回归 passed

## 全部完成 ✅ 9/9 patches pushed to port/kunpeng950-sdc-stress

| Patch | commit | 内容 | 本机验证 | 目标机待验证 |
|---|---|---|---|---|
| 1 | 0fd4437b5 | SVE2 march 探测 + CONFIG_CFLAGS 注入 | 3 路径全测（auto=no/force=1 yes 904 SVE 指令/force=0 no） | 950 上 auto 应 yes |
| 2 | a23c294d5 | --taskset physical | 128 核全选（=all 语义） | 950 上应选 191 核 |
| 3 | 7a0fc24f1 | scripts/sdc-scan.sh | 4 核小规模全流程跑通 | 950 全量 191 核 sweep |
| 4 | fb9fd9cb6 | fma verify 位级诊断 | 故障注入验证输出正确 | — |
| 5 | 6af367f39 | vecfp/matrix 同款 | matrix 故障注入验证 | — |
| 6 | 774d5b81d | stress-sve2.c | 编译级（真实 fmla/bext z 指令）+ 诚实跳过 | `--sve2 N` 应 passed |
| 7 | ae954f194 | stress-ls64.c | 编译级（ld64b/st64b 指令）+ 诚实跳过 | `--ls64 N` 应 passed |
| 8 | 39408bda9 | cpu-method crc32 | **全功能验证**（hw 指令 + 双路径比对 passed） | — |
| 9 | (this) | README/man 文档 | man 渲染确认 | — |

## 执行纪律
- 每单元：plan 勾选 → 编码 → 自验证（引用真实输出）→ commit → push 到 `port/kunpeng950-sdc-stress`
- 不动 x86 路径；ARM guard 一律 `#if defined(__aarch64__)` + 编译期宏
- 本机无法真跑的特性（SVE2/SMT/ls64 硬件行为）必须诚实跳过并在 commit message 写明"功能验证需目标机"
