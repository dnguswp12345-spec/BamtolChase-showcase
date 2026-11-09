#include "BamtolGameState.h"
#include "BamtolLoc.h"
#include "BamtolNet.h"
#include "BamtolSettings.h"
#include "BamtolRoomRules.h"
#include "BamtolMaps.h"
#include "BamtolDaily.h"
#include "HAL/IConsoleManager.h"

#include "Engine/Engine.h"
#include "BamtolTreeFall.h"
#include "Net/UnrealNetwork.h"
#include "BamtolPlayerState.h"

void ABamtolGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABamtolGameState, LobbySlots);
	DOREPLIFETIME(ABamtolGameState, ClimbStartServerTime);
	DOREPLIFETIME(ABamtolGameState, ClimbBossAt);
	DOREPLIFETIME(ABamtolGameState, bExpedition);
	DOREPLIFETIME(ABamtolGameState, TimeLeft);
	DOREPLIFETIME(ABamtolGameState, RoomCode);
	DOREPLIFETIME(ABamtolGameState, RoomName);
	DOREPLIFETIME(ABamtolGameState, NetHostPid);
	DOREPLIFETIME(ABamtolGameState, NetHeirPid);
	DOREPLIFETIME(ABamtolGameState, bQuickRoom);
	DOREPLIFETIME(ABamtolGameState, bMidJoinRoom);
	DOREPLIFETIME(ABamtolGameState, bRoomVoice);
	DOREPLIFETIME(ABamtolGameState, RoundMinutes);
	DOREPLIFETIME(ABamtolGameState, BossMode);
	DOREPLIFETIME(ABamtolGameState, Weather);
	DOREPLIFETIME(ABamtolGameState, bWeatherLive);
	DOREPLIFETIME(ABamtolGameState, VillageSky);
	DOREPLIFETIME(ABamtolGameState, DayShift);
	DOREPLIFETIME(ABamtolGameState, BotFill);
	DOREPLIFETIME(ABamtolGameState, BossDifficulty);
	DOREPLIFETIME(ABamtolGameState, RuleFlags);
	DOREPLIFETIME(ABamtolGameState, RuleCode);
	DOREPLIFETIME(ABamtolGameState, BlindLeft);
	DOREPLIFETIME(ABamtolGameState, SurpriseId);
	DOREPLIFETIME(ABamtolGameState, LastRunnerPS);
	DOREPLIFETIME(ABamtolGameState, Pings);
	DOREPLIFETIME(ABamtolGameState, Targets);
	DOREPLIFETIME(ABamtolGameState, BossSpeedPct);
	DOREPLIFETIME(ABamtolGameState, MapSize);
	DOREPLIFETIME(ABamtolGameState, MissionLevel);
	DOREPLIFETIME(ABamtolGameState, ViewRule);
	DOREPLIFETIME(ABamtolGameState, RoundStyle);
	DOREPLIFETIME(ABamtolGameState, bCageRound);
	DOREPLIFETIME(ABamtolGameState, RoundBossKind);
	DOREPLIFETIME(ABamtolGameState, bChaseRound);
	DOREPLIFETIME(ABamtolGameState, RoundMap);
	DOREPLIFETIME(ABamtolGameState, RoomSize);
	DOREPLIFETIME(ABamtolGameState, BossCount);
	DOREPLIFETIME(ABamtolGameState, PlayMode);
	DOREPLIFETIME(ABamtolGameState, DesignatedBoss);
	DOREPLIFETIME(ABamtolGameState, Runners);
	DOREPLIFETIME(ABamtolGameState, Escaped);
	DOREPLIFETIME(ABamtolGameState, Caught);
	DOREPLIFETIME(ABamtolGameState, Phase);
	DOREPLIFETIME(ABamtolGameState, Tickets);
	DOREPLIFETIME(ABamtolGameState, TicketsNeeded);
	DOREPLIFETIME(ABamtolGameState, bBurning);
	DOREPLIFETIME(ABamtolGameState, bHideFar);
	DOREPLIFETIME(ABamtolGameState, HatPickLeft);
	DOREPLIFETIME(ABamtolGameState, ResultReason);
	DOREPLIFETIME(ABamtolGameState, RoundStory);
	DOREPLIFETIME(ABamtolGameState, bGambleOn);
	DOREPLIFETIME(ABamtolGameState, RevealPos);
	DOREPLIFETIME(ABamtolGameState, RevealServerTime);
	DOREPLIFETIME(ABamtolGameState, ClosingExit);
	DOREPLIFETIME(ABamtolGameState, ClosingIn);
	DOREPLIFETIME(ABamtolGameState, NextRoundIn);
	DOREPLIFETIME(ABamtolGameState, EndStage);
	DOREPLIFETIME(ABamtolGameState, EndSkipTo);
	DOREPLIFETIME(ABamtolGameState, EndSkipVotes);
	DOREPLIFETIME(ABamtolGameState, EndSkipNeed);
	DOREPLIFETIME(ABamtolGameState, RelayRound);
	DOREPLIFETIME(ABamtolGameState, RelayRounds);
	DOREPLIFETIME(ABamtolGameState, RelayGoal);
	DOREPLIFETIME(ABamtolGameState, RelayEndsAt);
	DOREPLIFETIME(ABamtolGameState, RelayThrowers);
	DOREPLIFETIME(ABamtolGameState, CourtDefendant);
	DOREPLIFETIME(ABamtolGameState, CourtJail);
	DOREPLIFETIME(ABamtolGameState, CourtForgive);
	DOREPLIFETIME(ABamtolGameState, CourtVerdict);
	DOREPLIFETIME(ABamtolGameState, bTitle);
	DOREPLIFETIME(ABamtolGameState, bLobby);
	DOREPLIFETIME(ABamtolGameState, bHighlightsOn);
	DOREPLIFETIME(ABamtolGameState, bPlayground);
	DOREPLIFETIME(ABamtolGameState, LobbyDepartureAt);
}

void ABamtolGameState::MulticastTreeFall_Implementation(FVector_NetQuantize Base, FVector_NetQuantizeNormal Dir)
{
	if (UBamtolTreeFall* TF = GetWorld()->GetSubsystem<UBamtolTreeFall>())
	{
		TF->Fell(Base, Dir);
	}
}

void ABamtolGameState::MulticastAnnounce_Implementation(const FString& Text, float Seconds, uint8 Priority)
{
	PushAnnounce(Text, Seconds, Priority);
}

void ABamtolGameState::PushAnnounce(const FString& Text, float Seconds, uint8 Priority)
{
	// 위 가운데 리본 공지 (UBamtolAnnounceWidget) — 줄 세우기: 한 번에 하나만 보이고, 차례가 오면 Time(사라질 때)을 정함
	//   같은 글이 기다리거나 보이는 중이면 새로 안 넣고 합침 (2026-10-06: 공지·리본·자막이 3개씩 겹쳐 뜨던 것)
	const double Now = GetWorld()->GetRealTimeSeconds();
	const FString Masked = ABamtolPlayerState::MaskNames(this, Text);   // 스트리머 모드: 남의 닉네임 가림
	const float Show = FMath::Max(Seconds, 1.5f);
	for (FBamtolFeedLine& Old : AnnounceLog)
	{
		if (Old.Text == Masked && (Old.Time <= 0.0 || Old.Time > Now))
		{
			Old.Show = FMath::Max(Old.Show, Show);
			Old.Priority = FMath::Max(Old.Priority, Priority);
			if (Old.Time > Now)
			{
				Old.Time = FMath::Max(Old.Time, Now + 1.0);   // 보이는 중이면 조금 더
			}
			UE_LOG(LogTemp, Display, TEXT("[Announce] (합침) %s"), *Text);
			return;
		}
	}
	FBamtolFeedLine L;
	L.Text = Masked;
	L.Priority = Priority;
	L.Born = Now;
	L.Show = Show;
	L.Time = 0.0;   // 차례 기다림
	AnnounceLog.Add(L);
	// 오래 기다린 작은 소식은 버림 (이미 지난 이야기), 너무 많으면 오래된 것부터
	AnnounceLog.RemoveAll([Now](const FBamtolFeedLine& X) { return (X.Time > 0.0 && X.Time < Now - 1.0) || (X.Time <= 0.0 && X.Priority <= 1 && Now - X.Born > 12.0); });
	while (AnnounceLog.Num() > 8)
	{
		AnnounceLog.RemoveAt(0);
	}
	UE_LOG(LogTemp, Display, TEXT("[Announce] %s"), *Text);
}

void ABamtolGameState::MulticastChat_Implementation(const FString& Name, int32 Color, const FString& Text)
{
	// 받은 글 다듬기 (2026-10-12, BamtolNet.h): 내가 음소거·차단한 사람 글은 버림 · 정해진 말(번호)은 내 언어로 · 금칙어 가림
	FString Clean = Text;
	bool bStranger = false;
	if (!BamtolNet::FilterChat(this, Color, Clean, &bStranger))
	{
		// 모르는 사람 글이라 숨겼으면 이 판에 한 번만 안내 (2026-10-25 사용자 QA: "멀티 채팅이 안 됨" — 조용히 숨겨서 고장으로 보였음)
		//   숨김 기본값은 그대로. 설정에서 켜면 보임
		if (bStranger && !bStrangerChatNoted)
		{
			bStrangerChatNoted = true;
			FBamtolChatLine L;
			L.Name = BTXT("알림");
			L.Text = BTXT("모르는 사람의 글·목소리는 숨기는 중이에요 — 설정 > '모르는 사람 글 보기'·'모르는 사람 목소리 듣기'로 켤 수 있어요");
			L.Time = GetWorld()->GetRealTimeSeconds();
			ChatLog.Add(L);
		}
		return;
	}
	FBamtolChatLine L;
	L.Name = ABamtolPlayerState::MaskNames(this, Name);
	L.Text = ABamtolPlayerState::MaskNames(this, Clean);
	L.Color = Color;
	L.Time = GetWorld()->GetRealTimeSeconds();
	ChatLog.Add(L);
	if (ChatLog.Num() > 30)
	{
		ChatLog.RemoveAt(0);
	}
	UE_LOG(LogTemp, Display, TEXT("[Chat] %s: %s"), *Name, *Text);
}

void ABamtolGameState::MulticastHighlightClip_Implementation(const FBamtolClip& Clip, uint8 Index, uint8 Count)
{
	if (UBamtolHighlights* H = GetWorld()->GetSubsystem<UBamtolHighlights>())
	{
		H->SetClipPart(Clip, Index, Count);
	}
	UE_LOG(LogTemp, Display, TEXT("[Highlight] 받은 장면 %d/%d (%s): %s"), Index + 1, Count, GetNetMode() == NM_Client ? TEXT("손님") : TEXT("방장"), *Clip.Title);
	if (Index + 1 == Count)
	{
		UE_LOG(LogTemp, Display, TEXT("[Highlight] 명장면 %d개 받음"), Count);
	}
}

void ABamtolGameState::MulticastHighlights_Implementation(const TArray<FBamtolClip>& Clips)
{
	if (UBamtolHighlights* H = GetWorld()->GetSubsystem<UBamtolHighlights>())
	{
		H->SetClips(Clips);
	}
	UE_LOG(LogTemp, Display, TEXT("[Highlight] 명장면 %d개 받음"), Clips.Num());
}

void ABamtolGameState::MulticastFeed_Implementation(EBamtolFeed Kind, const FString& Text, int32 Color)
{
	FBamtolFeedLine L;
	L.Text = ABamtolPlayerState::MaskNames(this, Text);
	L.Kind = Kind;
	L.Color = Color;
	L.Time = GetWorld()->GetRealTimeSeconds();
	FeedLog.Add(L);
	if (FeedLog.Num() > 12)
	{
		FeedLog.RemoveAt(0);
	}
	UE_LOG(LogTemp, Display, TEXT("[Feed] %s"), *Text);
}

void ABamtolGameState::CopyRules(const UBamtolGameUserSettings* S)
{
	if (!S)
	{
		return;
	}
	RuleFlags = (S->bRuleItems ? 1 : 0) | (S->bRuleThrow ? 2 : 0) | (S->bRuleMood ? 4 : 0) | (S->bRulePingRunners ? 8 : 0) | (S->bRulePingBoss ? 16 : 0) | (S->bQuarterFog ? 32 : 0) | (S->bCageSpectate ? 64 : 0) | (S->bRuleFriends ? 128 : 0);
	BossSpeedPct = FMath::Clamp(S->BossSpeedPct, 60, 160);
	MapSize = uint8(FMath::Clamp(S->MapSize, 0, 2));
	MissionLevel = uint8(FMath::Clamp(S->MissionLevel, 0, 2));
	ViewRule = uint8(FMath::Clamp(S->ViewRule, 0, 2));
	RoundStyle = uint8(FMath::Clamp(S->RoundStyle, 0, 1));
	RoundMap = BamtolMaps::RoundId(S->RoundMap);   // 술래잡기 맵 = 작은 맵 4개 (2026-10-10)
	// 방 규칙 전체 (2026-10-12 표, BamtolRoomRules.h): 방장 설정 → 값 묶음 (+ 시험 cvar Bamtol.Rule) → 코드 한 줄로 복제
	//   오늘의 도전 판은 모두 같은 조건이어야 하니 새 규칙(표의 Rec 칸 = 기록에 영향)은 기본값으로
	BamtolRules::FSet Set = BamtolRules::FromSettings(S);
	BamtolRules::ApplyTestOverride(Set);
	if (BamtolDaily::IsActive())
	{
		const TArray<BamtolRules::FRule>& All = BamtolRules::All();
		for (int32 i = 0; i < All.Num() && i < Set.Num(); ++i)
		{
			Set[i] = All[i].bRec ? All[i].Default : Set[i];
		}
	}
	// 깜짝 규칙 (2026-10-12): 판 맵에서 한 번 뽑아 이 판에만 덮음 — 방 코드·기록 제외 판정은 방장 규칙(RuleBaseSet) 그대로
	//   시험 판(cvar Bamtol.Title=0 또는 Bamtol.Rule 지정)은 무작위로 안 뽑음 — cvar Bamtol.Surprise=<Id> 로만
	if (!bSurprisePicked && !bLobby && !bTitle && !bPlayground)
	{
		bSurprisePicked = true;
		static FName GLastSurprise;
		static const IConsoleVariable* TitleCv = IConsoleManager::Get().FindConsoleVariable(TEXT("Bamtol.Title"));
		static const IConsoleVariable* ForceCv = IConsoleManager::Get().FindConsoleVariable(TEXT("Bamtol.Surprise"));
		static const IConsoleVariable* RuleCv = IConsoleManager::Get().FindConsoleVariable(TEXT("Bamtol.Rule"));
		const FString Force = ForceCv ? ForceCv->GetString() : FString();
		const bool bTestRound = (TitleCv && TitleCv->GetInt() == 0) || (RuleCv && !RuleCv->GetString().IsEmpty());
		FName Pick = NAME_None;
		if (!BamtolDaily::IsActive())
		{
			if (!Force.IsEmpty())
			{
				// 시험 지정: 그 후보만 남긴 것처럼 (규칙 끔·방장 우선·같이 못 나오는 조합은 그대로 지킴)
				const FName Want(*Force);
				for (int32 i = 0; i < 64 && Pick != Want && BamtolRules::FindSurprise(Want); ++i)
				{
					Pick = BamtolRules::PickSurprise(Set, NAME_None, (i + 0.5f) / 64.f);
				}
				Pick = Pick == Want ? Want : NAME_None;
			}
			else if (!bTestRound)
			{
				Pick = BamtolRules::PickSurprise(Set, GLastSurprise, FMath::FRand());
			}
		}
		GLastSurprise = Pick;
		SurpriseId = Pick;
		UE_LOG(LogTemp, Display, TEXT("[Rules] 깜짝 규칙: %s%s"), Pick.IsNone() ? TEXT("없음") : *Pick.ToString(),
			BamtolRules::Of(Set, TEXT("Surprise")) == 0 ? TEXT(" (방 규칙 끔)") : bTestRound && Force.IsEmpty() ? TEXT(" (시험 판)") : TEXT(""));
	}
	BamtolRules::FSet Live = Set;
	BamtolRules::ApplySurprise(SurpriseId, Live);
	if (const BamtolRules::FRule* SpeedRule = SurpriseId.IsNone() ? nullptr : BamtolRules::Find(TEXT("BossSpeed")))
	{
		// 옛 규칙 중 깜짝 규칙이 건드리는 것은 대장 속도뿐 — 그 값은 옛 복제 칸으로 읽히니 여기서 맞춤 (다른 옛 규칙은 깜짝 후보에 넣지 말 것)
		BossSpeedPct = FMath::Clamp(BamtolRules::StepValue(*SpeedRule, BamtolRules::Of(Live, TEXT("BossSpeed"))), 60, 160);
	}
	if (Live != RuleSet || Set != RuleBaseSet || RuleCode.IsEmpty())
	{
		RuleSet = MoveTemp(Live);
		RuleBaseSet = MoveTemp(Set);
		++RuleSerial;
		RuleCode = BamtolRules::EncodeSet(RuleBaseSet);
		UE_LOG(LogTemp, Display, TEXT("[Rules] 방 규칙 %s%s%s"), *RuleCode, BamtolRules::NoRecord(RuleBaseSet) ? TEXT(" (기록 제외)") : TEXT(""),
			*(RuleSet == BamtolRules::Defaults() ? FString(TEXT(" 기본")) : FString(TEXT(" — ")) + FString::Join(BamtolRules::Badges(RuleSet), TEXT(" · "))));
	}
}

void ABamtolGameState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	BamtolDaily::SetDayShift(DayShift);   // 새 방은 0 (손님은 복제로 곧 맞춤)
}

void ABamtolGameState::EndPlay(const EEndPlayReason::Type Reason)
{
	BamtolDaily::SetDayShift(0);   // 방을 나가면 넘긴 날을 지움 — 오늘의 도전·다른 방에 안 새게
	Super::EndPlay(Reason);
}

void ABamtolGameState::OnRep_DayShift() { BamtolDaily::SetDayShift(DayShift); }

void ABamtolGameState::AdvanceDay()
{
	if (!HasAuthority()) return;
	++DayShift;
	BamtolDaily::SetDayShift(DayShift);
	ForceNetUpdate();
}

void ABamtolGameState::OnRep_RuleCode()
{
	// 손님: 코드를 풀어 방 규칙 전체를 읽음 (늦게 들어와도 같은 값) + 이 판 깜짝 규칙을 덮음
	BamtolRules::FSet Set;
	if (BamtolRules::DecodeSet(RuleCode, Set))
	{
		BamtolRules::FSet Live = Set;
		BamtolRules::ApplySurprise(SurpriseId, Live);
		if (Live != RuleSet || Set != RuleBaseSet)
		{
			RuleSet = MoveTemp(Live);
			RuleBaseSet = MoveTemp(Set);
			++RuleSerial;
			UE_LOG(LogTemp, Display, TEXT("[Rules] 손님: 방 규칙 %s 받음 · 깜짝 규칙 %s"), *RuleCode, SurpriseId.IsNone() ? TEXT("없음") : *SurpriseId.ToString());
		}
	}
}
