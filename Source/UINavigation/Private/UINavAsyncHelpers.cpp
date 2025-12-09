// Copyright (C) 2023 Gonçalo Marques - All Rights Reserved

#include "UINavAsyncHelpers.h"
#include "UINavAsyncWidgetManager.h"
#include "UINavPCComponent.h"
#include "UINavMacros.h"
#include "Engine/World.h"

FGuid UUINavAsyncHelpers::LoadUINavWidgetAsync(
	UObject* WorldContext,
	TSoftClassPtr<UUINavWidget> WidgetClass,
	bool bRemoveParent,
	bool bDestroyParent,
	int32 ZOrder,
	int32 Priority)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		UINAV_LOG("LoadUINavWidgetAsync: Failed to get AsyncWidgetManager instance");
		return FGuid();
	}

	// 创建空的回调委托
	FOnWidgetLoaded OnSuccess;
	FOnWidgetLoadFailed OnFailure;

	return AsyncManager->LoadWidgetAsync(
		WidgetClass,
		OnSuccess,
		OnFailure,
		bRemoveParent,
		bDestroyParent,
		ZOrder,
		Priority
	);
}

FGuid UUINavAsyncHelpers::LoadUINavWidgetAsyncWithCallbacks(
	UObject* WorldContext,
	TSoftClassPtr<UUINavWidget> WidgetClass,
	const FUINavAsyncLoadCompleted& OnLoadCompleted,
	const FUINavAsyncLoadFailed& OnLoadFailed,
	bool bRemoveParent,
	bool bDestroyParent,
	int32 ZOrder,
	int32 Priority)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		UINAV_LOG("LoadUINavWidgetAsyncWithCallbacks: Failed to get AsyncWidgetManager instance");
		OnLoadFailed.ExecuteIfBound(TEXT("Failed to get AsyncWidgetManager instance"));
		return FGuid();
	}

	// 转换回调委托
	FOnWidgetLoaded OnSuccess;
	FOnWidgetLoadFailed OnFailure;
	
	// 绑定Lambda来转换委托调用
	if (OnLoadCompleted.IsBound())
	{
		OnSuccess.BindLambda([OnLoadCompleted](UUINavWidget* Widget)
		{
			OnLoadCompleted.ExecuteIfBound(Widget);
		});
	}

	if (OnLoadFailed.IsBound())
	{
		OnFailure.BindLambda([OnLoadFailed](const FString& ErrorMessage)
		{
			OnLoadFailed.ExecuteIfBound(ErrorMessage);
		});
	}

	return AsyncManager->LoadWidgetAsync(
		WidgetClass,
		OnSuccess,
		OnFailure,
		bRemoveParent,
		bDestroyParent,
		ZOrder,
		Priority
	);
}

FGuid UUINavAsyncHelpers::PreloadUINavWidget(
	UObject* WorldContext,
	TSoftClassPtr<UUINavWidget> WidgetClass,
	int32 Priority)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		UINAV_LOG("PreloadUINavWidget: Failed to get AsyncWidgetManager instance");
		return FGuid();
	}

	// 预加载不需要显示Widget，所以使用空回调
	FOnWidgetLoaded OnSuccess;
	OnSuccess.BindLambda([](UUINavWidget* Widget)
	{
		// 预加载完成，但不显示
		if (Widget)
		{
			UINAV_LOG("PreloadUINavWidget: Widget %s preloaded successfully", 
				*Widget->GetClass()->GetName());
			// 立即隐藏Widget，因为这只是预加载
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	});

	FOnWidgetLoadFailed OnFailure;
	OnFailure.BindLambda([](const FString& Error)
	{
		UINAV_LOG("PreloadUINavWidget: Failed to preload widget - %s", *Error);
	});

	return AsyncManager->LoadWidgetAsync(
		WidgetClass,
		OnSuccess,
		OnFailure,
		false, // bRemoveParent
		false, // bDestroyParent
		0,     // ZOrder
		Priority
	);
}

bool UUINavAsyncHelpers::CancelUINavWidgetLoad(
	UObject* WorldContext,
	const FGuid& RequestId)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		UINAV_LOG("CancelUINavWidgetLoad: Failed to get AsyncWidgetManager instance");
		return false;
	}

	return AsyncManager->CancelLoadRequest(RequestId);
}

int32 UUINavAsyncHelpers::CancelAllUINavWidgetLoads(
	UObject* WorldContext)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		UINAV_LOG("CancelAllUINavWidgetLoads: Failed to get AsyncWidgetManager instance");
		return 0;
	}

	return AsyncManager->CancelAllLoadRequests();
}

bool UUINavAsyncHelpers::IsUINavWidgetLoading(
	UObject* WorldContext,
	TSoftClassPtr<UUINavWidget> WidgetClass)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		return false;
	}

	return AsyncManager->IsWidgetLoading(WidgetClass);
}

void UUINavAsyncHelpers::GetAsyncLoadStatistics(
	UObject* WorldContext,
	int32& TotalRequests,
	int32& ActiveRequests,
	int32& PendingRequests,
	int32& CompletedRequests,
	int32& FailedRequests,
	int32& CancelledRequests)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		TotalRequests = ActiveRequests = PendingRequests = 0;
		CompletedRequests = FailedRequests = CancelledRequests = 0;
		return;
	}

	ActiveRequests = AsyncManager->GetActiveLoadRequestCount();
	PendingRequests = AsyncManager->GetPendingLoadRequestCount();
	AsyncManager->GetLoadStatistics(TotalRequests, CompletedRequests, FailedRequests, CancelledRequests);
}

void UUINavAsyncHelpers::SetAsyncLoadSettings(
	UObject* WorldContext,
	int32 MaxConcurrentLoads,
	float DefaultTimeout)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		UINAV_LOG("SetAsyncLoadSettings: Failed to get AsyncWidgetManager instance");
		return;
	}

	if (MaxConcurrentLoads > 0)
	{
		AsyncManager->SetMaxConcurrentLoads(MaxConcurrentLoads);
	}

	if (DefaultTimeout > 0.0f)
	{
		AsyncManager->SetLoadTimeout(DefaultTimeout);
	}
}

void UUINavAsyncHelpers::ClearAsyncLoadCache(
	UObject* WorldContext)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		UINAV_LOG("ClearAsyncLoadCache: Failed to get AsyncWidgetManager instance");
		return;
	}

	AsyncManager->ClearCache();
}

void UUINavAsyncHelpers::PrintAsyncLoadDebugInfo(
	UObject* WorldContext)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		UINAV_LOG("PrintAsyncLoadDebugInfo: Failed to get AsyncWidgetManager instance");
		return;
	}

	AsyncManager->PrintDebugInfo();
}

FGuid UUINavAsyncHelpers::LoadUINavWidgetAsyncWithEvents(
	UObject* WorldContext,
	TSoftClassPtr<UUINavWidget> WidgetClass,
	const FOnWidgetLoaded& OnLoadCompleted,
	const FOnWidgetLoadFailed& OnLoadFailed,
	bool bRemoveParent,
	bool bDestroyParent,
	int32 ZOrder,
	int32 Priority)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		UINAV_LOG("LoadUINavWidgetAsyncWithEvents: Failed to get AsyncWidgetManager instance");
		if (OnLoadFailed.IsBound())
		{
			OnLoadFailed.ExecuteIfBound(TEXT("Failed to get AsyncWidgetManager instance"));
		}
		return FGuid();
	}

	return AsyncManager->LoadWidgetAsync(
		WidgetClass,
		OnLoadCompleted,
		OnLoadFailed,
		bRemoveParent,
		bDestroyParent,
		ZOrder,
		Priority
	);
}

void UUINavAsyncHelpers::GetCacheStatistics(
	UObject* WorldContext,
	int32& CachedWidgetClasses,
	int32& TotalCacheSize)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		UINAV_LOG("GetCacheStatistics: Failed to get AsyncWidgetManager instance");
		CachedWidgetClasses = 0;
		TotalCacheSize = 0;
		return;
	}

	AsyncManager->GetCacheStatistics(CachedWidgetClasses, TotalCacheSize);
}

FGuid UUINavAsyncHelpers::PreloadUINavWidgetClass(
	UObject* WorldContext,
	TSoftClassPtr<UUINavWidget> WidgetClass,
	int32 Priority)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		UINAV_LOG("PreloadUINavWidgetClass: Failed to get AsyncWidgetManager instance");
		return FGuid();
	}

	return AsyncManager->PreloadWidgetClass(WidgetClass, Priority);
}

bool UUINavAsyncHelpers::IsUINavWidgetClassCached(
	UObject* WorldContext,
	TSoftClassPtr<UUINavWidget> WidgetClass)
{
	UUINavAsyncWidgetManager* AsyncManager = UUINavAsyncWidgetManager::GetInstance(WorldContext);
	if (!AsyncManager)
	{
		return false;
	}

	return AsyncManager->IsWidgetClassCached(WidgetClass);
}

UUINavAsyncWidgetManager* UUINavAsyncHelpers::GetAsyncWidgetManager(
	UObject* WorldContext)
{
	if (!WorldContext)
	{
		return nullptr;
	}

	return UUINavAsyncWidgetManager::GetInstance(WorldContext);
}
