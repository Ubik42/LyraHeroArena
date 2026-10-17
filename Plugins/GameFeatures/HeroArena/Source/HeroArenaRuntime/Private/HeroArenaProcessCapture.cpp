// Opt-in native viewport capture for a separate client. Never active in Shipping.
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "FrameGrabber.h"
#include "Slate/SceneViewport.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

#if !UE_BUILD_SHIPPING
namespace HeroArenaProcessCapture
{
    TUniquePtr<FFrameGrabber> Grabber;
    FDelegateHandle EndFrame;
    FString Folder;
    double Start = 0, Last = 0;
    int32 Count = 0;
    void Drain()
    {
        for (FCapturedFrameData& Frame : Grabber->GetCapturedFrames())
        {
            auto& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
            auto Image = Images.CreateImageWrapper(EImageFormat::JPEG);
            if (Image && Image->SetRaw(Frame.ColorBuffer.GetData(), Frame.ColorBuffer.Num() * sizeof(FColor),
                Frame.BufferSize.X, Frame.BufferSize.Y, ERGBFormat::BGRA, 8))
            {
                const auto& Bytes = Image->GetCompressed(85);
                const FString Path = Folder / FString::Printf(TEXT("f%06d_t%.3f.jpg"), Count++, FPlatformTime::Seconds() - Start);
                FFileHelper::SaveArrayToFile(Bytes, *Path);
            }
        }
    }
    void Tick()
    {
        if (!Grabber) return;
        const double Now = FPlatformTime::Seconds();
        if (Now - Last >= 1.0 / 15.0) { Grabber->CaptureThisFrame(FFramePayloadPtr()); Last = Now; }
        Drain();
    }
}
#endif

bool StartHeroArenaProcessCapture(const FString& Directory)
{
#if !UE_BUILD_SHIPPING
    using namespace HeroArenaProcessCapture;
    if (Grabber || !GEngine || !GEngine->GameViewport) return false;
    const auto Viewport = GEngine->GameViewport->GetGameViewport();
    if (!Viewport || Viewport->GetSizeXY().X <= 0) return false;
    Folder = Directory;
    IFileManager::Get().MakeDirectory(*Folder, true);
    // GameViewport owns this viewport until engine shutdown; stop before module teardown.
    TSharedRef<FSceneViewport> ViewportRef = MakeShareable(Viewport, [](FSceneViewport*) {});
    Grabber = MakeUnique<FFrameGrabber>(ViewportRef, Viewport->GetSizeXY(), PF_B8G8R8A8, 3);
    Start = FPlatformTime::Seconds(); Last = 0; Count = 0;
    Grabber->StartCapturingFrames();
    EndFrame = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
    UE_LOG(LogTemp, Display, TEXT("HeroArena process capture started: wall_seconds=%.6f"), Start);
    return true;
#else
    return false;
#endif
}

void StopHeroArenaProcessCapture()
{
#if !UE_BUILD_SHIPPING
    using namespace HeroArenaProcessCapture;
    if (!Grabber) return;
    FCoreDelegates::OnEndFrame.Remove(EndFrame);
    Grabber->StopCapturingFrames(); Drain(); Grabber->Shutdown(); Grabber.Reset();
    UE_LOG(LogTemp, Display, TEXT("HeroArena process capture stopped: frames=%d duration=%.3f"), Count, FPlatformTime::Seconds()-Start);
#endif
}
