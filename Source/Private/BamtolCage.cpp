#include "BamtolCage.h"

#include "BamtolAchievements.h"
#include "BamtolAudio.h"
#include "BamtolBoss.h"
#include "BamtolBossAI.h"
#include "BamtolChallenge.h"
#include "BamtolGameMode.h"
#include "BamtolGameState.h"
#include "BamtolHighlights.h"
#include "BamtolLoc.h"
#include "BamtolPlayer.h"
#include "BamtolPlayerState.h"
#include "BamtolQA.h"
#include "BamtolTuning.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	TAutoConsoleVariable<int32> CVarCageRule(TEXT("Bamtol.Cage"), 1, TEXT("새(추격) 구조 판: 1 = 잡히면 밤송이 우리에 갇힘 + 구출 + 3번째 탈락 · 0 = 예전처럼 바로 작은 밤톨 (전/후 비교)"));
	TAutoConsoleVariable<float> CVarCageStageSec(TEXT("Bamtol.CageStageSeconds"), 0.f, TEXT("우리 단계마다 초 덮어쓰기 (0 = 45초)"));
	TAutoConsoleVariable<int32> CVarCageMax(TEXT("Bamtol.CageMax"), 3, TEXT("이 번째 잡히면 바로 탈락 (기본 3)"));
	TAutoConsoleVariable<int32> CVarCageCount(TEXT("Bamtol.CageCount"), 0, TEXT("놓을 우리 수 덮어쓰기 (0 = 도망자 수로 6~8)"));
	TAutoConsoleVariable<int32> CVarCageTestOn(TEXT("Bamtol.CageTest"), 0, TEXT("시험: 1 = 내 몸을 일부러 잡히게 → 갇힘 → AI 구출 → ×3 → 탈락 (로그 [Cage])"));

	// 판 측정용 집계 (서버, 판마다)
	struct FCageTally
	{
		TWeakObjectPtr<const UWorld> World;
		int32 Caged = 0, Rescued = 0, Out = 0, Catches = 0;
	};
	FCageTally GCageTally;

	FCageTally& CageTallyFor(const UWorld* W)
	{
		if (GCageTally.World.Get() != W)
		{
			GCageTally = FCageTally();
			GCageTally.World = W;
		}
		return GCageTally;
	}
}

bool BamtolCage::On(const UWorld* W)
{
	const ABamtolGameState* GS = W ? W->GetGameState<ABamtolGameState>() : nullptr;
	return GS && GS->bCageRound;
}

bool BamtolCage::Setting() { return CVarCageRule.GetValueOnGameThread() != 0; }

bool BamtolCage::OutSpectate(const UWorld* W)
{
	const ABamtolGameState* GS = W ? W->GetGameState<ABamtolGameState>() : nullptr;
	return GS && GS->RuleCageSpectate();
}

float BamtolCage::StageSeconds() { return CVarCageStageSec.GetValueOnGameThread() > 0.f ? CVarCageStageSec.GetValueOnGameThread() : 45.f; }
int32 BamtolCage::MaxCatches()
{
	// 시험 cvar 가 기본(3)이 아니면 그것, 아니면 방 규칙 "잡히면"(1·2·3·5번째에 탈락 — Data/RoomRules.csv CatchLimit, 2026-10-12)
	const int32 Cv = FMath::Clamp(CVarCageMax.GetValueOnGameThread(), 1, 9);
	const UWorld* W = GWorld;
	return Cv != 3 || !W ? Cv : FMath::Clamp(BamtolRules::LiveValue(W, TEXT("CatchLimit")), 1, 9);
}
bool BamtolCage::Test() { return CVarCageTestOn.GetValueOnGameThread() > 0; }

int32 BamtolCage::CageCount(int32 Runners)
{
	return CVarCageCount.GetValueOnGameThread() > 0 ? CVarCageCount.GetValueOnGameThread() : FMath::Clamp(Runners, 6, 8);
}

void BamtolCage::Note(const UWorld* W, int32 Kind)
{
	FCageTally& T = CageTallyFor(W);
	(Kind == 0 ? T.Caged : Kind == 1 ? T.Rescued : Kind == 2 ? T.Out : T.Catches) += 1;
}

int32 BamtolCage::CatchCount(const UWorld* W)
{
	return CageTallyFor(W).Catches;
}

void BamtolCage::Counts(const UWorld* W, int32& OutCaged, int32& OutRescued, int32& OutOut)
{
	const FCageTally& T = CageTallyFor(W);
	OutCaged = T.Caged;
	OutRescued = T.Rescued;
	OutOut = T.Out;
}

void BamtolCage::ResetCounts(const UWorld* W)
{
	GCageTally = FCageTally();
	GCageTally.World = W;
}

// ============================ 우리 ============================

ABamtolCage::ABamtolCage()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;   // 개수가 적고, 모두 화면·미니맵에 자리를 봄
	// 복제 검사 초당 10번 (2026-10-23 성능, 예전 엔진 기본 100번): 갇힘 시계·문 여는 막대는 손님 화면에서 0.1초 단위로 충분.
	//   평소(빈 우리)엔 바뀌는 값이 없어 검사만 하던 것
	SetNetUpdateFrequency(10.f);

	// 몸: 원통 하나로 막음 (대장이 우리 안으로 못 들어감). 시야·카메라·총알은 안 막음
	Block = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Block"));
	Block->InitCapsuleSize(118.f, HalfHeight);
	Block->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Block->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Block->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Block->SetCanEverAffectNavigation(false);   // 작은 맵 내비는 구워 둔 것(바뀌지 않음) — 우리는 열린 곳에 놓고, 막히면 길 따라가기가 돌아감
	RootComponent = Block;

	auto MakeMesh = [this](const TCHAR* Name, const TCHAR* Path) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));
		ConstructorHelpers::FObjectFinder<UStaticMesh> M(Path);
		if (M.Succeeded())
		{
			C->SetStaticMesh(M.Object);
		}
		return C;
	};
	Body = MakeMesh(TEXT("Body"), TEXT("/Game/Bamtol/Props/Cage/SM_ChestnutCage.SM_ChestnutCage"));
	Door = MakeMesh(TEXT("Door"), TEXT("/Game/Bamtol/Props/Cage/SM_ChestnutCageDoor.SM_ChestnutCageDoor"));
	Ring = MakeMesh(TEXT("Ring"), TEXT("/Game/Bamtol/Props/Exits/SM_GlowRing.SM_GlowRing"));
	Ring->SetCastShadow(false);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(RootComponent);
	Glow->SetRelativeLocation(FVector(0.f, 0.f, 20.f));
	Glow->SetIntensity(0.f);
	Glow->SetAttenuationRadius(650.f);
	Glow->SetLightColor(FLinearColor(1.f, 0.5f, 0.15f));
	Glow->SetCastShadows(false);
}

void ABamtolCage::BeginPlay()
{
	Super::BeginPlay();
	if (!Body->GetStaticMesh())
	{
		BuildFallbackLook();
	}
	ApplyFreezeLook();
	DoorBase = Door->GetRelativeLocation();
	if (Ring->GetStaticMesh())
	{
		const FBoxSphereBounds B = Ring->GetStaticMesh()->GetBounds();
		const float K = 170.f / FMath::Max(B.BoxExtent.X, 1.f);
		Ring->SetRelativeScale3D(FVector(K, K, 1.f));
		Ring->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight + 4.f));
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UMaterialInterface* RM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Bamtol/FX/M_LaserBeam.M_LaserBeam"), nullptr, LOAD_NoWarn))
		{
			RingMID = Ring->CreateDynamicMaterialInstance(0, RM);
		}
	}
	Clock = FMath::FRand() * 10.f;
}

void ABamtolCage::BuildFallbackLook()
{
	// 모델을 못 읽으면(가져오기 전) 기본 원통으로라도 보이게
	if (UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), nullptr, LOAD_NoWarn))
	{
		Body->SetStaticMesh(Cyl);
		Body->SetRelativeScale3D(FVector(2.2f, 2.2f, 2.8f));
		Body->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight + 140.f));
	}
	UE_LOG(LogTemp, Warning, TEXT("[Cage] 우리 모델 없음 → 기본 원통 (Tools/UE/import_cage.py 를 돌릴 것)"));
}

void ABamtolCage::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABamtolCage, Prisoner);
	DOREPLIFETIME(ABamtolCage, Stage);
	DOREPLIFETIME(ABamtolCage, StageLeft);
	DOREPLIFETIME(ABamtolCage, OpenProgress);
	DOREPLIFETIME(ABamtolCage, OpenExtra);
	DOREPLIFETIME(ABamtolCage, Openers);
	DOREPLIFETIME(ABamtolCage, bCamped);
	DOREPLIFETIME(ABamtolCage, bFreezeCage);
	DOREPLIFETIME(ABamtolCage, bJail);
}

void ABamtolCage::OnRep_FreezeCage()
{
	ApplyFreezeLook();
}

void ABamtolCage::ApplyFreezeLook()
{
	// 얼음땡 (2026-10-13): 밤송이 우리 그대로이던 것 → 얼음으로 읽히는 모양. 굵은 얼음 덩어리 다섯(뒤는 크고 앞은 낮아 안의 녀석이 보임) + 몸을 감싼 옅은 얼음 껍질
	if (!bFreezeCage || bFreezeLook || GetNetMode() == NM_DedicatedServer || !Body || !Door)
	{
		return;
	}
	bFreezeLook = true;
	if (UStaticMesh* Ice = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Bamtol/Props/Festival/SM_IceCage.SM_IceCage"), nullptr, LOAD_NoWarn))
	{
		Body->SetStaticMesh(Ice);
		Body->EmptyOverrideMaterials();
		Body->SetRelativeScale3D(FVector(0.95f));
		Body->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));
		Body->SetCastShadow(false);   // 얼음은 빛이 비쳐 보이게 (녀석 위에 검은 그림자가 지지 않게)
		Door->SetVisibility(false);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cage] 얼음 모델 없음 → 밤송이 우리 모양 그대로 (Tools/Art/build_festival_props.py 뒤 가져오기)"));   // loc:keep (개발자용)
	}
	if (!IceShell)
	{
		UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"), nullptr, LOAD_NoWarn);
		UMaterialInterface* Sheen = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Bamtol/FX/M_LaserBeam.M_LaserBeam"), nullptr, LOAD_NoWarn);
		if (Ball && Sheen)
		{
			IceShell = NewObject<UStaticMeshComponent>(this);
			IceShell->SetupAttachment(RootComponent);
			IceShell->SetStaticMesh(Ball);
			IceShell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			IceShell->SetCastShadow(false);
			IceShell->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight + 78.f));
			IceShell->SetRelativeScale3D(FVector(1.55f, 1.55f, 1.75f));   // 녀석 몸(지름 약 70cm·키 1m)을 넉넉히 감쌈
			IceShell->RegisterComponent();
			IceShellMID = IceShell->CreateDynamicMaterialInstance(0, Sheen);
			if (IceShellMID)
			{
				IceShellMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.85f, 1.f));
				IceShellMID->SetScalarParameterValue(TEXT("Intensity"), 0.22f);
			}
		}
	}
	Glow->SetLightColor(FLinearColor(0.55f, 0.85f, 1.f));
}

float ABamtolCage::TotalLeft() const
{
	if (!Prisoner)
	{
		return 0.f;
	}
	const float Left = StageLeft + (Stage == 1 ? StageSeconds : 0.f);
	return LimitAt >= 0.f ? FMath::Min(Left, FMath::Max(0.f, LimitAt - GetWorld()->GetTimeSeconds())) : Left;
}

void ABamtolCage::SetTimeLimit(float Seconds)
{
	LimitAt = GetWorld()->GetTimeSeconds() + Seconds;
}

FVector ABamtolCage::InsideFeet() const
{
	return GetActorLocation() - FVector(0.f, 0.f, HalfHeight - 16.f);   // 받침 위
}

FVector ABamtolCage::DoorFront() const
{
	return GetActorLocation() - FVector(0.f, 0.f, HalfHeight) + GetActorForwardVector() * 200.f;   // 문(+X) 앞 2m, 땅 높이
}

bool ABamtolCage::IsOpenedBy(const ABamtolPlayer* P) const
{
	for (const TWeakObjectPtr<ABamtolPlayer>& H : Holders)
	{
		if (H.Get() == P)
		{
			return true;
		}
	}
	return false;
}

ABamtolCage* ABamtolCage::PickFor(UWorld* World, const FVector& CatchAt)
{
	ABamtolCage* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (TActorIterator<ABamtolCage> It(World); It; ++It)
	{
		if (It->IsOccupied() || It->bJail)
		{
			continue;   // 감옥 우리(JailFor)는 판 규칙 잡힘이 고르지 않음
		}
		const float D = FVector::Dist2D(It->GetActorLocation(), CatchAt);
		const float Score = D + (D < 1200.f ? 4000.f : 0.f);   // 대장(잡은 곳) 바로 옆 우리는 되도록 피함 → 대장이 걸어가야 지킴
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = *It;
		}
	}
	return Best;
}

ABamtolCage* ABamtolCage::FindNear(const UWorld* World, const FVector& At, float Extra)
{
	ABamtolCage* Best = nullptr;
	float BestD = TNumericLimits<float>::Max();
	for (TActorIterator<ABamtolCage> It(World); It; ++It)
	{
		const float D = FVector::Dist2D(It->GetActorLocation(), At);
		if (It->IsOccupied() && D < It->UseRadius + Extra && FMath::Abs(It->GetActorLocation().Z - HalfHeight - At.Z) < 350.f && D < BestD)
		{
			BestD = D;
			Best = *It;
		}
	}
	return Best;
}

void ABamtolCage::Lock(ABamtolPlayer* P, int32 InStage)
{
	if (!HasAuthority() || !P)
	{
		return;
	}
	Prisoner = P;
	ForceNetUpdate();   // 갇힘은 손님에게 바로 (평소 검사는 초당 10번)
	Stage = uint8(FMath::Clamp(InStage, 1, 2));
	StageSeconds = BamtolCage::StageSeconds();
	StageLeft = StageSeconds;
	Holders.Reset();
	Openers = 0;
	OpenProgress = 0.f;
	CampTime = 0.f;
	bCamped = false;
	LockedAt = GetWorld()->GetTimeSeconds();
	if (ABamtolPlayerState* PS = P->GetPlayerState<ABamtolPlayerState>())
	{
		PS->CageStage = Stage;
	}
	P->BeginCaged(this);
	MulticastFx(0);
	BamtolCage::Note(GetWorld(), 0);
	const FString Who = ABamtolPlayerState::NameOf(P);
	UE_LOG(LogTemp, Display, TEXT("[Cage] 갇힘: %s → %s (%d단계, %.0f초)"), *Who, *GetName(), Stage, TotalLeft());
	BamtolQA::Event(FString::Printf(TEXT("갇힘: %s (%d단계) %s"), *P->GetName(), Stage, *GetActorLocation().ToString()));   // loc:keep
	if (ABamtolGameState* GS = GetWorld()->GetGameState<ABamtolGameState>())
	{
		const ABamtolPlayerState* PS = P->GetPlayerState<ABamtolPlayerState>();
		GS->MulticastFeed(EBamtolFeed::Caught, bFreezeCage ? BamtolLoc::Fmt(TEXT("%s 꽁꽁 얼음! — [F] 꾹 2초로 녹여 줘요"), *Who) : Stage >= 2 ? BamtolLoc::Fmt(TEXT("%s 다시 갇힘! 2단계 — 이번엔 꼭 구해 줘요"), *Who) : BamtolLoc::Fmt(TEXT("%s 우리에 갇힘 — [F] 꾹 2초로 구출!"), *Who), PS ? PS->LookColor : -1);
	}
}

bool ABamtolCage::TryStartOpen(ABamtolPlayer* By)
{
	const ABamtolGameState* GS = GetWorld()->GetGameState<ABamtolGameState>();
	if (!HasAuthority() || !By || !Prisoner || By == Prisoner || By->IsCaught() || By->IsEscaped() || !GS || GS->Phase != EBamtolRoundPhase::Playing)
	{
		return false;
	}
	if (FVector::Dist2D(By->GetActorLocation(), GetActorLocation()) > UseRadius)
	{
		return false;
	}
	if (!IsOpenedBy(By))
	{
		Holders.Add(By);
		Openers = uint8(Holders.Num());
		UE_LOG(LogTemp, Display, TEXT("[Cage] %s 문 열기 시작 (%s 구하기, %.0f%%)"), *By->GetName(), *GetNameSafe(Prisoner), OpenProgress * 100.f);
	}
	return true;
}

void ABamtolCage::StopOpen(ABamtolPlayer* By)
{
	if (Holders.RemoveAll([By](const TWeakObjectPtr<ABamtolPlayer>& H) { return H.Get() == By; }) > 0)
	{
		Openers = uint8(Holders.Num());
		UE_LOG(LogTemp, Display, TEXT("[Cage] %s 손 뗌 (%.0f%%)"), *GetNameSafe(By), OpenProgress * 100.f);
	}
}

void ABamtolCage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		TickServer(DeltaSeconds);
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// ---- 보이는 것·소리 (각 화면) ----
	Clock += DeltaSeconds;
	OpenFlash = FMath::Max(0.f, OpenFlash - DeltaSeconds);
	const bool bOpening = Openers > 0 && (Prisoner || bJail);   // 감옥도 [F] 로 구하는 동안 문이 덜컹거림
	const float WantLift = OpenFlash > 0.f ? 1.f : bOpening ? 0.15f + 0.45f * OpenProgress : 0.f;
	DoorLift = FMath::FInterpTo(DoorLift, WantLift, DeltaSeconds, 8.f);
	const float Rattle = bOpening ? FMath::Sin(Clock * 40.f) * 3.f : 0.f;
	Door->SetRelativeLocation(DoorBase + FVector(0.f, Rattle, DoorLift * 150.f));
	if (bOpening)
	{
		RattleClock += DeltaSeconds;
		if (RattleClock >= 0.4f)
		{
			RattleClock = 0.f;
			UBamtolAudio::PlayAt(this, TEXT("Tick"), GetActorLocation(), 0.9f, 0.65f + OpenProgress * 0.4f);   // 덜컹덜컹 (시끄러움)
		}
	}
	// 빛: 비면 어둡게 · 1단계 주황 · 2단계 빨강 (시간이 적을수록 빨리 깜빡) · 대장 캠핑이면 푸르게(시계 느림)
	FLinearColor C(1.f, 0.55f, 0.15f);
	float I = 0.f, RingI = 0.15f;
	if (Prisoner)
	{
		const float Left = TotalLeft();
		const float Speed = Left < 15.f ? 9.f : Stage >= 2 ? 5.f : 2.5f;
		const float Pulse = 0.5f + 0.5f * FMath::Sin(Clock * Speed);
		C = bCamped ? FLinearColor(0.35f, 0.6f, 1.f) : Stage >= 2 ? FLinearColor(1.f, 0.12f, 0.05f) : FLinearColor(1.f, 0.55f, 0.12f);
		I = 2500.f + 4000.f * Pulse;
		RingI = 0.7f + 0.6f * Pulse;
	}
	if (bJail)
	{
		// 감옥: 시계·단계가 없으니 주황빛으로 천천히 숨 쉼
		const float Breath = 0.5f + 0.5f * FMath::Sin(Clock * 2.f);
		I = 2000.f + 1500.f * Breath;
		RingI = 0.6f + 0.4f * Breath;
	}
	if (bFreezeLook)
	{
		// 얼음땡: 시계가 없으니 급한 빨강 깜빡임 대신 하늘빛으로 천천히 숨 쉼 · 껍질은 갇힌 녀석이 있을 때만
		const float Breath = 0.5f + 0.5f * FMath::Sin(Clock * 1.8f);
		C = FLinearColor(0.55f, 0.85f, 1.f);
		I = Prisoner ? 1800.f + 1200.f * Breath : 0.f;
		RingI = Prisoner ? 0.5f + 0.3f * Breath : 0.15f;
		if (IceShell)
		{
			IceShell->SetVisibility(Prisoner != nullptr);
		}
		if (IceShellMID)
		{
			IceShellMID->SetScalarParameterValue(TEXT("Intensity"), 0.18f + 0.08f * Breath + (bOpening ? 0.25f * OpenProgress : 0.f));   // 풀어 주는 중이면 점점 밝아짐
		}
	}
	Glow->SetLightColor(C);
	Glow->SetIntensity(I);
	Ring->SetVisibility(Prisoner != nullptr);
	if (RingMID)
	{
		RingMID->SetVectorParameterValue(TEXT("Color"), C);
		RingMID->SetScalarParameterValue(TEXT("Intensity"), RingI);
	}
}

void ABamtolCage::TickServer(float DeltaSeconds)
{
	if (bJail)
	{
		TickJail(DeltaSeconds);   // 감옥은 판 규칙(시계·단계·탈락)이 없음 — [F] 구출만 셈
		return;
	}
	const ABamtolGameState* GS = GetWorld()->GetGameState<ABamtolGameState>();
	if (!Prisoner)
	{
		Holders.Reset();
		Openers = 0;
		OpenProgress = 0.f;
		return;
	}
	if (!IsValid(Prisoner) || !Prisoner->IsCaged())
	{
		Empty();   // 나감·사라짐
		return;
	}
	if (!GS || GS->Phase != EBamtolRoundPhase::Playing)
	{
		Holders.Reset();
		Openers = 0;
		return;   // 판 끝: 시계 멈춤 (갇힌 채 = 0점)
	}
	if (bFreezeCage)
	{
		StageLeft = StageSeconds;   // 방 규칙 "얼음땡": 시계 없음 — 친구가 풀어 줄 때까지 얼어 있음
	}
	const FVector Me = GetActorLocation();
	// 반캠핑: 큰 대장이 8m 안에 오래 있으면 시계가 느려짐
	bool bBossNear = false;
	for (TActorIterator<ABamtolBoss> It(GetWorld()); It; ++It)
	{
		if (!It->bSmallForm && !It->IsWaitingToStart() && FVector::Dist2D(It->GetActorLocation(), Me) < CampRadius && FMath::Abs(It->GetActorLocation().Z - Me.Z) < 500.f)
		{
			bBossNear = true;
			break;
		}
	}
	CampTime = bBossNear ? CampTime + DeltaSeconds : FMath::Max(0.f, CampTime - DeltaSeconds * 2.f);
	const bool bWasCamped = bCamped;
	bCamped = CampTime > CampGrace;
	if (bCamped != bWasCamped)
	{
		UE_LOG(LogTemp, Display, TEXT("[Cage] %s 대장 캠핑 %s (시계 ×%.2f)"), *GetName(), bCamped ? TEXT("시작") : TEXT("끝"), bCamped ? CampSlow : 1.f);
	}
	// 여는 녀석 정리: 멀어짐 · 잡힘 · 나뒹굴기 · 탈것
	Holders.RemoveAll([this, &Me](const TWeakObjectPtr<ABamtolPlayer>& H)
	{
		const ABamtolPlayer* P = H.Get();
		return !P || P->IsCaught() || P->IsEscaped() || P->IsTumbling() || P->IsRiding()
			|| FVector::Dist2D(P->GetActorLocation(), Me) > UseRadius + 120.f;
	});
	Openers = uint8(Holders.Num());
	// 시계: 여는 중엔 멈춤, 캠핑이면 느리게
	const float Rate = Openers > 0 ? 0.f : bCamped ? CampSlow : 1.f;
	StageLeft -= DeltaSeconds * Rate;
	if (StageLeft <= 0.f)
	{
		if (Stage <= 1)
		{
			Stage = 2;
			StageLeft = StageSeconds;
			if (ABamtolPlayerState* PS = Prisoner->GetPlayerState<ABamtolPlayerState>())
			{
				PS->CageStage = 2;
			}
			MulticastFx(1);
			UE_LOG(LogTemp, Display, TEXT("[Cage] %s 2단계 (%s, %.0f초 남음)"), *GetName(), *GetNameSafe(Prisoner), StageLeft);
			if (ABamtolGameState* WGS = GetWorld()->GetGameState<ABamtolGameState>())
			{
				WGS->MulticastFeed(EBamtolFeed::Info, BamtolLoc::Fmt(TEXT("%s 위험! 우리 2단계 — %.0f초 뒤 탈락"), *ABamtolPlayerState::NameOf(Prisoner), StageLeft), -1);
			}
		}
		else
		{
			Eliminate();
			return;
		}
	}
	if (Openers > 0)
	{
		OpenProgress = FMath::Min(1.f, OpenProgress + DeltaSeconds / FMath::Max(OpenSeconds, 0.3f));
		// 여는 소리: AI 대장이 듣고 옴 (구하러 온 녀석이 추격당하게)
		NoiseClock += DeltaSeconds;
		if (NoiseClock >= 1.f)
		{
			NoiseClock = 0.f;
			ABamtolBossAIController::ReportNoise(GetWorld(), Me, 40000.f);
		}
		if (OpenProgress >= 1.f)
		{
			ABamtolPlayer* By = nullptr;
			for (const TWeakObjectPtr<ABamtolPlayer>& H : Holders)
			{
				if (H.IsValid())
				{
					By = H.Get();
					break;
				}
			}
			Release(By);
		}
	}
	else
	{
		OpenProgress = FMath::Max(0.f, OpenProgress - DeltaSeconds * 0.5f);   // 손 떼면 천천히 다시 잠김
		NoiseClock = 0.9f;   // 다시 잡으면 바로 소리
	}
}

void ABamtolCage::Empty()
{
	LimitAt = -1.f;
	if (Prisoner)
	{
		if (ABamtolPlayerState* PS = Prisoner->GetPlayerState<ABamtolPlayerState>())
		{
			PS->CageStage = 0;
		}
	}
	Prisoner = nullptr;
	Stage = 0;
	StageLeft = 0.f;
	Holders.Reset();
	Openers = 0;
	OpenProgress = 0.f;
	OpenSeconds -= OpenExtra;   // 제비뽑기 꽝으로 늘어난 만큼 되돌림
	OpenExtra = 0.f;
	CampTime = 0.f;
	bCamped = false;
	if (bFreezeCage && HasAuthority())
	{
		SetLifeSpan(0.8f);   // 얼음땡 얼음: 풀리면 사라짐
	}
}

void ABamtolCage::ClearForEnd()
{
	Holders.Reset();
	Openers = 0;
}

bool ABamtolCage::SelfFree()
{
	// 제비뽑기 당첨: 구한 사람 없이 문이 열림 (기록·명장면은 승부 순간 쪽이 남김)
	ABamtolPlayer* P = Prisoner;
	if (!HasAuthority() || !P)
	{
		return false;
	}
	Empty();
	MulticastFx(2);
	P->EndCaged(DoorFront(), GetActorRotation().Yaw, FreeSafeSeconds);
	P->AddSpeedBoost(FreeSpeedBonus, FreeSpeedSeconds);
	BamtolCage::Note(GetWorld(), 1);
	UE_LOG(LogTemp, Display, TEXT("[Cage] 제비뽑기 당첨: %s 스스로 나옴"), *P->GetName());
	return true;
}

ABamtolPlayer* ABamtolCage::TakeOutQuiet()
{
	ABamtolPlayer* P = Prisoner;
	if (!HasAuthority() || !P)
	{
		return nullptr;
	}
	Empty();
	P->EndCaged(DoorFront(), GetActorRotation().Yaw, 0.f, false);
	return P;
}

bool ABamtolCage::RibbonOpen(ABamtolPlayer* By)
{
	if (!HasAuthority() || !Prisoner || Prisoner == By)
	{
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("[Cage] 구출 리본: %s 가 %s 의 우리를 엶 (%.0fm)"), *GetNameSafe(By), *Prisoner->GetName(), By ? FVector::Dist(By->GetActorLocation(), GetActorLocation()) / 100.f : 0.f);
	Release(By);
	return true;
}

void ABamtolCage::AddOpenSeconds(float Extra)
{
	if (HasAuthority() && Prisoner && Extra > 0.f)
	{
		OpenSeconds += Extra;
		OpenExtra += Extra;
		UE_LOG(LogTemp, Display, TEXT("[Cage] 제비뽑기 꽝: %s 구출 %.1f초로 늘어남"), *Prisoner->GetName(), OpenSeconds);
	}
}

void ABamtolCage::Release(ABamtolPlayer* By)
{
	ABamtolPlayer* P = Prisoner;
	const float LeftWas = TotalLeft();
	const float CagedFor = float(GetWorld()->GetTimeSeconds() - LockedAt);
	Empty();
	if (!P)
	{
		return;
	}
	MulticastFx(2);
	const FVector Out = DoorFront();
	P->EndCaged(Out, GetActorRotation().Yaw, FreeSafeSeconds);
	P->AddSpeedBoost(FreeSpeedBonus, FreeSpeedSeconds);
	BamtolCage::Note(GetWorld(), 1);
	// 대장이 가까운가 (우리 앞 추격) · 시간이 거의 다 됐나 (아슬아슬)
	float BossD = TNumericLimits<float>::Max();
	for (TActorIterator<ABamtolBoss> It(GetWorld()); It; ++It)
	{
		if (!It->bSmallForm)
		{
			BossD = FMath::Min(BossD, (float)FVector::Dist(It->GetActorLocation(), GetActorLocation()));
		}
	}
	const FString Who = ABamtolPlayerState::NameOf(P);
	const FString Hero = ABamtolPlayerState::NameOf(By);
	ABamtolPlayerState* HeroPS = By ? By->GetPlayerState<ABamtolPlayerState>() : nullptr;
	if (HeroPS)
	{
		++HeroPS->Rescues;
	}
	if (By)
	{
		BamtolChal::Report(By, TEXT("Rescues"), 1);
		BamtolAch::Grant(static_cast<const APawn*>(By), BamtolAch::Rescue);
		if (ABamtolPlayer* PetHero = Cast<ABamtolPlayer>(By))
		{
			PetHero->PetReact(static_cast<uint8>(EBamtolPetReact::Rescue));   // 구한 녀석의 펫이 기뻐함 (펫 없으면 아무것도 안 함)
		}
	}
	if (ABamtolGameState* GS = GetWorld()->GetGameState<ABamtolGameState>())
	{
		GS->MulticastFeed(EBamtolFeed::Rescue, bFreezeCage ? BamtolLoc::Fmt(TEXT("%s → %s 얼음에서 땡!"), *Hero, *Who) : BamtolLoc::Fmt(TEXT("%s → %s 우리에서 구출!"), *Hero, *Who), HeroPS ? HeroPS->LookColor : -1);
	}
	// 명장면: 아슬아슬 구출(남은 시간 10초 안) · 대장 코앞 구출(15m 안) · 그냥 구출
	const FBamtolHighlightScores& HS = UBamtolTuning::Get().HighlightScores;
	if (BossD < HS.CageChaseDistance)
	{
		UBamtolHighlights::Mark(this, HS.CageChaseRescue, BamtolLoc::Fmt(TEXT("대장 코앞에서! %s 가 %s 구출"), *Hero, *Who), P->GetActorLocation(), By);
	}
	else if (LeftWas < HS.CloseRescueSeconds)
	{
		UBamtolHighlights::Mark(this, HS.CloseRescue, BamtolLoc::Fmt(TEXT("아슬아슬! %s 가 %s 구출 (%.0f초 남기고)"), *Hero, *Who, LeftWas), P->GetActorLocation(), By);
	}
	else
	{
		UBamtolHighlights::Mark(this, HS.Rescue, BamtolLoc::Fmt(TEXT("%s 가 %s 구출!"), *Hero, *Who), P->GetActorLocation(), By);
	}
	UBamtolHighlights::NoteRescue(this, P->GetPlayerState());
	BamtolQA::Event(FString::Printf(TEXT("구출: %s by %s (갇힌 %.0f초, 남은 %.0f초, 대장 %.0fm)"), *P->GetName(), *GetNameSafe(By), CagedFor, LeftWas, BossD / 100.f));   // loc:keep
	UE_LOG(LogTemp, Display, TEXT("[Cage] 구출: %s by %s (갇힌 %.0f초, 남은 %.0f초, 대장 %.0fm)"), *P->GetName(), *GetNameSafe(By), CagedFor, LeftWas, BossD < 1e30f ? BossD / 100.f : -1.f);
}

void ABamtolCage::Eliminate()
{
	ABamtolPlayer* P = Prisoner;
	Empty();
	if (!P)
	{
		return;
	}
	MulticastFx(3);
	UE_LOG(LogTemp, Display, TEXT("[Cage] 시간 다 됨 → 탈락: %s"), *P->GetName());
	if (ABamtolGameMode* GM = GetWorld()->GetAuthGameMode<ABamtolGameMode>())
	{
		GM->CageOut(P, BTXT("우리 시간 다 됨"));
	}
}

void ABamtolCage::MulticastFx_Implementation(uint8 Kind)
{
	const FVector At = GetActorLocation() + FVector(0, 0, 60.f);
	switch (Kind)
	{
	case 0: UBamtolAudio::PlayAt(this, TEXT("Crack"), At, 1.f, 0.7f); UBamtolAudio::PlayAt(this, TEXT("Thud"), At, 1.f, 0.6f); break;     // 쾅 갇힘
	case 1: UBamtolAudio::PlayAt(this, TEXT("Bell"), At, 1.f, 0.6f); break;                                                               // 2단계: 낮은 종
	case 2: UBamtolAudio::PlayAt(this, TEXT("Chime"), At, 1.f, 1.1f); UBamtolAudio::PlayAt(this, TEXT("Pop"), At, 1.f); OpenFlash = 1.2f; break;   // 문 열림
	case 3: UBamtolAudio::PlayAt(this, TEXT("Warp"), At, 1.f, 0.8f); break;                                                               // 탈락
	default: break;
	}
}

// ============================ 시험 (Bamtol.CageTest=1) ============================
//   AI 친구 하나(주인공)를 일부러 잡히게 → 갇힘 → 다른 AI 친구를 우리 앞에 데려다 놓고 구하는지 봄 (6초 안에 안 열면 직접 [F])
//   → 5초 뒤(못 잡히는 시간) 다시 잡힘 = 2단계부터 → 또 구출 → 3번째 잡힘 = 탈락 → "[Cage] 시험 통과" → 화면용으로 한 명 더 가둠
void BamtolCage::TickTest(ABamtolGameMode* GM, float Dt)
{
	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		float T = 0.f;
		float StepAt = 0.f;
		float ForceAt = 0.f;
		int32 Done = 0;   // 0 진행 · 1 통과 줄 씀 · 2 화면용 가둠 끝
		TWeakObjectPtr<ABamtolPlayer> Subject;
		TWeakObjectPtr<ABamtolPlayer> Helper;
		TWeakObjectPtr<ABamtolCage> HelpedAt;
		bool bOutSeen = false;
	};
	static FState S;
	UWorld* W = GM ? GM->GetWorld() : nullptr;
	if (!W)
	{
		return;
	}
	if (S.World.Get() != W)
	{
		S = FState();
		S.World = W;
		S.StepAt = 3.f;
	}
	S.T += Dt;
	TArray<ABamtolPlayer*> Bots;
	for (TActorIterator<ABamtolPlayer> It(W); It; ++It)
	{
		if (!It->IsPlayerControlled() && !It->IsCaught() && !It->IsEscaped())
		{
			Bots.Add(*It);
		}
	}
	APlayerController* PC = W->GetFirstPlayerController();
	if (PC && S.Subject.IsValid() && PC->GetViewTarget() != S.Subject.Get())
	{
		PC->SetViewTarget(S.Subject.Get());   // 카메라: 주인공을 따라 봄 (화면 확인)
	}
	if (S.T < S.StepAt)
	{
		return;
	}
	if (S.Done >= 2)
	{
		return;
	}
	if (!S.Subject.IsValid())
	{
		if (S.Done == 0 && S.bOutSeen)
		{
			S.Done = 1;
		}
		if (S.Done == 1)
		{
			// 화면용: 한 명 더 가둬 두고 그 녀석을 봄 (우리·남은 시간·미니맵 표시 확인)
			if (Bots.Num() > 0)
			{
				S.Subject = Bots[0];
				GM->CatchRunner(Bots[0], nullptr);
				UE_LOG(LogTemp, Display, TEXT("[Cage] 시험: 화면용으로 %s 가둠 (%s)"), *Bots[0]->GetName(), Bots[0]->IsCaged() ? TEXT("갇힘") : TEXT("안 갇힘"));
			}
			S.Done = 2;
			return;
		}
		if (Bots.Num() < 2)
		{
			return;
		}
		S.Subject = Bots[0];
		UE_LOG(LogTemp, Display, TEXT("[Cage] 시험 시작: 주인공 %s"), *Bots[0]->GetName());
	}
	ABamtolPlayer* Sub = S.Subject.Get();
	ABamtolPlayerState* PS = Sub->GetPlayerState<ABamtolPlayerState>();
	if (PS && PS->bCageOut)
	{
		if (!S.bOutSeen)
		{
			S.bOutSeen = true;
			int32 Caged = 0, Rescued = 0, Out = 0;
			Counts(W, Caged, Rescued, Out);
			const bool bPass = Caged >= 2 && Rescued >= 2 && Out >= 1;
			UE_LOG(LogTemp, Display, TEXT("[Cage] 시험 %s: 갇힘 %d · 구출 %d · 탈락 %d (주인공 잡힌 횟수 %d)"), bPass ? TEXT("통과") : TEXT("실패"), Caged, Rescued, Out, PS->CageHooks);
			S.StepAt = S.T + 3.f;
		}
		if (S.T >= S.StepAt)
		{
			S.Subject.Reset();   // 작은 밤톨이 됨 → 다음 단계(화면용)
			S.Done = 1;
		}
		return;
	}
	if (Sub->IsCaged())
	{
		ABamtolCage* Cage = Sub->GetCage();
		// 갇힘: 다른 AI 친구를 우리 앞에 데려다 놓음 (AI 가 스스로 구하는지) — 6초 안에 안 열면 직접 [F]
		if (S.HelpedAt.Get() != Cage)
		{
			S.HelpedAt = Cage;
			UE_LOG(LogTemp, Display, TEXT("[Cage] 시험: %d번째 잡힘 → %d단계부터 (남은 %.0f초)"), PS ? PS->CageHooks : -1, Cage->GetStage(), Cage->TotalLeft());
			ABamtolPlayer* Help = nullptr;
			for (ABamtolPlayer* B : Bots)
			{
				if (B != Sub)
				{
					Help = B;
					break;
				}
			}
			S.Helper = Help;
			if (Help)
			{
				const FVector At = Cage->DoorFront() + Cage->GetActorForwardVector() * 300.f;
				Help->SetActorLocation(At + FVector(0.f, 0.f, Help->GetSimpleCollisionHalfHeight() + 40.f), false, nullptr, ETeleportType::TeleportPhysics);
				UE_LOG(LogTemp, Display, TEXT("[Cage] 시험: %s 를 우리 앞 5m 로 (AI 가 구하나)"), *Help->GetName());
			}
			S.ForceAt = S.T + 8.f;
		}
		if (S.T >= S.ForceAt && S.Helper.IsValid() && !S.Helper->IsCaught())
		{
			ABamtolPlayer* Help = S.Helper.Get();
			Help->SetActorLocation(Cage->DoorFront() + FVector(0.f, 0.f, Help->GetSimpleCollisionHalfHeight() + 40.f), false, nullptr, ETeleportType::TeleportPhysics);
			Cage->TryStartOpen(Help);
			S.ForceAt = S.T + 1.f;
			UE_LOG(LogTemp, Display, TEXT("[Cage] 시험: AI 가 안 열어서 직접 [F] (%s)"), *Help->GetName());
		}
		return;
	}
	// 풀려나 있음: 못 잡히는 시간이 지나면 다시 잡힘
	S.HelpedAt.Reset();
	if (!Sub->IsCaught() && !Sub->IsCageSafe())
	{
		GM->CatchRunner(Sub, nullptr);
		S.StepAt = S.T + 1.f;
	}
}

// ============================ 감옥 (JailFor — 판 끝 재판소) ============================
//   P 발밑에 우리를 세움 → P 를 우리 가운데로 옮기고 움직임을 끔 → 0.25초마다 다시 끔(넉백·착지가 걷기로 되돌려도) → Seconds 뒤 걷기로 되돌리고 우리를 지움.
//   P 가 먼저 사라지면 다음 다시 묶기에서 바로 우리를 지움 (끝내 남는 우리 없음).
ABamtolCage* ABamtolCage::JailFor(ABamtolPlayer* P, float Seconds)
{
	UWorld* W = P ? P->GetWorld() : nullptr;
	if (!W || !P->HasAuthority())
	{
		UE_LOG(LogTemp, Error, TEXT("[Cage] 감옥 못 함: %s"), !P ? TEXT("가둘 녀석 없음") : TEXT("서버가 아님"));
		return nullptr;
	}
	Seconds = FMath::Max(0.1f, Seconds);
	for (TActorIterator<ABamtolCage> It(W); It; ++It)
	{
		if (It->bJail && It->Jailed.Get() == P)
		{
			W->GetTimerManager().SetTimer(It->JailEndTimer, *It, &ABamtolCage::JailEnd, Seconds, false);   // 이미 감옥 → 시간만 다시
			UE_LOG(LogTemp, Display, TEXT("[Cage] 감옥 시간 다시: %s %.0f초"), *P->GetName(), Seconds);
			return *It;
		}
	}
	FVector Ground = P->GetActorLocation() - FVector(0.f, 0.f, P->GetSimpleCollisionHalfHeight());
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(BamtolJail), false, P);
	if (W->LineTraceSingleByChannel(Hit, P->GetActorLocation(), P->GetActorLocation() - FVector(0.f, 0.f, 3000.f), ECC_WorldStatic, Q))
	{
		Ground = Hit.ImpactPoint;
	}
	const FTransform T(FRotator(0.f, P->GetActorRotation().Yaw, 0.f), Ground + FVector(0.f, 0.f, HalfHeight));
	ABamtolCage* Jail = W->SpawnActorDeferred<ABamtolCage>(ABamtolCage::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Jail)
	{
		UE_LOG(LogTemp, Error, TEXT("[Cage] 감옥 못 함: 우리를 못 세움 (%s)"), *P->GetName());
		return nullptr;
	}
	Jail->bJail = true;
	Jail->Jailed = P;
	Jail->FinishSpawning(T);
	if (P->IsCarrying())
	{
		P->ReleaseCarried(false);   // 들고 있던 친구는 내려놓음
	}
	P->SetActorLocation(Ground + FVector(0.f, 0.f, P->GetSimpleCollisionHalfHeight() + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
	Jail->JailHold();
	W->GetTimerManager().SetTimer(Jail->JailHoldTimer, Jail, &ABamtolCage::JailHold, 0.25f, true);
	W->GetTimerManager().SetTimer(Jail->JailEndTimer, Jail, &ABamtolCage::JailEnd, Seconds, false);
	UE_LOG(LogTemp, Display, TEXT("[Cage] 감옥: %s %.0f초 (%s)"), *P->GetName(), Seconds, *Ground.ToString());
	return Jail;
}

void ABamtolCage::JailHold()
{
	ABamtolPlayer* P = Jailed.Get();
	if (!P)
	{
		JailEnd();   // 갇힌 녀석이 나감·사라짐 → 우리도 치움
		return;
	}
	UCharacterMovementComponent* Move = P->GetCharacterMovement();
	if (Move && Move->MovementMode != MOVE_None)
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
}

void ABamtolCage::JailEnd()
{
	GetWorldTimerManager().ClearTimer(JailHoldTimer);
	GetWorldTimerManager().ClearTimer(JailEndTimer);
	if (ABamtolPlayer* P = Jailed.Get())
	{
		if (UCharacterMovementComponent* Move = P->GetCharacterMovement(); Move && !P->IsCaught())
		{
			Move->SetMovementMode(MOVE_Falling);   // 땅에 닿으면 걷기로 (공중에 서 있지 않게)
		}
		UE_LOG(LogTemp, Display, TEXT("[Cage] 감옥 끝: %s 풀림"), *P->GetName());
	}
	Jailed.Reset();
	Destroy();
}

// 감옥 구출 (2026-10-27): 다른 사람이 감옥 앞에서 [F] 꾹 → OpenSeconds 초 차면 바로 풀림 + 피드 한 줄
//   판 우리의 구출(Release)과 따로: 빨라짐·못 잡힘·도전 기록·명장면 없음 (마을 감옥은 판이 아님)
ABamtolCage* ABamtolCage::FindJailNear(const UWorld* World, const FVector& At)
{
	for (TActorIterator<ABamtolCage> It(World); It; ++It)
	{
		if (It->bJail && It->Jailed.IsValid() && FVector::Dist2D(It->GetActorLocation(), At) < It->UseRadius
			&& FMath::Abs(It->GetActorLocation().Z - HalfHeight - At.Z) < 350.f)
		{
			return *It;
		}
	}
	return nullptr;
}

bool ABamtolCage::TryStartJailRescue(ABamtolPlayer* By)
{
	const ABamtolPlayer* P = Jailed.Get();
	if (!HasAuthority() || !bJail || !P || !By || By == P || By->IsCaught() || By->IsEscaped()
		|| FVector::Dist2D(By->GetActorLocation(), GetActorLocation()) > UseRadius)
	{
		return false;
	}
	if (!IsOpenedBy(By))
	{
		Holders.Add(By);
		Openers = uint8(Holders.Num());
		UE_LOG(LogTemp, Display, TEXT("[Cage] %s 감옥 문 열기 시작 (%s 구하기, %.0f%%)"), *By->GetName(), *P->GetName(), OpenProgress * 100.f);
	}
	return true;
}

void ABamtolCage::TickJail(float DeltaSeconds)
{
	const ABamtolPlayer* P = Jailed.Get();
	const FVector Me = GetActorLocation();
	// 여는 녀석 정리: 멀어짐 · 잡힘 · 나뒹굴기 · 탈것 · 갇힌 녀석 자신
	Holders.RemoveAll([this, P, &Me](const TWeakObjectPtr<ABamtolPlayer>& H)
	{
		const ABamtolPlayer* O = H.Get();
		return !O || O == P || O->IsCaught() || O->IsEscaped() || O->IsTumbling() || O->IsRiding()
			|| FVector::Dist2D(O->GetActorLocation(), Me) > UseRadius + 120.f;
	});
	Openers = uint8(Holders.Num());
	if (!P || Openers == 0)
	{
		OpenProgress = FMath::Max(0.f, OpenProgress - DeltaSeconds * 0.5f);   // 손 떼면 천천히 다시 잠김
		return;
	}
	OpenProgress = FMath::Min(1.f, OpenProgress + DeltaSeconds / FMath::Max(OpenSeconds, 0.3f));
	if (OpenProgress < 1.f)
	{
		return;
	}
	ABamtolPlayer* By = Holders[0].Get();
	const FString Who = ABamtolPlayerState::NameOf(P);
	const FString Hero = ABamtolPlayerState::NameOf(By);
	UE_LOG(LogTemp, Display, TEXT("[Cage] 감옥 구출: %s → %s"), *GetNameSafe(By), *P->GetName());
	BamtolQA::Event(FString::Printf(TEXT("감옥 구출: %s → %s"), *GetNameSafe(By), *P->GetName()));   // loc:keep
	MulticastFx(2);
	if (ABamtolGameState* GS = GetWorld()->GetGameState<ABamtolGameState>())
	{
		const ABamtolPlayerState* HeroPS = By ? By->GetPlayerState<ABamtolPlayerState>() : nullptr;
		GS->MulticastFeed(EBamtolFeed::Rescue, BamtolLoc::Fmt(TEXT("%s → %s 감옥에서 구출!"), *Hero, *Who), HeroPS ? HeroPS->LookColor : -1);
	}
	JailEnd();
}
