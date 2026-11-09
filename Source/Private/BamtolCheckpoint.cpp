// 다시 쓰는 길목 액터 — 군밤 화로 쉼터 · 솜사탕 안개 · 지름길(빗장 문·밧줄 사다리). 설명은 BamtolCheckpoint.h 맨 위 · Docs/Systems/Combat.md
#include "BamtolCheckpoint.h"

#include "BamtolCombat.h"
#include "BamtolBossFight.h"
#include "BamtolPlayer.h"
#include "BamtolMine.h"
#include "BamtolAudio.h"
#include "BamtolPuffFx.h"
#include "BamtolCombatFx.h"
#include "NiagaraComponent.h"
#include "BamtolLoc.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace BtCheck
{
	UStaticMesh* Mesh(const TCHAR* Path) { return LoadObject<UStaticMesh>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet); }
	UStaticMesh* Sphere() { static TWeakObjectPtr<UStaticMesh> M; if (!M.IsValid()) M = Mesh(TEXT("/Engine/BasicShapes/Sphere.Sphere")); return M.Get(); }
	UStaticMesh* Cube() { static TWeakObjectPtr<UStaticMesh> M; if (!M.IsValid()) M = Mesh(TEXT("/Engine/BasicShapes/Cube.Cube")); return M.Get(); }
	UStaticMesh* Cylinder() { static TWeakObjectPtr<UStaticMesh> M; if (!M.IsValid()) M = Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); return M.Get(); }
	UMaterialInstanceDynamic* Solid(UObject* Outer, const FLinearColor& C)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Base) return nullptr;
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Base, Outer);
		D->SetVectorParameterValue(TEXT("Color"), C);
		return D;
	}
	UStaticMeshComponent* Part(AActor* Owner, USceneComponent* Parent, const FName Name, UStaticMesh* M)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner, Name);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetStaticMesh(M);
		C->SetupAttachment(Parent);
		C->RegisterComponent();
		return C;
	}
	// 메시를 상자(가로 W · 세로 H · 두께 T, 가운데 Ctr)에 맞춤 — 얇은 축을 액터 X 로 돌려 놓음
	void Fit(UStaticMeshComponent* C, const FVector& Ctr, float W, float H, float T)
	{
		UStaticMesh* M = C ? C->GetStaticMesh() : nullptr;
		if (!M) return;
		const FBox B = M->GetBoundingBox();
		const FVector E = B.GetExtent() * 2.f, O = B.GetCenter();
		const bool bWideX = E.X > E.Y;   // 넓은 쪽이 X 면 90도 돌려 Y 로
		const FRotator R(0.f, bWideX ? 90.f : 0.f, 0.f);
		const FVector S(T / FMath::Max(1.f, float(bWideX ? E.Y : E.X)), W / FMath::Max(1.f, float(bWideX ? E.X : E.Y)), H / FMath::Max(1.f, float(E.Z)));
		C->SetRelativeRotation(R);
		C->SetRelativeScale3D(S);
		C->SetRelativeLocation(Ctr - R.RotateVector(O * S));
	}
	FString KeyOf(const ABamtolPlayer* P)
	{
		const APlayerState* PS = P ? P->GetPlayerState() : nullptr;
		return PS ? PS->GetPlayerName() + FString::FromInt(PS->GetPlayerId()) : (P ? P->GetName() : FString());
	}
	TMap<FString, TPair<TWeakObjectPtr<UWorld>, FVector>> GLastRest;
	struct FCarRest { TWeakObjectPtr<AActor> Car; FVector Local = FVector::ZeroVector; };
	TMap<FString, FCarRest> GCarRest;   // 캠핑카 침대에서 쉰 자리 (차를 따라다님) — 11-05 발헤임식 재구성
	TSet<FName> GOpenKeys;
	bool Downed(const ABamtolPlayer* P) { const UBamtolCombat* C = UBamtolCombat::Of(P); return C && C->IsDowned(); }
}

// ============================ 군밤 화로 쉼터 ============================

ABamtolRestFire::ABamtolRestFire()
{
	PrimaryActorTick.bCanEverTick = true;   // 불빛 깜빡 · 서버는 0.1초마다 둘레 사람 셈
	bReplicates = true;
	SetReplicatingMovement(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ABamtolRestFire::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABamtolRestFire, Lit);
}

ABamtolRestFire* ABamtolRestFire::Spawn(UWorld* W, const FVector& Ground, float Yaw, AActor* Owner)
{
	if (!W || W->GetNetMode() == NM_Client) return nullptr;
	// 겹침 피하기 (2026-11-03 사용자): 화로 자리(지름 약 1.2m · 높이 0.9m)가 바위·벽·소품에 파묻히면 둘레 빈 자리로 (60~300 · 8방향, 바닥 다시 잼)
	FVector At = Ground;
	{
		FCollisionQueryParams Q(SCENE_QUERY_STAT(BtRestSpot), false, Owner);
		const FCollisionObjectQueryParams Obj(FCollisionObjectQueryParams::AllStaticObjects);
		const FCollisionShape Box = FCollisionShape::MakeBox(FVector(60.f, 60.f, 40.f));
		auto Free = [&](const FVector& P) { return !W->OverlapAnyTestByObjectType(P + FVector(0, 0, 52.f), FQuat::Identity, Obj, Box, Q); };
		if (!Free(At))
		{
			bool bFound = false;
			for (float R = 60.f; R <= 300.f && !bFound; R += 60.f)
				for (int32 I = 0; I < 8 && !bFound; ++I)
				{
					FVector P = Ground + FRotator(0.f, Yaw + I * 45.f, 0.f).Vector() * R;
					FHitResult H;
					if (!W->LineTraceSingleByObjectType(H, P + FVector(0, 0, 200.f), P - FVector(0, 0, 400.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q)) continue;
					P.Z = H.ImpactPoint.Z;
					if (Free(P)) { At = P; bFound = true; }
				}
			UE_LOG(LogTemp, Display, TEXT("[Rest] fire spot %s → %s"), *Ground.ToCompactString(), bFound ? *At.ToCompactString() : TEXT("no free spot"));   // loc:keep (개발자용)
		}
	}
	const FTransform T(FRotator(0.f, Yaw, 0.f), At);
	ABamtolRestFire* F = W->SpawnActorDeferred<ABamtolRestFire>(ABamtolRestFire::StaticClass(), T, Owner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!F) return nullptr;
	F->bNetUseOwnerRelevancy = Owner != nullptr;
	F->FinishSpawning(T);
	return F;
}

ABamtolRestFire* ABamtolRestFire::Near(const AActor* Who, float Extra)
{
	if (!Who) return nullptr;
	for (TActorIterator<ABamtolRestFire> It(Who->GetWorld()); It; ++It)
		if (It->IsNear(Who->GetActorLocation(), Extra)) return *It;
	return nullptr;
}

void ABamtolRestFire::SetCarRest(const ABamtolPlayer* P, AActor* Car, const FVector& Local)
{
	if (!P || !Car) return;
	BtCheck::GCarRest.Add(BtCheck::KeyOf(P), { Car, Local });
	BtCheck::GLastRest.Add(BtCheck::KeyOf(P), { P->GetWorld(), Car->GetActorTransform().TransformPosition(Local) });   // 차가 사라졌을 때 쓸 마지막 자리
}

bool ABamtolRestFire::CarRestFor(const ABamtolPlayer* P)
{
	const BtCheck::FCarRest* C = P ? BtCheck::GCarRest.Find(BtCheck::KeyOf(P)) : nullptr;
	return C && C->Car.IsValid();
}

bool ABamtolRestFire::LastFor(const ABamtolPlayer* P, FVector& OutAt)
{
	if (const BtCheck::FCarRest* C = P ? BtCheck::GCarRest.Find(BtCheck::KeyOf(P)) : nullptr)
	{
		if (const AActor* Car = C->Car.Get()) { OutAt = Car->GetActorTransform().TransformPosition(C->Local); return true; }   // 부활 자리 = 차 (움직이면 따라감)
		BtCheck::GCarRest.Remove(BtCheck::KeyOf(P));
	}
	const TPair<TWeakObjectPtr<UWorld>, FVector>* L = BtCheck::GLastRest.Find(BtCheck::KeyOf(P));
	if (!L || !P || L->Key.Get() != P->GetWorld()) return false;
	OutAt = L->Value;
	return true;
}

bool ABamtolRestFire::IsNear(const FVector& At, float Extra) const
{
	const FVector L = GetActorLocation();
	return FVector::Dist2D(At, L) <= Radius + Extra && FMath::Abs(At.Z - L.Z) < 350.f;
}

FVector ABamtolRestFire::StandAt(int32 Slot) const
{
	// 불 앞 반원 (불을 보고 섬)
	const float Ang = GetActorRotation().Yaw + 180.f + ((Slot % 5) - 2) * 32.f;
	return GetActorLocation() + FRotator(0.f, Ang, 0.f).Vector() * (130.f + (Slot / 5) * 70.f);
}

void ABamtolRestFire::BeginPlay()
{
	Super::BeginPlay();
	Build();
}

void ABamtolRestFire::Build()
{
	if (GetNetMode() == NM_DedicatedServer || Fire) return;
	UStaticMesh* M = BtCheck::Mesh(TEXT("/Game/StylizedIsland/Mesh/Props/SM_Bonfire/SM_Stylized_Bonfire_mesh/SM_Stylized_Bonfire.SM_Stylized_Bonfire"));
	Fire = BtCheck::Part(this, RootComponent, TEXT("Fire"), M ? M : BtCheck::Cylinder());
	const float FireH = 100.f;
	if (M) Fire->SetRelativeScale3D(FVector(FireH / FMath::Max(1.f, float(M->GetBounds().BoxExtent.GetMax() * 2.f))));
	else { Fire->SetRelativeScale3D(FVector(.8f, .8f, .3f)); Fire->SetRelativeLocation(FVector(0, 0, 15.f)); }
	// 불 위 석쇠 + 군밤 다섯 알 (자리 채움 — 최종 그림은 사용자)
	UStaticMeshComponent* Grill = BtCheck::Part(this, RootComponent, TEXT("Grill"), BtCheck::Cylinder());
	Grill->SetRelativeScale3D(FVector(.75f, .75f, .03f));
	Grill->SetRelativeLocation(FVector(0.f, 0.f, FireH * .62f));
	if (UMaterialInstanceDynamic* D = BtCheck::Solid(this, FLinearColor(.08f, .08f, .09f))) Grill->SetMaterial(0, D);
	for (int32 I = 0; I < 5; ++I)
	{
		UStaticMeshComponent* N = BtCheck::Part(this, RootComponent, FName(*FString::Printf(TEXT("Nut%d"), I)), BtCheck::Sphere());
		const FVector Off = I == 0 ? FVector::ZeroVector : FRotator(0.f, I * 90.f + 20.f, 0.f).Vector() * 18.f;
		N->SetRelativeLocation(Off + FVector(0.f, 0.f, FireH * .62f + 8.f));
		N->SetRelativeScale3D(FVector(.17f, .17f, .14f));
		if (UMaterialInstanceDynamic* D = BtCheck::Solid(this, I % 2 ? FLinearColor(.28f, .12f, .04f) : FLinearColor(.4f, .2f, .07f))) N->SetMaterial(0, D);
		Nuts.Add(N);
	}
	Light = NewObject<UPointLightComponent>(this, TEXT("Light"));
	Light->SetupAttachment(RootComponent);
	Light->SetRelativeLocation(FVector(0.f, 0.f, FireH * .8f));
	Light->SetLightColor(FLinearColor(1.f, .55f, .2f));
	Light->SetIntensity(2500.f);
	Light->SetAttenuationRadius(520.f);
	Light->SetCastShadows(false);
	Light->RegisterComponent();
}

void ABamtolRestFire::Tick(float Dt)
{
	Super::Tick(Dt);
	if (Light)
	{
		const float T = GetWorld()->GetTimeSeconds();
		Light->SetIntensity((Lit ? 2600.f : 1700.f) + 600.f * FMath::Sin(T * 9.f) * FMath::Sin(T * 3.7f));
		for (int32 I = 0; I < Nuts.Num(); ++I) if (Nuts[I]) Nuts[I]->AddLocalOffset(FVector(0, 0, FMath::Sin(T * 7.f + I * 1.3f) * .05f));   // 지글
	}
	if (!HasAuthority()) return;
	Acc += Dt;
	if (Acc < .1f) return;
	const float Step = Acc;
	Acc = 0.f;
	for (TActorIterator<ABamtolPlayer> It(GetWorld()); It; ++It)
	{
		ABamtolPlayer* P = *It;
		if (!IsNear(P->GetActorLocation()) || BtCheck::Downed(P) || P->IsCaught()) { StandSec.Remove(P); continue; }
		float& S = StandSec.FindOrAdd(P);
		const bool bFirst = S < StandTime;
		S += Step;
		if (!bFirst || S < StandTime) continue;
		// 1초 서 있음 = 이번에 한 번 (떠났다 오면 또)
		BtCheck::GLastRest.Add(BtCheck::KeyOf(P), { GetWorld(), GetActorLocation() });
		BtCheck::GCarRest.Remove(BtCheck::KeyOf(P));   // 화로에서 쉬면 부활 자리는 화로 (차 자리는 풀림)
		UBamtolCombat* C = UBamtolCombat::Of(P);
		const bool bNeed = BamtolCombat::HeartsOf(P) < BamtolCombat::MaxHeartsOf(P) || (C && C->GetHeals() < BamtolCombat::Tune().HealsPerRest);
		if (!Lit) { Lit = 1; ForceNetUpdate(); }
		if (!bNeed) continue;
		BamtolCombat::Heal(P, BamtolCombat::MaxHeartsOf(P));
		if (C) C->RefillAtRest();
		++NumRests;
		P->ClientShowToast(BTXT("군밤 화로: 따끈… 하트·군밤 채움 (여기서 다시 시작)"), 1.8f);
		UBamtolAudio::PlayAt(P, TEXT("Chime"), P->GetActorLocation(), .6f);
		if (OnRested) OnRested(P);
	}
}

void ABamtolRestFire::DrawHud(AHUD* Hud, UCanvas* Canvas, UFont* Font)
{
	APawn* Me = Hud ? Hud->GetOwningPawn() : nullptr;
	if (!Me || !Canvas || !Font) return;
	const ABamtolRestFire* F = Near(Me);
	if (!F) return;
	if (const UBamtolCombat* C = UBamtolCombat::Of(Me); C && C->InFight()) return;
	const float S = Canvas->ClipY / 1080.f;
	FCanvasTextItem T(FVector2D(Canvas->ClipX * .5f, Canvas->ClipY * .74f), FText::FromString(BTXT("군밤 화로 — 잠깐 서 있으면 하트·군밤 채움")), Font, FLinearColor(1.f, .8f, .5f));
	T.bCentreX = true;
	T.Scale = FVector2D(1.05f * S);
	T.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(T);
}

// ============================ 솜사탕 안개 ============================

ABamtolFogGate::ABamtolFogGate()
{
	PrimaryActorTick.bCanEverTick = true;   // 몽실몽실 (그림만)
	bReplicates = true;
	SetReplicatingMovement(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ABamtolFogGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABamtolFogGate, State);
	DOREPLIFETIME(ABamtolFogGate, Width);
	DOREPLIFETIME(ABamtolFogGate, Height);
}

ABamtolFogGate* ABamtolFogGate::Spawn(UWorld* W, const FVector& Door, const FVector& Inward, float InWidth, AActor* Owner)
{
	if (!W || W->GetNetMode() == NM_Client) return nullptr;
	FVector In = Inward.GetSafeNormal2D();
	if (In.IsNearlyZero()) In = FVector::ForwardVector;
	const FTransform T(In.Rotation(), Door);
	ABamtolFogGate* G = W->SpawnActorDeferred<ABamtolFogGate>(ABamtolFogGate::StaticClass(), T, Owner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!G) return nullptr;
	G->Width = InWidth;
	G->bNetUseOwnerRelevancy = Owner != nullptr;
	G->FinishSpawning(T);
	return G;
}

float ABamtolFogGate::SideOf(const FVector& At) const
{
	return float(GetActorTransform().InverseTransformPosition(At).X);
}

bool ABamtolFogGate::InGate(const FVector& At, float Depth) const
{
	const FVector L = GetActorTransform().InverseTransformPosition(At);
	return FMath::Abs(L.X) < Depth && FMath::Abs(L.Y) < Width * .5f + 40.f && L.Z > -150.f && L.Z < Height + 150.f;
}

void ABamtolFogGate::BeginPlay()
{
	Super::BeginPlay();
	Build();
}

void ABamtolFogGate::BuildFx(uint8 ForState)
{
	for (UNiagaraComponent* C : FogFx) if (C) { C->Deactivate(); C->DestroyComponent(); }   // 상태가 바뀌면 옛 안개를 지우고 새로 깖
	FogFx.Reset();
	FxState = ForState;
	if (ForState == Cleared) return;
	// 문 너비·높이를 칸(140×120)으로 나눠 칸마다 안개 하나 (입자 크기는 그대로 → 넓은 문도 촘촘)
	const int32 Ny = FMath::Clamp(FMath::RoundToInt(Width / 140.f), 2, 7), Nz = FMath::Clamp(FMath::RoundToInt(Height / 120.f), 2, 5);
	for (int32 Y = 0; Y < Ny; ++Y)
		for (int32 Z = 0; Z < Nz; ++Z)
		{
			const FVector Rel(0.f, ((Y + .5f) / Ny - .5f) * Width, (Z + .1f) / Nz * Height);
			if (UNiagaraComponent* C = BamtolCombatFx::Attach(RootComponent, ForState == Sealed ? BamtolCombatFx::EKind::FogDense : BamtolCombatFx::EKind::Fog, Rel, FRotator::ZeroRotator, FVector(1.f, Width / Ny / 100.f, Height / Nz / 100.f)))
				FogFx.Add(C);
		}
}

void ABamtolFogGate::Build()
{
	if (GetNetMode() == NM_DedicatedServer || Puffs.Num() > 0 || FogFx.Num() > 0) return;
	BuildFx(State);
	if (FogFx.Num() > 0) return;   // 나이아가라 안개가 없을 때만 아래 덩이 (예비)
	static const FLinearColor Cols[] = { FLinearColor(1.f, .55f, .75f), FLinearColor(1.f, .78f, .88f), FLinearColor(1.f, .93f, .96f), FLinearColor(.86f, .72f, 1.f), FLinearColor(.7f, .88f, 1.f) };
	TArray<UMaterialInstanceDynamic*> Mats;
	for (const FLinearColor& C : Cols) Mats.Add(BtCheck::Solid(this, C));
	FRandomStream R(int32(GetTypeHash(GetActorLocation().GridSnap(10.f))));
	// 문을 채우는 솜사탕 덩이: 아래는 크고 위로 갈수록 작게 (자리 채움 — 최종 그림은 사용자)
	const int32 Cols_ = FMath::Clamp(FMath::RoundToInt(Width / 85.f), 3, 9), Rows = FMath::Clamp(FMath::RoundToInt(Height / 85.f), 3, 6);
	for (int32 Y = 0; Y < Cols_; ++Y)
		for (int32 Z = 0; Z < Rows; ++Z)
		{
			const float Fy = (Y + .5f) / Cols_ - .5f, Fz = (Z + .5f) / Rows;
			const FVector B(R.FRandRange(-25.f, 25.f), Fy * Width + R.FRandRange(-20.f, 20.f), Fz * Height + R.FRandRange(-15.f, 15.f));
			UStaticMeshComponent* P = BtCheck::Part(this, RootComponent, FName(*FString::Printf(TEXT("Puff%d_%d"), Y, Z)), BtCheck::Sphere());
			P->SetCastShadow(false);
			if (UMaterialInstanceDynamic* M = Mats[R.RandRange(0, Mats.Num() - 1)]) P->SetMaterial(0, M);
			Puffs.Add(P);
			PuffBase.Add(B);
			PuffSize.Add(R.FRandRange(95.f, 135.f) * (1.15f - Fz * .35f));
			PuffSeed.Add(R.FRandRange(0.f, 10.f));
		}
}

void ABamtolFogGate::Tick(float Dt)
{
	Super::Tick(Dt);
	if (FogFx.Num() > 0 || FxState != 0xFF)
	{
		if (State != FxState)
		{
			if (State == Cleared) { for (UNiagaraComponent* C : FogFx) if (C) C->Deactivate(); FogFx.Reset(); FxState = Cleared; }   // 걷힘: 새로 안 나오고 남은 솜사탕은 흩어짐
			else BuildFx(State);
		}
		return;
	}
	if (Puffs.Num() == 0) return;
	const float Want = State == Open ? 1.f : State == Sealed ? 1.35f : 0.f;
	Shown = FMath::FInterpTo(Shown, Want, Dt, State == Cleared ? 2.5f : 4.f);
	const float T = GetWorld()->GetTimeSeconds();
	for (int32 I = 0; I < Puffs.Num(); ++I)
	{
		UStaticMeshComponent* P = Puffs[I];
		if (!P) continue;
		const bool bShow = Shown > .03f;
		if (P->IsVisible() != bShow) P->SetVisibility(bShow);
		if (!bShow) continue;
		const float Sd = PuffSeed[I];
		const float Breath = 1.f + .07f * FMath::Sin(T * (State == Sealed ? 3.1f : 1.6f) + Sd);
		P->SetRelativeScale3D(FVector(PuffSize[I] / 100.f * Shown * Breath));
		P->SetRelativeLocation(PuffBase[I] + FVector(FMath::Sin(T * .9f + Sd) * 8.f, FMath::Sin(T * .7f + Sd * 2.f) * 10.f, FMath::Sin(T * 1.1f + Sd) * 12.f));
	}
}

// ============================ 지름길: 빗장 문 · 밧줄 사다리 ============================

ABamtolShortcutDoor::ABamtolShortcutDoor()
{
	PrimaryActorTick.bCanEverTick = true;   // 열리는 몸짓 · 서버는 0.1초마다 사람 셈
	bReplicates = true;
	SetReplicatingMovement(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Block = CreateDefaultSubobject<UBoxComponent>(TEXT("Block"));
	Block->SetupAttachment(RootComponent);
	Block->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Block->SetCanEverAffectNavigation(false);
	Block->SetHiddenInGame(true);
}

void ABamtolShortcutDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABamtolShortcutDoor, Style);
	DOREPLIFETIME(ABamtolShortcutDoor, Size);
	DOREPLIFETIME(ABamtolShortcutDoor, bOpen);
	DOREPLIFETIME(ABamtolShortcutDoor, OpenAt);
}

ABamtolShortcutDoor* ABamtolShortcutDoor::Spawn(UWorld* W, uint8 InStyle, const FVector& At, float Yaw, float InSize, AActor* Owner)
{
	if (!W || W->GetNetMode() == NM_Client) return nullptr;
	const FTransform T(FRotator(0.f, Yaw, 0.f), At);
	ABamtolShortcutDoor* D = W->SpawnActorDeferred<ABamtolShortcutDoor>(ABamtolShortcutDoor::StaticClass(), T, Owner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!D) return nullptr;
	D->Style = InStyle;
	D->Size = InSize > 0.f ? InSize : (InStyle == RopeLadder ? 400.f : 300.f);
	D->bNetUseOwnerRelevancy = Owner != nullptr;
	D->FinishSpawning(T);
	return D;
}

void ABamtolShortcutDoor::BeginPlay()
{
	Super::BeginPlay();
	const float H = 320.f;
	if (Style == BarDoor)
	{
		Block->SetBoxExtent(FVector(22.f, Size * .5f, H * .5f));
		Block->SetRelativeLocation(FVector(0.f, 0.f, H * .5f));
	}
	else Block->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (HasAuthority() && (bStartOpen || (!SaveKey.IsNone() && BtCheck::GOpenKeys.Contains(SaveKey)))) { bOpen = true; OpenAt = -100.0; }
	Build();
	OnRep_Open();
}

bool ABamtolShortcutDoor::CanOpenFrom(const FVector& At) const
{
	const FVector L = GetActorTransform().InverseTransformPosition(At);
	if (Style == BarDoor) return L.X > 15.f && L.X < 230.f && FMath::Abs(L.Y) < Size * .5f + 60.f && FMath::Abs(L.Z - 100.f) < 220.f;
	// 사다리: 위 (모서리 뒤쪽 땅)
	return L.X < 80.f && L.X > -260.f && FMath::Abs(L.Y) < 200.f && L.Z > -70.f && L.Z < 250.f;
}

void ABamtolShortcutDoor::OpenNow(ABamtolPlayer* By)
{
	if (!HasAuthority() || bOpen) return;
	bOpen = true;
	OpenAt = BamtolCombat::Now(this);
	if (!SaveKey.IsNone()) BtCheck::GOpenKeys.Add(SaveKey);
	if (By) By->ClientShowToast(Style == BarDoor ? BTXT("끼익… 빗장 풀었다! 지름길이 열렸다") : BTXT("밧줄 사다리를 휙 내렸다! 지름길이 열렸다"), 2.2f);
	UBamtolAudio::PlayAt(this, Style == BarDoor ? TEXT("Crack") : TEXT("Whoosh"), GetActorLocation(), .9f, .8f);
	OnRep_Open();
	ForceNetUpdate();
}

void ABamtolShortcutDoor::OnRep_Open()
{
	if (Style == BarDoor && Block) Block->SetCollisionEnabled(bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
}

void ABamtolShortcutDoor::Build()
{
	if (GetNetMode() == NM_DedicatedServer || LeafL || Roll) return;
	const float H = 320.f;
	if (Style == BarDoor)
	{
		// 두 짝 문 (돌쩌귀 = 양 끝) + 여는 쪽(+X)에 가로 빗장
		UStaticMesh* Leaf = BtCheck::Mesh(TEXT("/Game/Asian_Village/meshes/building/SM_door_01.SM_door_01"));
		UMaterialInstanceDynamic* Wood = BtCheck::Solid(this, FLinearColor(.36f, .22f, .11f));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float Sg = Side == 0 ? -1.f : 1.f;
			USceneComponent* Hinge = NewObject<USceneComponent>(this, Side == 0 ? TEXT("HingeL") : TEXT("HingeR"));
			Hinge->SetupAttachment(RootComponent);
			Hinge->SetRelativeLocation(FVector(0.f, Sg * Size * .5f, 0.f));
			Hinge->RegisterComponent();
			UStaticMeshComponent* L = BtCheck::Part(this, Hinge, Side == 0 ? TEXT("LeafL") : TEXT("LeafR"), Leaf ? Leaf : BtCheck::Cube());
			BtCheck::Fit(L, FVector(0.f, -Sg * Size * .25f, H * .5f), Size * .5f - 4.f, H, 14.f);
			if (!Leaf && Wood) L->SetMaterial(0, Wood);
			(Side == 0 ? LeafL : LeafR) = L;
		}
		Bar = BtCheck::Part(this, RootComponent, TEXT("Bar"), BtCheck::Cube());
		Bar->SetRelativeScale3D(FVector(.2f, Size * 1.08f / 100.f, .2f));
		Bar->SetRelativeLocation(FVector(22.f, 0.f, H * .48f));
		if (UMaterialInstanceDynamic* D = BtCheck::Solid(this, FLinearColor(.22f, .12f, .05f))) Bar->SetMaterial(0, D);
		// 빗장 걸이 (작은 쇠 고리 둘)
		for (int32 I = 0; I < 2; ++I)
		{
			UStaticMeshComponent* K = BtCheck::Part(this, RootComponent, I ? TEXT("HookR") : TEXT("HookL"), BtCheck::Cube());
			K->SetRelativeScale3D(FVector(.3f, .1f, .34f));
			K->SetRelativeLocation(FVector(18.f, (I ? 1.f : -1.f) * Size * .3f, H * .48f));
			if (UMaterialInstanceDynamic* D = BtCheck::Solid(this, FLinearColor(.12f, .12f, .13f))) K->SetMaterial(0, D);
		}
		return;
	}
	// 밧줄 사다리: 닫힘 = 위에 돌돌 말림 · 열림 = 아래로 풀림 (줄 둘 + 가로 발판)
	UMaterialInstanceDynamic* Rope = BtCheck::Solid(this, FLinearColor(.62f, .48f, .28f));
	UMaterialInstanceDynamic* Rung = BtCheck::Solid(this, FLinearColor(.4f, .25f, .12f));
	Roll = BtCheck::Part(this, RootComponent, TEXT("Roll"), BtCheck::Cylinder());
	Roll->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
	Roll->SetRelativeScale3D(FVector(.32f, .32f, .8f));
	Roll->SetRelativeLocation(FVector(-10.f, 0.f, 16.f));
	if (Rope) Roll->SetMaterial(0, Rope);
	for (int32 I = 0; I < 2; ++I)
	{
		UStaticMeshComponent* S = BtCheck::Part(this, RootComponent, I ? TEXT("RopeR") : TEXT("RopeL"), BtCheck::Cylinder());
		if (Rope) S->SetMaterial(0, Rope);
		Rungs.Add(S);   // 0·1 = 줄
	}
	const int32 N = FMath::Clamp(FMath::RoundToInt(Size / 45.f), 3, 30);
	for (int32 I = 0; I < N; ++I)
	{
		UStaticMeshComponent* R = BtCheck::Part(this, RootComponent, FName(*FString::Printf(TEXT("Rung%d"), I)), BtCheck::Cube());
		if (Rung) R->SetMaterial(0, Rung);
		Rungs.Add(R);
	}
	// 위 말뚝 둘
	for (int32 I = 0; I < 2; ++I)
	{
		UStaticMeshComponent* K = BtCheck::Part(this, RootComponent, I ? TEXT("StakeR") : TEXT("StakeL"), BtCheck::Cylinder());
		K->SetRelativeScale3D(FVector(.14f, .14f, .5f));
		K->SetRelativeLocation(FVector(-25.f, (I ? 1.f : -1.f) * 40.f, 25.f));
		if (Rung) K->SetMaterial(0, Rung);
	}
}

void ABamtolShortcutDoor::ApplyLook(float Dt)
{
	const double E = bOpen ? (OpenAt < 0.0 ? 10.0 : BamtolCombat::Now(this) - OpenAt) : -1.0;
	const float H = 320.f;
	if (Style == BarDoor)
	{
		// 빗장 들림(0~.5초) → 옆으로 빠짐(.5~.9) → 문짝이 안쪽(-X)으로 열림(.6~1.6)
		if (Bar)
		{
			const float Up = E < 0.0 ? 0.f : float(FMath::Clamp(E / .5, 0.0, 1.0)) * 45.f;
			const float Out = E < 0.0 ? 0.f : float(FMath::Clamp((E - .5) / .4, 0.0, 1.0)) * Size * 1.1f;
			Bar->SetRelativeLocation(FVector(22.f, Out, H * .48f + Up));
			Bar->SetVisibility(E < 1.2);
		}
		const float A = E < 0.0 ? 0.f : FMath::SmoothStep(0.f, 1.f, float(FMath::Clamp((E - .6) / 1.0, 0.0, 1.0)));
		if (LeafL && LeafL->GetAttachParent()) LeafL->GetAttachParent()->SetRelativeRotation(FRotator(0.f, 95.f * A, 0.f));
		if (LeafR && LeafR->GetAttachParent()) LeafR->GetAttachParent()->SetRelativeRotation(FRotator(0.f, -95.f * A, 0.f));
		return;
	}
	// 사다리: 줄이 위에서부터 풀려 내려옴 (0.8초)
	const float Drop = E < 0.0 ? 0.f : FMath::SmoothStep(0.f, 1.f, float(FMath::Clamp(E / .8, 0.0, 1.0)));
	if (Roll) Roll->SetVisibility(Drop < .95f);
	const float Len = Size * Drop;
	for (int32 I = 0; I < Rungs.Num(); ++I)
	{
		UStaticMeshComponent* C = Rungs[I];
		if (!C) continue;
		if (I < 2)
		{
			C->SetVisibility(Len > 5.f);
			C->SetRelativeScale3D(FVector(.05f, .05f, FMath::Max(.01f, Len / 100.f)));
			C->SetRelativeLocation(FVector(28.f, (I ? 1.f : -1.f) * 35.f, -Len * .5f + 10.f));
			continue;
		}
		const float Z = -float(I - 1) * 45.f + 10.f;
		C->SetVisibility(-Z < Len);
		C->SetRelativeScale3D(FVector(.06f, .74f, .06f));
		C->SetRelativeLocation(FVector(28.f + FMath::Sin((I + GetWorld()->GetTimeSeconds()) * 1.3f) * 1.5f, 0.f, Z));
	}
}

void ABamtolShortcutDoor::Tick(float Dt)
{
	Super::Tick(Dt);
	if (GetNetMode() != NM_DedicatedServer) ApplyLook(Dt);
	if (!HasAuthority()) return;
	Acc += Dt;
	if (Acc < .1f) return;
	ServerTick(Acc);
	Acc = 0.f;
}

void ABamtolShortcutDoor::ServerTick(float Dt)
{
	const double Now = BamtolCombat::Now(this);
	for (auto It = Climbing.CreateIterator(); It; ++It)
	{
		ABamtolPlayer* P = It->Key.Get();
		if (!P || Now - It->Value > 2.0) { It.RemoveCurrent(); continue; }
		const float Feet = P->GetActorLocation().Z - P->GetSimpleCollisionHalfHeight();
		if (Feet < GetActorLocation().Z + 25.f) continue;
		P->LaunchCharacter(-GetActorForwardVector().GetSafeNormal2D() * 380.f, true, false);   // 턱 위로 올라섬
		It.RemoveCurrent();
	}
	for (TActorIterator<ABamtolPlayer> It(GetWorld()); It; ++It)
	{
		ABamtolPlayer* P = *It;
		const FVector L = P->GetActorLocation();
		if (FVector::Dist(L, GetActorLocation()) > Size + 700.f) { NearSec.Remove(P); LiftSec.Remove(P); continue; }
		if (!bOpen)
		{
			// 여는 쪽에 0.5초 서 있으면 열림 · 반대쪽이면 한 번 안내
			if (CanOpenFrom(L) && !BtCheck::Downed(P))
			{
				float& S = NearSec.FindOrAdd(P);
				S += Dt;
				if (S >= .5f) { OpenNow(P); return; }
				continue;
			}
			NearSec.Remove(P);
			const FVector Lo = GetActorTransform().InverseTransformPosition(L);
			const bool bLockedSide = Style == BarDoor ? (Lo.X < -15.f && Lo.X > -230.f && FMath::Abs(Lo.Y) < Size * .5f + 60.f && FMath::Abs(Lo.Z - 100.f) < 220.f)
				: (Lo.X > 0.f && Lo.X < 260.f && FMath::Abs(Lo.Y) < 220.f && Lo.Z < -Size + 250.f && Lo.Z > -Size - 150.f);
			float& Hint = HintAt.FindOrAdd(P);
			if (bLockedSide && Now - Hint > 6.0)
			{
				Hint = float(Now);
				++NumLockedHints;
				P->ClientShowToast(Style == BarDoor ? BTXT("꿈쩍도 안 한다… 빗장이 반대편에 걸려 있다") : BTXT("밧줄이 위에 돌돌 말려 있다… 위에서 내려 줘야 할 듯"), 2.f);
			}
			continue;
		}
		// 열린 사다리: 아래에서 줄에 붙어 0.35초 = 영차 올라감 (위로 휙)
		if (Style != RopeLadder) continue;
		const FVector Lo = GetActorTransform().InverseTransformPosition(L);
		const bool bAtFoot = Lo.X > -10.f && Lo.X < 150.f && FMath::Abs(Lo.Y) < 90.f && Lo.Z < -90.f && Lo.Z > -Size - 160.f;
		float& S = LiftSec.FindOrAdd(P);
		if (!bAtFoot || BtCheck::Downed(P)) { S = S < 0.f ? FMath::Min(S + Dt, 0.f) : 0.f; continue; }   // 음수 = 방금 올라감 (쉼) · 발밑을 떠나면 다시 0부터
		if (S < 0.f) { S += Dt; continue; }
		S += Dt;
		if (S < .35f) continue;
		S = -1.2f;
		UCharacterMovementComponent* Cm = P->GetCharacterMovement();
		const float G = Cm ? FMath::Abs(Cm->GetGravityZ()) : 980.f;
		const float Feet = L.Z - P->GetSimpleCollisionHalfHeight();
		const float Rise = FMath::Max(100.f, GetActorLocation().Z + 110.f - Feet);
		const float Vz = FMath::Sqrt(2.f * G * Rise);
		// 먼저 곧게 위로 (턱에 안 걸리게) → 턱보다 높아지면 위 땅 쪽으로 (아래 Climbing)
		P->LaunchCharacter(FVector(0.f, 0.f, Vz) - GetActorForwardVector().GetSafeNormal2D() * 20.f, true, true);
		Climbing.Add(P, Now);
		P->MarkSafeFall(3.f);
		++NumLifts;
		P->ClientShowToast(BTXT("영차영차! (밧줄 타고 올라감)"), 1.2f);
		UBamtolAudio::PlayAt(P, TEXT("Boing"), L, .7f, 1.2f);
	}
}
