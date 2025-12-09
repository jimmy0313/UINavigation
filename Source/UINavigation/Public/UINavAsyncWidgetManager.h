// Copyright (C) 2023 Gonçalo Marques - All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/StreamableManager.h"
#include "UINavWidget.h"
#include "UINavAsyncWidgetManager.generated.h"

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnWidgetLoaded, UUINavWidget*, LoadedWidget);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnWidgetLoadFailed, const FString&, ErrorMessage);

USTRUCT(BlueprintType)
struct FAsyncWidgetLoadRequest
{
	GENERATED_BODY()

	// 唯一请求ID
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	// Widget类的软引用
	UPROPERTY(BlueprintReadOnly)
	TSoftClassPtr<UUINavWidget> WidgetClass;

	// 是否移除父Widget
	UPROPERTY(BlueprintReadOnly)
	bool bRemoveParent = false;

	// 是否销毁父Widget
	UPROPERTY(BlueprintReadOnly)
	bool bDestroyParent = false;

	// Z-Order
	UPROPERTY(BlueprintReadOnly)
	int32 ZOrder = 0;

	// 加载完成回调
	UPROPERTY(BlueprintReadOnly)
	FOnWidgetLoaded OnLoadCompleted;

	// 加载失败回调
	UPROPERTY(BlueprintReadOnly)
	FOnWidgetLoadFailed OnLoadFailed;

	// 请求发起时间
	UPROPERTY(BlueprintReadOnly)
	float RequestTime = 0.0f;

	// 是否已被取消
	UPROPERTY(BlueprintReadOnly)
	bool bCancelled = false;

	// 优先级 (数值越高越优先)
	UPROPERTY(BlueprintReadOnly)
	int32 Priority = 0;

	FAsyncWidgetLoadRequest()
	{
		RequestId = FGuid::NewGuid();
		RequestTime = FPlatformTime::Seconds();
	}

	bool operator<(const FAsyncWidgetLoadRequest& Other) const
	{
		// 高优先级排在前面，如果优先级相同则早请求的排前面
		if (Priority != Other.Priority)
		{
			return Priority > Other.Priority;
		}
		return RequestTime < Other.RequestTime;
	}
};

UCLASS(BlueprintType, Blueprintable)
class UINAVIGATION_API UUINavAsyncWidgetManager : public UObject
{
	GENERATED_BODY()

public:
	UUINavAsyncWidgetManager();

	// 获取全局单例
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget", CallInEditor = true)
	static UUINavAsyncWidgetManager* GetInstance(UObject* WorldContext = nullptr);

	// 异步加载并打开Widget
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget")
	FGuid LoadWidgetAsync(
		TSoftClassPtr<UUINavWidget> WidgetClass,
		const FOnWidgetLoaded& OnLoadCompleted,
		const FOnWidgetLoadFailed& OnLoadFailed = FOnWidgetLoadFailed(),
		bool bRemoveParent = false,
		bool bDestroyParent = false,
		int32 ZOrder = 0,
		int32 Priority = 0
	);

	// 取消异步加载请求
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget")
	bool CancelLoadRequest(const FGuid& RequestId);

	// 取消所有异步加载请求
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget")
	void CancelAllLoadRequests();

	// 获取当前正在加载的请求数量
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UINav Async Widget")
	int32 GetActiveLoadRequestCount() const;

	// 获取等待队列中的请求数量
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UINav Async Widget")
	int32 GetPendingLoadRequestCount() const;

	// 检查是否有特定Widget正在加载
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UINav Async Widget")
	bool IsWidgetLoading(TSoftClassPtr<UUINavWidget> WidgetClass) const;

	// 设置最大并发加载数量
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget")
	void SetMaxConcurrentLoads(int32 NewMaxConcurrentLoads);

	// 设置超时时间（秒）
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget")
	void SetLoadTimeout(float TimeoutSeconds);

	// 获取请求状态
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UINav Async Widget")
	bool GetRequestStatus(const FGuid& RequestId, bool& bIsActive, bool& bIsPending, bool& bIsCancelled) const;

protected:
	// 处理下一个等待中的请求
	void ProcessNextRequest();

	// 开始加载Widget
	void StartLoadingWidget(const FAsyncWidgetLoadRequest& Request);

	// Widget加载完成回调
	void OnWidgetClassLoaded(const FAsyncWidgetLoadRequest& Request);

	// 处理加载超时
	void HandleLoadTimeout(const FGuid& RequestId);

	// 清理已完成或取消的请求
	void CleanupCompletedRequests();

	// 创建并设置Widget
	UUINavWidget* CreateAndSetupWidget(TSubclassOf<UUINavWidget> WidgetClass, const FAsyncWidgetLoadRequest& Request);

private:
	// 流式管理器
	UPROPERTY()
	FStreamableManager StreamableManager;

	// 当前正在执行的加载请求
	UPROPERTY()
	TArray<FAsyncWidgetLoadRequest> ActiveRequests;

	// 等待队列中的请求
	UPROPERTY()
	TArray<FAsyncWidgetLoadRequest> PendingRequests;

	// 已取消的请求ID集合（用于快速查找）
	UPROPERTY()
	TSet<FGuid> CancelledRequestIds;

	// 活跃的Streamable句柄
	UPROPERTY()
	TMap<FGuid, TSharedPtr<FStreamableHandle>> ActiveHandles;

	// 超时定时器句柄
	UPROPERTY()
	TMap<FGuid, FTimerHandle> TimeoutHandles;

	// 最大并发加载数量
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (AllowPrivateAccess = "true"))
	int32 MaxConcurrentLoads = 3;

	// 加载超时时间（秒）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (AllowPrivateAccess = "true"))
	float LoadTimeoutSeconds = 30.0f;

	// 清理间隔时间（秒）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings", meta = (AllowPrivateAccess = "true"))
	float CleanupInterval = 5.0f;

	// 清理定时器句柄
	FTimerHandle CleanupTimerHandle;

	// 全局实例
	static UUINavAsyncWidgetManager* Instance;

	// World Context用于获取UI系统
	UPROPERTY()
	TWeakObjectPtr<UWorld> WorldContext;

public:
	// 调试功能：打印当前状态
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget", CallInEditor = true)
	void PrintDebugInfo() const;

	// 获取加载统计信息
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UINav Async Widget")
	void GetLoadStatistics(int32& TotalRequests, int32& CompletedRequests, int32& FailedRequests, int32& CancelledRequests) const;

	// 缓存管理功能
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget")
	void ClearCache();

	// 预加载Widget类（不创建实例）
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget")
	FGuid PreloadWidgetClass(TSoftClassPtr<UUINavWidget> WidgetClass, int32 Priority = 0);

	// 检查Widget类是否已缓存
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UINav Async Widget")
	bool IsWidgetClassCached(TSoftClassPtr<UUINavWidget> WidgetClass) const;

	// 获取缓存统计信息
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UINav Async Widget")
	void GetCacheStatistics(int32& CachedWidgetClasses, int32& TotalCacheSize) const;

private:
	// 统计计数器
	UPROPERTY()
	int32 TotalRequestCount = 0;

	UPROPERTY()
	int32 CompletedRequestCount = 0;

	UPROPERTY()
	int32 FailedRequestCount = 0;

	UPROPERTY()
	int32 CancelledRequestCount = 0;

	// Widget类缓存
	UPROPERTY()
	TMap<TSoftClassPtr<UUINavWidget>, TSubclassOf<UUINavWidget>> WidgetClassCache;

	// 缓存的软引用，防止被垃圾回收
	UPROPERTY()
	TArray<TSharedPtr<FStreamableHandle>> CacheHandles;

	// 添加Widget类到缓存
	void AddToWidgetClassCache(TSoftClassPtr<UUINavWidget> SoftClass, TSubclassOf<UUINavWidget> LoadedClass);

	// 从缓存获取Widget类
	TSubclassOf<UUINavWidget> GetFromWidgetClassCache(TSoftClassPtr<UUINavWidget> SoftClass) const;

	// 清理缓存句柄
	void CleanupCacheHandles();
};