

# Katamino Tower

<img src="./logo.png" width="300">

> [百变方块立体塔](https://en.gigamic.com/family-games/1109-katamino-tower.html)求解器

简体中文 | [English](./README.md)

## 使用

### 3D 交互版

```bash
open katamino_tower_solver.html
```
- **拖拽**移动积木，**R** 翻转五联骨牌，**算法求解**3D可视化

- https://niyongsheng.github.io/katamino-tower/katamino_tower_solver.html

### C 命令行版

```bash
# 编译（默认单线程）
gcc -o katamino_tower_solver katamino_tower_solver.c

# 编译（OpenMP 多线程加速穷举, macOS）
brew install libomp
gcc -I$(brew --prefix libomp)/include -Xclang -fopenmp \
    -lomp -L$(brew --prefix libomp)/lib \
    -o katamino_tower_solver katamino_tower_solver.c

# 随机求解（默认，2000 次尝试内找一个解）
./katamino_tower_solver

# 穷举模式：遍历所有环配置，统计解的总数
# 固定 Ring0 旋转=0，共 120×12^4 ≈ 250 万种配置
./katamino_tower_solver -a

# 穷举模式，找到 N 个解后停止
./katamino_tower_solver -a -n 10
```

## 规则
用15块积木围绕中心立柱拼成完整的5层圆柱体。每层填满 12 列（30°/列）。

ps:包装上的6岁+是认真的吗❓

## 算法

MRV（最少剩余值）回溯搜索，两阶段求解：

1. **环排列** — 5 个环随机分配到 5 层并随机旋转，确定每层被环外弧占用的列
2. **MRV 回溯** — 预计算所有五联骨牌的合法放置，每次选候选最少的空格尝试，随机化搜索顺序

外层最多尝试 2000 次随机环配置，通常在 10~200 次内找到解。

## 联系

+  [Email](mailto:yongshengni@gmail.com)
+  [WhatsApp](https://wa.me/8618853936112)
