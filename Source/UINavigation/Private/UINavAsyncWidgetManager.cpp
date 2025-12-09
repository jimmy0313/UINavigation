// Copyright (C) 2023 Gonçalo Marques - All Rights Reserved

#include "UINavAsyncWidgetManager.h"
#include "UINavPCComponent.h"
#include "UINavBlueprintFunctionLibrary.h"
#include "UINavMacros.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

// 静态实例初始化
UUINavAsyncWidgetManager* UUINavAsyncWidgetManager::Instance = nullptr;

UUINavAsyncWidgetManager::UUINavAsyncWidgetManager()
{
	// 设置默认值
	MaxConcurrentLoads = 3;
	LoadTimeoutSeconds = 30.0f;
	CleanupInterval = 5.0f;

	// 重置统计计数器
	TotalRequestCount = 0;
	CompletedRequestCount = 0;
	FailedRequestCount = 0;
	CancelledRequestCount = 0;
}

UUINavAsyncWidgetManager* UUINavAsyncWidgetManager::GetInstance(UObject* WorldContext)
{
	if (!Instance)
	{
		Instance = NewObject<UUINavAsyncWidgetManager>(GetTransientPackage());
		Instance->AddToRoot(); // 防止被垃圾回收
		
		// 设置世界上下文
		if (WorldContext)
		{
			Instance->WorldContext = WorldContext->GetWorld();
		}
		else if (GEngine && GEngine->GetCurrentPlayWorld())
		{
			Instance->WorldContext = GEngine->GetCurrentPlayWorld();
		}

		// 启动清理定时器
		if (Instance->WorldContext.IsValid())
		{
			Instance->WorldContext->GetTimerManager().SetTimer(
				Instance->CleanupTimerHandle,
				Instance,
				&UUINavAsyncWidgetManager::CleanupCompletedRequests,
				Instance->CleanupInterval,
				true
			);
		}

		UINAV_LOG("UINavAsyncWidgetManager instance created");
	}

	return Instance;
}

FGuid UUINavAsyncWidgetManager::LoadWidgetAsync(
	TSoftClassPtr<UUINavWidget> WidgetClass,
	const FOnWidgetLoaded& OnLoadCompleted,
	const FOnWidgetLoadFailed& OnLoadFailed,
	bool bRemoveParent,
	bool bDestroyParent,
	int32 ZOrder,
	int32 Priority)
{
	if (WidgetClass.IsNull())
	{
		UINAV_LOG("LoadWidgetAsync: Invalid widget class provided");
		if (OnLoadFailed.IsBound())
		{
			OnLoadFailed.ExecuteIfBound(TEXT("Invalid widget class provided"));
		}
		return FGuid();
	}

	// 创建新的加载请求
	FAsyncWidgetLoadRequest NewRequest;
	NewRequest.WidgetClass = WidgetClass;
	NewRequest.bRemoveParent = bRemoveParent;
	NewRequest.bDestroyParent = bDestroyParent;
	NewRequest.ZOrder = ZOrder;
	NewRequest.Priority = Priority;
	NewRequest.OnLoadCompleted = OnLoadCompleted;
	NewRequest.OnLoadFailed = OnLoadFailed;

	TotalRequestCount++;

	UINAV_LOG("LoadWidgetAsync: Requesting load for %s (ID: %s, Priority: %d)", 
		*WidgetClass.GetAssetName(), 
		*NewRequest.RequestId.ToString(), 
		Priority);

	// 如果有空闲槽位，直接开始加载
	if (ActiveRequests.Num() < MaxConcurrentLoads)
	{
		ActiveRequests.Add(NewRequest);
		StartLoadingWidget(NewRequest);
	}
	else
	{
		// 添加到等待队列并按优先级排序
		PendingRequests.Add(NewRequest);
		PendingRequests.Sort();
		
		UINAV_LOG("LoadWidgetAsync: Request queued (Queue size: %d)", PendingRequests.Num());
	}

	return NewRequest.RequestId;
}

bool UUINavAsyncWidgetManager::CancelLoadRequest(const FGuid& RequestId)
{
	if (!RequestId.IsValid())
	{
		return false;
	}

	// 检查活跃请求
	for (int32 i = 0; i < ActiveRequests.Num(); ++i)
	{
		if (ActiveRequests[i].RequestId == RequestId)
		{
			UINAV_LOG("CancelLoadRequest: Cancelling active request %s", *RequestId.ToString());
			
			// 取消Streamable句柄
			if (TSharedPtr<FStreamableHandle>* HandlePtr = ActiveHandles.Find(RequestId))
			{
				if (HandlePtr->IsValid())
				{
					(*HandlePtr)->CancelHandle();
				}
				ActiveHandles.Remove(RequestId);
			}

			// 清除超时定时器
			if (FTimerHandle* TimeoutHandle = TimeoutHandles.Find(RequestId))
			{
				if (WorldContext.IsValid())
				{
					WorldContext->GetTimerManager().ClearTimer(*TimeoutHandle);
				}
				TimeoutHandles.Remove(RequestId);
			}

			// 标记为已取消并移除
			ActiveRequests[i].bCancelled = true;
			CancelledRequestIds.Add(RequestId);
			ActiveRequests.RemoveAt(i);
			CancelledRequestCount++;

			// 处理下一个请求
			ProcessNextRequest();
			return true;
		}
	}

	// 检查等待队列
	for (int32 i = 0; i < PendingRequests.Num(); ++i)
	{
		if (PendingRequests[i].RequestId == RequestId)
		{
			UINAV_LOG("CancelLoadRequest: Cancelling pending request %s", *RequestId.ToString());
			
			PendingRequests[i].bCancelled = true;
			CancelledRequestIds.Add(RequestId);
			PendingRequests.RemoveAt(i);
			CancelledRequestCount++;
			return true;
		}
	}

	return false;
}

void UUINavAsyncWidgetManager::CancelAllLoadRequests()
{
	UINAV_LOG("CancelAllLoadRequests: Cancelling all requests");

	// 取消所有活跃请求
	for (FAsyncWidgetLoadRequest& Request : ActiveRequests)
	{
		Request.bCancelled = true;
		CancelledRequestIds.Add(Request.RequestId);
		
		// 取消Streamable句柄
		if (TSharedPtr<FStreamableHandle>* HandlePtr = ActiveHandles.Find(Request.RequestId))
		{
			if (HandlePtr->IsValid())
			{
				(*HandlePtr)->CancelHandle();
			}
		}

		// 清除超时定时器
		if (FTimerHandle* TimeoutHandle = TimeoutHandles.Find(Request.RequestId))
		{
			if (WorldContext.IsValid())
			{
				WorldContext->GetTimerManager().ClearTimer(*TimeoutHandle);
			}
		}
	}

	// 取消所有等待请求
	for (FAsyncWidgetLoadRequest& Request : PendingRequests)
	{
		Request.bCancelled = true;
		CancelledRequestIds.Add(Request.RequestId);
	}

	CancelledRequestCount += ActiveRequests.Num() + PendingRequests.Num();

	// 清理所有容器
	ActiveRequests.Empty();
	PendingRequests.Empty();
	ActiveHandles.Empty();
	TimeoutHandles.Empty();
}

int32 UUINavAsyncWidgetManager::GetActiveLoadRequestCount() const
{
	return ActiveRequests.Num();
}

int32 UUINavAsyncWidgetManager::GetPendingLoadRequestCount() const
{
	return PendingRequests.Num();
}

bool UUINavAsyncWidgetManager::IsWidgetLoading(TSoftClassPtr<UUINavWidget> WidgetClass) const
{
	// 检查活跃请求
	for (const FAsyncWidgetLoadRequest& Request : ActiveRequests)
	{
		if (Request.WidgetClass == WidgetClass && !Request.bCancelled)
		{
			return true;
		}
	}

	// 检查等待队列
	for (const FAsyncWidgetLoadRequest& Request : PendingRequests)
	{
		if (Request.WidgetClass == WidgetClass && !Request.bCancelled)
		{
			return true;
		}
	}

	return false;
}

void UUINavAsyncWidgetManager::SetMaxConcurrentLoads(int32 NewMaxConcurrentLoads)
{
	MaxConcurrentLoads = FMath::Max(1, NewMaxConcurrentLoads);
	UINAV_LOG("SetMaxConcurrentLoads: Set to %d", MaxConcurrentLoads);
	
	// 如果增加了并发数，尝试处理更多请求
	while (ActiveRequests.Num() < MaxConcurrentLoads && PendingRequests.Num() > 0)
	{
		ProcessNextRequest();
	}
}

void UUINavAsyncWidgetManager::SetLoadTimeout(float TimeoutSeconds)
{
	LoadTimeoutSeconds = FMath::Max(1.0f, TimeoutSeconds);
	UINAV_LOG("SetLoadTimeout: Set to %.2f seconds", LoadTimeoutSeconds);
}

bool UUINavAsyncWidgetManager::GetRequestStatus(const FGuid& RequestId, bool& bIsActive, bool& bIsPending, bool& bIsCancelled) const
{
	bIsActive = false;
	bIsPending = false;
	bIsCancelled = CancelledRequestIds.Contains(RequestId);

	if (bIsCancelled)
	{
		return true;
	}

	// 检查活跃请求
	for (const FAsyncWidgetLoadRequest& Request : ActiveRequests)
	{
		if (Request.RequestId == RequestId)
		{
			bIsActive = true;
			return true;
		}
	}

	// 检查等待队列
	for (const FAsyncWidgetLoadRequest& Request : PendingRequests)
	{
		if (Request.RequestId == RequestId)
		{
			bIsPending = true;
			return true;
		}
	}

	return false;
}

void UUINavAsyncWidgetManager::ProcessNextRequest()
{
	if (PendingRequests.Num() > 0 && ActiveRequests.Num() < MaxConcurrentLoads)
	{
		// 获取最高优先级的请求
		FAsyncWidgetLoadRequest NextRequest = PendingRequests[0];
		PendingRequests.RemoveAt(0);

		// 检查是否已被取消
		if (NextRequest.bCancelled || CancelledRequestIds.Contains(NextRequest.RequestId))
		{
			UINAV_LOG("ProcessNextRequest: Skipping cancelled request %s", *NextRequest.RequestId.ToString());
			// 递归处理下一个请求
			ProcessNextRequest();
			return;
		}

		ActiveRequests.Add(NextRequest);
		StartLoadingWidget(NextRequest);
	}
}

void UUINavAsyncWidgetManager::StartLoadingWidget(const FAsyncWidgetLoadRequest& Request)
{
	UINAV_LOG("StartLoadingWidget: Starting load for %s (ID: %s)", 
		*Request.WidgetClass.GetAssetName(), 
		*Request.RequestId.ToString());

	// 首先检查缓存
	TSubclassOf<UUINavWidget> CachedClass = GetFromWidgetClassCache(Request.WidgetClass);
	if (CachedClass)
	{
		UINAV_LOG("StartLoadingWidget: Found cached class for %s, creating widget immediately", 
			*Request.WidgetClass.GetAssetName());
		
		// 直接使用缓存的类创建Widget
		UUINavWidget* CreatedWidget = CreateAndSetupWidget(CachedClass, Request);
		
		if (CreatedWidget)
		{
			UINAV_LOG("StartLoadingWidget: Widget created successfully from cache for %s", 
				*Request.WidgetClass.GetAssetName());
			
			if (Request.OnLoadCompleted.IsBound())
			{
				Request.OnLoadCompleted.ExecuteIfBound(CreatedWidget);
			}
			
			CompletedRequestCount++;
		}
		else
		{
			UINAV_LOG("StartLoadingWidget: Failed to create widget from cache for %s", 
				*Request.WidgetClass.GetAssetName());
			
			if (Request.OnLoadFailed.IsBound())
			{
				Request.OnLoadFailed.ExecuteIfBound(TEXT("Failed to create widget from cached class"));
			}
			
			FailedRequestCount++;
		}

		// 从活跃列表中移除
		for (int32 i = 0; i < ActiveRequests.Num(); ++i)
		{
			if (ActiveRequests[i].RequestId == Request.RequestId)
			{
				ActiveRequests.RemoveAt(i);
				break;
			}
		}

		// 处理下一个请求
		ProcessNextRequest();
		return;
	}

	// 缓存中没有找到，需要异步加载
	// 设置超时定时器
	if (WorldContext.IsValid())
	{
		FTimerHandle TimeoutHandle;
		WorldContext->GetTimerManager().SetTimer(
			TimeoutHandle,
			[this, RequestId = Request.RequestId]()
			{
				HandleLoadTimeout(RequestId);
			},
			LoadTimeoutSeconds,
			false
		);
		TimeoutHandles.Add(Request.RequestId, TimeoutHandle);
	}

	// 开始异步加载
	TSharedPtr<FStreamableHandle> Handle = StreamableManager.RequestAsyncLoad(
		Request.WidgetClass.ToSoftObjectPath(),
		[this, Request]()
		{
			OnWidgetClassLoaded(Request);
		}
	);

	if (Handle.IsValid())
	{
		ActiveHandles.Add(Request.RequestId, Handle);
	}
	else
	{
		UINAV_LOG("StartLoadingWidget: Failed to create streamable handle for %s", *Request.WidgetClass.GetAssetName());
		
		// 立即回调失败
		if (Request.OnLoadFailed.IsBound())
		{
			Request.OnLoadFailed.ExecuteIfBound(TEXT("Failed to create streamable handle"));
		}
		
		// 从活跃列表中移除
		for (int32 i = 0; i < ActiveRequests.Num(); ++i)
		{
			if (ActiveRequests[i].RequestId == Request.RequestId)
			{
				ActiveRequests.RemoveAt(i);
				break;
			}
		}
		
		FailedRequestCount++;
		ProcessNextRequest();
	}
}

void UUINavAsyncWidgetManager::OnWidgetClassLoaded(const FAsyncWidgetLoadRequest& Request)
{
	UINAV_LOG("OnWidgetClassLoaded: Load completed for %s (ID: %s)", 
		*Request.WidgetClass.GetAssetName(), 
		*Request.RequestId.ToString());

	// 清理句柄和定时器
	ActiveHandles.Remove(Request.RequestId);
	
	if (FTimerHandle* TimeoutHandle = TimeoutHandles.Find(Request.RequestId))
	{
		if (WorldContext.IsValid())
		{
			WorldContext->GetTimerManager().ClearTimer(*TimeoutHandle);
		}
		TimeoutHandles.Remove(Request.RequestId);
	}

	// 检查是否已被取消
	if (Request.bCancelled || CancelledRequestIds.Contains(Request.RequestId))
	{
		UINAV_LOG("OnWidgetClassLoaded: Request %s was cancelled", *Request.RequestId.ToString());
		
		// 从活跃列表中移除
		for (int32 i = 0; i < ActiveRequests.Num(); ++i)
		{
			if (ActiveRequests[i].RequestId == Request.RequestId)
			{
				ActiveRequests.RemoveAt(i);
				break;
			}
		}
		
		ProcessNextRequest();
		return;
	}

	// 加载Widget类
	UClass* LoadedClass = Request.WidgetClass.Get();
	if (!LoadedClass)
	{
		UINAV_LOG("OnWidgetClassLoaded: Failed to get loaded class for %s", *Request.WidgetClass.GetAssetName());
		
		if (Request.OnLoadFailed.IsBound())
		{
			Request.OnLoadFailed.ExecuteIfBound(TEXT("Failed to load widget class"));
		}
		
		FailedRequestCount++;
	}
	else
	{
		// 将加载的类添加到缓存（如果还没有缓存的话）
		if (!IsWidgetClassCached(Request.WidgetClass))
		{
			AddToWidgetClassCache(Request.WidgetClass, LoadedClass);
		}

		// 创建并设置Widget
		UUINavWidget* CreatedWidget = CreateAndSetupWidget(LoadedClass, Request);
		
		if (CreatedWidget)
		{
			UINAV_LOG("OnWidgetClassLoaded: Widget created successfully for %s", *Request.WidgetClass.GetAssetName());
			
			if (Request.OnLoadCompleted.IsBound())
			{
				Request.OnLoadCompleted.ExecuteIfBound(CreatedWidget);
			}
			
			CompletedRequestCount++;
		}
		else
		{
			UINAV_LOG("OnWidgetClassLoaded: Failed to create widget for %s", *Request.WidgetClass.GetAssetName());
			
			if (Request.OnLoadFailed.IsBound())
			{
				Request.OnLoadFailed.ExecuteIfBound(TEXT("Failed to create widget instance"));
			}
			
			FailedRequestCount++;
		}
	}

	// 从活跃列表中移除
	for (int32 i = 0; i < ActiveRequests.Num(); ++i)
	{
		if (ActiveRequests[i].RequestId == Request.RequestId)
		{
			ActiveRequests.RemoveAt(i);
			break;
		}
	}

	// 处理下一个请求
	ProcessNextRequest();
}

void UUINavAsyncWidgetManager::HandleLoadTimeout(const FGuid& RequestId)
{
	UINAV_LOG("HandleLoadTimeout: Request %s timed out", *RequestId.ToString());

	// 查找并取消请求
	for (int32 i = 0; i < ActiveRequests.Num(); ++i)
	{
		if (ActiveRequests[i].RequestId == RequestId)
		{
			FAsyncWidgetLoadRequest& Request = ActiveRequests[i];
			
			// 取消Streamable句柄
			if (TSharedPtr<FStreamableHandle>* HandlePtr = ActiveHandles.Find(RequestId))
			{
				if (HandlePtr->IsValid())
				{
					(*HandlePtr)->CancelHandle();
				}
				ActiveHandles.Remove(RequestId);
			}

			// 回调失败
			if (Request.OnLoadFailed.IsBound())
			{
				Request.OnLoadFailed.ExecuteIfBound(TEXT("Load timeout"));
			}

			// 标记为已取消并移除
			Request.bCancelled = true;
			CancelledRequestIds.Add(RequestId);
			ActiveRequests.RemoveAt(i);
			
			FailedRequestCount++;
			TimeoutHandles.Remove(RequestId);

			// 处理下一个请求
			ProcessNextRequest();
			break;
		}
	}
}

void UUINavAsyncWidgetManager::CleanupCompletedRequests()
{
	// 清理已取消的请求ID（保留最近的一些用于查询）
	const int32 MaxCancelledIds = 100;
	if (CancelledRequestIds.Num() > MaxCancelledIds)
	{
		// 转换为数组以便排序和移除旧的
		TArray<FGuid> CancelledArray = CancelledRequestIds.Array();
		CancelledArray.Sort([](const FGuid& A, const FGuid& B) {
			return A.ToString() < B.ToString(); // 简单的排序方式
		});

		// 移除一半旧的ID
		int32 ToRemove = CancelledRequestIds.Num() - MaxCancelledIds / 2;
		for (int32 i = 0; i < ToRemove; ++i)
		{
			CancelledRequestIds.Remove(CancelledArray[i]);
		}

		UINAV_LOG("CleanupCompletedRequests: Cleaned up %d old cancelled request IDs", ToRemove);
	}
}

UUINavWidget* UUINavAsyncWidgetManager::CreateAndSetupWidget(TSubclassOf<UUINavWidget> WidgetClass, const FAsyncWidgetLoadRequest& Request)
{
	if (!WidgetClass)
	{
		return nullptr;
	}

	// 获取UINavPC组件来创建Widget
	UUINavPCComponent* UINavPC = nullptr;
	
	if (WorldContext.IsValid())
	{
		if (APlayerController* PC = WorldContext->GetFirstPlayerController())
		{
			UINavPC = PC->FindComponentByClass<UUINavPCComponent>();
		}
	}

	if (!UINavPC)
	{
		UINAV_LOG("CreateAndSetupWidget: No UINavPCComponent found");
		return nullptr;
	}

	// 使用UINavPC的GoToWidget方法创建Widget
	UUINavWidget* CreatedWidget = UINavPC->GoToWidget(WidgetClass, Request.bRemoveParent, Request.bDestroyParent, Request.ZOrder);
	
	return CreatedWidget;
}

void UUINavAsyncWidgetManager::PrintDebugInfo() const
{
	UE_LOG(LogTemp, Warning, TEXT("=== UINavAsyncWidgetManager Debug Info ==="));
	UE_LOG(LogTemp, Warning, TEXT("Active Requests: %d"), ActiveRequests.Num());
	UE_LOG(LogTemp, Warning, TEXT("Pending Requests: %d"), PendingRequests.Num());
	UE_LOG(LogTemp, Warning, TEXT("Max Concurrent Loads: %d"), MaxConcurrentLoads);
	UE_LOG(LogTemp, Warning, TEXT("Load Timeout: %.2f seconds"), LoadTimeoutSeconds);
	UE_LOG(LogTemp, Warning, TEXT("Cancelled Request IDs: %d"), CancelledRequestIds.Num());
	UE_LOG(LogTemp, Warning, TEXT("Statistics:"));
	UE_LOG(LogTemp, Warning, TEXT("  Total: %d, Completed: %d, Failed: %d, Cancelled: %d"), 
		TotalRequestCount, CompletedRequestCount, FailedRequestCount, CancelledRequestCount);
	
	if (ActiveRequests.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Active Requests Details:"));
		for (const FAsyncWidgetLoadRequest& Request : ActiveRequests)
		{
			UE_LOG(LogTemp, Warning, TEXT("  ID: %s, Class: %s, Priority: %d"), 
				*Request.RequestId.ToString(), 
				*Request.WidgetClass.GetAssetName(), 
				Request.Priority);
		}
	}
}

void UUINavAsyncWidgetManager::GetLoadStatistics(int32& TotalRequests, int32& CompletedRequests, int32& FailedRequests, int32& CancelledRequests) const
{
	TotalRequests = TotalRequestCount;
	CompletedRequests = CompletedRequestCount;
	FailedRequests = FailedRequestCount;
	CancelledRequests = CancelledRequestCount;
}

void UUINavAsyncWidgetManager::ClearCache()
{
	UINAV_LOG("ClearCache: Clearing widget class cache (%d entries)", WidgetClassCache.Num());
	
	// 清空Widget类缓存
	WidgetClassCache.Empty();
	
	// 取消所有缓存句柄
	for (const auto& CacheHandle : CacheHandles)
	{
		if (CacheHandle.IsValid() && CacheHandle->IsActive())
		{
			CacheHandle->CancelHandle();
		}
	}
	CacheHandles.Empty();
	
	// 清理StreamableManager的缓存
	StreamableManager.RequestAsyncLoad(TArray<FSoftObjectPath>(), FStreamableDelegate(), FStreamableManager::AsyncLoadHighPriority);
	
	UINAV_LOG("ClearCache: Cache cleared successfully");
}

FGuid UUINavAsyncWidgetManager::PreloadWidgetClass(TSoftClassPtr<UUINavWidget> WidgetClass, int32 Priority)
{
	if (!WidgetClass.IsValid())
	{
		UINAV_LOG("PreloadWidgetClass: Invalid widget class");
		return FGuid();
	}

	// 检查是否已经缓存
	if (IsWidgetClassCached(WidgetClass))
	{
		UINAV_LOG("PreloadWidgetClass: Widget class %s already cached", *WidgetClass.ToString());
		return FGuid();
	}

	// 创建预加载请求
	FAsyncWidgetLoadRequest PreloadRequest;
	PreloadRequest.WidgetClass = WidgetClass;
	PreloadRequest.Priority = Priority;
	PreloadRequest.bRemoveParent = false;
	PreloadRequest.bDestroyParent = false;
	PreloadRequest.ZOrder = 0;

	// 创建预加载完成回调
	PreloadRequest.OnLoadCompleted.BindLambda([this, WidgetClass](UUINavWidget* Widget)
	{
		if (Widget)
		{
			// 将加载的类添加到缓存
			AddToWidgetClassCache(WidgetClass, Widget->GetClass());
			UINAV_LOG("PreloadWidgetClass: Successfully preloaded and cached %s", *WidgetClass.ToString());
			
			// 立即销毁Widget实例，我们只需要类引用
			Widget->RemoveFromParent();
		}
	});

	PreloadRequest.OnLoadFailed.BindLambda([WidgetClass](const FString& ErrorMessage)
	{
		UINAV_LOG("PreloadWidgetClass: Failed to preload %s - %s", *WidgetClass.ToString(), *ErrorMessage);
	});

	UINAV_LOG("PreloadWidgetClass: Starting preload for %s with priority %d", *WidgetClass.ToString(), Priority);

	// 添加到请求队列
	++TotalRequestCount;

	// 如果有空闲槽位，直接开始加载
	if (ActiveRequests.Num() < MaxConcurrentLoads)
	{
		ActiveRequests.Add(PreloadRequest);
		StartLoadingWidget(PreloadRequest);
	}
	else
	{
		// 添加到等待队列并按优先级排序
		PendingRequests.Add(PreloadRequest);
		PendingRequests.Sort();
		UINAV_LOG("PreloadWidgetClass: Request queued (Queue size: %d)", PendingRequests.Num());
	}

	return PreloadRequest.RequestId;
}

bool UUINavAsyncWidgetManager::IsWidgetClassCached(TSoftClassPtr<UUINavWidget> WidgetClass) const
{
	if (!WidgetClass.IsValid())
	{
		return false;
	}

	return WidgetClassCache.Contains(WidgetClass);
}

void UUINavAsyncWidgetManager::GetCacheStatistics(int32& CachedWidgetClasses, int32& TotalCacheSize) const
{
	CachedWidgetClasses = WidgetClassCache.Num();
	
	// 估算缓存大小（简单估算，每个缓存条目大约占用的字节数）
	TotalCacheSize = 0;
	for (const auto& CacheEntry : WidgetClassCache)
	{
		// 软引用路径字符串大小 + 类指针大小
		TotalCacheSize += CacheEntry.Key.ToString().Len() * sizeof(TCHAR) + sizeof(TSubclassOf<UUINavWidget>);
	}
	
	// 添加缓存句柄的大小
	TotalCacheSize += CacheHandles.Num() * sizeof(TSharedPtr<FStreamableHandle>);
}

void UUINavAsyncWidgetManager::AddToWidgetClassCache(TSoftClassPtr<UUINavWidget> SoftClass, TSubclassOf<UUINavWidget> LoadedClass)
{
	if (!SoftClass.IsValid() || !LoadedClass)
	{
		return;
	}

	WidgetClassCache.Add(SoftClass, LoadedClass);
	UINAV_LOG("AddToWidgetClassCache: Added %s to cache", *SoftClass.ToString());
}

TSubclassOf<UUINavWidget> UUINavAsyncWidgetManager::GetFromWidgetClassCache(TSoftClassPtr<UUINavWidget> SoftClass) const
{
	if (const TSubclassOf<UUINavWidget>* FoundClass = WidgetClassCache.Find(SoftClass))
	{
		return *FoundClass;
	}
	return nullptr;
}

void UUINavAsyncWidgetManager::CleanupCacheHandles()
{
	// 移除已经完成或无效的缓存句柄
	for (int32 i = CacheHandles.Num() - 1; i >= 0; --i)
	{
		if (!CacheHandles[i].IsValid() || !CacheHandles[i]->IsActive())
		{
			CacheHandles.RemoveAt(i);
		}
	}
}
