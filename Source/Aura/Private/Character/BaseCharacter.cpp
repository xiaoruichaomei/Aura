


#include "Character/BaseCharacter.h"

#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/AuraNiagaraComponent.h"
#include "Aura/Aura.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraSystem.h"

ABaseCharacter::ABaseCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	
	Weapon = CreateDefaultSubobject<USkeletalMeshComponent>("Weapon");
	check(Weapon);
	Weapon->SetupAttachment(GetMesh(), FName("WeaponHandSocket"));
	Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCapsuleComponent()->SetGenerateOverlapEvents(false);
	GetMesh()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
	GetMesh()->SetCollisionResponseToChannel(ECC_Beam, ECR_Block);
	GetMesh()->SetGenerateOverlapEvents(true);
	
	Weapon->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	BurnNiagaraComponent = CreateDefaultSubobject<UAuraNiagaraComponent>("BurnNiagaraComponent");
	check(BurnNiagaraComponent);
	BurnNiagaraComponent->SetupAttachment(GetMesh());

	StunNiagaraComponent = CreateDefaultSubobject<UAuraNiagaraComponent>("StunNiagaraComponent");
	check(StunNiagaraComponent);
	StunNiagaraComponent->SetupAttachment(GetMesh());
	StunNiagaraComponent->bDeactivateImmediately = true;
	StunNiagaraComponent->InitialSimulationTime = 2.f;
}

void ABaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 燃烧特效：标签在 BeginPlay 时已注册（不能在构造函数里读 GameplayTag）；
	// Niagara 资产默认用火系火焰系统，可在蓝图里覆盖。
	if (BurnNiagaraComponent)
	{
		BurnNiagaraComponent->GameplayTag = FAuraGameplayTags::Get().Effects_Debuff_Burn;
		if (!BurnNiagaraComponent->GetAsset())
		{
			if (UNiagaraSystem* FireSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Assets/Effects/Fire/NS_Fire.NS_Fire")))
			{
				BurnNiagaraComponent->SetAsset(FireSystem);
			}
		}
	}

	// 眩晕特效：标签 Effects.Debuff.Stun 存在时显示转圈星星（NS_Stars），移除时自动停用。
	// 出现位置不在此定位：用户在各自敌人蓝图里手动调整 StunNiagaraComponent 的 Relative Location。
	if (StunNiagaraComponent)
	{
		StunNiagaraComponent->GameplayTag = FAuraGameplayTags::Get().Effects_Debuff_Stun;
		// NS_Stars 是一次性不循环特效：标签持续期间自动检测播完并重启，形成持续显示（循环资产不会被打断）
		StunNiagaraComponent->bLoopWhileActive = true;
		if (!StunNiagaraComponent->GetAsset())
		{
			if (UNiagaraSystem* StarsSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Assets/Effects/Stun/NS_Stars.NS_Stars")))
			{
				StunNiagaraComponent->SetAsset(StarsSystem);
			}
		}
	}
}

UAbilitySystemComponent* ABaseCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

UAnimMontage* ABaseCharacter::GetHitReactMontage_Implementation()
{
	return HitReactMontage;
}

void ABaseCharacter::Die()
{
	Weapon->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepWorld, true));
	MulticastHandleDeath();
}

void ABaseCharacter::SetDeathImpulse(const FVector& InImpulse)
{
	DeathImpulse = InImpulse;
}

void ABaseCharacter::ApplyDeathImpulse()
{
	if (IsValid(GetMesh()))
	{
		// 把冲量施加到每个物理骨骼：只作用于根骨骼会被约束吸收，尸体几乎不动。
		// bVelocityChange=true 时每个骨骼都直接加上该速度，整个 ragdoll 作为整体被抛飞。
		const int32 NumBones = GetMesh()->GetNumBones();
		for (int32 i = 0; i < NumBones; ++i)
		{
			GetMesh()->AddImpulse(DeathImpulse, GetMesh()->GetBoneName(i), true);
		}
	}
}

TArray<FTaggedMontage> ABaseCharacter::GetAttackMontages_Implementation()
{
	return AttackMontages;
}

UNiagaraSystem* ABaseCharacter::GetBloodEffect_Implementation()
{
	return BloodEffect;
}

FTaggedMontage ABaseCharacter::GetTaggedMontageByTag_Implementation(const FGameplayTag& Tag)
{
	for (const FTaggedMontage& TaggedMontage : AttackMontages)
	{
		if (TaggedMontage.MontageTag == Tag)
		{
			return TaggedMontage;
		}
	}
	return FTaggedMontage();
}

int32 ABaseCharacter::GetMinionsLimit_Implementation()
{
	return MinionsCount;
}

void ABaseCharacter::IncrementMinionCount_Implementation(int32 Amount)
{
	MinionsCount += Amount ;
}

ECharacterClass ABaseCharacter::GetCharacterClass_Implementation()
{
	return CharacterClass;
}

void ABaseCharacter::MulticastHandleDeath_Implementation()
{
	UGameplayStatics::PlaySoundAtLocation(this, DeathSound, GetActorLocation(), GetActorRotation());
	
	Weapon->SetSimulatePhysics(true);
	Weapon->SetEnableGravity(true);
	Weapon->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetEnableGravity(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	GetMesh()->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);

	// 死亡冲量：按命中方向把尸体击飞。
	// 延迟 0.05s 再施加，等物理体初始化完成（同帧立即 AddImpulse 会被丢弃/不稳定）。
	if (!DeathImpulse.IsZero())
	{
		FTimerHandle DeathImpulseTimer;
		GetWorldTimerManager().SetTimer(DeathImpulseTimer, this, &ABaseCharacter::ApplyDeathImpulse, 0.05f, false);
	}


	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Dissolve();
	bDead = true;
}

void ABaseCharacter::InitAbilityActorInfo()
{
}

void ABaseCharacter::InitializeDefaultAttributes() const
{
	ApplyEffectToSelf(DefaultPrimaryAttributes, 1.f);
	ApplyEffectToSelf(DefaultSecondaryAttributes, 1.f);
	ApplyEffectToSelf(DefaultVitalAttributes, 1.f);
}

void ABaseCharacter::AddCharacterAbilities()
{
	if (!HasAuthority())
	{
		return;
	}
	
	UAuraAbilitySystemComponent* AuraASC = CastChecked<UAuraAbilitySystemComponent>(AbilitySystemComponent);
	
	AuraASC->AddCharacterAbilities(StartupAbility, PassiveAbility);
}

void ABaseCharacter::Dissolve()
{
	if (IsValid(DissolveMaterialInstance))
	{
		UMaterialInstanceDynamic* DynamicMatInst = UMaterialInstanceDynamic::Create(DissolveMaterialInstance, this);
		GetMesh()->SetMaterial(0, DynamicMatInst);
		
		StartDissolveTimeline(DynamicMatInst);
	}
	if (IsValid(WeaponDissolveMaterialInstance))
	{
		UMaterialInstanceDynamic* DynamicMatInst = UMaterialInstanceDynamic::Create(WeaponDissolveMaterialInstance, this);
		Weapon->SetMaterial(0, DynamicMatInst);
		
		StartWeaponDissolveTimeline(DynamicMatInst);
	}
}

FVector ABaseCharacter::GetCombatSockettLocation_Implementation(const FGameplayTag& MontageTag)
{
	if (MontageTag.MatchesTagExact(FAuraGameplayTags::Get().CombatSocket_Weapon) && IsValid(Weapon))
	{
		return Weapon->GetSocketLocation(WeaponTipSocketName);
	}
	if (MontageTag.MatchesTagExact(FAuraGameplayTags::Get().CombatSocket_RightHand))
	{
		return GetMesh()->GetSocketLocation(RightHandTipSocketName);
	}
	if (MontageTag.MatchesTagExact(FAuraGameplayTags::Get().CombatSocket_LeftHand))
	{
		return GetMesh()->GetSocketLocation(LeftHandTipSocketName);
	}
	if (MontageTag.MatchesTagExact(FAuraGameplayTags::Get().CombatSocket_Tail))
	{
		return GetMesh()->GetSocketLocation(TailSocketName);
	}
	return FVector();
}

bool ABaseCharacter::IsDead_Implementation() const
{
	return bDead;
}

AActor* ABaseCharacter::GetAvatar_Implementation()
{
	return this;
}

void ABaseCharacter::ApplyEffectToSelf(const TSubclassOf<UGameplayEffect> GameplayEffectClass, const float Level) const
{
	check(GetAbilitySystemComponent());
	check(GameplayEffectClass);
	FGameplayEffectContextHandle ContextHandle = GetAbilitySystemComponent()->MakeEffectContext();
	ContextHandle.AddSourceObject(this);
	const FGameplayEffectSpecHandle SpecHandle = GetAbilitySystemComponent()->MakeOutgoingSpec(GameplayEffectClass, Level, ContextHandle);
	GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), GetAbilitySystemComponent());
}
