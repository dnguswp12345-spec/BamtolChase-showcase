// 다시 자라는 줍기 점 — 설명 BamtolPickable.h · 기획 Docs/Plan_SmallGaps_1105.md 1절
#include "BamtolPickable.h"

#include "BamtolAnimals.h"
#include "BamtolGrowth.h"   // [growth-extra]
#include "BamtolAudio.h"
#include "BamtolDaily.h"
#include "BamtolGround.h"
#include "BamtolLoc.h"
#include "BamtolPlayer.h"
#include "BamtolPouch.h"
#include "BamtolPuffFx.h"
#include "BamtolSaveSafe.h"
#include "BamtolTower.h"
#include "BamtolWater.h"

#include "Components/DrawSphereComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DateTime.h"
#include "Net/UnrealNetwork.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
	FString GPkTestSlot;

	int64 PkNow() { return FDateTime::UtcNow().ToUnixTimestamp(); }

	// 줍기 점 소리 이름표 → 지금 있는 소리 (Wwise 로 옮기면 이 표만 지움). Docs/Plan_SmallGaps_1105.md 소리 이름표
	void PkSound(const UObject* Ctx, const TCHAR* Name, const FVector& At, float Vol, float Pitch)
	{
		if (!Ctx || !Ctx->GetWorld() || Ctx->GetWorld()->GetNetMode() == NM_DedicatedServer) return;
		const FName Temp = FString(Name) == TEXT("Pick_Pop") ? FName(TEXT("Pop")) : FName(TEXT("Chime"));   // 임시 연결: MS_Pop · MS_Chime
		UBamtolAudio::PlayAt(Ctx, Temp, At, Vol, Pitch);
	}

	uint32 PkKeyOf(const UWorld* W, const FVector& At)
	{
		const FString Map = W ? UWorld::RemovePIEPrefix(W->GetMapName()) : FString();
		return FCrc::StrCrc32(*FString::Printf(TEXT("%s|%d|%d|%d"), *Map, FMath::RoundToInt(At.X / 10.f), FMath::RoundToInt(At.Y / 10.f), FMath::RoundToInt(At.Z / 10.f)));
	}

	// 둘레에 길을 막는 물건이 없나 (채집 FgxClear 와 같은 생각: 몸을 막는 것과 겹치면 안 됨)
	bool PkClear(UWorld* W, const FVector& At, float R)
	{
		TArray<FOverlapResult> Hits;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(PickableClear), false);
		W->OverlapMultiByChannel(Hits, At + FVector(0, 0, R + 30.f), FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(R), Q);
		for (const FOverlapResult& O : Hits)
		{
			const AActor* A = O.GetActor();
			if (!A || A->IsA<ALandscapeProxy>() || A->IsA<ABamtolPickable>() || A->IsA<APawn>() || A->ActorHasTag(TEXT("KartRoad"))) continue;
			const UPrimitiveComponent* C = O.GetComponent();
			if (C && C->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block) return false;
		}
		return true;
	}

	ABamtolPickable* PkSpawn(UWorld* W, const BamtolPickable::FPlan& P, EBamtolPickRespawn Mode, float Secs, float Bonus, float Stagger, AActor* Owner)
	{
		const FTransform T(FRotator(0.f, P.Yaw, 0.f), P.At);
		ABamtolPickable* S = W->SpawnActorDeferred<ABamtolPickable>(ABamtolPickable::StaticClass(), T, Owner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!S) return nullptr;
		S->ItemId = P.Id;
		S->RespawnMode = Mode;
		S->RespawnSeconds = Secs;
		S->BonusChance = Bonus;
		S->StaggerMinutes = Stagger;
		S->FinishSpawning(T);
		return S;
	}
}

// ============================ 계획 ============================

TArray<FBamtolPickKind> BamtolPickable::DefaultKinds()
{
	TArray<FBamtolPickKind> K;
	auto Add = [&](const TCHAR* Id, float W) { FBamtolPickKind X; X.Id = Id; X.Weight = W; K.Add(X); };
	Add(TEXT("FgShroom"), 3.f);
	Add(TEXT("FgBerry"), 3.f);
	Add(TEXT("FgFlower"), 2.f);
	Add(TEXT("FgMugwort"), 2.f);
	Add(TEXT("FgPinecone"), 1.f);
	Add(TEXT("FgChestnut"), 1.f);
	return K;
}

void BamtolPickable::PlanClusters(UWorld* W, const FVector& Center, float Radius, int32 Count, int32 Clusters, const TArray<FBamtolPickKind>& InKinds, int32 Seed, TArray<FPlan>& Out)
{
	Out.Reset();
	if (!W || Count <= 0 || Clusters <= 0 || Radius < 50.f) return;
	const TArray<FBamtolPickKind> Kinds = InKinds.Num() > 0 ? InKinds : DefaultKinds();
	float Total = 0.f;
	for (const FBamtolPickKind& K : Kinds) Total += FMath::Max(0.f, K.Weight);
	if (Total <= 0.f) return;
	auto PickKind = [&](float F) -> FName
	{
		float A = F * Total;
		FName Last;
		for (const FBamtolPickKind& K : Kinds)
		{
			if (K.Weight <= 0.f) continue;
			Last = K.Id;
			A -= K.Weight;
			if (A <= 0.f) return K.Id;
		}
		return Last;
	};
	const FVector2D C2(Center.X, Center.Y);
	FRandomStream R(Seed);
	for (int32 C = 0; C < Clusters && Out.Num() < Count; ++C)
	{
		const float CAng = R.FRandRange(0.f, 360.f);
		const float CRad = Radius * FMath::Sqrt(R.FRand());
		const FName Primary = PickKind(R.FRand());
		const FVector2D CC = C2 + FVector2D(FMath::Cos(FMath::DegreesToRadians(CAng)), FMath::Sin(FMath::DegreesToRadians(CAng))) * CRad;
		const int32 Want = FMath::Min(R.RandRange(3, 6), Count - Out.Num());   // 무리 3~6개 (고른 간격 아님)
		for (int32 i = 0; i < Want; ++i)
		{
			for (int32 Try = 0; Try < 10; ++Try)
			{
				// 시도마다 같은 수만큼 뽑아 둠 (결과가 땅 검사에 따라 흔들리지 않게)
				const float A = R.FRandRange(0.f, 360.f), D = 60.f + R.FRand() * 240.f, Mix = R.FRand(), Alt = R.FRand(), Yaw = R.FRand() * 360.f;
				const FVector2D P = CC + FVector2D(FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A))) * D;
				if (FVector2D::Distance(P, C2) > Radius + 200.f) continue;
				FHitResult H;
				if (!BamtolGroundAt(W, P, H) || H.ImpactNormal.Z < 0.866f) continue;   // 경사 30도 안
				const FVector G(H.ImpactPoint);
				if (BamtolInWater(W, G)) continue;
				bool bNear = false;
				for (const FPlan& O : Out) bNear |= FVector::Dist(O.At, G) < 120.f;   // 줍기 점끼리 1.2m
				if (bNear || !PkClear(W, G, 40.f)) continue;
				FPlan Pl;
				Pl.At = G;
				Pl.Id = Mix < 0.8f ? Primary : PickKind(Alt);
				Pl.Yaw = Yaw;
				Out.Add(Pl);
				break;
			}
		}
	}
}

bool BamtolPickable::Present(const UWorld* W)
{
	const UBamtolPickableWorld* S = UBamtolPickableWorld::Get(W);
	return S && S->Num() > 0;
}

FString BamtolPickable::SlotName() { return GPkTestSlot.IsEmpty() ? FString(TEXT("BamtolPickables")) : GPkTestSlot; }
void BamtolPickable::UseTestSlot(const FString& Slot) { GPkTestSlot = Slot; }

// ============================ 월드 관리자 ============================

bool UBamtolPickableWorld::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UBamtolPickableWorld::Deinitialize()
{
	UWorld* W = GetWorld();
	if (bLoaded && W && W->GetNetMode() != NM_Client && W->GetTimerManager().IsTimerActive(SaveTimer)) SaveNow();
	Super::Deinitialize();
}

void UBamtolPickableWorld::EnsureLoaded()
{
	if (bLoaded) return;
	bLoaded = true;
	Rows.Reset();
	InitMaps.Reset();
	if (const UBamtolPickableSave* S = Cast<UBamtolPickableSave>(UGameplayStatics::LoadGameFromSlot(BamtolPickable::SlotName(), 0)))
	{
		for (const FBamtolPickableRow& R : S->Rows) Rows.Add(R.Key, R);
		InitMaps = S->InitMaps;
	}
	const FString Map = UWorld::RemovePIEPrefix(GetWorld()->GetMapName());
	bFirstRun = !InitMaps.Contains(Map);
	if (bFirstRun) { InitMaps.Add(Map); Dirty(); }
}

void UBamtolPickableWorld::Register(ABamtolPickable* P)
{
	if (!P) return;
	Spots.AddUnique(P);
	UWorld* W = GetWorld();
	if (W && P->HasAuthority() && W->GetNetMode() != NM_Client)
	{
		EnsureLoaded();
		if (!W->GetTimerManager().IsTimerActive(PollTimer)) W->GetTimerManager().SetTimer(PollTimer, this, &UBamtolPickableWorld::Poll, 1.f, true);
	}
}

void UBamtolPickableWorld::Unregister(ABamtolPickable* P) { Spots.Remove(P); }

bool UBamtolPickableWorld::Find(uint32 Key, FBamtolPickableRow& Out) const
{
	if (const FBamtolPickableRow* R = Rows.Find(Key)) { Out = *R; return true; }
	return false;
}

void UBamtolPickableWorld::Put(const FBamtolPickableRow& Row) { EnsureLoaded(); Rows.Add(Row.Key, Row); Dirty(); }
void UBamtolPickableWorld::Erase(uint32 Key) { if (Rows.Remove(Key) > 0) Dirty(); }

void UBamtolPickableWorld::Poll()
{
	const int64 Now = PkNow();
	const int32 Today = BamtolDaily::TodayKey();
	for (int32 i = Spots.Num() - 1; i >= 0; --i)
	{
		ABamtolPickable* P = Spots[i].Get();
		if (!P) { Spots.RemoveAtSwap(i); continue; }
		if (P->HasAuthority()) P->ServerCheckRegrow(Now, Today);
	}
}

void UBamtolPickableWorld::Dirty()
{
	UWorld* W = GetWorld();
	if (!W || W->GetNetMode() == NM_Client) return;
	if (!W->GetTimerManager().IsTimerActive(SaveTimer)) W->GetTimerManager().SetTimer(SaveTimer, this, &UBamtolPickableWorld::SaveNow, 3.f, false);
}

void UBamtolPickableWorld::SaveNow()
{
	UWorld* W = GetWorld();
	if (!W || W->GetNetMode() == NM_Client || !bLoaded) return;   // 방장만 저장
	W->GetTimerManager().ClearTimer(SaveTimer);
	// 목록이 계속 커지지 않게: 45일 넘은 줄 · 4000줄 넘으면 오래된 것부터 지움
	const int64 Now = PkNow();
	for (auto It = Rows.CreateIterator(); It; ++It) if (It.Value().Unix < Now - 45LL * 86400LL) It.RemoveCurrent();
	if (Rows.Num() > 4000)
	{
		TArray<FBamtolPickableRow> All;
		Rows.GenerateValueArray(All);
		All.Sort([](const FBamtolPickableRow& A, const FBamtolPickableRow& B) { return A.Unix < B.Unix; });
		for (int32 i = 0; i < All.Num() - 4000; ++i) Rows.Remove(All[i].Key);
	}
	UBamtolPickableSave* S = Cast<UBamtolPickableSave>(UGameplayStatics::CreateSaveGameObject(UBamtolPickableSave::StaticClass()));
	if (!S) return;
	Rows.GenerateValueArray(S->Rows);
	S->InitMaps = InitMaps;
	BamtolSave::ToSlot(S, BamtolPickable::SlotName(), 0);
}

void UBamtolPickableWorld::ReloadFromFile()
{
	bLoaded = false;
	EnsureLoaded();
}

// ============================ 줍기 점 ============================

ABamtolPickable::ABamtolPickable()
{
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(2.f);
	SetNetCullDistanceSquared(FMath::Square(7000.f));
	PrimaryActorTick.bCanEverTick = true;   // 흔들림·반짝은 매 프레임 필요하나 가까울 때만 (멀면 0.5초·빈 자리 1초 간격, 서버 전용은 꺼짐)
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Root);
	Tags.Add(TEXT("NoClimb"));
}

void ABamtolPickable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABamtolPickable, ItemId);
	DOREPLIFETIME(ABamtolPickable, bReady);
}

void ABamtolPickable::BeginPlay()
{
	Super::BeginPlay();
	UWorld* W = GetWorld();
	Key = PkKeyOf(W, GetActorLocation());
	Phase = float(Key % 6283) / 1000.f;
	if (UBamtolPickableWorld* Sub = UBamtolPickableWorld::Get(W))
	{
		bRegistered = true;
		Sub->Register(this);
		if (HasAuthority() && W->GetNetMode() != NM_Client)
		{
			const int64 Now = PkNow();
			const int32 Today = BamtolDaily::TodayKey();
			FBamtolPickableRow R;
			if (Sub->Find(Key, R))   // 저장돼 있음: 저장대로 (벌써 자랄 때가 됐으면 자란 것)
			{
				bReady = ReadyBy(R, Now, Today);
				if (bReady) Sub->Erase(Key);
			}
			else if (Sub->IsFirstRun() && StaggerMinutes > 0.f)   // 처음 켜짐: 자리마다 0~StaggerMinutes 분 섞어 첫 준비
			{
				FRandomStream S{ int32(Key) };
				// 처음 들어온 사람이 빈 밭을 보지 않게: 70% 는 바로 준비, 30% 만 0~StaggerMinutes 분 뒤 (메인 11-05)
				const bool bLate = S.FRand() < .3f;
				const int64 Delay = bLate ? int64(S.FRand() * StaggerMinutes * 60.f) : 0;
				R.Key = Key;
				R.Day = 0;
				R.Unix = Now + Delay - (RespawnMode == EBamtolPickRespawn::Seconds ? int64(RespawnSeconds) : 0);
				bReady = Delay <= 0;
				if (!bReady) Sub->Put(R);
			}
			else bReady = true;
		}
	}
	if (GetNetMode() == NM_DedicatedServer) SetActorTickEnabled(false);
	else { BuildLook(); ApplyReady(false); }
}

void ABamtolPickable::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bRegistered) if (UBamtolPickableWorld* Sub = UBamtolPickableWorld::Get(GetWorld())) Sub->Unregister(this);
	bRegistered = false;
	Super::EndPlay(Reason);
}

bool ABamtolPickable::ReadyBy(const FBamtolPickableRow& R, int64 NowUnix, int32 TodayKey) const
{
	if (R.Unix - NowUnix > 86400LL) return true;   // 컴퓨터 시계를 되돌린 경우: 영영 빈 자리가 되지 않게
	const int64 At = R.Unix + (RespawnMode == EBamtolPickRespawn::Seconds ? int64(FMath::Max(RespawnSeconds, 0.f)) : 0);
	if (NowUnix < At) return false;
	if (RespawnMode == EBamtolPickRespawn::NextDay && R.Day != 0 && R.Day == TodayKey) return false;   // 같은 날은 안 자람
	return true;
}

bool ABamtolPickable::ServerCheckRegrow(int64 NowUnix, int32 TodayKey)
{
	UBamtolPickableWorld* Sub = UBamtolPickableWorld::Get(GetWorld());
	if (!HasAuthority() || bReady || !Sub) return false;
	FBamtolPickableRow R;
	if (Sub->Find(Key, R) && !ReadyBy(R, NowUnix, TodayKey)) return false;
	bReady = true;
	Sub->Erase(Key);   // 다시 자란 자리는 지움 (목록이 안 커짐)
	ForceNetUpdate();
	ApplyReady(true);
	return true;
}

bool ABamtolPickable::CanUse(const ABamtolPlayer* P) const
{
	if (!bReady || !P || P->IsCaught() || P->IsEscaped() || P->IsTumbling() || P->IsRiding() || P->IsCarrying() || P->IsCarried()) return false;
	return FVector::Dist2D(P->GetActorLocation(), GetActorLocation()) < 200.f && FMath::Abs(P->GetActorLocation().Z - GetActorLocation().Z) < 260.f;
}

FString ABamtolPickable::Prompt(const ABamtolPlayer* P) const
{
	return BamtolLoc::Fmt(TEXT("[F] %s 줍기"), *BamtolAnimals::ItemName(ItemId));
}

bool ABamtolPickable::Use(ABamtolPlayer* P)
{
	if (!HasAuthority() || !CanUse(P)) return false;
	ServerPick(P);
	return true;
}

bool ABamtolPickable::ServerPick(ABamtolPlayer* P, int32* OutGot)
{
	if (OutGot) *OutGot = 0;
	if (!HasAuthority() || !bReady || !P || !BamtolAnimals::IsItem(ItemId)) return false;
	UBamtolPouch* Pouch = UBamtolPouch::Ensure(P->GetPlayerState());
	if (!Pouch) return false;
	const int32 Want = BamtolGrowth::GatherAmount(this, FMath::Max(1, Amount) + (FMath::FRand() < BonusChance ? 1 : 0));   // 덤 1개 · 난이도 재료 양 // [growth-extra]
	const int32 Got = Pouch->Give(ItemId, Want);
	if (Got <= 0)
	{
		P->ClientShowToast(BTXT("주머니가 가득 찼어요"), 1.8f);
		return false;
	}
	if (OutGot) *OutGot = Got;
	bReady = false;
	if (UBamtolPickableWorld* Sub = UBamtolPickableWorld::Get(GetWorld()))
	{
		FBamtolPickableRow R;
		R.Key = Key;
		R.Day = BamtolDaily::TodayKey();
		R.Unix = PkNow();
		Sub->Put(R);
	}
	ForceNetUpdate();
	ApplyReady(true);
	P->ClientShowToast(BamtolLoc::Fmt(TEXT("%s +%d"), *BamtolAnimals::ItemName(ItemId), Got), 1.8f);
	return true;
}

void ABamtolPickable::OnRep_Ready() { ApplyReady(true); }
void ABamtolPickable::OnRep_Item() { BuildLook(); }

void ABamtolPickable::BuildLook()
{
	if (GetNetMode() == NM_DedicatedServer || ItemId.IsNone() || (Mesh && BuiltItem == ItemId)) return;
	const FBamtolForageDef* D = BamtolAnimals::Item(ItemId);
	UStaticMesh* M = D ? LoadObject<UStaticMesh>(nullptr, D->Mesh, nullptr, LOAD_NoWarn) : nullptr;
	if (!M) return;
	if (!Mesh)
	{
		Mesh = NewObject<UStaticMeshComponent>(this, TEXT("PickMesh"));
		Mesh->SetupAttachment(Pivot);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->bAffectDistanceFieldLighting = false;
		Mesh->RegisterComponent();
	}
	BuiltItem = ItemId;
	BamtolTower::FitMesh(Mesh, M, D->Size, true);
}

void ABamtolPickable::SetScale(float S)
{
	Pivot->SetRelativeScale3D(FVector(FMath::Max(S, 0.001f)));
}

// 준비됨 ↔ 비었음이 바뀔 때 모양·소리 (bAnimate 가 거짓이면 처음 상태 그대로 놓음)
void ABamtolPickable::ApplyReady(bool bAnimate)
{
	SetActorEnableCollision(bReady);
	if (GetNetMode() == NM_DedicatedServer) { bVisInit = true; bVisReady = bReady; return; }
	if (!bVisInit || !bAnimate)
	{
		bVisInit = true;
		bVisReady = bReady;
		AnimKind = 0;
		SetScale(bReady ? 1.f : 0.f);
		Pivot->SetVisibility(bReady, true);
		SetActorTickEnabled(true);
		return;
	}
	if (bReady == bVisReady) return;
	bVisReady = bReady;
	AnimKind = bReady ? 2 : 1;   // 1 쏙 작아짐 · 2 튀어 오르며 자람
	AnimT = 0.f;
	Pivot->SetVisibility(true, true);
	SetScale(bReady ? 0.f : 1.f);
	SetActorTickEnabled(true);
	SetActorTickInterval(0.f);
	if (!bReady)
	{
		ABamtolPuffFx::Spawn(GetWorld(), GetActorLocation(), 3, 28.f);
		PkSound(this, TEXT("Pick_Pop"), GetActorLocation(), 0.7f, 1.f + 0.15f * FMath::Sin(Phase));
	}
	else PkSound(this, TEXT("Pick_Regrow"), GetActorLocation(), 0.45f, 1.1f);
}

void ABamtolPickable::Tick(float Dt)
{
	Super::Tick(Dt);
	if (GetNetMode() == NM_DedicatedServer || !Pivot) return;
	Clock += Dt;
	if (AnimKind == 1)   // 쏙: 0.25초, 살짝 커졌다 쏙 작아짐
	{
		AnimT += Dt;
		const float U = AnimT / 0.25f;
		if (U >= 1.f) { AnimKind = 0; SetScale(0.f); Pivot->SetVisibility(false, true); }
		else SetScale((1.f - U * U) * (1.f + 0.2f * FMath::Sin(U * PI)));
		return;
	}
	if (AnimKind == 2)   // 0 → 1.1 → 1 튀어 오름 (0.45초)
	{
		AnimT += Dt;
		const float U = AnimT / 0.45f;
		if (U >= 1.f) { AnimKind = 0; SetScale(1.f); Pivot->SetRelativeRotation(FRotator::ZeroRotator); }
		else if (U < 0.6f) SetScale(1.1f * FMath::Sin(U / 0.6f * HALF_PI));
		else SetScale(1.1f - 0.1f * FMath::SmoothStep(0.f, 1.f, (U - 0.6f) / 0.4f));
		return;
	}
	if (!bVisReady)
	{
		SetActorTickInterval(1.f);
		if (Glint) Glint->SetVisibility(false);
		return;
	}
	// 준비됨: 가까울 때만 움직임(멀면 가끔만 확인) — 400개가 있어도 가볍게
	FVector Eye = GetActorLocation();
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PC->PlayerCameraManager) Eye = PC->PlayerCameraManager->GetCameraLocation();
		else if (PC->GetPawn()) Eye = PC->GetPawn()->GetActorLocation();
	}
	const float D2 = FVector::DistSquared(Eye, GetActorLocation());
	if (D2 > FMath::Square(3000.f))
	{
		SetActorTickInterval(0.5f);
		if (Glint) Glint->SetVisibility(false);
		return;
	}
	SetActorTickInterval(0.f);
	const float T = Clock;
	Pivot->SetRelativeRotation(FRotator(FMath::Cos(T * 1.3f + Phase) * 2.f, 0.f, FMath::Sin(T * 1.6f + Phase) * 2.5f));   // 숨쉬듯 살짝 흔들림
	SetScale(1.f + 0.025f * FMath::Sin(T * 2.2f + Phase));
	if (D2 > FMath::Square(800.f)) { if (Glint) Glint->SetVisibility(false); return; }
	if (!Glint)   // 근처 8m 에 처음 올 때만 만듦: 작은 점 하나 + 번지는 반짝 재질 (MI_Puff_Sparkle)
	{
		static UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		static UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Bamtol/FX/MI_Puff_Sparkle.MI_Puff_Sparkle"));
		if (!Sphere || !Mat) { SetActorTickInterval(2.f); return; }
		Glint = NewObject<UStaticMeshComponent>(this, TEXT("PickGlint"));
		Glint->SetStaticMesh(Sphere);
		Glint->SetMaterial(0, Mat);
		Glint->SetupAttachment(Pivot);
		Glint->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Glint->SetCanEverAffectNavigation(false);
		Glint->CastShadow = false;
		Glint->bAffectDistanceFieldLighting = false;
		Glint->RegisterComponent();
	}
	const float H = 26.f + 10.f * FMath::Sin(T * 2.f + Phase);
	Glint->SetVisibility(true);
	Glint->SetRelativeLocation(FVector(FMath::Cos(T * 1.2f + Phase) * 16.f, FMath::Sin(T * 1.2f + Phase) * 16.f, H));
	Glint->SetRelativeScale3D(FVector(0.05f + 0.06f * (0.5f + 0.5f * FMath::Sin(T * 5.f + Phase * 2.f))));   // 반짝반짝
}

// ============================ 뿌리개 ============================

ABamtolPickableField::ABamtolPickableField()
{
	bReplicates = false;   // 서버만 심고, 줍기 점이 복제됨
	PrimaryActorTick.bCanEverTick = false;
	Ring = CreateDefaultSubobject<UDrawSphereComponent>(TEXT("Ring"));
	RootComponent = Ring;
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->bDrawOnlyIfSelected = false;
	Ring->SetHiddenInGame(true);
	Ring->ShapeColor = FColor(120, 230, 90);
	Ring->SetSphereRadius(Radius);
}

void ABamtolPickableField::OnConstruction(const FTransform& T)
{
	Super::OnConstruction(T);
	if (Ring) Ring->SetSphereRadius(Radius);   // 에디터에서 반지름 보이게
}

void ABamtolPickableField::ComputePlan(TArray<BamtolPickable::FPlan>& Out) const
{
	const int32 Clusters = FMath::Max(1, FMath::RoundToInt(Count / 4.5f));
	BamtolPickable::PlanClusters(GetWorld(), GetActorLocation(), Radius, Count, Clusters, Kinds, Seed, Out);
}

int32 ABamtolPickableField::SpawnAll()
{
	UWorld* W = GetWorld();
	if (!W || GetNetMode() == NM_Client) return 0;
	TArray<BamtolPickable::FPlan> Plan;
	ComputePlan(Plan);
	for (const BamtolPickable::FPlan& P : Plan)
		if (ABamtolPickable* S = PkSpawn(W, P, RespawnMode, RespawnSeconds, BonusChance, StaggerMinutes, this)) Spawned.Add(S);
	UE_LOG(LogTemp, Display, TEXT("[Pickable] 뿌리개 %s: %d개 심음 (씨앗 %d)"), *GetName(), Spawned.Num(), Seed);   // loc:keep (개발자용)
	return Spawned.Num();
}

void ABamtolPickableField::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_Client) SpawnAll();   // 복제 안 하는 액터는 손님 화면에서도 HasAuthority 가 참 → 넷 모드로 가림
}
