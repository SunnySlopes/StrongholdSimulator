# stronghold-simulator

要塞末影传送门与布局模拟器。基于 [cubiomes](https://github.com/xpple/cubiomes)，独立编译运行，无需仓库其他部分。

给定种子和版本，输出：

1. **末影传送门精确中心 X/Z**（3×3 `end_portal` 方块区域的正中）与**默认已生成的末影之眼数量**（0–12）；
2. 可选：**每座要塞全部生成的房间/走廊/楼梯**及其精确 X/Z 范围、旋转、深度、箱位；
3. 可选：**整座要塞的平面覆盖范围**（总包围盒、真实并集面积、按部件类型分解、ASCII 俯视图）。

## 关于 Y

**没有可用的世界 Y。** cubiomes 把布局锚定在名义 `y=64`，不建模原版之后施加的竖直放置——`features/stronghold.c` 里那段调整只在**失败**的 attempt 循环中执行，真正产出传送门房的那一轮从不经过。所以所有 Y 字段都命名为 `nominal*`。

精确的是**竖直几何**（部件之间的高差）：楼梯确实向下、走廊确实平接。已知真实起始 Y 时整体平移即可：

```
真实Y = nominalY + (realStartY - 64)
```

既然 Y 给不了，**平面覆盖（§3.3）才是「整座要塞占多大」的正确答案**。

## 1. 构建与运行

```bash
git clone --recursive <this-repo>
# 已在克隆里则：
git submodule update --init --recursive

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/stronghold-sim --seed 123 --version 1.21.6 --limit 6
```

cubiomes 通过 `third_party/cubiomes` 子模块提供。若要用外部 checkout：

```bash
cmake -S . -B build -DSSIM_CUBIOMES_DIR=/path/to/cubiomes
```

## 2. 命令行参数

| 参数 | 说明 | 默认 |
|---|---|---|
| `--seed <值>` | 种子。支持十进制（含负数）、`0x` 十六进制、任意文本（按 Java `String.hashCode`） | `0` |
| `--version <文本>` | Minecraft 版本，如 `1.21.6`、`18.2`、`26.3` | 最新版 |
| `--limit <n>` | 最多枚举多少座要塞 | `128` |
| `--index <n>` | 只显示第 n 座（0 基） | 全部 |
| `--eyes <n>` | 只显示恰好 n 眼的 | 不过滤 |
| `--min-eyes <n>` | 只显示至少 n 眼的 | 不过滤 |
| `--pieces` | 同时列出每个生成部件 | 不列 |
| `--type <名>` | 配合 `--pieces`：只看某一类部件 | 全部 |
| `--footprint` | 同时显示整座要塞的平面覆盖 | 不显示 |
| `--mask` | 配合 `--footprint`：ASCII 俯视图（隐含 `--footprint`） | 不显示 |
| `--csv` / `--json` | 结构化输出 | 表格 |
| `--quiet` | 不打印摘要 | 打印 |
| `--check` | 校验不变式，失败返回 1 | 不校验 |
| `--versions` | 列出所有支持的版本 | — |
| `--all-versions` | 同一 seed 跑遍所有版本 | — |

`--type` 取值：`straight`、`prison`、`left`、`right`、`crossing`、`straight_stairs`、`stairs`、`five`、`chest`、`library`、`portal`、`filler`。

## 3. 输出

### 3.1 表格

```
$ stronghold-sim --seed 123 --version 1.21.6 --limit 3
   #  start X  start Z   portal X  portal Z   eyes  3x3 frame               status
   0     -476    -2620       -521     -2651   4/12  -522..-520, -2652..-2650  ok
   1     1988      788       1976       847   2/12  1975..1977, 846..848      ok
   2    -1852     1636      -1870      1611   1/12  -1871..-1869, 1610..1612  ok
```

| 列 | 含义 |
|---|---|
| `start X/Z` | **要塞起点**：起始楼梯（chunk 内 `(4,4)`）。所有要塞查找器和地图图标给的都是这个，**不是传送门** |
| `portal X/Z` | **传送门中心**：3×3 `end_portal` 区域正中。瞄准这里下去 |
| `eyes` | 默认已生成的末影之眼数；`?` 表示该版本不建模眼数（1.13 前） |
| `3x3 frame` | 3×3 门方块的 X / Z 范围（含端点） |

`start` 与 `portal` 通常相距 20–60 格。

### 3.2 部件列表（`--pieces`）

```
$ stronghold-sim --seed 123 --version 1.21.6 --index 0 --pieces
      ord code  rot depth    X range     Z range    nominal Y chests
        0 SHSD    0     0  -478..-474  -2622..-2618      64..74 -
       17 SHLi    2     6  -495..-482  -2601..-2587      68..78 2 @ (-492,-2596) ...
       35 SHPR    3     8  -526..-511  -2656..-2646      60..67 -
      (227 pieces)
```

### 3.3 平面覆盖（`--footprint`）

```
$ stronghold-sim --seed 123 --version 1.21.6 --index 0 --footprint
      footprint : X -565..-434  Z -2670..-2563   (132 x 108 blocks)
      chunks    : X -36..-28  Z -167..-161   (63 chunks)
      covered   : 6185 blocks of 14256 in the box (43.4%)
      by type   :
        SHS   Straight corridor    x63   X -565..-449  Z -2660..-2568  (2124 blocks)
        SHLi  Library              x2    X -495..-434  Z -2632..-2587  (420 blocks)
        SHPR  Portal room          x1    X -526..-511  Z -2656..-2646  (176 blocks)
        ...
```

`covered` 是**真实矩形并集**，不是各部件面积相加。实测这组数据里直接相加是 7862，真实并集 6185——**虚高 27%**（部件会互相重叠，`SHFC` 填充走廊的存在就是为了封堵缝隙）。

`--mask` 再给一份 ASCII 俯视图。

### 3.4 结构化输出

JSON 与 CSV 的字段名里**没有** `y` / `minY` / `maxY`，Y 一律以 `nominal*` 出现。

```json
{
  "startX": -476, "startZ": -2620,
  "portalX": -521, "portalZ": -2651,
  "frame": { "minX": -522, "minZ": -2652, "maxX": -520, "maxZ": -2650 },
  "eyes": 4,
  "footprint": { "width": 132, "depth": 108, "area": 6185, ... }
}
```

### 3.5 不变式校验（`--check`）

`--check` 会对每一座枚举到的要塞跑一组硬性校验，任一失败即以状态 1 退出：门框恒为 3×3、门中心恰为门框中点、眼数在 0–12、门在传送门房内、门在平面覆盖内、覆盖面积在 `(0, 包围盒]` 内、各类部件计数之和等于部件总数、每座恰有 1 个 `SHPR`、包围盒未反转、箱位落在所属部件内。

## 4. 版本兼容性

**1.0 – 26.3，共 33 个档位**（`--versions` 可列出）。

| 版本段 | 传送门 X/Z | 眼数 / 箱位 | 原因 |
|---|---|---|---|
| 1.0 – 1.12.2 | ✅ 精确 | ❌ 报 `null` | 眼数与箱位由 `getStrongholdLoot()` 产出，它需要 structure salt config，cubiomes 只从 1.13 起提供。此时回退到 `getStrongholdPieces()`：布局仍精确，但不建模眼数，报 `null` 而不是误导性的 0 |
| 1.13 – 26.3 | ✅ 精确 | ✅ 精确 | salt 分四档：`{2,1}` / `{5,0}` / `{4,8}` / `{4,19}` / `{4,37}` |

低于 1.0（含 Beta 1.8）会被拒绝——那时要塞还不存在。

```bash
$ stronghold-sim --seed 123 --all-versions
version       start X   portal X   portal Z   eyes pieces
26.3             -476       -521      -2651      2 227
1.21.6           -476       -521      -2651      4 227
1.19.2           -444       -401      -2528      2 363
1.18             -444       -401      -2528      1 363
1.13             -476       -490      -2622      2 166
1.12             -508       -477      -2579      ? 194
```

## 5. 原理

```
注意：起始楼梯与传送门是两个不同的点
nextStronghold()                     → 要塞起点（起始楼梯）
  ↓ chunkX = startX >> 4
getStrongholdLoot(pieces, 400, ...)  → 完整布局 + 每个部件的 additionalData
  ↓ 找 type == SH_PORTAL_ROOM
rotPos(bb0, bb1, local, rot)         → 局部坐标转世界坐标
```

| 量 | 房间局部坐标 | 说明 |
|---|---|---|
| 3×3 门方块 | `x 4..6 × z 9..11` | 12 个眼框环的内圈 |
| 门中心 | `(5, 10)` | 报告值 |
| 12 个眼框 | `x 3..7 × z 8..12` 去四角 | `additionalData` 第 0–11 位 |

眼数 = `(additionalData & 0xFFF)` 的 popcount。这 12 次判定在每个与传送门房相交的区块里各重播一遍，只有眼位落在当前区块才置位，所以跨区块的门房也被正确合并。

**部件上限**：`getStrongholdPieces()` 对缓冲区长度敏感。达到 400 上限仍未找到传送门房时标记 `truncated` 并且**不推导任何坐标**——宁可报错也不给可能错的位置。

## 6. 作为库使用

`src/stronghold_sim.h` 是干净的 C 接口，不依赖 CLI：

```c
#include "stronghold_sim.h"

/* 只要门和眼数 */
StrongholdSim p;
if (ssim_locateFirst(&p, MC_1_21_6, 123) && p.ok)
    printf("portal (%d, %d), eyes %d/12\n", p.portalX, p.portalZ, p.eyes);

/* 要全部房间范围 */
StrongholdSimPiece pieces[SSIM_PIECE_CAPACITY];
int n = ssim_compute(&p, pieces, SSIM_PIECE_CAPACITY, MC_1_21_6, 123, p.chunkX, p.chunkZ);
for (int i = 0; i < n; i++)
    printf("%-5s X %d..%d Z %d..%d\n", pieces[i].code,
           pieces[i].minX, pieces[i].maxX, pieces[i].minZ, pieces[i].maxZ);
```

| 函数 | 用途 |
|---|---|
| `ssim_compute(&p, pieces, n, mc, seed, cx, cz)` | 已知起始区块，算一座（可选导出部件） |
| `ssim_locateFirst(&p, mc, seed)` | 原点最近的那座 |
| `ssim_locateAll(&arr, n, mc, seed)` | 全部（≤128） |
| `ssim_unionArea(rects, n, scratch)` | 任意矩形精确并集面积 |
| `ssim_footprintMask(pieces, n, &p, bits, max, &w, &h)` | 逐块覆盖掩码 |
| `ssim_versionSupported(mc)` / `ssim_lootSupported(mc)` | 版本能力查询 |
| `ssim_versionAt(i)` / `ssim_versionCount()` | 遍历支持版本 |

`ssim_prepare(mc, seed)` / `ssim_release()` 用来显式管理内部 biome 生成器缓存；`sim_locateAll` 自己会处理。

## 7. 不判断「天然点亮的传送门」

只报**眼数**。cubiomes 完全不建模方块：`stronghold.c` 里 11 行 `generateBox()` 全被注释，只保留「跳过 760 次 RNG」以对齐随机流。`end_portal` 方块是否被写入世界取决于原版 `PortalRoom` 的方块放置，cubiomes 没有对应实现。

眼数 `eyes` 就是「需要自带几个末影之眼」的准确答案，比「门是否亮着」对玩家更有用——后者在数学上需要约 `1e12` 座要塞才会命中一次（每眼 10%，12 眼全满 = `1e-12`）。

## 8. 文件结构

```
stronghold-simulator/
├── .gitignore
├── CMakeLists.txt
├── LICENSE
├── README.md
├── src/
│   ├── stronghold_sim.h     公开接口 + 结构体
│   ├── stronghold_sim.c     布局驱动、坐标换算、眼数、部件导出、平面覆盖
│   └── main.c               命令行前端
└── third_party/cubiomes/    git 子模块
```

## 9. 许可

本项目以 MIT 许可发布，全文见 [LICENSE](LICENSE)。

依赖的 [cubiomes](https://github.com/xpple/cubiomes) 同样是 MIT 许可（见 `third_party/cubiomes/LICENSE`），与本项目的许可兼容。
