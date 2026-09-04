# Aura 项目面试八股与源码口径

> 本文只描述当前仓库中能够由源码或配置证明的实现。答案分为“已实现”“部分实现”“未实现”三种口径。面试时不要把“可行的后续方案”说成已经完成的功能。

## 0. 项目口径速查

| 主题 | 当前真实状态 |
| --- | --- |
| 联机模型 | Listen Server，局域网直连 IP，服务端权威 |
| 大厅与账号 | 未接入 OnlineSubsystem、Session、Steam/EOS、账号系统 |
| 地图切换 | 非 Seamless 的相对 `ServerTravel`，客户端跟随服务端 |
| GAS | 玩家 ASC 位于 PlayerState，Mixed；敌人 ASC 位于 Pawn，Minimal |
| 伤害 | GE + ExecutionCalculation + IncomingDamage，核心命中由服务端产生 |
| 存档 | 服务端同步 SaveGame，v3 世界快照；玩家仍按会话索引归属 |
| AI | Behavior Tree/Blackboard；导航使用 Recast + Detour Crowd |
| 玩家自动寻路 | 服务端算路径，EQS 选落点/脱困点，Crowd 局部避让，客户端移动预测 |
| UI | 战斗界面使用 WidgetController 委托式 MVC；存档界面使用 UE MVVM |
| 对象池 | UWorldSubsystem 管理敌人、普通投射物和 FireBlast 火球 |
| 测试 | 主要是 PIE 双端和日志回归；未实现 Automation/Functional Test |

主要源码入口：

- 网络、自动寻路：`AuraPlayerController.cpp`
- 地图、存档、复活：`AuraGameModeBase.cpp`
- GAS：`AuraAbilitySystemComponent.cpp`、`AuraAttributeSet.cpp`、`ExecCalc_Damage.cpp`
- AI：`AuraEnemy.cpp`、`AuraAIController.cpp`、`AuraAutoMoveEQS.cpp`
- 对象池：`AuraProjectilePoolSubsystem.cpp`、`AuraEnemyPoolSubsystem.cpp`
- UI：`UI/WidgetController`、`UI/ViewModel`

---

## 1. UE 对象模型、架构与生命周期

### 1.1 GameMode、GameInstance、PlayerController、PlayerState、Pawn 在项目中分别负责什么？

**已实现。**

- `UAuraGameInstance` 只保存当前选择的槽名和槽索引，跨地图保留。
- `AAuraGameModeBase` 只在权威端存在，负责存档、世界恢复、地图 Travel、玩家死亡复活、登录和退出清理。
- `AAuraPlayerController` 负责本地输入、鼠标检测、自动寻路请求、网络 RPC 和退出请求。
- `AAuraPlayerState` 持有玩家 ASC、AttributeSet、等级、XP、属性点和技能点。
- `AAuraCharacter` 是 ASC 的 Avatar，负责移动、动画、摄像机、表现和当前 Pawn 生命周期。

将玩家 ASC 放在 PlayerState 的直接收益是：死亡后替换 Pawn 时，AbilitySpec、等级和属性容器不会随旧 Pawn 一起销毁，只需重新执行 `InitAbilityActorInfo(PlayerState, NewPawn)`。

### 1.2 ASC 的 OwnerActor 和 AvatarActor 分别是谁？为什么？

玩家的 OwnerActor 是 `AAuraPlayerState`，AvatarActor 是当前 `AAuraCharacter`。服务器在 `PossessedBy`、客户端在 `OnRep_PlayerState` 中初始化 ActorInfo。复活后 ASC 仍在 PlayerState，只把 Avatar 重新指向新 Pawn。敌人的 ASC 直接创建在 `AAuraEnemy` 上，因此 Owner 与 Avatar 都随敌人 Pawn 生命周期存在。

### 1.3 为什么复活后不能再次授予全部技能？

因为玩家 ASC 跨 Pawn 存活，重复 `GiveAbility` 会产生重复 AbilitySpec、重复被动监听和 UI 重复记录。项目用 PlayerState 中服务端布尔量 `bCharacterDataInitialized` 做门控：第一次 Possess 初始化默认属性和技能；复活只重绑 ActorInfo，并恢复被死亡流程取消的常驻能力、已装备被动。

### 1.4 项目如何处理 UObject/Actor 引用失效？

持久拥有关系主要使用 `TObjectPtr`；定时复活表、离线 Controller、技能目标、FireBall 命中集合等可能失效的跨帧引用使用 `TWeakObjectPtr`；访问前使用 `IsValid`。退出时还会取消 Ability、清 Timer、清 AI Target，减少延迟回调访问 Pending Kill 对象的机会。

### 1.5 为什么对象池使用 UWorldSubsystem，而不是 GameInstanceSubsystem？

**已实现为 UWorldSubsystem。** 敌人和投射物属于具体 World，地图切换后不应带到新 World。WorldSubsystem 与 World 同生命周期，能直接 Spawn Actor、访问 NetMode，并在 `Deinitialize` 销毁池对象。GameInstanceSubsystem 会跨地图存在，容易保留旧 World Actor 引用；ActorComponent 又要求选定一个宿主 Actor，不适合做全局按类分桶的池。

### 1.6 项目是否使用 Seamless Travel？

**未实现。** 源码没有设置 `bUseSeamlessTravel`，也没有实现 Seamless Travel 迁移逻辑。当前使用相对 `ServerTravel(MapPath, false)`；跨图状态依赖 SaveGame 和新 World 中的恢复流程，而不是依赖 Seamless Travel 保留 PlayerController/PlayerState。

### 1.7 项目如何使用接口降低耦合？

`IAbilitySystemInterface` 用于统一获取 ASC；`ICombatInterface` 暴露等级、死亡、攻击插槽、蒙太奇等战斗能力；`IPlayerInterface` 暴露 XP、升级奖励、魔法阵等玩家行为；`IEnemyInterface` 处理鼠标高亮。技能和 AttributeSet 不需要硬编码每个具体角色类。

### 1.8 为什么客户端不能直接从 GameMode 取数据？

GameMode 只存在于服务器，客户端 `UGameplayStatics::GetGameMode` 会得到空。项目的存档 ViewModel 在取不到 AuraGameMode 时记录日志并返回；地图选择也只允许主机执行。需要同步给客户端的长期状态应放在 PlayerState/GameState 或通过 RPC/复制属性传递，不能让客户端依赖 GameMode。当前项目没有自定义 GameState 世界状态层。

### 1.9 OnRep_PlayerState 和 OnRep_Pawn 在项目中分别解决什么？

`AAuraCharacter::OnRep_PlayerState` 在客户端 PlayerState 可用后初始化 ASC ActorInfo 和 HUD；`AAuraPlayerController::OnRep_Pawn` 在初次占有/复活后清旧输入状态、重置 ASC 缓存，并显式重新初始化 CrowdFollowingComponent。后者是因为客户端 Pawn 替换后，路径组件必须绑定到新 Pawn 的 MovementComponent，不能继续持有旧 Pawn 状态。

---

## 2. 网络模型、复制与 RPC

### 2.1 项目的网络架构是什么？为什么选择 Listen Server？

项目是局域网 Listen Server 原型：主机同时是服务器和本地玩家，客户端通过 IP 直连。服务端负责伤害、死亡、复活、存档、地图切换、投射物和完整路径决策。它部署简单，适合个人项目双端验证；缺点是主机有零延迟优势、主机退出会结束会话、算力和带宽受主机限制，也不具备专用服务器的稳定性和安全性。

### 2.2 Authority、LocalController、NetMode 在本项目中分别解决什么判断？

- `HasAuthority()` 判断该 Actor 是否由当前进程权威控制，保护存档、生成、伤害、复活和路径决策。
- `IsLocalController()` 判断 Controller 是否属于当前机器的本地玩家，用于本地 UI/输入，以及区分 Listen Host 与远端玩家。
- `GetNetMode()` 区分 Standalone、Listen Server、Client，例如对象池不在 NM_Client 创建，客户端不能发起地图 Travel。

Listen Server 的主机 Controller 同时满足 Authority 和 Local；远端玩家在服务端上的 Controller 满足 Authority，但不是 Local。

### 2.3 Actor 属性什么时候复制？属性复制是可靠 RPC 吗？

项目中的 Actor 先启用复制，再通过 `GetLifetimeReplicatedProps` 注册属性。复制系统在 Actor 对连接 Relevant、属性脏且达到网络更新时机时发送状态。属性复制不是“每次变化对应一个 Reliable RPC”；中间值可以被合并，但最新状态会持续参与复制直到连接确认。因此 HP 从 100 连续变到 80、50，客户端可能只看到 50，这符合状态同步语义。

### 2.4 Gameplay Attribute 在本项目中如何复制？

Health、Mana、主属性、次级属性和抗性都使用 `ReplicatedUsing`，并通过 `DOREPLIFETIME_CONDITION_NOTIFY(..., COND_None, REPNOTIFY_Always)` 注册；OnRep 内调用 `GAMEPLAYATTRIBUTE_REPNOTIFY`，让 ASC 属性变化委托正确得到通知。`IncomingDamage`、`IncomingXP` 是瞬时 Meta Attribute，未注册复制，只用于 GE 执行后的服务端结算管线。

### 2.5 玩家和敌人的 ASC 为什么采用不同复制模式？

玩家 ASC 位于 PlayerState，设置 `Mixed`，拥有者需要较完整的 GameplayEffect 信息以支持技能、CD 和 UI，其他客户端只需较少信息。敌人 ASC 设置 `Minimal`，客户端主要需要 Tag/Cue 和最终属性表现，不需要每个敌人的完整 Active GE 细节，以降低大量 AI 时的网络开销。

### 2.6 项目中 Server、Client、Multicast RPC 各自用于什么？

- Server Reliable：自动寻路开始/停止、技能升级/装备、魔法阵确认、退出请求等关键命令。
- Server Unreliable：持续鼠标/光束/魔法阵位置，允许丢弃旧样本。
- Client Reliable：伤害数字、技能 UI 状态、自动移动开关、远端客户端返回主菜单。
- Client Unreliable：Crowd 转向速度，下一帧的新方向比补发旧方向更重要。
- Reliable Multicast：角色死亡、升级表现。
- Unreliable Multicast：投射物命中特效。

### 2.7 Listen Server 调用 Multicast，主机自己会执行吗？

会。由权威端调用的 Multicast 会在服务器本地实例执行，并发送给相关客户端。所以 `MulticastHandleDeath` 同时驱动主机和客户端的尸体表现。但要注意：客户端自己调用 Multicast 不会把调用转发给服务器和其他客户端。

### 2.8 项目的点击地面特效 Multicast 是否完全正确？

**存在实现边界。** `MulticastSpawnClickEffect` 定义为 NetMulticast，但当前从本地输入函数直接调用。远端客户端直接调用 Multicast 只会本地执行，不会广播到主机和其他客户端。若要求所有玩家都看到，正确链路应是 Client -> Server RPC -> 服务端调用 Multicast。当前不应在面试中声称它已实现全员同步。

### 2.9 Reliable RPC 是否绝对不会丢？网络抖动时死亡会不同步吗？

Reliable 表示连接存活时按序重传，不表示零延迟，也不能跨断线保证。项目的死亡判定在服务端完成，可靠 Multicast 播放死亡表现，随后 GameMode 停止 Controller、取消技能、清 Debuff，并在延迟后替换 Pawn。客户端即使暂时没收到死亡表现，也无法在服务端制造有效的后续权威伤害；但当前没有单独复制的 `bDead` 状态，极端延迟下仍可能短暂看到本地移动或预测技能，直到 Multicast、服务器纠正或 Pawn 替换到达。这是原型现有边界。

### 2.10 如何阻止死亡角色继续移动或施法？

本地输入入口会通过 `ICombatInterface::IsDead` 和 `Player.Block` 检查；死亡 Multicast 调用 Controller 的 `HandleControlledPawnDeath`，停止自动移动和瞄准；服务端死亡流程取消所有 Ability、移除 Debuff、停止 Controller Movement，并通知 AI 清目标。真正的伤害、死亡和投射物仍由服务端裁决。

### 2.11 客户端自动寻路为什么是服务端决策，而不是客户端上传路径点？

客户端只上传终点，服务端验证可导航路径并运行完整 EQS、Recast 和 Crowd。这样客户端不能通过伪造路径点穿墙，AI/玩家拥堵判断也使用同一份服务端世界状态。代价是主机 CPU 压力更大、客户端需要等待 RPC，且服务端 Crowd 转向必须额外下发给 owning client 来改善预测和朝向。

### 2.12 客户端自动移动如何降低卡顿？

服务端 CrowdFollowing 得到新转向速度后，以最多约 20Hz、方向急转或停止时立即发送 `ClientSetAutoMoveSteering`，向量使用 `FVector_NetQuantize10` 和 Unreliable RPC。客户端用该方向调用 `AddMovementInput`，接入 CharacterMovement 的本地预测；朝向通过插值更新。服务器复制位置仍是最终权威。

### 2.13 RPC 是否做了安全校验？

**部分实现。** 自动寻路会拒绝 NaN、无 Pawn、输入阻塞、无有效完整 NavPath 的请求；ArcaneShards 会校验 NaN、施法距离和 `CommitAbility`。但项目没有 RPC `_Validate`、调用频率限制、账号权限，也有不足：`ServerSpendSpellPoint` 自身没有再次检查技能点是否大于 0，主要依赖客户端 UI 前置判断；装备 RPC 的槽位类型校验也不完整。因此这是局域网原型，不应宣传为防作弊完备。

### 2.14 中途加入是否支持？

**基础引擎链路可用，产品级功能未完成。** Listen Server 正在监听时，客户端可通过当前 IP 直接连接当前地图；`PostLogin` 会延迟调用 `RestoreCurrentWorld`，等 Pawn/PlayerState 可用后恢复对应记录。世界 Actor 的当前状态由服务器复制。但没有 Session 搜索、加入 UI、账号认证和稳定玩家 ID；新玩家按会话索引匹配存档，可能复用离线玩家记录，因此不能称为完整的中途加入/断线重连系统。

### 2.15 客户端退出为什么曾导致服务端一起退出？现在如何处理？

旧逻辑把所有退出都处理成服务端 `ServerTravel`。现在同一个 Server RPC 在服务端检查请求 Controller：Listen Host 的本地 Controller 仍保存并 `ServerTravel`，远端 Controller 则先保存快照，再调用 `ClientReturnToMainMenuWithTextReason`，只让该客户端断开。`Logout` 清理复活 Timer、能力、Debuff、AI Target 和会话索引，主机 World 保持运行。

### 2.16 项目有没有 Replication Graph、Iris 或 Fast Array？

**没有。** 当前使用 UE 通用复制模型，日志也会显示没有自定义 ReplicationDriver。技能列表使用 GAS 自带 AbilitySpec 复制，没有自定义 FastArray；大量 AI 场景还未实现 ReplicationGraph/Iris 分区优化。

### 2.17 为什么 Server RPC 放在 PlayerController/ASC，而不是 GameMode？

远端客户端拥有自己的 PlayerController，玩家 ASC 的 Owner 又是该玩家 PlayerState，因此具备合法的 Client -> Server RPC ownership 链。GameMode 不复制到客户端，客户端也不拥有它，不能直接从客户端对 GameMode 发 Server RPC。项目先在 PlayerController/ASC 接收请求，再调用权威 GameMode 或执行服务端技能逻辑。

### 2.18 NetUpdateFrequency 和 ForceNetUpdate 在项目中怎样使用？

玩家 PlayerState 将 `NetUpdateFrequency` 提高到 100，减少 ASC/属性位于 PlayerState 时的状态更新延迟。池对象切换 Active 状态、敌人身份/等级、FireBall 状态时调用 `ForceNetUpdate`，让关键复用边界尽快进入复制，而不是等普通更新周期。项目没有按距离动态调频、Dormancy 管理或网络预算系统。

---

## 3. GAS 架构与技能系统

### 3.1 为什么使用 GAS，而不是把技能都写在 Character 中？

项目需要主动/被动技能、属性、资源消耗、CD、状态 Tag、伤害类型、Debuff、技能升级与装备、网络预测和 UI 推送。GAS 把 Ability、Effect、Attribute、Tag、Cue 和网络同步拆成可组合数据管线，Character 只维护 Avatar 和表现，避免每种技能直接耦合角色类。

### 3.2 Ability、GameplayEffect、AttributeSet、GameplayCue 在项目中的职责是什么？

- Ability 组织释放流程、目标数据、动画、投射物和 Commit。
- GameplayEffect 描述属性修改、持续时间、Granted Tag、CD 和 Debuff。
- AttributeSet 维护属性，并在 GE 执行后统一消费伤害/XP，处理死亡与升级。
- GameplayCue/Niagara 负责电击光束、命中、爆炸和状态视觉，不直接修改权威数值。

### 3.3 项目的技能输入如何映射到 AbilitySpec？

Enhanced Input 的 InputAction 在 `UAuraInputConfig` 中映射到 `Input.*` GameplayTag。自定义 InputComponent 把 Started/Completed/Triggered 分别绑定为 Pressed/Released/Held。ASC 遍历 AbilitySpec 的 DynamicSpecSourceTags，找到相同输入 Tag，再调用 `AbilitySpecInputPressed/Released` 或 `TryActivateAbility`。因此换键位/技能槽不需要在 Controller 中硬编码技能类。

### 3.4 技能状态机如何实现？

AbilitySpec 的动态 Tag 表示 `Locked -> Eligible -> Unlocked -> Equipped`，另一个 `Input.*` Tag 表示槽位。升级时根据 DataAsset 的等级要求创建 Eligible Spec；花技能点变为 Unlocked 或提升 Spec Level；装备时清理目标槽旧技能、更新新旧状态并 `MarkAbilitySpecDirty`；被动技能还会尝试激活，失败则回滚槽位与状态。

### 3.5 为什么修改 AbilitySpec 后要调用 MarkAbilitySpecDirty？

AbilitySpec 在服务端发生 Level 或 Dynamic Tag 变化后，需要标记脏才能进入 GAS 的复制更新。项目在恢复存档、解锁、升级、装备和清槽后调用它，否则客户端技能栏可能仍显示旧等级、旧状态或旧输入槽。

### 3.6 项目用了哪些 InstancingPolicy 和 NetExecutionPolicy？

FireBlast、ArcaneShards、BeamSpell 使用 `InstancedPerExecution` 或每次执行实例化，以隔离每次施法的 Timer、目标集合和状态；被动技能用 `InstancedPerActor`，在角色生命周期内持续监听事件。FireBlast 和 ArcaneShards 在 C++ 明确设为 `LocalPredicted`。其他技能若策略配置在 Blueprint 资产中，仅靠当前 C++ 无法统一断言。

### 3.7 Local Predicted 技能为什么仍要让伤害由服务端产生？

预测用于立刻响应输入、资源/CD体验或本地表现，不应让客户端决定命中伤害。FireBlast 的客户端预测实例不会生成火球，`SpawnFireBalls` 有 Authority Guard；ArcaneShards 的落点通过 Server RPC 校验距离并在服务端 Commit；普通投射物只在服务端对象池 Acquire，Overlap 后也只有 Authority 应用 Damage Spec。

### 3.8 ExecutionCalculation 在服务端还是客户端执行？

准确回答是：ExecCalc 在“应用并执行该 GE 的 ASC 所在端”运行，不是类本身天然只运行服务端。本项目通过服务端生成投射物、服务端命中、服务端应用伤害 GE，使核心伤害 ExecCalc 实际由服务端产生权威结果。部分通用 Helper（如 `CauseDamage`）本身没有统一 Authority Guard，因此不能脱离具体技能调用链声称任何调用都绝不会在客户端执行；生产化时应在统一伤害入口再加权威断言。

### 3.9 伤害计算管线是什么？

1. Ability 用 SetByCaller 写入火、雷、奥术、物理伤害及 Debuff 参数。
2. `ExecCalc_Damage` 捕获攻击方/目标属性。
3. 分伤害类型计算抗性减伤。
4. 判定格挡并减半。
5. 按等级曲线计算护甲穿透和有效护甲减伤。
6. 计算暴击率、暴击抗性和暴击额外伤害。
7. 把结果输出到 Meta Attribute `IncomingDamage`。
8. AttributeSet 消费 IncomingDamage，扣 HP、处理 Debuff/击退/受击/死亡/XP/伤害数字。

### 3.10 为什么不让 ExecCalc 直接修改 Health？

项目把计算结果写入 `IncomingDamage`，AttributeSet 再统一结算。这样普通伤害、护盾吸收、重复死亡保护、Debuff、击退、伤害事件、飘字和 XP 都经过同一出口；Meta Attribute 消费后归零，也不会作为长期角色状态复制。

### 3.11 PreAttributeChange 与 PostGameplayEffectExecute 有什么区别？项目如何使用？

`PreAttributeChange` 在属性当前值即将改变时做即时约束，项目用于将 Health/Mana Clamp 到 `[0, Max]`。`PostGameplayEffectExecute` 在 Instant/Periodic GE 已执行后获得 EffectSpec、Source/Target Context，项目在这里消费 IncomingDamage/XP并处理死亡等副作用，同时再次 Clamp Health/Mana。只做 Pre 不足以承载死亡和来源信息；只做 Post 又不能覆盖所有直接属性变化入口，所以项目两处都保护。

### 3.12 属性复制丢包时，客户端会永久保留旧 HP 吗？

通常不会。属性是状态复制，最新值会继续复制而不是只发送一次事件；OnRep 又用 `GAMEPLAYATTRIBUTE_REPNOTIFY` 驱动 UI。死亡还走可靠 Multicast 和 Pawn 替换。但客户端可能短时间显示旧 HP，项目没有自定义确认包或独立死亡状态复制来完全消除极端延迟下的瞬态差异。

### 3.13 自定义 GameplayEffectContext 做了什么？

`FAuraGameplayEffectContext` 扩展了格挡、暴击、Debuff 参数、伤害类型、死亡冲量、击退强度和击退向量；实现 `Duplicate` 和 `NetSerialize`，用位掩码只序列化存在字段。`UAuraAbilitySystemGlobals::AllocGameplayEffectContext` 返回该类型，并在 `DefaultGame.ini` 配置自定义 Globals。这样结算端和表现端可以从同一次命中的 Context 获得一致元数据。

### 3.14 TargetDataUnderMouse 如何在网络中工作？

本地控制端采集鼠标 HitResult，在 `FScopedPredictionWindow` 中调用 `ServerSetReplicatedTargetData`，携带 SpecHandle、ActivationPredictionKey 和 TargetData。服务端 AbilityTask 注册 `AbilityTargetDataSetDelegate`，未收到时进入等待；收到后消费数据并广播。它避免服务端直接读取不存在的远端鼠标状态。

### 3.15 项目如何处理冷却 UI？

`UWaitCooldownChange` 监听 Cooldown Tag 数量变化和 Active GE 新增事件；Tag 出现时查询匹配 Active Effect 的最大剩余时间并广播 CooldownStart，Tag 清零广播 CooldownEnd。`bCooldownActive` 防止同一个 CD 因 Tag 与 GE 两条通知重复触发开始事件。当前 C++ 只广播开始时长和结束，逐帧数字/材质变暗主要由 UMG 资产层完成，不能声称 C++ 每帧同步 CD。

### 3.16 为什么被动技能复活或读档后容易失效？项目怎么处理？

ASC 在 PlayerState 上跨 Pawn 存活，但死亡会 `CancelAllAbilities`，AbilitySpec 仍在、实例可能已结束。项目在 ActorInfo 重绑、AbilitySpec 复制和存档恢复后扫描已装备被动槽并 `TryActivateAbility`；被动能力自身在 EndAbility 清 Timer、事件 Task、GE Handle 和委托，避免旧实例泄漏。

### 3.17 电击技能如何同步持续光束和眩晕？

BeamSpell持续更新鼠标目标，按最近邻形成链式目标。伤害 Timer 周期结算；眩晕 GE 只由 Authority 添加/移除，进入链时挂无限 Channel，离开时先挂短 Tail 再移除 Channel，避免 Stun Tag 瞬间归零导致动画闪断。光束使用 GameplayCueNotifyActor，Cue 中用弱引用跟踪起点/终点并复位 Niagara、音频和附着状态。

### 3.18 GameplayTag 在项目中承担哪些职责？

属性映射、输入槽、技能身份/类型/状态、伤害类型与抗性映射、Debuff、Cooldown、GameplayCue、Player.Block、事件路由都使用 Tag。Native Tag 在自定义 AssetManager 的 `StartInitialLoading` 中集中注册，减少字符串散落并支持层级匹配。

### 3.19 GAS 中还有哪些真实不足？

- 通用 `CauseDamage` 未统一加 Authority Guard，安全依赖具体技能调用链。
- 部分 Server RPC 的资源和槽位校验不完整。
- 没有自动化测试验证伤害公式、技能状态迁移和预测回滚。
- 部分技能策略位于 Blueprint，C++ 无法静态证明所有资产配置一致。

### 3.20 MMC 和 ExecutionCalculation 在项目中如何分工？

`MMC_MaxHealth` 和 `MMC_MaxMana` 计算单个 Modifier Magnitude，分别捕获 Vigor/Intelligence，并结合 Context SourceObject 的角色等级得到上限；捕获设置为非 Snapshot。`ExecCalc_Damage` 同时读取来源和目标的多项属性、SetByCaller 和随机判定，最后输出 IncomingDamage，适合一次复杂结算。读档修改等级后，项目重新应用次级属性 GE，确保 MMC 按新等级重算。

### 3.21 项目使用了哪些 GameplayEffect DurationPolicy？

默认属性/资产侧 GE 由 Blueprint 配置；C++ 明确实现的 Debuff GE 是 `HasDuration + Period`，并关闭应用瞬间的首 Tick；电击 Channel 是 `Infinite`，离开后的 Tail 是 2 秒 `HasDuration`；被动状态 GE 是 `Infinite`。回收、死亡和退出时按 Tag 或保存的 ActiveEffectHandle 移除这些长期效果。

### 3.22 电击眩晕为什么使用 AggregateBySource？

电击 Stun GE 设置同来源聚合、StackLimit 1、成功应用时刷新时长。同一个施法者反复切换目标不会在同一目标堆出大量重复 GE；不同施法者仍各自维护一份，某个施法者停止时不会误删另一个施法者的眩晕。项目使用 Channel + Tail 两类 GE维持 Tag 连续性。

### 3.23 项目中的 Attribute Capture 是 Snapshot 还是实时捕获？

伤害 ExecCalc 的 Armor、穿透、格挡、暴击和抗性 CaptureDef 都设置 `bSnapshot=false`，在执行时读取当前聚合属性；MaxHealth/MaxMana MMC 的 Vigor/Intelligence 也设置为 false。项目没有为蓄力弹等场景实现“施法瞬间锁定攻击力”的 Snapshot 伤害模型。

### 3.24 AbilityTask 是否已被项目使用？

仓库实现了 `TargetDataUnderMouse` 和 `TargetDataMagicCircle` 的预测键/复制逻辑，BeamSpell 注释与服务端分支也按 TargetDataUnderMouse 的数据流设计；但实际创建这些 Task 的节点可能位于 GA Blueprint `.uasset`，纯文本源码无法证明每个资产当前是否连接正确。面试可讲 Task 的已实现机制，若被问具体哪张蓝图调用，应在编辑器中现场核验后再回答。

---

## 4. 死亡、复活与角色状态

### 4.1 玩家死亡完整流程是什么？

服务端 AttributeSet 判定 HP 到 0，调用 CombatInterface `Die`；角色发 Reliable Multicast 做音效、Ragdoll、关闭 Capsule、溶解并标记本地死亡；GameMode 记录复活 Transform、取消技能和 Debuff、停止移动、让 AI 清理目标，然后启动复活 Timer。到时服务端销毁旧 Pawn，寻找安全位置并 `RestartPlayerAtTransform`，新 Pawn 重绑旧 PlayerState ASC，按配置恢复部分 HP/Mana和被动能力。

### 4.2 为什么复活采用替换 Pawn，而不是把尸体直接站起来？

尸体已经进入 Ragdoll、修改 Mesh/Capsule 碰撞、材质和动画状态，原地恢复需要清理大量易遗漏状态。替换 Pawn 能获得干净 Character 组件，同时 PlayerState 保留成长与 ASC。敌人因为采用对象池，则必须实现更完整的原地状态复位。

### 4.3 复活点被怪物占据时如何处理？

先用默认/记录 Transform；若 Capsule Overlap 阻塞，则围绕目标按环采样，通过 `ProjectPointToNavigation` 投影到 NavMesh，做地面高度和 Capsule Clearance 检查，按距离排序选最近安全点。若暂时找不到，进入复活重试 Timer，而不是强行 Spawn 到碰撞体内。

### 4.4 为什么复活后曾无法升级？

根因是 ASC 在 PlayerState 上保留，但死亡 `CancelAllAbilities` 会结束常驻监听类 Ability；新 Pawn 只重绑 ActorInfo、不应重新 Give 全部技能。项目把“首次授予”和“复活重新激活常驻能力”拆开，既避免重复 Spec，又恢复 XP 事件监听和被动能力。

### 4.5 为什么部分 AI 在玩家复活后不再追踪？

AI 的旧 Blackboard Target 指向已销毁 Pawn。项目的 BT Service 不缓存“第一个玩家”，而是每次遍历 PlayerController，跳过无 Pawn/死亡 Pawn并选最近有效玩家；玩家死亡和 Logout 时还主动通知敌人清旧 Target，复活后 BT 可重新选择新 Pawn。

### 4.6 玩家死亡状态是否做了 ReplicatedUsing？

**没有。** `bDead` 是 Character 本地成员，由 Reliable Multicast 在各端设置，之后还会通过 Pawn 替换自然收敛。它不是独立 Replicated 属性。这是目前能够工作的原型实现，但生产项目可增加显式复制状态或统一状态 Tag，增强晚加入、Dormancy 和极端延迟下的一致性。

---

## 5. 存档系统

### 5.1 存档保存了哪些内容？

SaveGame v3 按地图保存世界快照：

- 玩家：Transform、Level、XP、属性点、技能点、HP、Mana、AbilityTag、技能等级、状态 Tag、槽位 Tag。
- 生成器：Spawner FGuid、随机种子、生成序列、下次生成时间、启用状态。
- 敌人：实例 FGuid、Spawner FGuid、SoftClassPath、Transform、等级、HP。

槽位摘要另存玩家名、地图名、主机玩家等级和槽状态。

### 5.2 为什么敌人和生成器使用 FGuid，而不是 Actor 名称或运行时 UniqueID？

运行时 ID 和自动 Actor 名称会随 Spawn 顺序、地图重载、PIE 前缀变化，不适合作为跨会话身份。Spawner 使用稳定 FGuid，敌人每次生成获得实例 FGuid，存档据此归属到生成器并恢复。**玩家目前没有使用 FGuid**，仍按会话索引保存；面试时不能把敌人 GUID 方案说成玩家账号方案。

### 5.3 存档为什么只能由服务端写？

多人世界的玩家、敌人和生成器权威状态都在服务器；客户端本地数据可能延迟、被预测或被篡改。`SaveCurrentWorldInternal` 位于 GameMode，GameMode 只存在于权威端。客户端退出也先请求服务器保存权威快照，再断开。

### 5.4 存档时序最危险的问题是什么？

新地图刚创建 Pawn 时，它可能仍是 Level 1/默认技能；`PostLogin` 的恢复要等 Pawn 和 PlayerState 就绪。若自动存档或退出保存先发生，就会用默认值覆盖旧记录。项目为每个会话玩家维护 `RestoredPlayerIndices`：恢复完成前若已有有效记录，保存流程保留旧数据；等待复活时也保留旧记录。PlayerState 的 `bSaveRestoreInProgress` 则阻止 UI 提前广播默认属性。

### 5.5 为什么加载顺序是等级 -> 派生属性 -> 当前 HP/Mana？

MaxHealth/MaxMana 的 MMC 读取角色等级。若先恢复当前 HP，再把一级的次级属性换成高等级上限，UI 和 Clamp 会得到错误结果。项目先写 Level，重新应用默认次级 GE 计算新上限，再恢复 XP/点数/技能，最后把 HP/Mana Clamp 到新上限。

### 5.6 实现了哪些存档版本迁移？

**已实现的迁移包括：**

- 老版本单玩家 `PlayerData` 迁移到 `Players[0]`。
- 旧自动存档重复追加的 Ability 记录按 AbilityTag 压缩，只保留最新记录。
- `SaveVersion < 3` 更新为 3。
- 老地图显示名归一到 Dungeon，兼容 PIE 地图前缀。
- 单生成器旧存档允许 Spawner ID 不匹配的兼容恢复。

没有实现通用版本迁移框架、逐版本函数表或数据备份回滚。

### 5.7 玩家数据如何区分主机和客户端？

当前把 Controller 排序：Listen Host 优先，其余按 PlayerState PlayerId，然后给每个在线 Controller 维护会话内 `PlayerSaveIndices`。索引 0 同时写入旧版 `PlayerData` 做兼容。它可以维持同一会话内主客机数据，但不是持久身份；重启后加入顺序变化、离线索引复用，可能把记录分给错误玩家。

### 5.8 中途加入应该读客户端本地存档还是服务端世界存档？

当前实现只读主机选择的服务器 SaveGame，不读远端客户端本地存档。原因是世界和角色数值由服务器权威管理。但由于没有账号 ID，新玩家只能按会话索引匹配记录。正式方案需要登录认证后的 UniqueNetId/持久玩家 GUID 映射，当前未实现。

### 5.9 主机保存时客户端退出会不会并发写坏存档？

当前使用 `UGameplayStatics::SaveGameToSlot` 同步写盘，GameMode 保存和 Logout 都在游戏线程顺序执行，不存在两个后台线程同时修改同一个 SaveGame 对象的实现。远端主动退出会先保存，再发返回主菜单指令。但它可能造成主机卡顿，也没有显式临时文件、校验和、双备份或崩溃恢复，因此不能声称具备数据库级事务安全。

### 5.10 存档什么时候触发？

非菜单地图 BeginPlay 后每 10 秒自动保存；主动保存/退出、远端客户端退出时保存；World EndPlay 作为最终兜底保存。服务端主动返回菜单前设置 `bSkipEndPlaySave`，避免相同切图流程重复保存。

### 5.11 存档是同步还是异步？

**同步。** 使用 `SaveGameToSlot`/`LoadGameFromSlot`，没有 `AsyncSaveGameToSlot`、后台序列化或存档任务队列。数据规模扩大后需要改为快照与磁盘写入分离，并处理同槽写入合并。

### 5.12 当前存档还有哪些明确边界？

- 玩家身份不是账号级，断线重连归属不可靠。
- SaveGame 存在主机本机，客户端没有云同步。
- 没有加密、防篡改、CRC、备份和失败恢复。
- 同步写盘可能产生帧卡顿。
- 只对当前项目已有字段做了有限迁移。

---

## 6. AI、导航、EQS 与拥堵处理

### 6.1 AI 基础架构是什么？

敌人由 `AAuraAIController` 控制，使用 BehaviorTreeComponent 和 BlackboardComponent。AIController 继承 `ADetourCrowdAIController`，启用 Crowd 转弯预判、障碍避让、可见性/拓扑优化，关闭目标点减速和 Separation。BT Service 周期寻找最近有效玩家，攻击行为主要由行为树和 GAS 资产组织；C++ `UBTTask_Attack` 当前没有自定义攻击逻辑。

### 6.2 Recast、Detour Crowd、EQS 在玩家自动寻路中分别做什么？

- Recast NavMesh/NavigationSystem：计算起点到目标或绕行点的全局可达路径。
- Detour Crowd：沿路径移动时，根据周围动态 Agent 计算局部转向速度。
- EQS：目标被占用时选择附近可落点；持续无进展时选择绕开拥堵的恢复点。

EQS 不替代 A*；EQS 选出点后仍调用 `FindPathSync`，再把路径交给 CrowdFollowing `RequestMove`。

### 6.3 导航到 EQS 点调用了什么 API？

候选点先通过 `ProjectPointToNavigation` 投影；评分阶段使用 `UNavigationSystemV1::FindPathToLocationSynchronously` 计算路径长度；真正移动时构造 `FPathFindingQuery` 调用 `FindPathSync`，然后 `UAuraCrowdFollowingComponent::RequestMove(FAIMoveRequest, Path)`。

### 6.4 EQS 候选如何生成和评分？

项目运行时构造自定义 UEnvQuery：以点击目标或玩家当前位置为中心按多个半径环形采样，投影到 NavMesh。候选必须与 Pawn 保持最小边缘间距并存在完整路径；恢复查询还考虑接近路径净空、候选到最终目标的路径成本、最大后退距离、前向净空以及已经失败的方向扇区，最后将综合 Cost 转成 Score。

### 6.5 如何判断角色被堵住？

每 0.15 秒比较到当前路径拐点/移动目标的距离；至少推进设定距离才算进展。累计约 0.45 秒无进展且重规划冷却结束，就触发恢复。路径组件直接返回失败时不再等待完整超时，而是立即进入恢复查询。日志记录状态、速度、路径点、Crowd Simulation 和 Avoidance，便于区分“没路径”和“有路径但局部速度为零”。

### 6.6 如何防止到达终点后左右来回走？

项目同时做三层终止检查：Tick 中检查到最终目标的二维距离；PathFinished 不能只信 Success，还要再次比较实际距离和 AcceptanceRadius；如果成功到达恢复点，再重新规划最终目标。Goal EQS 若选出的点已经在接受半径内，也立即停止。

### 6.7 为什么目标附近怪多时会抖动或停住？

全局 NavMesh 通常不把动态 Pawn 当静态障碍，A* 认为路径可走；Crowd 是局部速度避让，在密集、对称或无足够空间时可能得到接近零速度。若同时频繁重规划，又会在左右候选间振荡。项目通过进展检测、恢复冷却、失败方向记忆、目标净空和恢复点接受半径缓解，但并不能在没有物理空间时保证一定穿过敌群。

### 6.8 当前导航性能风险是什么？

EQS 测试会对多个候选同步调用路径长度查询，选中后又做一次同步 FindPath；大量单位同时拥堵时 CPU 成本较高。当前没有异步 PathFinding、分帧重规划、查询预算、结果缓存或 Mass Crowd。Detour Crowd 的内部实现可能使用引擎任务，但项目没有自行把 Gameplay Actor/ASC/Blackboard 操作放到工作线程；游戏状态修改仍在游戏线程。

### 6.9 如果大量 AI 同时寻路，项目已经做了哪些优化？还没做哪些？

已做：Crowd 统一局部避让、只在堵塞时运行恢复 EQS、重规划冷却、失败方向去重、对象池复用 AI。未做：按距离降低 BT Service 频率、路径请求分帧、异步查询、NavInvoker、分区 Crowd、LOD AI、ReplicationGraph、性能采样基线。因此只能说有针对性控制，不能说已完成大规模 AI 性能方案。

### 6.10 为什么客户端配置 `bAllowClientSideNavigation=True`，却仍由服务端算路径？

该配置允许客户端具备导航数据，便于本地组件和预测相关能力，但当前业务入口 `RequestAutoMove` 对远端客户端只发终点 RPC；完整 Goal/Recovery EQS 和 RequestMove 都有 Authority 条件。客户端主要消费服务器转向速度并用 CharacterMovement 预测，不上传自己算出的路径。

### 6.11 Detour Crowd 是否对所有单位生效？

AIController 继承 DetourCrowd；玩家 Controller 自建 `UAuraCrowdFollowingComponent`。敌人在客户端还根据对象池 Active 状态向 CrowdManager 注册/注销，避免不可见池对象继续作为 Agent。实际避让参数由 C++ 设置，但 NavMesh/Crowd 项目设置和资产碰撞仍会影响最终效果。

---

## 7. 对象池、特效与状态复位

### 7.1 项目池化了哪些对象？

`UAuraProjectilePoolSubsystem` 按类管理普通投射物和 FireBlast 火球；`UAuraEnemyPoolSubsystem` 按敌人类管理敌人。每个 Bucket 持有 All、Available、Active，敌人额外有 Dying；支持预热、硬上限，敌人可配置是否扩容。池只在非客户端 World 创建和 Acquire，状态通过 Actor 复制给客户端。

### 7.2 为什么池对象不能只 Hide？

仅 Hide 会保留碰撞、Movement、Timer、粒子、音频、Owner、Instigator、GE Spec、命中集合和委托，可能产生“看不见的伤害”“旧目标被再次命中”“旧声音继续播放”“新投射物继承旧伤害”等问题。因此项目在回收时逐项清理并禁用碰撞和 Tick。

### 7.3 普通投射物回收/复用清理了什么？

清 Life Timer、停 ProjectileMovement、禁 Sphere Collision、隐藏 Actor、停飞行音频、立即停 Niagara Trail、清 DamageEffectSpecHandle、Owner 和 Instigator。激活时重置 bHit、Transform、速度、碰撞、特效和 Timer，并 ForceNetUpdate。命中只在服务端应用 GE，命中特效用 Unreliable Multicast。

### 7.4 FireBlast 火球还需要额外清理什么？

除碰撞、Tick、Niagara、Timer、Owner/Instigator/Spec 外，还清 SourceActor、出程/回程/本地视觉命中集合、完成标记、状态委托。激活时重新配置出程和回程参数；移动只在服务端 Tick，位置和状态复制给客户端。

### 7.5 敌人对象池为什么最复杂？

敌人经历 Ragdoll、重力、Mesh/Weapon 脱离、动态溶解材质、Root Motion、蒙太奇、BT/Blackboard、ASC Debuff、目标、血条和客户端预测状态。项目预先捕获 Mesh/Weapon Transform、材质、动画类和碰撞；复用时清物理速度、恢复附着/材质/动画/RootMotion/Collision/Movement、清战斗状态，重启 BehaviorTree 并清 Blackboard Target。

### 7.6 怪物死亡后血条或模型掉到地下，根因是什么？如何处理？

根因是池化复用时，死亡 Ragdoll 的物理、Mesh 相对 Transform、Root Motion、碰撞或客户端预测状态残留，而 Capsule 与 Mesh 状态不同步。项目在 `ResetNativePoolState` 中关闭所有刚体模拟与重力、清线/角速度、恢复 Mesh 和 Weapon 初始 Transform/材质/AnimClass、清 RootMotion，重设 Walking，并在客户端重置 Movement Prediction；受击/眩晕蒙太奇期间忽略 RootMotion，防止动画根轨迹推动权威 Capsule 入地。

### 7.7 如果投射物携带未结束的 GameplayEffect 被回收会怎样？

项目投射物保存的是用于命中时应用的 `FGameplayEffectSpecHandle`，不是已挂在投射物自身的 Active GE。回收时清空该 Handle，避免下一次复用携带旧 Source/伤害。已经应用到目标 ASC 的持续 GE 由目标 ASC 持有，不依赖投射物继续存在。若未来让投射物自身持有 Active GE，还必须保存 Handle 并在回收时显式移除；当前没有这种实现。

### 7.8 GameplayCueNotifyActor 自身是否会复用？如何清理？

自定义电击 Cue 设置预分配实例数，并实现 `Recycle/ReuseAfterRecycle`。回收时立即停止 Niagara、停止音频、解除附着、清 Source/Target 弱引用和光束状态；复用时重新启 Tick，避免上一条链式光束的附着点泄漏到下一次 Cue。

### 7.9 对象池优化效果是否做过正式 Profiling？

**没有形成可证明的性能报告。** 源码确实减少了频繁 Spawn/Destroy，但仓库没有 Unreal Insights 对比数据、固定压测场景或统计文档。面试可说“实现了池化并针对状态污染修复”，不能声称具体提升百分比。

### 7.10 为什么池初始化使用 SpawnActorDeferred？

普通投射物和 FireBall 先 `SpawnActorDeferred`，在 `FinishSpawning` 前设置 `bPoolManaged=true`，使其 BeginPlay 从第一次开始就走池对象分支，不会误设普通 Actor LifeSpan、播放飞行效果或产生一次激活闪烁。敌人池则使用 SpawnActor 后立即确保默认 Controller 并回收到 Inactive 状态。

### 7.11 池状态如何同步到客户端？

池只在服务器管理；投射物复制 `bPoolActive`，FireBall 复制 Active/Source/State，敌人复制 `PoolState`、等级和 GUID。客户端 OnRep 负责显隐、碰撞、Niagara、Movement/Crowd 注册和本地复位。项目没有把池 Actor 设为 Dormant，因此不涉及激活前主动 FlushNetDormancy 的实现。

---

## 8. UI、MVC、MVVM 与事件驱动

### 8.1 为什么项目同时使用 MVC 和 MVVM？

战斗 HUD、属性菜单、技能菜单的数据来自 ASC、AttributeSet、PlayerState 多个运行时对象，并包含命令式交互，项目用 WidgetController 聚合数据与委托，UMG 只显示，属于委托驱动的 MVC。存档槽是稳定表单数据，ViewModel 可直接暴露 PlayerName、Level、MapName 和 Slot 状态，适合 FieldNotify + View Binding。两者按业务特点拆分，不是在同一 UI 重复套架构。

### 8.2 WidgetController 的数据流是什么？

PlayerState/ASC/AttributeSet -> 原生属性变化委托或项目委托 -> WidgetController 转成 UI 语义 -> BlueprintAssignable 动态委托 -> UMG。HUD 创建并缓存 Controller，将 PC、PS、ASC、AS 参数注入，然后先 Bind，再 BroadcastInitialValues。控件不直接每帧轮询 GAS。

### 8.3 为什么不让每个 UMG 控件直接绑定 AttributeSet？

直接绑定会让大量 Widget 知道 GAS 类型、生命周期和格式化规则，复活换 Pawn或读档时更难统一处理。WidgetController 集中做 XP 百分比、AbilityInfo、消息 DataTable 和初始时序保护，Widget 只订阅业务事件。代价是多一层类和委托管理。

### 8.4 与 UI Tick 轮询相比，事件驱动有什么优缺点？

优点是属性不变时没有每帧查询，数据变化立即推送，职责清晰；缺点是必须处理“绑定前已经变化”的初始值、委托解绑和对象生命周期。项目通过 `BroadcastInitialValues`、AbilitiesGiven/OnRep 再广播和存档恢复完成事件补齐初始状态。

### 8.5 UE MVVM 什么情况下不会自动刷新？项目怎么做？

仅直接修改字段、Setter 没有触发 FieldNotify、绑定路径失效或 ViewModel 类型变化时不会刷新。项目的 LoadSlot Setter 和 NumLoadSlots Setter 都使用 `UE_MVVM_SET_PROPERTY_VALUE`；槽状态切换和按钮启用仍使用动态委托。之前 C++ 函数/委托签名变化会让 WBP 旧节点和绑定路径失效，这类 Blueprint 需要 Refresh/Rebind，C++ FieldNotify 本身不能自动修复失效资产节点。

### 8.6 为什么读档时 UI 曾先闪默认值？如何保护？

HUD 创建通常早于延迟存档恢复，若立刻广播会先显示 Level 1/满血，再跳到存档值。项目让 PlayerState 复制 `bSaveRestoreInProgress`；Overlay 在恢复中跳过初始广播，恢复结束后统一广播权威等级、XP、HP/Mana。HUD 初次创建也延迟到下一 Tick广播，并使用弱指针避免回调访问失效 Controller。

### 8.7 客户端技能栏为什么可能为空？项目如何补偿 AbilitySpec 与 RPC 时序？

服务端的 `ClientRefreshAbilityUI` 可能早于 AbilitySpec 列表复制到达。项目在 `OnRep_ActivateAbilities` 每次 Ability 列表复制后重新设置 `bStartupAbilitiesGiven` 并广播；Overlay 若技能已给则立即遍历，否则监听 AbilitiesGiven。装备和状态变化另用 Client RPC 驱动 UI。

### 8.8 属性、技能和存档 UI 是否完全由 C++ 实现？

**不是。** C++ 实现数据控制器、ViewModel、委托和初始化时序；具体 Widget 布局、动画、材质、按钮连线和部分 CD 显示在 UMG Blueprint 资产中。文档能证明 C++ 数据管线，但不能仅凭源码断言所有 Blueprint 绑定都永远正确。

### 8.9 当前 UI 委托实现有什么工程化风险？

部分 WidgetController 使用捕获 `this` 的 Lambda，并未系统保存/移除所有 DelegateHandle；当前 Controller 由 HUD 持有，生命周期较稳定，但复杂切图/重复初始化时仍有重复绑定或悬空风险。生产化应统一使用弱 Lambda/Handle，在销毁时解绑，并为 WidgetController 的依赖重绑定设计明确生命周期。

---

## 9. 输入、移动与表现同步

### 9.1 Enhanced Input 在项目中如何使用？

本地 PlayerController BeginPlay 向 `UEnhancedInputLocalPlayerSubsystem` 添加 MappingContext；Move 和 Camera Action 直接绑定，技能 Action 通过 DataAsset 的 InputTag 表驱动。按下、松开、持续三个 Trigger 阶段分别转给 ASC。

### 9.2 点击移动和按住移动有什么区别？

右键按住且没有敌人目标时，客户端持续朝鼠标点 `AddMovementInput`；短按释放时发起自动寻路。点击敌人则切为技能输入。手动移动会停止当前自动寻路，施法/死亡的 `Player.Block` 或死亡状态会阻止移动输入。

### 9.3 客户端朝向为什么需要额外插值？

权威 Crowd 的转向速度在服务器产生，而远端客户端本地 CharacterMovement 需要方向输入才能平滑预测。项目发送量化 SteeringVelocity，客户端据此 AddMovementInput，并用当前速度或 Steering 方向按 RotationRate 做 `RInterpConstantTo`，避免只等低频服务器 Transform 时角色横移不转身。

### 9.4 魔法阵和光束鼠标位置为什么用 Unreliable RPC？

它们是高频、可被下一样本覆盖的连续状态，旧位置重传价值低。技能最终确认仍有 Server Reliable 或 GAS TargetData，并做 NaN/距离/Commit 校验。当前持续位置 RPC 没有显式限频，只有“位置变化才发送”的部分控制，仍有优化空间。

---

## 10. 资源管理、打包与局域网部署

### 10.1 打包时为什么编辑器正常的特效会丢？项目如何处理？

Cook 只收集可追踪引用和明确要求的资源。动态 `LoadObject`、运行时 GameplayCue 路径或仅字符串引用可能未被依赖分析发现。项目在 `DefaultGame.ini` 配置 GameplayCueNotifyPaths，并把 Cue、Shock、ArcaneShards、Stun 等目录加入 `DirectoriesToAlwaysCook`；部分关键 Niagara 还使用 ConstructorHelpers、UPROPERTY 软/硬引用或运行时 fallback 路径。

### 10.2 当前有哪些地图会被打包？

`MainMenu`、`LoadMenu`、`Dungeon` 明确写入 `MapsToCook`。启用了 Pak、IoStore、Oodle/Kraken 压缩，并设置 `IncludePrerequisites=True`。

### 10.3 为什么另一台电脑仍可能要求安装 C++ 运行库？

项目配置会把 prerequisites 安装程序包含在打包产物中，但不会把所有运行库静态链接进 exe，也不会保证用户已经执行 prerequisites。`IncludeAppLocalPrerequisites=False`，所以运行库不是逐 DLL 放在应用目录。分发时应携带完整 Staged/Package 目录并运行 `Engine/Extras/Redist` 中的先决条件安装器；当前项目没有自定义安装程序。

### 10.4 两台电脑如何局域网游玩？

主机以 `/Game/Maps/MainMenu?listen -port=7777` 启动，客户端以 `主机IPv4:7777` 连接；两边构建版本必须一致，Windows 防火墙允许 UDP 7777。当前没有服务器列表、自动发现、NAT 穿透或互联网中继。

### 10.5 打包后的多人功能有哪些限制？

只能直连 Listen Server；主机关闭进程会结束会话；没有 Session 重连、平台账号、邀请、匹配、版本协商 UI和 Dedicated Server Target。客户端主动退出已能不影响主机继续运行，但数据归属仍受会话索引限制。

### 10.6 HttpListener 8000 端口报错属于游戏网络端口吗？

不是 Aura 的 Gameplay NetDriver 端口。游戏监听示例是 UDP 7777；8000 报错来自启用的编辑器/工具插件 HTTP Listener。仓库启用了 ModelContextProtocol/MCP 等编辑器插件，Cook 环境端口冲突会导致日志报错。处理时应关闭占用进程或禁用不参与 Runtime 打包的工具插件，而不是修改游戏联机端口。

---

## 11. 性能、线程与稳定性

### 11.1 项目做了哪些实际性能优化？

- 敌人、普通投射物、FireBlast 火球对象池。
- GameplayCue 预分配/复用。
- 玩家 ASC Mixed、敌人 ASC Minimal。
- Crowd 转向 Unreliable、量化且约 20Hz 节流。
- EQS 只在目标落点解析或检测到堵塞时运行，并有重规划冷却。
- Character/多数 Actor 关闭不必要 Tick，FireBall 仅激活时 Tick。

没有可证明的 Unreal Insights 数据，因此只能陈述实现手段，不能给出虚构的帧率提升。

### 11.2 Detour Crowd/异步寻路开启多线程会有什么项目风险？

当前项目没有自己创建寻路线程，也没有从工作线程访问 Actor、ASC、Blackboard 或 UObjects。所有 EQS 回调、路径提交和 Gameplay 状态修改按游戏线程逻辑编写。若未来异步化，只能在线程安全的纯数据快照上计算，回到游戏线程后还要用弱引用、请求序号和当前位置验证结果，防止 Pawn 已死亡、复活、切图或目标改变后应用旧路径。

### 11.3 当前最明显的主线程开销在哪里？

拥堵恢复 EQS 对每个候选调用同步路径查询；自动保存每 10 秒同步序列化并写盘；BT Service 遍历 PlayerController；大量复制 Actor 和 GAS 属性也增加主机负担。对象池减少 Spawn/Destroy，但没有解决同步路径和同步 IO 峰值。

### 11.4 项目如何避免 Timer/委托访问销毁对象？

死亡、退出、回池和 EndAbility 都主动清对应 Timer；GameMode 的 Respawn Timer 用 Controller 弱键并在 Logout 清理；HUD NextTick 使用 `TWeakObjectPtr`；池对象清动态委托；GameplayCue 清弱目标。仍有部分 WidgetController Lambda 未统一解绑，是剩余风险。

### 11.5 项目如何定位多人时序问题？

在 GameMode BeginPlay、Travel、连接、Possess、存档恢复、自动寻路状态和 Crowd Velocity/PathFinished 等位置记录 NetMode、Authority、World、Controller、路径状态和玩家索引；使用 Listen Server + Client 重复执行进入、退出、复活和二次重载，观察 NetDriver shutdown、Browse 地址、端口和恢复顺序。当前主要靠日志与人工回归，尚未自动化。

---

## 12. C++/UE 工程细节

### 12.1 UPROPERTY、TObjectPtr、TWeakObjectPtr 在项目中怎样选择？

需要被 UObject GC 跟踪且属于对象长期状态的成员使用 UPROPERTY + TObjectPtr；只观察可能销毁对象而不拥有生命周期时使用 TWeakObjectPtr；短期局部变量可用原始指针并立即 IsValid 检查。对象池 Bucket 用 TObjectPtr 保证池中 Actor 被引用，技能目标/Controller Map 用弱指针避免阻止销毁。

### 12.2 为什么地图和敌人类使用 Soft Reference？

地图用 `TSoftObjectPtr<UWorld>`、敌人存档用 `FSoftClassPath`，可持久化资产路径且不要求 SaveGame 序列化运行时 UClass 指针，也避免不必要的启动硬加载。Travel 或恢复时再解析/加载。代价是路径改名、Cook 收集和加载失败必须处理，项目做了默认地图和部分兼容 fallback。

### 12.3 动态委托和原生委托分别用在哪里？

需要 Blueprint 绑定的 UI/ViewModel 使用 `DECLARE_DYNAMIC_MULTICAST_DELEGATE` + `BlueprintAssignable`；纯 C++ 的 PlayerState、ASC、Ability 状态使用原生 Multicast Delegate，开销更低、类型更直接。Gameplay Ability Task 与 UFUNCTION 回调则使用 Dynamic Delegate 以支持反射。

### 12.4 为什么 GameplayTag 在构造函数中不能随意 Request？

Native Tag 在自定义 AssetManager `StartInitialLoading` 中初始化。部分组件在 BeginPlay 后才设置最终 Tag，并延迟到下一 Tick绑定 ASC，避免组件 BeginPlay 早于 Owner 完成 Tag 配置。项目也尽量通过 `FAuraGameplayTags::Get()` 集中访问，而不是到处使用字符串。

### 12.5 项目有没有自定义内存分配、智能指针算法或复杂模板？

**没有。** 使用 UE 容器、GC 指针、SharedPtr（EQS/Navigation 结果）和少量模板（输入绑定、对象池 Bucket、DataTable 查找）。不应把项目包装成自研内存管理系统。

### 12.6 DataAsset、DataTable、CurveTable 在项目中分别存什么？

DataAsset 用于结构稳定、需要类型引用的配置：CharacterClassInfo、AbilityInfo、AttributeInfo、LevelUpInfo 和 InputConfig；DataTable 用于按 GameplayTag 查找 UI 消息行；CurveTable 用于护甲穿透和有效护甲等按等级变化的伤害系数。C++ 负责读取和结算，具体数值由资产配置，保持玩法数据与流程代码分离。

### 12.7 为什么使用 SpawnActorDeferred/FinishSpawning 时要注意初始化顺序？

Deferred Spawn 允许在 Construction/BeginPlay 前写入决定初始化分支的字段，项目用于池化投射物和运行时默认生成器。必须最终调用 `FinishSpawning`，且 Finish 后再改“只在 BeginPlay 读取”的字段已经太晚。项目在 Finish 前设置池管理标记、Spawner 配置等初始数据。

---

## 13. 高频追问：必须诚实回答“未实现”的内容

### 13.1 是否支持 Dedicated Server？

没有专门 Dedicated Server Target、部署脚本和验证记录。大部分 Authority 代码理论上可迁移，但当前产品流程围绕 Listen Host 和本地槽位设计，不能声称已经支持。

### 13.2 是否有房间创建、搜索、邀请和断线重连？

没有 OnlineSubsystem/Session。只有 Listen Server IP 直连；远端客户端可单独退出。没有账号认证、重连 Token、Session 恢复和玩家持久 ID。

### 13.3 是否有完整反作弊？

没有。核心伤害/Spawn/存档/路径在服务器能防止最直接篡改，但 RPC 验证和限流不完整，也没有服务器行为检测、签名、加密或平台反作弊。

### 13.4 是否有大规模并发 AI 性能数据？

没有。实现了 Crowd、拥堵恢复和对象池，但同步 EQS 路径查询仍是明确瓶颈，也没有 Insights 报告或固定压测指标。

### 13.5 是否有完整存档事务、云存档和账号绑定？

没有。当前是主机本地同步 SaveGame；有有限版本迁移和时序保护，但没有事务、备份、校验、云同步与账号级归属。

### 13.6 是否有自动化测试和 CI？

没有 Automation Test、Functional Test 或仓库内 CI 流水线。当前通过 UBT 编译、PIE 双端、打包 Cook 和日志进行人工回归。

### 13.7 是否所有效果和 UI 都能从 C++ 证明？

不能。大量 GA/GE、BehaviorTree、Animation、UMG、Niagara 配置在 `.uasset` 中。C++ 能证明接口和数据流，但资产标签、NetExecutionPolicy、Montage Notify、Widget Binding 等仍需要在编辑器中逐项核验。

---

## 14. 面试回答模板：最大的技术难点

可以按以下真实流程回答：

1. **问题表现**：Listen Server 首次进入正常，多次地图切换后客户端不能跟随，玩家数据还可能串档；退出、复活又会与 GAS/AI 生命周期互相影响。
2. **建立复现矩阵**：固定两端不同等级和技能，测试首次进入、二次重载、重启编辑器、主机退出、客户端退出、死亡复活。
3. **插入证据日志**：记录 NetMode、Authority、World、Browse/NetDriver、Possess、Controller、玩家索引和恢复状态。
4. **定位 Travel**：发现客户端曾自行 Browse 并销毁连接，服务器又重建 Listen；统一改为服务端相对 ServerTravel，客户端只跟随。
5. **定位存档时序**：发现默认 Pawn 在延迟恢复前被自动保存；加入 RestoredPlayerIndices、PendingRespawn 保护和恢复重试。
6. **拆分退出语义**：Host 退出是保存并全员 Travel；远端客户端退出是服务端保存后只让该连接 ReturnToMainMenu。
7. **清生命周期残留**：Logout/死亡时取消 Ability、Debuff、Timer、AI Target；复活重新绑定 PlayerState ASC 和常驻能力。
8. **回归验证**：重复进入退出、复活、打包与双端日志验证，而不是只测第一次成功。
9. **诚实说明边界**：当前仍是会话索引，不是账号 ID；无 Session/Seamless Travel/自动化测试。

---

## 15. 面试前源码核验清单

面试前建议在编辑器中再确认以下资产侧配置，因为 C++ 无法完整证明：

- 各 GA Blueprint 的 NetExecutionPolicy、InstancingPolicy、Cost/Cooldown GE。
- Damage GE 是否确实使用 `ExecCalc_Damage`。
- Attribute GE/MMC 的 Modifier 与曲线表。
- Behavior Tree 的 Service 频率、MoveTo/Attack 节点和 Blackboard Key。
- UMG 的 MVVM Binding、CD 材质和退出按钮 RPC 连线。
- Niagara/GameplayCue 资产路径和 Cook 后效果。
- 各地图 GameMode、PlayerController、HUD、DefaultPawn 配置。

这部分如果没有在编辑器里现场核验，回答时应说“C++ 管线已实现，资产配置需要以当前 Blueprint 为准”，不要凭记忆补全。

---

## 16. 源码证据索引

| 面试主题 | 可核验的源码符号/配置 |
| --- | --- |
| 玩家 ASC 位于 PlayerState、Mixed 复制 | `AAuraPlayerState::AAuraPlayerState` |
| Attribute RepNotify | `UAuraAttributeSet::GetLifetimeReplicatedProps`、全部 `OnRep_*` |
| ASC 重绑与复活保留 | `AAuraCharacter::PossessedBy`、`OnRep_PlayerState`、`InitAbilityActorInfo` |
| 伤害结算 | `UExecCalc_Damage::Execute_Implementation`、`UAuraAttributeSet::PostGameplayEffectExecute` |
| 自定义 EffectContext | `FAuraGameplayEffectContext::NetSerialize`、`UAuraAbilitySystemGlobals::AllocGameplayEffectContext`、`DefaultGame.ini` |
| 技能输入和 Spec Tag | `UAuraInputComponent::BindAbilityActions`、`UAuraAbilitySystemComponent::AbilityInputTag*` |
| 技能解锁/装备/回滚 | `UpdateAbilityStatus`、`ServerSpendSpellPoint_Implementation`、`ServerEquipAbility_Implementation` |
| 技能存档 | `ExportSavedAbilities`、`RestoreSavedAbilities` |
| TargetData 预测 | `UTargetDataUnderMouse`、`UTargetDataMagicCircle` |
| 冷却监听 | `UWaitCooldownChange` |
| 被动能力 | `UAuraPassiveAbility`、`ActivateEquippedPassiveAbilities`、`UAuraNiagaraComponent` |
| 电击链、眩晕与 Cue | `UAuraBeamSpell`、`AAuraGameplayCueNotifyActor` |
| 服务端地图 Travel | `AAuraGameModeBase::TravelToMap`、`ExecutePendingMapTravel`、`SaveAndReturnToMainMenu` |
| 客户端独立退出 | `AAuraPlayerController::ServerTravelToLoadMenu_Implementation`、`HandleRemotePlayerLeave`、`Logout` |
| 玩家死亡复活 | `HandlePlayerDeath`、`FindNearestValidRespawnTransform`、`RespawnPlayer` |
| 存档时序保护 | `SaveCurrentWorldInternal`、`RestoreCurrentWorld`、`RestoredPlayerIndices` |
| 存档迁移 | `AuraSaveConstants::NormalizeSavedAbilities`、`GetSaveSlotData` |
| 世界/敌人存档 | `FMapSaveData`、`AAuraEnemySpawnVolume::ExportEnemies/RestoreEnemies` |
| AI 索敌 | `UBTService_FindNearestPlayer::TickNode` |
| Crowd 参数 | `AAuraAIController::AAuraAIController`、`AAuraPlayerController::AAuraPlayerController` |
| EQS + Recast + Crowd | `GenerateAutoMoveEQSItems`、`ScoreAutoMoveEQSItem`、`RequestAutoRunPath` |
| 堵塞恢复 | `UpdateAutoMoveProgress`、`HandleBlockedAutoMove`、`HandleAutoMovePathFinished` |
| 客户端自动移动预测 | `HandleCrowdSteeringVelocity`、`ApplyClientAutoMoveSteering`、`UpdateAutoMoveFacing` |
| 投射物池 | `UAuraProjectilePoolSubsystem`、`AAuraProjectile::ActivateFromPool/DeactivateToPool` |
| FireBlast 火球池 | `AAuraFireBall::ActivateFromPool/DeactivateToPool` |
| 敌人池完整复位 | `AAuraEnemy::CapturePoolDefaults`、`ResetNativePoolState`、`DeactivateToPool` |
| 战斗 UI MVC | `AAuraHUD`、全部 `*WidgetController::BindCallbacksToDependencies` |
| 存档 UI MVVM | `UMVVM_LoadScreen`、`UMVVM_LoadSlot`、`UE_MVVM_SET_PROPERTY_VALUE` |
| GameplayTag 初始化 | `UAuraAssetManager::StartInitialLoading`、`FAuraGameplayTags::InitializeNativeGameplayTags` |
| Cook/打包配置 | `Config/DefaultGame.ini` 的 `ProjectPackagingSettings` 和 `GameplayCueNotifyPaths` |
| 当前未接入 OnlineSubsystem | `Aura.Build.cs` 中仍为注释依赖，项目没有 Session API 调用 |

本文档本身不替代源码。代码迭代后，应优先以这些符号的当前实现更新答案。
