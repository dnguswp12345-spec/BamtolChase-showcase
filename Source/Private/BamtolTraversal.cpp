#include "BamtolTraversal.h"
#include "BamtolReactions.h"
#include "LandscapeProxy.h"

#include "BamtolBoss.h"
#include "BamtolGround.h"
#include "BamtolPlayer.h"
#include "BamtolBus.h"
#include "BamtolCannon.h"
#include "BamtolGameplayProps.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
	UStaticMeshComponent* MakePart(AActor* Owner, UStaticMesh* M, USceneComponent* Parent, bool bShadow = true)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
		C->SetStaticMesh(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->CastShadow = bShadow;
		C->SetupAttachment(Parent);
		C->RegisterComponent();
		C->SetComponentTickEnabled(false);   // 성능: 그림만 (미끄럼틀 조각 수백 개가 매 프레임 틱하던 것)
		return C;
	}
}

// ============================ 바람 길 ============================

ABamtolWindZone::ABamtolWindZone()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetUpdateFrequency(1.f);   // 바람 칸 자체는 복제할 값이 없음 — 검사 초당 1번. 돌풍 띠(GustBand)는 단계가 바뀔 때 ForceNetUpdate 로 바로 보냄 (2026-10-26 성능, 예전 100번)
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	RootComponent = Box;
}

void ABamtolWindZone::BeginPlay()
{
	Super::BeginPlay();
	Box->SetBoxExtent(Size * 0.5f);
	// 바람 줄기: 상자 안을 앞으로 흘러가는 하얀 리본
	UStaticMesh* SM = StreakMesh.LoadSynchronous();
	UMaterialInterface* Mat = StreakMaterial.LoadSynchronous();
	FRandomStream R(GetUniqueID());
	for (int32 i = 0; SM && i < StreakCount; ++i)
	{
		UStaticMeshComponent* C = MakePart(this, SM, RootComponent, false);
		if (Mat)
		{
			C->SetMaterial(0, Mat);
		}
		C->SetRelativeRotation(FRotator(0.f, 0.f, R.FRandRange(30.f, 150.f)));   // 리본을 세워 옆·뒤에서도 보이게
		Streaks.Add(C);
		StreakRot.Add(C->GetRelativeRotation().Quaternion());
		StreakSeed.Add(FVector(R.FRand(), R.FRandRange(-0.45f, 0.45f), R.FRandRange(0.1f, 0.75f)));
	}
	// 들어가는 쪽 양옆 표지판
	if (UStaticMesh* Sign = SignMesh.LoadSynchronous())
	{
		for (const float Side : { -1.f, 1.f })
		{
			UStaticMeshComponent* C = MakePart(this, Sign, RootComponent);
			C->SetRelativeLocation(FVector(-Size.X * 0.5f, Side * Size.Y * 0.55f, -Size.Z * 0.5f));
			Signs.Add(C);
		}
	}
}

void ABamtolWindZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 1) 밀기: 서버와 조종하는 사람 화면이 같은 힘을 줌 (이동 예측이 어긋나지 않게)
	const FVector Dir = GetActorForwardVector().GetSafeNormal2D();
	// 상자 안의 캐릭터 (겹침 기능 대신 위치로 직접 — 사람은 많아야 8명 + 밤톨)
	TArray<ACharacter*> In;
	const FTransform& XF = GetActorTransform();
	// 성능: 0.25초마다 상자 근처 캐릭터만 후보로 (매 프레임 모두를 변환하던 것)
	CandidateClock -= DeltaSeconds;
	if (CandidateClock <= 0.f)
	{
		CandidateClock = 0.25f;
		Candidates.Reset();
		const float R = Size.Size() * 0.5f + 1500.f;
		for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
		{
			if (FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < R * R)
			{
				Candidates.Add(*It);
			}
		}
	}
	for (const TWeakObjectPtr<ACharacter>& WC : Candidates)
	{
		ACharacter* Cand = WC.Get();
		if (!Cand)
		{
			continue;
		}
		const FVector L = XF.InverseTransformPosition(Cand->GetActorLocation());
		if (FMath::Abs(L.X) < Size.X * 0.5f && FMath::Abs(L.Y) < Size.Y * 0.5f && FMath::Abs(L.Z) < Size.Z * 0.5f + 150.f)
		{
			In.Add(Cand);
		}
	}
	for (ACharacter* C : In)
	{
		if (!C || !(C->HasAuthority() || C->IsLocallyControlled()))
		{
			continue;
		}
		if (const ABamtolPlayer* P = Cast<ABamtolPlayer>(C); P && (P->IsRiding() || P->IsCaught() || P->IsTraveling()))
		{
			continue;
		}
		UCharacterMovementComponent* M = C->GetCharacterMovement();
		const float K = M->IsFalling() ? AirMult : 1.f;
		if (ABamtolPlayer* WP = Cast<ABamtolPlayer>(C))
		{
			WP->ApplyWind(Dir * Strength);   // 걷는 최고 속도도 올려 줌 (안 그러면 최고 속도에 막혀 안 밀림)
		}
		const float BossK = BamtolReact::BodyScale(C);   // 몸 무게 (얽힘 표 Wind×Heavy 대장 0.5 · Wind×Floaty 풍선 밤 2 · 그 밖 1)
		M->AddForce(Dir * Strength * K * BossK * M->Mass);
		if (M->IsFalling() && M->Velocity.Z < 0.f)
		{
			M->AddForce(FVector(0.f, 0.f, 600.f) * M->Mass);   // 공중이면 살짝 띄워 줌 (더 멀리 날아감)
		}
	}
	// 1-2) 가벼운 것(솔방울·미끼)도 밂 — 서버, 0.1초마다 (얽힘 표 Wind×Light)
	if (HasAuthority())
	{
		ReactClock += DeltaSeconds;
		if (ReactClock >= 0.1f)
		{
			BamtolReact::Wind(GetWorld(), &XF, Size, Dir, Strength, ReactClock);
			ReactClock = 0.f;
		}
	}
	// 2) 바람 줄기 (모든 화면)
	if (GetNetMode() == NM_DedicatedServer || Streaks.Num() == 0)
	{
		return;
	}
	// 성능 (2026-10-06): 줄기 22개 × 바람 길 16곳을 매 프레임 옮기던 것(프레임당 700번 위치 계산 + 그림 갱신)
	//   → 화면에 그려진 바람 길만 움직임. 안 보이는 동안 흐른 시간은 모아 뒀다가 보이면 한 번에 (줄기 자리는 계속 이어짐)
	VisDt += DeltaSeconds;
	VisCheck -= DeltaSeconds;
	if (VisCheck <= 0.f)
	{
		VisCheck = 0.2f;
		bVisNow = false;
		for (const UStaticMeshComponent* C : Streaks)
		{
			if (C && C->WasRecentlyRendered(0.3f))
			{
				bVisNow = true;
				break;
			}
		}
		for (const UStaticMeshComponent* C : Signs)
		{
			bVisNow |= C && C->WasRecentlyRendered(0.3f);
		}
	}
	if (!bVisNow)
	{
		return;
	}
	const float Dt = FMath::Min(VisDt, 1.f);
	VisDt = 0.f;
	VisClock += Dt;
	for (int32 i = 0; i < Streaks.Num(); ++i)
	{
		FVector& S = StreakSeed[i];
		S.X += StreakSpeed / FMath::Max(Size.X, 1.f) * Dt * (0.8f + 0.4f * FMath::Frac(i * 0.37f));
		S.X = FMath::Frac(S.X);   // 오래 안 보였다 와도 0~1 안으로
		const float Fade = FMath::Sin(S.X * PI);   // 들어올 때·나갈 때 가늘게
		const FVector Local((S.X - 0.5f) * Size.X, S.Y * Size.Y + FMath::Sin(VisClock * 2.f + i) * 30.f, (S.Z - 0.5f) * Size.Z + FMath::Sin(VisClock * 3.f + i * 1.3f) * 25.f);
		// 위치·크기를 한 번에 (따로 부르면 위치 계산·그림 갱신이 두 번)
		Streaks[i]->SetRelativeTransform(FTransform(StreakRot[i], Local, FVector(0.6f + 0.6f * Fade, FMath::Max(0.05f, Fade), 1.f)));   // 리본 폭이 들어올 때·나갈 때 가늘게
	}
}

// ============================ 미끄럼틀 ============================

ABamtolSlide::ABamtolSlide()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);   // 복제할 값(길 점·관 여부)은 만들 때 한 번만 정해짐 — 검사 초당 1번, 만든 직후엔 ForceNetUpdate (2026-10-26 성능, 예전 100번)
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ABamtolSlide::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABamtolSlide, Points);
	DOREPLIFETIME(ABamtolSlide, bTube);
}

bool ABamtolSlide::BuildDownhill(const FVector& Start, int32 MaxSegments, float Step)
{
	// 가장 가파른 내리막을 따라 Step 씩: 땅 기울기 방향으로 가되 갑자기 꺾이지 않게 부드럽게
	Points.Reset();
	FHitResult Hit;
	if (!BamtolGroundAt(GetWorld(), FVector2D(Start), Hit))
	{
		return false;
	}
	FVector P = Hit.ImpactPoint;
	FVector Dir = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return false;
	}
	Points.Add(P + FVector(0, 0, 8.f));
	for (int32 i = 0; i < MaxSegments; ++i)
	{
		const FVector Probe = P + Dir * Step;
		if (!BamtolGroundAt(GetWorld(), FVector2D(Probe), Hit))
		{
			break;
		}
		const FVector Down = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
		const FVector Next = Hit.ImpactPoint;
		const float Drop = P.Z - Next.Z;
		if (Drop < Step * 0.08f || Hit.ImpactNormal.Z < 0.6f)
		{
			break;   // 평평해지거나 너무 가파르면 끝
		}
		// 바위·나무·소품에 막히면 끝 (땅만 보고 정해서 바위 밑으로 지나가던 것): 탄 사람 몸 높이로 공을 굴려 보고, 위로 머리 공간도
		{
			FCollisionQueryParams Q(SCENE_QUERY_STAT(BamtolSlideClear), false, this);
			FHitResult Block;
			const FVector A = P + FVector(0, 0, 110.f);
			const FVector B = Next + FVector(0, 0, 110.f);
			const bool bSide = GetWorld()->SweepSingleByChannel(Block, A, B, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(70.f), Q)
				&& !Cast<ALandscapeProxy>(Block.GetActor());
			const bool bHead = GetWorld()->LineTraceSingleByChannel(Block, Next + FVector(0, 0, 40.f), Next + FVector(0, 0, 350.f), ECC_WorldStatic, Q)
				&& !Cast<ALandscapeProxy>(Block.GetActor());
			if (bSide || bHead)
			{
				break;
			}
		}
		Points.Add(Next + FVector(0, 0, 8.f));
		P = Next;
		if (!Down.IsNearlyZero())
		{
			Dir = (Dir * 0.7f + Down * 0.3f).GetSafeNormal2D();
		}
	}
	if (Points.Num() < 8)
	{
		Points.Reset();
		return false;
	}
	LiftAboveGround(20.f);
	OnRep_Points();
	ForceNetUpdate();   // 손님도 바로 같은 길을 받게
	return true;
}

bool ABamtolSlide::BuildUphill(const FVector& Start, int32 MaxSegments, float Step)
{
	// 가장 가파른 오르막을 따라 (내리막의 거꾸로). 관이라 조금 더 가파른 곳도 올라감
	Points.Reset();
	bTube = true;
	FHitResult Hit;
	if (!BamtolGroundAt(GetWorld(), FVector2D(Start), Hit))
	{
		return false;
	}
	FVector P = Hit.ImpactPoint;
	FVector Dir = -FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return false;
	}
	Points.Add(P + FVector(0, 0, 8.f));
	for (int32 i = 0; i < MaxSegments; ++i)
	{
		const FVector Probe = P + Dir * Step;
		if (!BamtolGroundAt(GetWorld(), FVector2D(Probe), Hit))
		{
			break;
		}
		const FVector Up = -FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
		const FVector Next = Hit.ImpactPoint;
		const float Rise = Next.Z - P.Z;
		if (Rise < Step * 0.05f || Hit.ImpactNormal.Z < 0.45f)
		{
			break;   // 꼭대기·절벽이면 끝
		}
		FCollisionQueryParams Q(SCENE_QUERY_STAT(BamtolTubeClear), false, this);
		FHitResult Block;
		const bool bSide = GetWorld()->SweepSingleByChannel(Block, P + FVector(0, 0, 130.f), Next + FVector(0, 0, 130.f), FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(90.f), Q)
			&& !Cast<ALandscapeProxy>(Block.GetActor());
		if (bSide)
		{
			break;
		}
		Points.Add(Next + FVector(0, 0, 8.f));
		P = Next;
		if (!Up.IsNearlyZero())
		{
			Dir = (Dir * 0.75f + Up * 0.25f).GetSafeNormal2D();
		}
	}
	if (Points.Num() < 16)   // 관은 60m 넘게 (길게)
	{
		Points.Reset();
		return false;
	}
	LiftAboveGround(30.f);
	OnRep_Points();
	ForceNetUpdate();   // 손님도 바로 같은 관을 받게
	return true;
}

void ABamtolSlide::LiftAboveGround(float Clear)
{
	// 점과 점은 곧은 조각이라 볼록한 땅에선 가운데가 묻힘 → 사이 3곳의 땅 높이를 보고 양 끝을 올림
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 i = 0; i + 1 < Points.Num(); ++i)
		{
			const FVector A = Points[i], B = Points[i + 1];
			float Need = 0.f;
			for (int32 k = 1; k <= 3; ++k)
			{
				const FVector M = FMath::Lerp(A, B, k / 4.f);
				FHitResult G;
				if (BamtolGroundAt(GetWorld(), FVector2D(M), G))
				{
					Need = FMath::Max(Need, G.ImpactPoint.Z + Clear - M.Z);
				}
			}
			if (Need > 0.f)
			{
				Points[i].Z += Need;
				Points[i + 1].Z += Need;
			}
		}
	}
}

int32 ABamtolSlide::ClearAround(float Radius) const
{
	int32 Gone = 0;
	TArray<AActor*> Kill;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* A = *It;
		if (!(Cast<ABamtolItemBox>(A) || Cast<ABamtolProp>(A) || Cast<ABamtolBouncePad>(A) || Cast<ABamtolBoostPad>(A) || Cast<ABamtolCannon>(A)
			|| Cast<ABamtolBus>(A) || Cast<ABamtolRollingLog>(A)) || A->ActorHasTag(TEXT("Map")))
		{
			continue;
		}
		const FVector L = A->GetActorLocation();
		for (int32 i = 0; i + 1 < Points.Num(); ++i)
		{
			const FVector C = FMath::ClosestPointOnSegment(L, FVector(Points[i]), FVector(Points[i + 1]));
			if (FVector::DistSquared2D(C, L) < FMath::Square(Radius) && FMath::Abs(C.Z - L.Z) < 500.f)
			{
				Kill.Add(A);
				break;
			}
		}
	}
	for (AActor* A : Kill)
	{
		A->Destroy();
		++Gone;
	}
	return Gone;
}

void ABamtolSlide::OnRep_Points()
{
	CumLen.Reset();
	float L = 0.f;
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		if (i > 0)
		{
			L += FVector::Dist(Points[i - 1], Points[i]);
		}
		CumLen.Add(L);
	}
	BuildMeshes();
}

void ABamtolSlide::BuildMeshes()
{
	for (UStaticMeshComponent* C : Segments)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	Segments.Reset();
	UStaticMesh* SM = bTube ? TubeMesh.LoadSynchronous() : nullptr;
	if (!SM)
	{
		SM = SegmentMesh.LoadSynchronous();
	}
	for (int32 i = 0; SM && i + 1 < Points.Num(); ++i)
	{
		const FVector A = Points[i], B = Points[i + 1];
		UStaticMeshComponent* C = MakePart(this, SM, RootComponent, false);
		C->SetWorldLocationAndRotation(A, (B - A).Rotation());
		C->SetWorldScale3D(FVector(FVector::Dist(A, B) / 400.f + 0.02f, 1.f, 1.f));
		Segments.Add(C);
	}
}

float ABamtolSlide::Length() const
{
	return CumLen.Num() > 0 ? CumLen.Last() : 0.f;
}

FVector ABamtolSlide::PointAt(float Dist) const
{
	if (Points.Num() < 2)
	{
		return GetActorLocation();
	}
	for (int32 i = 1; i < CumLen.Num(); ++i)
	{
		if (Dist <= CumLen[i])
		{
			const float T = (Dist - CumLen[i - 1]) / FMath::Max(CumLen[i] - CumLen[i - 1], 1.f);
			return FMath::Lerp(FVector(Points[i - 1]), FVector(Points[i]), FMath::Clamp(T, 0.f, 1.f));
		}
	}
	return Points.Last();
}

FVector ABamtolSlide::DirAt(float Dist) const
{
	if (Points.Num() < 2)
	{
		return FVector::ForwardVector;
	}
	for (int32 i = 1; i < CumLen.Num(); ++i)
	{
		if (Dist <= CumLen[i])
		{
			return (FVector(Points[i]) - FVector(Points[i - 1])).GetSafeNormal();
		}
	}
	return (FVector(Points.Last()) - FVector(Points[Points.Num() - 2])).GetSafeNormal();
}

float ABamtolSlide::Nearest(const FVector& At, float& OutSide, float& OutUp) const
{
	float BestD = TNumericLimits<float>::Max();
	float BestDist = 0.f;
	OutSide = OutUp = TNumericLimits<float>::Max();
	for (int32 i = 1; i < Points.Num(); ++i)
	{
		const FVector A = Points[i - 1], B = Points[i];
		const FVector AB = B - A;
		const float T = FMath::Clamp(FVector::DotProduct(At - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
		const FVector Q = A + AB * T;
		const float D = FVector::DistSquared2D(Q, At);
		if (D < BestD)
		{
			BestD = D;
			BestDist = CumLen[i - 1] + AB.Size() * T;
			OutSide = FMath::Sqrt(D);
			OutUp = At.Z - Q.Z;
		}
	}
	return BestDist;
}

void ABamtolSlide::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || Points.Num() < 2)
	{
		return;
	}
	// 0.1초마다: 길 위에 들어온 도망자를 태움 (끝 10m 앞까지)
	CheckClock -= DeltaSeconds;
	if (CheckClock > 0.f)
	{
		return;
	}
	CheckClock = 0.1f;
	const FVector Mid = PointAt(Length() * 0.5f);
	for (TActorIterator<ABamtolPlayer> It(GetWorld()); It; ++It)
	{
		ABamtolPlayer* P = *It;
		if (FVector::DistSquared2D(P->GetActorLocation(), Mid) > FMath::Square(Length() * 0.6f + 500.f) || !P->CanStartTravel())
		{
			continue;
		}
		float Side = 0.f, Up = 0.f;
		const float D = Nearest(P->GetActorLocation(), Side, Up);
		if (Side < HalfWidth && Up > -60.f && Up < 220.f && D < Length() - 1000.f && (!bTube || D < TubeEntry))
		{
			P->StartSlide(this, D);
		}
	}
}

// ============================ 짚라인 ============================

ABamtolZipline::ABamtolZipline()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);   // 복제할 값(줄 위·아래 끝)은 놓을 때 한 번만 정해짐 — 검사 초당 1번 (2026-10-26 성능, 예전 100번)
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ABamtolZipline::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABamtolZipline, Top);
	DOREPLIFETIME(ABamtolZipline, Bottom);
}

bool ABamtolZipline::Setup(const FVector& InTop, const FVector& InBottom, bool bForce)
{
	Top = InTop;
	Bottom = InBottom;
	ForceNetUpdate();   // 손님도 바로 같은 줄 자리를 받게
	// 줄 아래(매달린 사람 발 높이)가 땅에 막히면 안 됨
	const FVector Down(0.f, 0.f, -HangDepth - 120.f);
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ZipCheck), false, this);
	if (!bForce && GetWorld()->LineTraceSingleByChannel(Hit, AnchorA() + Down + Dir() * 300.f, AnchorB() + Down - Dir() * 300.f, ECC_WorldStatic, Q))
	{
		return false;
	}
	SetActorLocation(InTop);
	OnRep_Ends();
	return true;
}

void ABamtolZipline::OnRep_Ends()
{
	BuildMeshes();
}

void ABamtolZipline::BuildMeshes()
{
	for (UStaticMeshComponent* C : Parts)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	Parts.Reset();
	const FVector D2 = (FVector(Bottom) - FVector(Top)).GetSafeNormal2D();
	const FRotator Face = D2.Rotation();
	if (UStaticMesh* Pole = PoleMesh.LoadSynchronous())
	{
		UStaticMeshComponent* A = MakePart(this, Pole, RootComponent);
		A->SetWorldLocationAndRotation(FVector(Top), Face);
		UStaticMeshComponent* B = MakePart(this, Pole, RootComponent);
		B->SetWorldLocationAndRotation(FVector(Bottom), Face + FRotator(0.f, 180.f, 0.f));
		B->SetWorldScale3D(FVector(1.f, 1.f, BottomAnchorHeight / TopAnchorHeight));
		Parts.Add(A);
		Parts.Add(B);
	}
	if (UStaticMesh* Cable = CableMesh.LoadSynchronous())
	{
		UStaticMeshComponent* C = MakePart(this, Cable, RootComponent, false);
		C->SetWorldLocationAndRotation(AnchorA(), Dir().Rotation());
		C->SetWorldScale3D(FVector(Length() / 100.f, 1.f, 1.f));
		Parts.Add(C);
	}
	if (UStaticMesh* H = HandleMesh.LoadSynchronous())
	{
		Handle = MakePart(this, H, RootComponent);
		Handle->SetWorldLocationAndRotation(AnchorA() + Dir() * 60.f, Face);
		Parts.Add(Handle);
	}
}

void ABamtolZipline::SetHandle(int32 Index, const FVector& At)
{
	if (Handle)
	{
		Handle->SetWorldLocation(At);
	}
}
