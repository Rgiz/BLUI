#include "BluPrivatePCH.h"

UBluEye::UBluEye(const class FObjectInitializer& PCIP)
	: Super(PCIP)
{

	Width = 800;
	Height = 600;

	bIsTransparent = false;

}

void UBluEye::init()
{

	browserSettings.universal_access_from_file_urls = STATE_ENABLED;
	browserSettings.file_access_from_file_urls = STATE_ENABLED;

	info.width = Width;
	info.height = Height;

	// Set transparant option
	info.SetAsWindowless(NULL, bIsTransparent);

	renderer = new RenderHandler(Width, Height, this);
	g_handler = new BrowserClient(renderer);
	browser = CefBrowserHost::CreateBrowserSync(info, g_handler.get(), "about:blank", browserSettings, NULL);

	// Setup JS event emitter
	g_handler->SetEventEmitter(&ScriptEventEmitter);

	UE_LOG(LogBlu, Log, TEXT("Component Initialized"));
	CefString str = *DefaultURL;
	UE_LOG(LogBlu, Log, TEXT("Loading URL: %s"), *DefaultURL);

	// Load the default URL
	LoadURL(DefaultURL);
	ResetTexture();

}

void UBluEye::ResetTexture()
{

	// Here we init the texture to its initial state
	DestroyTexture();

	// init the new Texture2D
	Texture = UTexture2D::CreateTransient(Width, Height);
	Texture->AddToRoot();
	Texture->UpdateResource();

	ResetMatInstance();

}

void UBluEye::DestroyTexture()
{
	// Here we destory the texture and its resource
	if (Texture)
	{
		Texture->RemoveFromRoot();

		if (Texture->Resource)
		{
			BeginReleaseResource(Texture->Resource);
			FlushRenderingCommands();
		}

		Texture->MarkPendingKill();
		Texture = nullptr;
	}
}

void UBluEye::TextureUpdate(const void *buffer)
{
	if (!browser || !bEnabled)
	{
		UE_LOG(LogBlu, Warning, TEXT("NO BROWSER ACCESS OR NOT ENABLED"))
		return;
	}

	if (Texture && Texture->Resource)
	{

		// Is our texture ready?
		auto ref = static_cast<FTexture2DResource*>(Texture->Resource)->GetTexture2DRHI();
		if (!ref)
		{
			UE_LOG(LogBlu, Warning, TEXT("NO REF"))
				return;
		}

		if (buffer == nullptr)
		{
			UE_LOG(LogBlu, Warning, TEXT("NO TEXTDATA"))
				return;
		}

		const size_t size = Width * Height * sizeof(uint32);

		// @TODO This is a bit heavy to keep reallocating/deallocating, but not a big deal. Maybe we can ping pong between buffers instead.
		TArray<uint32> ViewBuffer;
		ViewBuffer.Init(Width * Height);
		FMemory::Memcpy(ViewBuffer.GetData(), buffer, size);

		TextureDataPtr dataPtr = MakeShareable(new TextureData);

		dataPtr->SetRawData(Width, Height, sizeof(uint32), ViewBuffer);

		// Clean up from the per-render
		ViewBuffer.Empty();
		buffer = 0;

		ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER(
			TextureData,
			TextureDataPtr, ImageData, dataPtr,
			FTexture2DRHIRef, TargetTexture, ref,
			const size_t, DataSize, size,
			{
			uint32 stride = 0;
			void* MipData = GDynamicRHI->RHILockTexture2D(TargetTexture, 0, RLM_WriteOnly, stride, false);

			if (MipData)
			{
				FMemory::Memcpy(MipData, ImageData->GetRawBytesPtr(), ImageData->GetDataSize());
				GDynamicRHI->RHIUnlockTexture2D(TargetTexture, 0, false);
			}

			ImageData.Reset();
		});

	}
	else {
		UE_LOG(LogBlu, Warning, TEXT("no Texture or Texture->resource"))
	}

}

void UBluEye::ExecuteJS(const FString& code)
{
	CefString codeStr = *code;
	UE_LOG(LogBlu, Log, TEXT("Execute JS: %s"), *code)
	browser->GetMainFrame()->ExecuteJavaScript(codeStr, "", 0);
	// create an event function that can be called from JS
}

void UBluEye::LoadURL(const FString& newURL)
{

	// Check if we want to load a local file

	if (newURL.Contains(TEXT("blui://"), ESearchCase::IgnoreCase, ESearchDir::FromStart))
	{

		// Get the current working directory
		FString GameDir = FPaths::ConvertRelativePathToFull(FPaths::GameDir());

		// We're loading a local file, so replace the proto with our game directory path
		FString LocalFile = newURL.Replace(TEXT("blui://"), *GameDir, ESearchCase::IgnoreCase);

		// Now we use the file proto
		LocalFile = FString(TEXT("file:///")) + LocalFile;

		UE_LOG(LogBlu, Log, TEXT("Load Local File: %s"), *LocalFile)

		// Load it up 
		browser->GetMainFrame()->LoadURL(*LocalFile);

		return;

	}

	// Load as usual
	browser->GetMainFrame()->LoadURL(*newURL);

}

void UBluEye::TriggerMouseMove(const FVector2D& pos, const float scale)
{

	mouse_event.x = pos.X / scale;
	mouse_event.y = pos.Y / scale;

	browser->GetHost()->SendFocusEvent(true);
	browser->GetHost()->SendMouseMoveEvent(mouse_event, false);

}

void UBluEye::TriggerLeftClick(const FVector2D& pos, const float scale)
{

	mouse_event.x = pos.X / scale;
	mouse_event.y = pos.Y / scale;

	browser->GetHost()->SendMouseClickEvent(mouse_event, MBT_LEFT, false, 1);
	browser->GetHost()->SendMouseClickEvent(mouse_event, MBT_LEFT, true, 1);

}

void UBluEye::TriggerRightClick(const FVector2D& pos, const float scale)
{

	mouse_event.x = pos.X / scale;
	mouse_event.y = pos.Y / scale;

	browser->GetHost()->SendMouseClickEvent(mouse_event, MBT_RIGHT, false, 1);
	browser->GetHost()->SendMouseClickEvent(mouse_event, MBT_RIGHT, true, 1);

}

void UBluEye::KeyDown(FKeyEvent InKey)
{

	processKeyMods(InKey);
	processKeyCode(InKey);

	key_event.type = KEYEVENT_KEYDOWN;
	browser->GetHost()->SendKeyEvent(key_event);

}

void UBluEye::KeyUp(FKeyEvent InKey)
{

	processKeyMods(InKey);
	processKeyCode(InKey);

	key_event.type = KEYEVENT_KEYUP;
	browser->GetHost()->SendKeyEvent(key_event);

}

void UBluEye::KeyPress(FKeyEvent InKey)
{

	// Simply trigger down, then up key events
	KeyDown(InKey);
	KeyUp(InKey);

}

void UBluEye::processKeyCode(FKeyEvent InKey)
{
	key_event.native_key_code = InKey.GetKeyCode();
	key_event.windows_key_code = InKey.GetKeyCode();
}

void UBluEye::CharKeyPress(FCharacterEvent CharEvent)
{

	// Process keymods like usual
	processKeyMods(CharEvent);

	// Below char input needs some special treatment, se we can't use the normal key down/up methods

	key_event.windows_key_code = CharEvent.GetCharacter();
	key_event.native_key_code = CharEvent.GetCharacter();
	//key_event.type = KEYEVENT_KEYDOWN;
	browser->GetHost()->SendKeyEvent(key_event);
	key_event.type = KEYEVENT_CHAR;
	browser->GetHost()->SendKeyEvent(key_event);

	key_event.windows_key_code = CharEvent.GetCharacter();
	key_event.native_key_code = CharEvent.GetCharacter();
	// bits 30 and 31 should be always 1 for WM_KEYUP
	key_event.type = KEYEVENT_KEYUP;
	browser->GetHost()->SendKeyEvent(key_event);

}

void UBluEye::processKeyMods(FInputEvent InKey)
{

	int mods = 0;

	// Test alt
	if (InKey.IsAltDown())
	{
		mods |= cef_event_flags_t::EVENTFLAG_ALT_DOWN;
	}
	else
	// Test control
	if (InKey.IsControlDown())
	{
		mods |= cef_event_flags_t::EVENTFLAG_CONTROL_DOWN;
	} 
	else
	// Test shift
	if (InKey.IsShiftDown())
	{
		mods |= cef_event_flags_t::EVENTFLAG_SHIFT_DOWN;
	}

	key_event.modifiers = mods;

}

UTexture2D* UBluEye::GetTexture() const
{
	check(Texture)
	return Texture;
}

UMaterialInstanceDynamic* UBluEye::GetMaterialInstance() const
{
	return MaterialInstance;
}

void UBluEye::ResetMatInstance()
{
	if (!Texture || !BaseMaterial || TextureParameterName.IsNone())
	{
		return;
	}

	// Create material instance
	if (!MaterialInstance)
	{
		MaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, NULL);
		if (!MaterialInstance)
		{
			UE_LOG(LogBlu, Warning, TEXT("UI Material instance can't be created"));
			return;
		}
	}

	// Check again, we must have material instance
	if (!MaterialInstance)
	{
		UE_LOG(LogBlu, Error, TEXT("UI Material instance wasn't created"));
		return;
	}

	// Check we have desired parameter
	UTexture* Tex = nullptr;
	if (!MaterialInstance->GetTextureParameterValue(TextureParameterName, Tex))
	{
		UE_LOG(LogBlu, Warning, TEXT("UI Material instance Texture parameter not found"));
		return;
	}

	MaterialInstance->SetTextureParameterValue(TextureParameterName, GetTexture());
}

void UBluEye::BeginDestroy()
{
	if (browser)
	{
		browser->GetHost()->CloseBrowser(true);
	}
	
	DestroyTexture();
	Super::BeginDestroy();
}