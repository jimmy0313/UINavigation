// Copyright (C) 2023 Gonçalo Marques - All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UINavAsyncWidgetManager.h"
#include "UINavAsyncHelpers.generated.h"

/**
 * 蓝图辅助函数库，用于简化异步Widget加载的使用
 */
UCLASS()
class UINAVIGATION_API UUINavAsyncHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	
	// 创建简单的异步Widget加载请求 (蓝图友好版本)
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget", meta = (AdvancedDisplay = "2"))
	static FGuid LoadUINavWidgetAsync(
		UObject* WorldContext,
		TSoftClassPtr<UUINavWidget> WidgetClass,
		bool bRemoveParent = false,
		bool bDestroyParent = false,
		int32 ZOrder = 0,
		int32 Priority = 0
	);

	// 带回调的异步Widget加载 (C++版本)
	static FGuid LoadUINavWidgetAsyncWithCallbacks(
		UObject* WorldContext,
		TSoftClassPtr<UUINavWidget> WidgetClass,
		TFunction<void(UUINavWidget*)> OnSuccess,
		TFunction<void(const FString&)> OnFailure = nullptr,
		bool bRemoveParent = false,
		bool bDestroyParent = false,
		int32 ZOrder = 0,
		int32 Priority = 0
	);

	// 取消Widget加载请求
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget")
	static bool CancelUINavWidgetLoad(UObject* WorldContext, const FGuid& RequestId);

	// 取消所有Widget加载请求
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget")
	static void CancelAllUINavWidgetLoads(UObject* WorldContext);

	// 检查特定Widget是否正在加载
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UINav Async Widget")
	static bool IsUINavWidgetLoading(UObject* WorldContext, TSoftClassPtr<UUINavWidget> WidgetClass);

	// 获取异步加载统计信息
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UINav Async Widget")
	static void GetAsyncLoadStatistics(
		UObject* WorldContext,
		int32& TotalRequests,
		int32& ActiveRequests,
		int32& PendingRequests,
		int32& CompletedRequests,
		int32& FailedRequests,
		int32& CancelledRequests
	);

	// 设置异步加载参数
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget")
	static void SetAsyncLoadSettings(
		UObject* WorldContext,
		int32 MaxConcurrentLoads = 3,
		float TimeoutSeconds = 30.0f
	);

	// 预加载Widget类但不显示
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget")
	static FGuid PreloadUINavWidget(UObject* WorldContext, TSoftClassPtr<UUINavWidget> WidgetClass, int32 Priority = 0);

	// 创建加载进度回调的异步请求
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget", CustomThunk = "true", meta = (CustomStructureParam = "OnLoadCompleted,OnLoadFailed"))
	static FGuid LoadUINavWidgetAsyncWithEvents(
		UObject* WorldContext,
		TSoftClassPtr<UUINavWidget> WidgetClass,
		const FOnWidgetLoaded& OnLoadCompleted,
		const FOnWidgetLoadFailed& OnLoadFailed,
		bool bRemoveParent = false,
		bool bDestroyParent = false,
		int32 ZOrder = 0,
		int32 Priority = 0
	);

	void execLoadUINavWidgetAsyncWithEvents(FFrame& Stack, RESULT_DECL);

	
	// 获取缓存统计信息
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UINav Async Widget", meta = (WorldContext = "WorldContext"))
	static void GetCacheStatistics(
		UObject* WorldContext,
		int32& CachedWidgetClasses,
		int32& TotalCacheSize
	);

	// 预加载Widget类
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget", meta = (WorldContext = "WorldContext"))
	static FGuid PreloadUINavWidgetClass(
		UObject* WorldContext,
		TSoftClassPtr<UUINavWidget> WidgetClass,
		int32 Priority = 0
	);

	// 检查Widget类是否已缓存
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "UINav Async Widget", meta = (WorldContext = "WorldContext"))
	static bool IsUINavWidgetClassCached(
		UObject* WorldContext,
		TSoftClassPtr<UUINavWidget> WidgetClass
	);

	// 获取异步加载管理器实例（用于高级操作）
	UFUNCTION(BlueprintCallable, Category = "UINav Async Widget", meta = (WorldContext = "WorldContext"))
	static UUINavAsyncWidgetManager* GetAsyncWidgetManager(UObject* WorldContext);

protected:
	// 获取异步Widget管理器的辅助函数
	static UUINavAsyncWidgetManager* GetAsyncWidgetManager(UObject* WorldContext);
};