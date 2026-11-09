#include "BamtolPlayerState.h"
#include "BamtolPlayer.h"
#include "BamtolLoc.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "BamtolSettings.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"

void ABamtolPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABamtolPlayerState, LookColor);
	DOREPLIFETIME(ABamtolPlayerState, Nickname);
	DOREPLIFETIME(ABamtolPlayerState, LookHat);
	DOREPLIFETIME(ABamtolPlayerState, HatLooks);
	DOREPLIFETIME(ABamtolPlayerState, CosWear);
	DOREPLIFETIME(ABamtolPlayerState, Acorns);
	DOREPLIFETIME(ABamtolPlayerState, bAllowMimic);
	DOREPLIFETIME(ABamtolPlayerState, VoiceStyle);
	DOREPLIFETIME(ABamtolPlayerState, PhotoPose);
	DOREPLIFETIME(ABamtolPlayerState, RunnerKind);
	DOREPLIFETIME(ABamtolPlayerState, BossKind);
	DOREPLIFETIME(ABamtolPlayerState, Title);
	DOREPLIFETIME(ABamtolPlayerState, Nation);
	DOREPLIFETIME(ABamtolPlayerState, RunState);
	DOREPLIFETIME(ABamtolPlayerState, RoundPoints);
	DOREPLIFETIME(ABamtolPlayerState, EscapeHow);
	DOREPLIFETIME(ABamtolPlayerState, AliveSeconds);
	DOREPLIFETIME(ABamtolPlayerState, DistanceMeters);
	DOREPLIFETIME(ABamtolPlayerState, ItemsUsed);
	DOREPLIFETIME(ABamtolPlayerState, Hits);
	DOREPLIFETIME(ABamtolPlayerState, Rescues);
	DOREPLIFETIME(ABamtolPlayerState, CageHooks);
	DOREPLIFETIME(ABamtolPlayerState, CageStage);
	DOREPLIFETIME(ABamtolPlayerState, bCageOut);
	DOREPLIFETIME(ABamtolPlayerState, TicketsFound);
	DOREPLIFETIME(ABamtolPlayerState, Catches);
	DOREPLIFETIME(ABamtolPlayerState, bReady);
	DOREPLIFETIME(ABamtolPlayerState, bWantBoss);
	DOREPLIFETIME(ABamtolPlayerState, TargetScore);
	DOREPLIFETIME(ABamtolPlayerState, TargetChalEnd);
	DOREPLIFETIME(ABamtolPlayerState, TargetChalScore);
	DOREPLIFETIME(ABamtolPlayerState, TargetChalBest);
	DOREPLIFETIME(ABamtolPlayerState, JumpStage);
	DOREPLIFETIME(ABamtolPlayerState, JumpRunStart);
	DOREPLIFETIME(ABamtolPlayerState, JumpFalls);
	DOREPLIFETIME(ABamtolPlayerState, JumpLastTime);
	DOREPLIFETIME(ABamtolPlayerState, BossHead);
	DOREPLIFETIME(ABamtolPlayerState, BossBall);
	DOREPLIFETIME(ABamtolPlayerState, PrefColor);
}

FString ABamtolPlayerState::ColorName(int32 Index)
{
	static const TCHAR* Names[] = { BTXT("민트"), BTXT("산호"), BTXT("하늘"), BTXT("레몬"), BTXT("보라"), BTXT("분홍"), BTXT("연두"), BTXT("크림"),
		BTXT("주황"), BTXT("남색"), BTXT("청록"), BTXT("자주"), BTXT("회색"), BTXT("흰색"), BTXT("빨강"), BTXT("초록") };
	return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : FString();
}

FLinearColor ABamtolPlayerState::ColorOf(int32 Index)
{
	// MI_Bamtol_Body_* 와 비슷한 화면용 색 (sRGB)
	static const FColor Cols[] = { FColor(110, 220, 175), FColor(255, 128, 100), FColor(110, 180, 255), FColor(255, 225, 90),
		FColor(180, 145, 240), FColor(255, 145, 190), FColor(160, 225, 85), FColor(240, 208, 160),
		FColor(255, 160, 60), FColor(80, 100, 190), FColor(50, 190, 190), FColor(215, 80, 170),
		FColor(165, 170, 178), FColor(245, 245, 240), FColor(230, 60, 55), FColor(45, 150, 75) };
	return Index >= 0 && Index < UE_ARRAY_COUNT(Cols) ? FLinearColor(Cols[Index]) : FLinearColor(0.6f, 0.45f, 0.3f);
}

const TCHAR* ABamtolPlayerState::ColorKey(int32 Index)
{
	static const TCHAR* Keys[NumColors] = { TEXT("Mint"), TEXT("Coral"), TEXT("Sky"), TEXT("Lemon"), TEXT("Lavender"), TEXT("Pink"), TEXT("Lime"), TEXT("Cream"),
		TEXT("Orange"), TEXT("Navy"), TEXT("Teal"), TEXT("Magenta"), TEXT("Gray"), TEXT("White"), TEXT("Red"), TEXT("Green") };
	return Index >= 0 && Index < NumColors ? Keys[Index] : TEXT("Mint");
}

const TCHAR* ABamtolPlayerState::PortraitKey(int32 Index)
{
	if (Index == 7)
	{
		return TEXT("Cream2");   // 크림은 진한 판 아이콘
	}
	return Index >= 0 && Index < NumColors ? ColorKey(Index) : TEXT("Boss2");
}

UTexture2D* ABamtolPlayerState::PortraitOf(int32 Index)
{
	static TMap<int32, UTexture2D*> Cache;
	UTexture2D*& T = Cache.FindOrAdd(Index);
	if (!T)
	{
		const FString K = PortraitKey(Index);
		T = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/Bamtol/UI/Icons/T_Portrait_%s.T_Portrait_%s"), *K, *K), nullptr, LOAD_NoWarn);
		if (T)
		{
			T->AddToRoot();
		}
	}
	return T;
}

FLinearColor ABamtolPlayerState::BossPalette(int32 Index)
{
	static const FColor Cols[] = { FColor(120, 62, 30), FColor(255, 205, 40), FColor(90, 190, 80), FColor(90, 170, 255),
		FColor(255, 130, 180), FColor(160, 110, 230), FColor(230, 60, 50), FColor(45, 42, 48), FColor(240, 235, 225) };
	return FLinearColor(Cols[FMath::Clamp(Index, 0, 8)]);
}

FString ABamtolPlayerState::BossPaletteName(int32 Index)
{
	static const TCHAR* Names[] = { BTXT("밤색"), BTXT("노랑"), BTXT("초록"), BTXT("하늘"), BTXT("분홍"), BTXT("보라"), BTXT("빨강"), BTXT("검정"), BTXT("흰색") };
	return Names[FMath::Clamp(Index, 0, 8)];
}

FString ABamtolPlayerState::ShowName() const
{
	if (!Nickname.IsEmpty() && !IsNameHiddenHere())
	{
		return Nickname;
	}
	const FString C = ColorName(LookColor);
	const FString N = C.IsEmpty() ? GetPlayerName() : C;
	return IsABot() ? N + TEXT(" (AI)") : N;   // 빈자리 AI 친구
}

bool ABamtolPlayerState::IsNameHiddenHere() const
{
	const UBamtolGameUserSettings* G = UBamtolGameUserSettings::Get();
	if (!G || !G->bStreamerMode)
	{
		return false;
	}
	const APlayerController* PC = GetPlayerController();
	return !(PC && PC->IsLocalController());   // 내 이름은 그대로
}

FString ABamtolPlayerState::MaskNames(const UObject* Ctx, const FString& Text)
{
	const UBamtolGameUserSettings* G = UBamtolGameUserSettings::Get();
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	if (!G || !G->bStreamerMode || !W || !W->GetGameState())
	{
		return Text;
	}
	FString Out = Text;
	for (const APlayerState* P : W->GetGameState()->PlayerArray)
	{
		const ABamtolPlayerState* PS = Cast<ABamtolPlayerState>(P);
		if (PS && !PS->Nickname.IsEmpty() && PS->IsNameHiddenHere())
		{
			const FString C = ColorName(PS->LookColor);
			Out = Out.Replace(*PS->Nickname, *(C.IsEmpty() ? FString(BTXT("친구")) : C), ESearchCase::CaseSensitive);
		}
	}
	return Out;
}

FString ABamtolPlayerState::NameOf(const AController* C)
{
	if (const ABamtolPlayerState* PS = C ? C->GetPlayerState<ABamtolPlayerState>() : nullptr)
	{
		return PS->ShowName();
	}
	return C && C->PlayerState ? C->PlayerState->GetPlayerName() : FString(BTXT("누군가"));
}

FString ABamtolPlayerState::NameOf(const APawn* P)
{
	return P ? NameOf(P->GetController()) : FString(BTXT("누군가"));
}

ABamtolPlayerState::FOnJumpStage ABamtolPlayerState::OnJumpStageChanged;

void ABamtolPlayerState::SetJumpStage(int32 Stage)
{
	const int32 Old = JumpStage;
	JumpStage = uint8(FMath::Clamp(Stage, 0, 255));
	if (Old != JumpStage) OnJumpStageChanged.Broadcast(this, Old, JumpStage);   // 방장 화면·서버 쪽 듣는 곳 (손님은 OnRep 에서)
}

void ABamtolPlayerState::OnRep_JumpStage(uint8 Old)
{
	if (Old != JumpStage) OnJumpStageChanged.Broadcast(this, Old, JumpStage);
}

void ABamtolPlayerState::OnRep_HatLooks()
{
	if (ABamtolPlayer* P = Cast<ABamtolPlayer>(GetPawn()))
	{
		P->RefreshHatLook();
	}
}

void ABamtolPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	if (ABamtolPlayerState* N = Cast<ABamtolPlayerState>(PlayerState))
	{
		N->JumpLastTime = JumpLastTime;
		N->TargetScore = TargetScore;
		N->TargetChalBest = TargetChalBest;
		N->CosWear = CosWear;   // 치장·도토리도 맵 이동 때 그대로 (2026-10-12)
		N->Acorns = Acorns;
	}
}
