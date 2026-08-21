# 第 29 章 FireBlast 实施计划

## 1. 目标与实现边界

本章新增一个火系主动技能 `FireBlast`：角色一次向四周发射多颗火球；火球先向外扩散，到达最大距离后返回施法者；飞出和返回途中可以伤害经过的敌人；全部返回完成后，以施法者为中心触发一次范围爆炸。

实现采用“服务器裁定、Actor 复制、客户端本地表现”的结构：

- 服务器激活技能、扣除法力、施加冷却、生成火球并结算所有伤害。
- 每颗火球是复制 Actor，客户端根据复制的位置显示运动，不为每颗火球发送运动 RPC。
- 飞行命中特效由火球在各端的本地碰撞回调调用 `InvokeGameplayCueEvent`，不通过 `ExecuteGameplayCue` 逐次发送网络 RPC。
- 爆炸伤害只执行一次，由服务器在所有火球返回或超时后统一触发。

不要直接修改 `AAuraProjectile`。现有投射物在第一次有效碰撞后立即销毁，适合 FireBolt，不适合 FireBlast 的往返、多目标伤害和最终爆炸。应新增专用 `AAuraFireBall`，但复用现有投射物的碰撞通道、阵营判断和 Damage Spec 做法。

## 2. 对照现有实现

| FireBlast 内容 | 参照位置 | 复用方式 |
| --- | --- | --- |
| GAS 伤害参数与 Spec | `UAuraDamageGameplayAbility` | 使用 `FAuraDamageEffectParams` 和 `MakeDamageEffectSpec` |
| 投射物碰撞与阵营过滤 | `AAuraProjectile` | 使用 `ECC_Projectile`、Overlap、`IsNotFriend` |
| 多点范围伤害 | `UAuraArcaneShards::TriggerShardAtPoint` | 使用 `GetLivePlayersWithinRadius`，逐目标应用 Spec |
| 消耗与冷却 | `GA_ArcaneShards`、`GE_Cost_ArcaneShards`、`GE_Cooldown_ArcaneShards` | 新建 FireBlast 对应 GE，激活时 `CommitAbility` |
| 技能信息与技能框 CD | `DA_AbilityInfo`、`WBP_SpellGlobe` | 配置 AbilityTag、CooldownTag，沿用 AbilityInfo 委托链 |
| 本地命中特效 | Gameplay Cue 路由 + `AAuraProjectile::OnSphereOverlap` | 在本地碰撞时直接 `InvokeGameplayCueEvent`，不发送 Cue RPC |

## 3. 关键数据与类设计

### 3.1 新增 Gameplay Tags

在 `FAuraGameplayTags` 中增加并注册：

- `Abilities.Fire.FireBlast`
- `Cooldown.Fire.FireBlast`
- `GameplayCue.FireBlast.Hit`
- `GameplayCue.FireBlast.Explosion`

`Cooldown.Fire.FireBlast` 必须同时配置到冷却 GE 和 `DA_AbilityInfo`，否则技能能冷却但技能框不会显示倒计时。

### 3.2 新增技能类 `UAuraFireBlast`

继承 `UAuraDamageGameplayAbility`，职责仅限：

- 校验并提交技能消耗、冷却。
- 计算火球数量和环形方向。
- 在服务器生成 `AAuraFireBall`。
- 跟踪本次施法尚未返回的火球数量。
- 在所有火球返回或超时后触发一次中心爆炸伤害。
- 正确结束 Ability，不让实例长期残留。

建议蓝图可配参数：

- `FireBallClass`
- `NumFireBalls`，默认 12
- `MaxFireBalls`，例如 24
- `SpawnRadius`，默认 80 cm
- `ExplosionRadius`，默认 350 cm
- `ReturnTimeout`，默认 5 s
- `TravelDamageParams`
- `ExplosionDamageParams`

如果数量随等级增长，统一使用：

`ActualCount = Clamp(BaseCount + PerLevelCount * (AbilityLevel - 1), 1, MaxFireBalls)`

### 3.3 新增 Actor `AAuraFireBall`

核心组件：

- `USphereComponent`：Overlap 检测，使用 `ECC_Projectile`。
- `UNiagaraComponent` 或 Mesh：火球本体表现。
- 可选 `UProjectileMovementComponent`：只用于初始方向/速度配置；真正的往返位置建议由 Actor 状态更新。

核心状态：

```text
Outgoing -> Returning -> Arrived
              |
              +-> Timeout/OwnerInvalid -> Destroyed
```

关键复制数据：

- `SourceActor`：施法者。
- `InitialDirection`：环形发射方向。
- `MaxTravelDistance`。
- `OutgoingDuration`、`ReturnDuration` 或对应曲线。
- `TravelDamageSpecHandle`。
- 当前阶段及阶段开始时间。

需要提供一个服务器回调/委托通知技能实例“该火球已返回”。必须保证每颗火球最多上报一次。

### 3.4 伤害去重

火球不应像普通 Projectile 一样命中即销毁。每颗火球维护本次阶段已伤害对象集合：

- `TSet<TWeakObjectPtr<AActor>> OutgoingHitActors`
- `TSet<TWeakObjectPtr<AActor>> ReturningHitActors`

同一敌人在飞出阶段最多受该火球一次伤害，返回阶段可以再受一次。死亡、无 ASC、友军、施法者直接忽略。

## 4. 按章节执行步骤

## 步骤 1：29.1 FireBlast Ability——建立技能骨架

### 实施

1. 在 `AuraGameplayTags.h/.cpp` 注册 `Abilities.Fire.FireBlast` 和相关标签。
2. 新建 `UAuraFireBlast : UAuraDamageGameplayAbility`。
3. 新建蓝图 `GA_FireBlast`，父类选择 `UAuraFireBlast`。
4. 在 `GA_FireBlast` 设置：
   - Ability Tag：`Abilities.Fire.FireBlast`
   - Ability Type：Offensive
   - Instancing：沿用 C++ 的 `InstancedPerExecution`
   - Net Execution：`LocalPredicted`
5. 在 `DA_AbilityInfo` 添加 FireBlast 行，配置技能类、名称、图标、背景和等级要求。
6. 将技能放入现有 Spell Menu 火系技能树，确认能够解锁、装备到技能框并触发。

### 验收

- Spell Menu 中出现 FireBlast，名称和图标正确。
- 技能可装备到任意输入槽。
- 按键后 Ability 成功激活并正常结束，没有重复激活或永久 Active。
- 暂未生成火球属于本步骤预期。

## 步骤 2：29.2 FireBlast Cost and Cooldown——消耗与冷却

### 实施

1. 新建 `GE_Cost_FireBlast`：Instant，修改 Mana，数值使用负的 Scalable Float。
2. 新建 `GE_Cooldown_FireBlast`：Has Duration，授予 `Cooldown.Fire.FireBlast`。
3. 在 `GA_FireBlast` 配置 Cost GE 和 Cooldown GE。
4. 激活后只在确认能释放时调用一次 `CommitAbility`；失败立即取消，不生成火球。
5. 在 `DA_AbilityInfo` 的 FireBlast 行设置 `CooldownTag = Cooldown.Fire.FireBlast`。
6. 技能描述支持 `{ManaCost}`、`{Cooldown}`，沿用 `UAuraGameplayAbility::GetResolvedDescription`。

### 验收

- 法力不足时技能不激活、不进入冷却、不生成任何 Actor。
- 成功释放时只扣一次法力、只施加一次冷却。
- 技能框立即显示 CD 遮罩和倒计时，结束后恢复。
- 升级技能后，消耗和冷却按 GE 曲线正确变化。

## 步骤 3：29.3 Aura Fire Ball——专用火球 Actor

### 实施

1. 新建 `AAuraFireBall` C++ 类及 `BP_FireBall` 蓝图。
2. 配置复制：`bReplicates = true`，启用移动复制；服务器拥有生命周期控制权。
3. Sphere 碰撞参照 `AAuraProjectile`：忽略默认通道，仅对 WorldStatic、WorldDynamic、Pawn 使用 Overlap。
4. 添加火球 Niagara、飞行音效和必要的光照组件；表现组件不参与伤害裁定。
5. 定义 `Outgoing`、`Returning`、`Arrived` 三种状态。
6. 火球保存施法者弱引用；施法者失效或死亡时安全销毁，避免空指针。

### 验收

- 服务器生成一颗火球时客户端可见，位置同步。
- 火球不会与施法者碰撞，也不会因碰到第一个敌人立即销毁。
- 施法者销毁、切图或技能取消时火球能安全清理。
- Dedicated Server/两客户端 PIE 下无重复火球。

## 步骤 4：29.4 Spawning FireBalls——环形生成

### 实施

1. `UAuraFireBlast::SpawnFireBalls` 只在 Authority 执行。
2. 以角色位置和水平面为基准，均匀计算方向：

   `Yaw = Index * 360 / NumFireBalls`

3. 使用 `SpawnActorDeferred` 生成，生成前写入：SourceActor、Direction、伤害 Spec、最大距离和时间参数。
4. 生成点为角色位置加 `Direction * SpawnRadius`，Z 使用角色中心或战斗 Socket 的高度。
5. 为本次施法记录火球数量和每个火球的完成回调。
6. 不在循环中调用 Gameplay Cue 网络 RPC。

### 验收

- 默认一次生成 12 颗，角度间隔一致，覆盖完整 360°。
- 火球数量修改为 1、10、12、24 均无除零、重叠或漏生成。
- 多人 PIE 中服务器与客户端看到相同数量和方向。
- 一次释放只扣一次法力和产生一次冷却。

## 步骤 5：29.5 FireBall Timelines——飞出与返回

### 实施

1. 使用一条归一化位移曲线或 C++ 平滑函数驱动火球，不在每颗 Actor 内创建蓝图 Timeline 组件。
2. Outgoing 阶段保存 `StartLocation` 和 `OutboundTarget = StartLocation + Direction * MaxTravelDistance`。
3. 通过 `OutgoingDuration` 将位置从 Start 插值到 OutboundTarget；到达后切换 Returning。
4. Returning 阶段每帧读取施法者当前世界位置作为目标，因此角色移动后火球仍返回当前角色，而不是旧坐标。
5. 返回位置进入 `ArrivalRadius` 后切换 Arrived，关闭碰撞和表现并通知技能。
6. 增加最大生命周期/超时兜底，防止状态永远无法结束。

推荐插值：

- 飞出：`EaseOut`，前快后慢。
- 返回：`EaseIn`，越靠近角色越快。
- 所有时间和距离参数在 `BP_FireBall` 或 `GA_FireBlast` 可配置。

### 验收

- 火球能到达配置的最大距离，随后平滑返回。
- 返回期间角色移动，火球能持续追踪角色当前位置。
- 低帧率和高帧率下总时长与最大距离基本一致。
- 火球到达后只回调一次，并全部销毁或进入等待爆炸的隐藏状态。

## 步骤 6：29.6 Causing FireBall Damage——往返沿途伤害

### 实施

1. Sphere Overlap 在服务器做伤害裁定；客户端只处理表现。
2. 排除 SourceActor、友军、死亡对象和没有 ASC 的对象。
3. Outgoing 使用 `OutgoingHitActors` 去重；Returning 使用 `ReturningHitActors` 去重。
4. 使用技能生成的 `TravelDamageSpecHandle` 对目标 ASC 应用 Gameplay Effect。
5. 每次命中根据火球运动方向设置 Knockback/DeathImpulse；返回阶段可降低或关闭击退，避免把敌人持续推向玩家。
6. 命中不销毁火球，运动状态保持不变。

### 验收

- 单颗火球飞出时对同一敌人只造成一次伤害。
- 返回时再次经过同一敌人，可以再造成一次伤害，但不会每帧连续结算。
- 多颗火球命中同一敌人时，各自正常造成伤害。
- 友军、施法者、尸体不受伤。
- 伤害数字、抗性、暴击、格挡和火系 Debuff 沿用现有 GAS 结算。

## 步骤 7：29.7 FireBall Explosive Damage——最终范围爆炸

### 实施

1. `UAuraFireBlast` 在 `ReturnedCount == SpawnedCount` 时调用 `ExplodeAtOwner`。
2. 使用 `GetLivePlayersWithinRadius` 获取 `ExplosionRadius` 内目标，并使用 `IsNotFriend` 过滤。
3. 每个目标只应用一次 `ExplosionDamageParams` 对应的 Spec。
4. 击退方向从角色指向目标；DeathImpulse 同方向并附加小幅 Z 分量，做法参照 Arcane Shards。
5. 爆炸表现使用一次 `GameplayCue.FireBlast.Explosion`。这是每次技能仅一次的 Cue，可以走标准 `ExecuteGameplayCue` 网络路径。
6. 超时情况下，以已返回/仍存活火球的状态安全收尾，并确保爆炸最多一次。
7. 爆炸完成后结束 Ability、清除计时器和 Actor 回调。

### 验收

- 所有火球返回后只爆炸一次。
- 范围内每个敌人只受到一次爆炸伤害，范围外不受伤。
- 角色移动后，爆炸中心使用角色当前位置。
- 火球被提前销毁或丢失时，超时兜底仍能结束技能，不永久占用 Ability。
- 爆炸击退方向以玩家为中心向外。

## 步骤 8：29.8 Execute Local Gameplay Cues——规避 Cue RPC 上限

### 原因

如果服务器在同一网络更新中为 12 颗以上火球逐个调用 `ExecuteGameplayCue`，会生成大量同函数 Multicast RPC，可能触发引擎单次网络更新的 RPC 数量限制。火球 Actor 的生成与移动已经复制，因此命中特效不需要再由服务器逐个广播。

### 实施

1. 新建 `GC_FireBlast_Hit`，Tag 为 `GameplayCue.FireBlast.Hit`，配置命中 Niagara 和音效。
2. 每个客户端的 `AAuraFireBall::OnSphereOverlap` 都会收到本地重叠事件；在该回调中构造 `FGameplayCueParameters`：
   - `Location = SweepResult.ImpactPoint`，无 Sweep 时使用火球位置。
   - `Normal = SweepResult.ImpactNormal`。
   - `Instigator = SourceActor`。
   - `EffectCauser = this`。
3. 直接调用本地路由：

   `ASC->InvokeGameplayCueEvent(HitCueTag, EGameplayCueEvent::Executed, CueParameters);`

   不调用 `ExecuteGameplayCue`、`NetMulticast_InvokeGameplayCue...` 或自定义 Multicast RPC。
4. 本地 Cue 必须与伤害去重分离：服务器伤害集合决定是否结算伤害；各客户端可以使用独立的表现去重集合，防止组件持续重叠导致重复特效。
5. 如果客户端碰撞预测与服务器略有差异，允许只影响视觉，绝不能在本地 Cue 中应用伤害。
6. 爆炸 Cue 每次技能仅一次，继续使用标准网络 Gameplay Cue，不属于 RPC 风险点。

### 验收

- 将火球数量设为 12、20、24 连续释放，日志中不再出现 `NetMulticast_InvokeGameplayCue...` 的 RPC 数量警告。
- 每个客户端都能看到命中特效，Dedicated Server 不尝试渲染。
- 命中特效次数与有效碰撞一致，不随帧率重复刷出。
- 关闭命中特效不影响服务器伤害；断言伤害逻辑与表现逻辑完全解耦。
- 网络延迟下火球、伤害和爆炸仍能完成，不会因为本地 Cue 丢失导致技能卡住。

## 5. 蓝图与资产清单

建议路径保持与现有工程一致：

```text
/Game/Blueprints/AbilitySystem/GameplayAbilities/Attack/Ranged/FireBlast/
  GA_FireBlast
  GE_Cost_FireBlast
  GE_Cooldown_FireBlast

/Game/Blueprints/Actor/FireBlast/
  BP_FireBall

/Game/Blueprints/AbilitySystem/Cues/
  GC_FireBlast_Hit
  GC_FireBlast_Explosion
```

C++ 文件建议：

```text
Source/Aura/Public/AbilitySystem/Abilities/AuraFireBlast.h
Source/Aura/Private/AbilitySystem/Abilities/AuraFireBlast.cpp
Source/Aura/Public/Actor/AuraFireBall.h
Source/Aura/Private/Actor/AuraFireBall.cpp
```

数据资产需要更新：

- `DA_AbilityInfo`：FireBlast 技能条目、CooldownTag、文案、图标和技能类。
- 如项目角色启动技能列表或职业配置中维护技能类，则加入 `GA_FireBlast`。
- Spell Menu 火系技能树加入 FireBlast 节点与前置关系。

## 6. 建议初始参数

| 参数 | 初始值 | 说明 |
| --- | ---: | --- |
| 火球数量 | 12 | 足以覆盖并验证超过 10 个的网络场景 |
| 生成半径 | 80 cm | 避免与角色胶囊重叠 |
| 最大飞行距离 | 700 cm | 形成清晰扩散范围 |
| 飞出时长 | 0.8 s | 前快后慢 |
| 返回时长 | 0.65 s | 回收略快 |
| 到达半径 | 60 cm | 防止高速越过目标点 |
| 火球碰撞半径 | 35 cm | 根据特效视觉尺寸调整 |
| 爆炸半径 | 350 cm | 与火球回收视觉匹配 |
| 超时 | 5 s | 防止技能卡死 |

最终数值应通过 CurveTable 或 GE 的 Scalable Float 调整，不在蓝图事件图中散落常量。

## 7. 最终回归验收

按以下顺序执行，前一项通过后再测试下一项：

1. 单机：技能装备、消耗、冷却、文案和 CD UI。
2. 单机：1 颗火球的飞出、返回、两阶段伤害和最终爆炸。
3. 单机：12/24 颗环形分布、去重伤害、一次爆炸。
4. Listen Server + 1 Client：双方数量、运动、命中特效和伤害一致。
5. Dedicated Server + 2 Clients：服务器只裁定，不渲染；两客户端表现正常。
6. 人为制造 150 ms 延迟和丢包：技能最终仍能爆炸并结束。
7. 施法期间移动、死亡、切换地图或 Actor 被销毁：没有残留火球、计时器或 Active Ability。
8. 连续释放 24 颗火球：无 RPC 数量警告、无重复爆炸、无客户端崩溃。

## 8. 完成定义

只有同时满足以下条件，才算第 29 章完成：

- FireBlast 能解锁、装备、释放，并正确显示蓝耗、CD 与技能文案。
- 火球数量、半径、时长、伤害等关键参数可配置且支持等级缩放。
- 火球完成完整的飞出、返回和最终爆炸流程。
- 飞出与返回伤害具有明确的逐阶段去重规则。
- 所有伤害由服务器权威结算，客户端本地 Cue 只负责表现。
- 超过 10 颗火球时不产生 Gameplay Cue Multicast RPC 限制警告。
- 单机、Listen Server 和 Dedicated Server 场景均通过验收。
