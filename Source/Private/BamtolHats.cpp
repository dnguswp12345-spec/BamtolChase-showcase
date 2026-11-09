#include "BamtolHats.h"
#include "HAL/IConsoleManager.h"
#include "BamtolRoundBrief.h"
#include "Components/HorizontalBox.h"
#include "BamtolLoc.h"
#include "BamtolAudio.h"

#include "BamtolBossAI.h"
#include "BamtolGameState.h"
#include "BamtolGround.h"
#include "BamtolHatFx.h"
#include "BamtolBoss.h"
#include "BamtolFootsteps.h"
#include "Net/UnrealNetwork.h"
#include "BamtolPlayer.h"
#include "BamtolPuffFx.h"
#include "Blueprint/WidgetTree.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Engine/Texture2D.h"
#include "BamtolHelpWidget.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

namespace
{
	FBamtolHatRow Row(EBamtolHatAbility A, const TCHAR* Name, const TCHAR* Ability, const TCHAR* Desc, float Value, float Cooldown, float Radius, bool bActive, const TCHAR* Detail)
	{
		FBamtolHatRow R;
		R.Ability = A;
		R.DisplayName = Name;
		R.AbilityName = Ability;
		R.Description = Desc;
		R.Value = Value;
		R.Cooldown = Cooldown;
		R.Radius = Radius;
		R.bActive = bActive;
		R.MaxPerMatch = 2;
		R.Detail = Detail;
		return R;
	}

	const TMap<FName, FBamtolHatRow>& Defaults()
	{
		// Data/Hats.csv 와 같게 유지 (표가 없을 때만 씀)
		static TMap<FName, FBamtolHatRow> M;
		if (M.Num() == 0)
		{
			M.Add(TEXT("Acorn"), Row(EBamtolHatAbility::Stamina, BTXT("도토리 모자"), BTXT("튼튼한 다리"), BTXT("달리면 → 기력이 30% 더 오래 가요"), 0.3f, 0.f, 0.f, false, BTXT("기력 막대가 길어져요. 벽 오르기도 더 오래 해요. 기력이 거의 떨어지면 모자가 반짝!")));
			M.Add(TEXT("MushroomRed"), Row(EBamtolHatAbility::Bouncy, BTXT("빨간 버섯"), BTXT("통통"), BTXT("점프하면 → 더 높이 퐁! 착지 때 통!"), 0.25f, 0.f, 0.f, false, BTXT("점프가 25% 높아요. 높은 데서 떨어지면 한 번 더 통 튀어요. 담·상자를 넘기 좋아요")));
			M.Add(TEXT("Pinecone"), Row(EBamtolHatAbility::Scatter, BTXT("솔방울"), BTXT("솔방울 뿌리기"), BTXT("[R] 뒤에 솔방울 3개 → 밟은 술래 미끄덩"), 2.5f, 18.f, 70.f, true, BTXT("솔방울은 잠깐만 남아요. 밟으면 옆으로 미끄러져요(기절은 아님). 친구도 미끄러져요! 술래는 점프하거나 옆으로 돌면 피해요")));
			M.Add(TEXT("Leaf"), Row(EBamtolHatAbility::Glider, BTXT("나뭇잎"), BTXT("바람 타기"), BTXT("공중에서 [Space] 꾹 → 4초 활강"), 0.6f, 0.f, 0.f, false, BTXT("높은 곳에서 뛰어내려 멀리 날아가요. 이 모자만 활강할 수 있어요")));
			M.Add(TEXT("BeetleHelm"), Row(EBamtolHatAbility::Shield, BTXT("장수풍뎅이 투구"), BTXT("껍질 방패"), BTXT("날아오는 것에 맞으면 → 한 번 팅! 막아요"), 0.f, 40.f, 0.f, false, BTXT("총·대포·덫·통나무·차·대장 능력에 날아가는 것을 한 번 막아요. 투구가 반짝이면 준비된 거예요. 잡히는 건 못 막아요")));
			M.Add(TEXT("ChestnutBurr"), Row(EBamtolHatAbility::Spiky, BTXT("밤송이"), BTXT("따끔"), BTXT("달려서 부딪히면 → 상대가 튕겨 나가요"), 700.f, 1.5f, 120.f, false, BTXT("친구와 꼬마 술래를 튕겨 내요. 술래 대장은 못 튕겨요")));
			M.Add(TEXT("MapleLeaf"), Row(EBamtolHatAbility::Dash, BTXT("단풍잎"), BTXT("빙글 대시"), BTXT("[R] 누르면 → 보는 쪽으로 휙 날아가요"), 1500.f, 14.f, 0.f, true, BTXT("틈·담을 뛰어넘거나 술래 손을 피할 때 써요. 물에서는 못 써요")));
			M.Add(TEXT("Raspberry"), Row(EBamtolHatAbility::Lure, BTXT("산딸기"), BTXT("미끼 던지기"), BTXT("[R] 던지면 → 그곳에 가짜 발소리·발자국"), 1400.f, 22.f, 7000.f, true, BTXT("산딸기가 8초 동안 뛰는 소리를 내요. 술래 눈에는 가짜 발자국이 보여요. 가까이서 보면 산딸기인 걸 들켜요")));
			M.Add(TEXT("SnailShell"), Row(EBamtolHatAbility::Hide, BTXT("달팽이 집"), BTXT("물건 흉내"), BTXT("[R] 누르면 → 10초 동안 근처 물건으로 변신"), 10.f, 20.f, 0.f, true, BTXT("천천히 움직일 수 있고 대장 감각·냄새에 안 걸려요. [Shift]로 풀면 쏙 빨라져요. 움직이는 물건은 수상해 보여요")));
			M.Add(TEXT("FlowerCrown"), Row(EBamtolHatAbility::Cheer, BTXT("꽃 화관"), BTXT("응원"), BTXT("내 곁에 있으면 → 친구 기력이 빨리 차요"), 1.8f, 0.f, 1400.f, false, BTXT("꽃잎 원 안의 친구와 나는 기력이 1.8배 빨리 차요. 응원 받는 친구 머리 위에 꽃이 떠요")));
			M.Add(TEXT("BirdNest"), Row(EBamtolHatAbility::Scout, BTXT("새둥지"), BTXT("정찰 새"), BTXT("30초마다 → 새가 술래 쪽을 알려 줘요"), 3.f, 30.f, 0.f, false, BTXT("새가 날아올라 3초 동안 술래 위치를 보여 줘요. 날아오른 새는 술래 눈에도 보여요")));
			M.Add(TEXT("Bellflower"), Row(EBamtolHatAbility::Bell, BTXT("도라지꽃"), BTXT("종소리 경보"), BTXT("술래가 다가오면 → 딸랑! 방향 알림"), 1500.f, 12.f, 2000.f, false, BTXT("술래가 20m 안에 오면 울려요. 15m 안 친구에게도 알려 줘요. 종소리는 술래도 들어요")));
			M.Add(TEXT("MotoHelmet"), Row(EBamtolHatAbility::None, BTXT("오토바이 헬멧"), TEXT(""), BTXT("부릉부릉! 바람을 가르는 풀페이스"), 0.f, 0.f, 0.f, false, TEXT("")));
			M.Add(TEXT("KnightHelm"), Row(EBamtolHatAbility::None, BTXT("기사 투구"), TEXT(""), BTXT("중세 기사처럼 번쩍! 깃털 장식은 덤"), 0.f, 0.f, 0.f, false, TEXT("")));
			M.Add(TEXT("Snapback"), Row(EBamtolHatAbility::None, BTXT("스냅백"), TEXT(""), BTXT("요즘 힙한 챙 납작 모자"), 0.f, 0.f, 0.f, false, TEXT("")));
			M.Add(TEXT("Beanie"), Row(EBamtolHatAbility::None, BTXT("비니"), TEXT(""), BTXT("폭신폭신 겨울 털모자, 방울 달림"), 0.f, 0.f, 0.f, false, TEXT("")));
			M.Add(TEXT("BucketHat"), Row(EBamtolHatAbility::None, BTXT("버킷햇"), TEXT(""), BTXT("동그란 챙이 귀여운 벙거지"), 0.f, 0.f, 0.f, false, TEXT("")));
			M.Add(TEXT("BackwardsCap"), Row(EBamtolHatAbility::None, BTXT("거꾸로 야구모자"), TEXT(""), BTXT("챙은 뒤로! 달릴 때 더 빨라 보여요"), 0.f, 0.f, 0.f, false, TEXT("")));
			M.Add(TEXT("Headphones"), Row(EBamtolHatAbility::None, BTXT("헤드폰"), TEXT(""), BTXT("음악이 없어도 리듬을 타요"), 0.f, 0.f, 0.f, false, TEXT("")));
			M.Add(TEXT("Crown"), Row(EBamtolHatAbility::None, BTXT("왕관"), TEXT(""), BTXT("오늘은 내가 산의 왕!"), 0.f, 0.f, 0.f, false, TEXT("")));
			M.Add(TEXT("PirateHat"), Row(EBamtolHatAbility::None, BTXT("해적 모자"), TEXT(""), BTXT("해골 무늬 삼각 모자, 부끄러움은 바다에 버렸어요"), 0.f, 0.f, 0.f, false, TEXT("")));
			M.Add(TEXT("WizardHat"), Row(EBamtolHatAbility::None, BTXT("마법사 모자"), TEXT(""), BTXT("끝이 휘어진 뾰족 모자, 별이 반짝"), 0.f, 0.f, 0.f, false, TEXT("")));
		}
		return M;
	}
}

const TArray<FName>& BamtolHats::Ids()
{
	// ABamtolPlayer::HatChoices 순서와 같음. 2026-10-08: 4번 WalnutShell → BeetleHelm (옛 저장값은 BamtolHatLook::Migrate), 11번 Bellflower 추가
	static const TArray<FName> I = { TEXT("Acorn"), TEXT("MushroomRed"), TEXT("Pinecone"), TEXT("Leaf"), TEXT("BeetleHelm"), TEXT("ChestnutBurr"),
		TEXT("MapleLeaf"), TEXT("Raspberry"), TEXT("SnailShell"), TEXT("FlowerCrown"), TEXT("BirdNest"), TEXT("Bellflower"),
		// 2026-11-03 (13.795) 꾸미기 전용 모자 (능력 없음, 치장 표 Hat_<Id> 로 사고 입음)
		TEXT("MotoHelmet"), TEXT("KnightHelm"), TEXT("Snapback"), TEXT("Beanie"), TEXT("BucketHat"), TEXT("BackwardsCap"), TEXT("Headphones"), TEXT("Crown"), TEXT("PirateHat"), TEXT("WizardHat") };
	return I;
}

const FBamtolHatRow* BamtolHats::Find(FName Id)
{
	static TSoftObjectPtr<UDataTable> Table(FSoftObjectPath(TEXT("/Game/Bamtol/Data/DT_BamtolHats.DT_BamtolHats")));
	if (const UDataTable* T = Table.LoadSynchronous())
	{
		if (const FBamtolHatRow* R = T->FindRow<FBamtolHatRow>(Id, TEXT("BamtolHat"), false))
		{
			return R;
		}
	}
	return Defaults().Find(Id);
}

static TAutoConsoleVariable<int32> CVarHatAbilities(TEXT("Bamtol.HatAbilities"), 0, TEXT("모자 능력 켜기 (기본 0 = 모자는 꾸미기만, 2026-11-03 사용자 결정). 옛 능력 시험·재사용용"));

bool BamtolHats::AbilitiesOn()
{
	return CVarHatAbilities.GetValueOnAnyThread() != 0;
}

FName BamtolHats::CosId(FName HatId)
{
	return FName(*(FString(TEXT("Hat_")) + HatId.ToString()));
}

int32 BamtolHats::IndexOfCos(FName InCosId)
{
	const FString S = InCosId.ToString();
	return S.StartsWith(TEXT("Hat_")) ? Ids().IndexOfByKey(FName(*S.Mid(4))) : -1;
}

FName BamtolHats::DefaultCosId()
{
	return CosId(TEXT("Acorn"));
}

// ============================ 고르기 창 ============================

void UBamtolHatButton::Setup()
{
	OnClicked.AddDynamic(this, &UBamtolHatButton::HandleClick);
	OnHovered.AddDynamic(this, &UBamtolHatButton::HandleHover);
}

void UBamtolHatButton::HandleHover()
{
	UBamtolAudio::Play(this, TEXT("Hover"));
	if (UBamtolHatSelectWidget* W = Owner.Get())
	{
		W->Hover(Index);
	}
}

UTexture2D* UBamtolHatSelectWidget::HatIcon(int32 Index)
{
	if (!BamtolHats::Ids().IsValidIndex(Index))
	{
		return nullptr;
	}
	const FString Id = BamtolHats::Ids()[Index].ToString();
	return LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/Bamtol/UI/Icons/T_Hat_%s.T_Hat_%s"), *Id, *Id), nullptr, LOAD_NoWarn);
}

void UBamtolHatSelectWidget::Hover(int32 Index)
{
	Hovered = Index;
	Refresh();
}

void UBamtolHatButton::HandleClick()
{
	UBamtolAudio::Play(this, TEXT("Click"));
	if (UBamtolHatSelectWidget* W = Owner.Get())
	{
		W->Choose(Index);
	}
}

void UBamtolHatSelectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// WBP 에 칸이 없으면 기본 모양 (세로 목록)
	if (!HatList && WidgetTree)
	{
		HatList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("HatList"));
		if (!WidgetTree->RootWidget)
		{
			WidgetTree->RootWidget = HatList;
		}
	}
	if (!Title && WidgetTree && HatList)
	{
		Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
		HatList->AddChildToVerticalBox(Title);
	}
	if (!HatList)
	{
		return;
	}
	// 카드 격자: 그림 + 이름 + 능력 이름, 마우스 올리면 아래에 설명
	// 2026-10-08 모자 12개 → 6칸 × 2줄 (빈칸 없이, 아래 게임 화면이 더 보이게). 개수가 바뀌면 빈칸 없는 칸 수로
	// "이번 판" 한 줄 (2026-10-12): 모자를 고를 이유가 판마다 달라지게 — 대장·숲 친구·맵 사건·깜짝 규칙·날씨 (아이콘 + 짧은 글)
	Brief = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RoundBrief"));
	if (UVerticalBoxSlot* BS = HatList->AddChildToVerticalBox(Brief))
	{
		BS->SetHorizontalAlignment(HAlign_Center);
		BS->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	}
	const int32 NumCards = BamtolHats::Ids().Num();
	const int32 Cols = NumCards % 6 == 0 ? 6 : (NumCards % 4 == 0 ? 4 : 6);
	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("HatGrid"));
	Grid->SetSlotPadding(FMargin(6.f));
	if (UVerticalBoxSlot* GS = HatList->AddChildToVerticalBox(Grid))
	{
		GS->SetPadding(FMargin(0.f, 10.f, 0.f, 6.f));
		GS->SetHorizontalAlignment(HAlign_Center);
	}
	const TArray<FName>& Ids = BamtolHats::Ids();
	for (int32 i = 0; i < Ids.Num(); ++i)
	{
		UBamtolHatButton* B = WidgetTree->ConstructWidget<UBamtolHatButton>(UBamtolHatButton::StaticClass());
		B->Index = i;
		B->Owner = this;
		B->Setup();
		if (BamtolUI::FrameTex(TEXT("UI_Card")))
		{
			// 양피지 카드 그림 (색은 BackgroundColor 로 살짝 물들임)
			FButtonStyle St = B->GetStyle();
			St.Normal = BamtolUI::Frame(TEXT("UI_Card"), FMargin(0.13f, 0.12f, 0.13f, 0.12f), FLinearColor::White, 0.55f);
			St.Hovered = St.Normal;
			St.Pressed = St.Normal;
			St.NormalPadding = FMargin(24.f, 22.f, 24.f, 26.f);
			St.PressedPadding = FMargin(24.f, 24.f, 24.f, 24.f);
			B->SetStyle(St);
		}
		else
		{
			BamtolUI::RoundButton(B, 16.f);
		}
		B->SetBackgroundColor(FLinearColor::White);
		UVerticalBox* Card = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		IconBox->SetWidthOverride(104.f);
		IconBox->SetHeightOverride(104.f);
		UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		if (UTexture2D* T = HatIcon(i))
		{
			Icon->SetBrushFromTexture(T);
		}
		else
		{
			Icon->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.f));
		}
		IconBox->AddChild(Icon);
		if (UVerticalBoxSlot* S = Card->AddChildToVerticalBox(IconBox))
		{
			S->SetHorizontalAlignment(HAlign_Center);
		}
		UTextBlock* L = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo F = L->GetFont();
		F.Size = 15;
		L->SetFont(F);
		L->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* S = Card->AddChildToVerticalBox(L))
		{
			S->SetHorizontalAlignment(HAlign_Center);
		}
		// 딱지 (2026-10-12): "[R] 누르기"(주황) / "늘 켜짐"(풀색) — 쓰는 법이 한눈에
		UTextBlock* Tag = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo TF = Tag->GetFont();
		TF.Size = 12;
		Tag->SetFont(TF);
		Tag->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* S = Card->AddChildToVerticalBox(Tag))
		{
			S->SetHorizontalAlignment(HAlign_Center);
			S->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
		}
		Tags.Add(Tag);
		B->AddChild(Card);
		if (UUniformGridSlot* S = Grid->AddChildToUniformGrid(B, i / Cols, i % Cols))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
		}
		Labels.Add(L);
		Cards.Add(B);
	}
	Desc = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Desc"));
	FSlateFontInfo DF = Desc->GetFont();
	DF.Size = 18;
	Desc->SetFont(DF);
	Desc->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* DS = HatList->AddChildToVerticalBox(Desc))
	{
		DS->SetHorizontalAlignment(HAlign_Center);
	}
	// 자세한 설명 (2026-10-12: 작게, 한 줄 설명 아래) — 길면 줄 바꿈
	DescMore = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DescMore"));
	FSlateFontInfo MF = DescMore->GetFont();
	MF.Size = 13;
	DescMore->SetFont(MF);
	DescMore->SetJustification(ETextJustify::Center);
	// (AutoWrapText 를 같이 켜면 가운데 정렬 칸에서 폭이 0 으로 접혀 한 글자씩 줄 바꿈됨 — WrapTextAt 만)
	DescMore->SetWrapTextAt(900.f);
	if (UVerticalBoxSlot* MS = HatList->AddChildToVerticalBox(DescMore))
	{
		MS->SetHorizontalAlignment(HAlign_Center);
		MS->SetPadding(FMargin(0.f, 3.f, 0.f, 0.f));
	}
	Refresh();
}

void UBamtolHatSelectWidget::Refresh()
{
	const ABamtolPlayer* Me = Cast<ABamtolPlayer>(GetOwningPlayerPawn());
	TMap<int32, int32> Count;
	for (TActorIterator<ABamtolPlayer> It(GetWorld()); It; ++It)
	{
		Count.FindOrAdd(It->GetLookHat())++;
	}
	const TArray<FName>& Ids = BamtolHats::Ids();
	for (int32 i = 0; i < Labels.Num() && i < Ids.Num(); ++i)
	{
		const FBamtolHatRow* R = BamtolHats::Find(Ids[i]);
		if (!R)
		{
			continue;
		}
		const bool bMine = Me && Me->GetLookHat() == i;
		const int32 N = Count.FindRef(i);
		const bool bFull = N >= R->MaxPerMatch && !bMine;
		Labels[i]->SetText(FText::FromString(BamtolLoc::T(R->DisplayName) + TEXT("\n") + BamtolLoc::T(R->AbilityName)));
		if (Tags.IsValidIndex(i) && Tags[i])
		{
			Tags[i]->SetText(FText::FromString(BamtolHatFx::Tag(*R)));
			Tags[i]->SetColorAndOpacity(FSlateColor(R->bActive ? FLinearColor(0.80f, 0.33f, 0.02f) : FLinearColor(0.16f, 0.42f, 0.14f)));
		}
		const bool bFrame = BamtolUI::FrameTex(TEXT("UI_Card")) != nullptr;
		Labels[i]->SetColorAndOpacity(FSlateColor(bFrame ? (bMine ? FLinearColor(0.55f, 0.25f, 0.f) : BamtolUI::Ink())
			: (bMine ? FLinearColor(1.f, 0.85f, 0.3f) : (bFull ? FLinearColor(0.45f, 0.45f, 0.45f) : FLinearColor::White))));
		if (Cards.IsValidIndex(i))
		{
			// 내 모자: 금색, 꽉 참: 흐리게, 마우스 올림: 밝게
			Cards[i]->SetBackgroundColor(bFrame ? (bMine ? FLinearColor(1.f, 0.78f, 0.35f) : (i == Hovered ? FLinearColor(1.f, 1.f, 0.9f) : FLinearColor(0.92f, 0.9f, 0.85f)))
				: (bMine ? FLinearColor(0.85f, 0.6f, 0.15f, 1.f) : (i == Hovered ? FLinearColor(0.35f, 0.28f, 0.18f, 1.f) : FLinearColor(0.16f, 0.12f, 0.08f, 0.95f))));
			Cards[i]->SetRenderOpacity(bFull ? 0.45f : 1.f);
			Cards[i]->SetRenderScale(FVector2D(i == Hovered ? 1.07f : 1.f));
		}
	}
	if (Desc && BamtolUI::FrameTex(TEXT("UI_Card")) && !bEmbedded)
	{
		Desc->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.96f, 0.85f)));
		Desc->SetShadowOffset(FVector2D(2.f, 2.f));
		Desc->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
	}
	if (Desc)
	{
		const int32 Show = Hovered >= 0 ? Hovered : (Me ? Me->GetLookHat() : -1);
		const FBamtolHatRow* R = BamtolHats::Ids().IsValidIndex(Show) ? BamtolHats::Find(BamtolHats::Ids()[Show]) : nullptr;
		// 한 줄: "이름 — 무엇을 하면 → 무슨 일이", 그 아래 작게: 딱지 · 쿨다운 · 자세한 설명 (2026-10-12)
		Desc->SetText(FText::FromString(R ? BamtolLoc::Fmt(TEXT("%s — %s"), *R->DisplayName, *R->Description) : FString()));
		if (DescMore)
		{
			FString More;
			if (R)
			{
				const FString Cd = BamtolHatFx::CooldownLine(*R);
				More = BamtolHatFx::Tag(*R) + (Cd.IsEmpty() ? FString() : TEXT(" · ") + Cd) + (R->Detail.IsEmpty() ? FString() : TEXT("   ") + BamtolLoc::T(R->Detail));
			}
			DescMore->SetText(FText::FromString(More));
			if (bEmbedded)
			{
				DescMore->SetColorAndOpacity(FSlateColor(BamtolUI::Ink()));   // 꾸미기 창 밝은 판 위
			}
			else
			{
				DescMore->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.93f, 0.78f, 0.92f)));
				DescMore->SetShadowOffset(FVector2D(1.f, 1.f));
				DescMore->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
			}
		}
	}
	BamtolBrief::Fill(WidgetTree, Brief, GetWorld(), BriefSig);
	if (Title)
	{
		const ABamtolGameState* GS = GetWorld()->GetGameState<ABamtolGameState>();
		Title->SetText(FText::FromString(BamtolLoc::Fmt(TEXT("모자를 골라요   %.0f"), GS ? FMath::Max(GS->HatPickLeft, 0.f) : 0.f)));
		Title->SetJustification(ETextJustify::Center);
		if (bEmbedded && GS && GS->bLobby)
		{
			Title->SetText(FText::FromString(BTXT("모자 모양 — 모자마다 능력이 달라요 (판 준비 시간에도 바꿀 수 있어요)")));   // 대기실엔 시계가 안 감
		}
		if (bEmbedded)
		{
			Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.2f, 0.05f)));
			if (Desc)
			{
				Desc->SetColorAndOpacity(FSlateColor(BamtolUI::Ink()));
			}
		}
	}
}

void UBamtolHatSelectWidget::Choose(int32 Index)
{
	if (ABamtolPlayer* Me = Cast<ABamtolPlayer>(GetOwningPlayerPawn()))
	{
		Me->RequestHat(Index);
		const ABamtolGameState* GS = GetWorld()->GetGameState<ABamtolGameState>();
		if (bEmbedded && GS && GS->bLobby)
		{
			return;   // 대기실 꾸미기 창 안: 창은 그대로 (다른 탭도 볼 수 있게)
		}
		// 고르면 바로 닫힘 (게임 화면이 보이게). 바꾸려면 [H]
		Me->ShowHatSelect(false);
		Me->ShowToast(BTXT("모자를 바꾸려면 [H] (준비 시간 동안)"), 2.5f);
	}
}

void UBamtolHatSelectWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshClock += InDeltaTime;
	if (RefreshClock > 0.25f)
	{
		RefreshClock = 0.f;
		Refresh();
	}
	// 고르기 시간이 끝나면 저절로 닫힘
	const ABamtolGameState* GS = GetWorld()->GetGameState<ABamtolGameState>();
	if (GS && GS->HatPickLeft == 0.f)
	{
		if (ABamtolPlayer* Me = Cast<ABamtolPlayer>(GetOwningPlayerPawn()))
		{
			Me->ShowHatSelect(false);
		}
	}
}

// ============================ 산딸기 미끼 ============================

ABamtolLure::ABamtolLure()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Mesh;
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Bamtol/Characters/Hats/SM_Hat_Raspberry.SM_Hat_Raspberry"), nullptr, LOAD_NoWarn))
	{
		Mesh->SetStaticMesh(M);
		Mesh->SetRelativeScale3D(FVector(1.8f));
	}
}

void ABamtolLure::Init(const FVector& Velocity, float NoiseRadius, float Seconds)
{
	Vel = Velocity;
	Radius = NoiseRadius;
	Life = Seconds;
	BamtolReact::Register(this, TEXT("Light"), [this](const BamtolReact::FHit& H) { return bLanded && Drift.Take(H); }, 40.f);
}

void ABamtolLure::SetThrower(ABamtolPlayer* P)
{
	Thrower = P;
}

void ABamtolLure::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABamtolLure, bLanded);
}

void ABamtolLure::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 가짜 발소리 (2026-10-12, 모든 화면 — 사람 술래도 속게): 떨어진 뒤 누가 뛰어다니는 것처럼 타닥타닥 + 발 먼지
	//   술래 화면엔 가짜 발자국도 (BamtolHatFx::LureTrail → HUD 흔적 보기), 도망자 화면엔 "미끼" 딱지
	if (bLanded && GetNetMode() != NM_DedicatedServer)
	{
		StepClock += DeltaSeconds;
		if (StepClock >= 0.27f)
		{
			StepClock = 0.f;
			bStepLeft = !bStepLeft;
			const float Ang = GetGameTimeSinceCreation() * 1.75f + (GetUniqueID() % 7);
			const FVector Foot = GetActorLocation() + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * 300.f;
			UBamtolAudio::PlayAt(this, TEXT("StepLeaf"), Foot, 0.9f, bStepLeft ? 1.25f : 1.4f);
			ABamtolPuffFx::Spawn(GetWorld(), Foot, 2, 14.f);
		}
	}
	if (!HasAuthority())
	{
		return;
	}
	if (!bLanded)
	{
		Vel.Z -= 980.f * DeltaSeconds;
		FVector To = GetActorLocation() + Vel * DeltaSeconds;
		// 2026-10-12: 날아가는 동안 땅 높이만 봐서 벽·지붕을 뚫던 것 — 벽이면 튕겨서 그 앞에 떨어지고, 지붕·마루면 그 위에 앉음
		if (BamtolReact::FixOn(16))
		{
			FHitResult Wl;
			FCollisionQueryParams WQ(SCENE_QUERY_STAT(BamtolLureWall), false, this);
			WQ.AddIgnoredActor(Thrower.Get());
			if (GetWorld()->LineTraceSingleByChannel(Wl, GetActorLocation(), To, ECC_Visibility, WQ) && !Cast<APawn>(Wl.GetActor()))
			{
				if (Wl.ImpactNormal.Z < 0.55f)
				{
					const FVector N = Wl.ImpactNormal.GetSafeNormal2D();
					Vel = FVector(Vel.X, Vel.Y, 0.f).MirrorByVector(N) * 0.2f + FVector(0.f, 0.f, FMath::Min(Vel.Z, 0.f));
					To = Wl.ImpactPoint + Wl.ImpactNormal * 12.f;
					UE_LOG(LogTemp, Display, TEXT("[Hat] 산딸기 미끼: 벽에 맞고 떨어짐 (%s)"), *GetNameSafe(Wl.GetActor()));
				}
				else if (Vel.Z < 0.f && !BamtolGroundAt(GetWorld(), FVector2D(Wl.ImpactPoint), Wl, Wl.ImpactPoint.Z + 30.f, Wl.ImpactPoint.Z - 30.f))
				{
					bLanded = true;
					SetActorLocation(Wl.ImpactPoint + FVector(0, 0, 10.f));
					NoiseClock = 100.f;
					return;
				}
			}
		}
		FHitResult G;
		if (BamtolGroundAt(GetWorld(), FVector2D(To), G, To.Z + 200.f, To.Z - 200.f) && To.Z <= G.ImpactPoint.Z + 10.f)
		{
			bLanded = true;
			SetActorLocation(G.ImpactPoint + FVector(0, 0, 10.f));
			NoiseClock = 100.f;
		}
		else
		{
			SetActorLocation(To);
			AddActorWorldRotation(FRotator(0.f, 400.f * DeltaSeconds, 0.f));
		}
		return;
	}
	Life -= DeltaSeconds;
	Drift.Tick(this, DeltaSeconds);   // 바람·쓸림에 굴러감
	NoiseClock += DeltaSeconds;
	if (NoiseClock >= 2.f)
	{
		NoiseClock = 0.f;
		BamtolReact::Noise(GetWorld(), GetActorLocation(), Radius, TEXT("Lure"), false, this);   // AI 대장 + 숲 친구·새 떼도 속음 (얽힘 표 Lure 줄)
		ABamtolPuffFx::Spawn(GetWorld(), GetActorLocation(), 3, 18.f);
	}
	// 판 기록: 술래가 속아서 8m 안까지 옴 (한 번)
	if (!bWorked && Thrower.IsValid())
	{
		for (TActorIterator<ABamtolBoss> It(GetWorld()); It; ++It)
		{
			if (FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FMath::Square(800.f))
			{
				bWorked = true;
				UBamtolHatStats::NoteWork(Thrower.Get());
				UE_LOG(LogTemp, Display, TEXT("[Hat] 산딸기 미끼에 술래가 옴: %s"), *It->GetName());
				break;
			}
		}
	}
	if (Life <= 0.f)
	{
		Destroy();
	}
}
