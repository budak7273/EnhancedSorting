#include "EnhancedSorting.h"
#include "Patching/NativeHookManager.h"
#include "FGItemDescriptor.h"
#include "FGInventoryComponent.h"
#include "ItemAmount.h"

#define LOCTEXT_NAMESPACE "FEnhancedSortingModule"

void FEnhancedSortingModule::ShutdownModule() {
}

void SortItemAmountsByName(TArray<FItemAmount> inArray, TArray<FItemAmount>& outArray) {

	TArray<FString> ItemNames;

	bool descending = false;

	for (int i = 0; i < inArray.Num(); i++) {
		auto z = inArray[i];
		auto ItemName = UFGItemDescriptor::GetItemName(z.ItemClass);

		ItemNames.Add(ItemName.ToString());
	}

	ItemNames.Sort(); // Sort array using built in function (sorts A-Z)

	if (descending == true) {
		TArray<FString> newArray; // Define "temp" holding array
		int x = ItemNames.Num() - 1;

		while (x > -1) {
			newArray.Add(ItemNames[x]);
			// loop through A-Z sorted array and remove element from back and place it in beginning of "temp" array
			--x;
		}

		ItemNames = newArray; // Set reference array to "temp" array order, array is now Z-A
	}

	TArray<FItemAmount> newItemAmounts;
	newItemAmounts.AddZeroed(ItemNames.Num());

	for (int i = 0; i < ItemNames.Num(); i++) {
		FString ItemName = ItemNames[i];
		for (int j = 0; j < inArray.Num(); j++) {
			auto z = inArray[j];
			auto TempItemName = UFGItemDescriptor::GetItemName(z.ItemClass);

			if (TempItemName.ToString() == ItemName) {
				newItemAmounts[i] = z;
			}
		}
	}

	outArray = newItemAmounts;
}

void SetupHooks() {
	UFGInventoryComponent* DefaultInventoryClass = GetMutableDefault<UFGInventoryComponent>();

	SUBSCRIBE_METHOD_VIRTUAL(UFGInventoryComponent::Server_SortInventory_Implementation, DefaultInventoryClass, [&](auto& Scope, UFGInventoryComponent* Inventory) {
		if (!Inventory->HasAuthority()) {
			// Clients should do the vanilla implementation (let the server handle it)
			Scope(Inventory);
			return;
		}
		// Replace the base game's sorting method
		Scope.Cancel();

		if (!IsValid(Inventory)) return;

		// Cache all Inventory items and condense them
		TArray<FItemAmount> InventoryCachedItemAmounts;

		for (int i = 0; i < Inventory->GetSizeLinear(); i++) {
			FInventoryStack outStack;
			Inventory->GetStackFromIndex(i, outStack);

			if (!outStack.HasItems()) continue;

			bool HasExistingCachedItem = false;

			for (auto& cachedItemAmount : InventoryCachedItemAmounts) {
				if (cachedItemAmount.ItemClass == outStack.Item.GetItemClass()) {
					cachedItemAmount.Amount += outStack.NumItems;
					HasExistingCachedItem = true;
				}
			}

			if (!HasExistingCachedItem) {
				FItemAmount itemAmount = FItemAmount(outStack.Item.GetItemClass(), outStack.NumItems);
				InventoryCachedItemAmounts.Add(itemAmount);
			}
		}

		// Empty the inventory
		Inventory->Empty();

		//Sort ItemAmounts alphabetically
		TArray<FItemAmount> SortedItemAmounts;
		SortItemAmountsByName(InventoryCachedItemAmounts, SortedItemAmounts);

		// Re-add the items
		for (FItemAmount& ItemAmount : SortedItemAmounts) {

			int AttemptsToAddItem = 0;

			while (ItemAmount.Amount > 0) {
				if (AttemptsToAddItem > 10) {
					break;
				}

				for (int i = 0; i < Inventory->GetSizeLinear(); i++) {

					if (!Inventory->IsItemAllowed(ItemAmount.ItemClass, i)) {
						continue;
					}

					FInventoryStack outStack;
					Inventory->GetStackFromIndex(i, outStack);

					int slotSize = Inventory->GetSlotSize(i, ItemAmount.ItemClass);

					int RemainingSpace = 0;

					if (outStack.HasItems()) {
						if (outStack.Item.GetItemClass() != ItemAmount.ItemClass) continue;
						RemainingSpace = slotSize - outStack.NumItems;
					} else {
						RemainingSpace = slotSize;
					}

					if (RemainingSpace <= 0) continue;

					int AmountToAdd = ItemAmount.Amount;

					if (AmountToAdd > RemainingSpace) {
						AmountToAdd = RemainingSpace;
					}

					FInventoryStack itemStack;
					itemStack.NumItems = AmountToAdd;
					itemStack.Item = FInventoryItem(ItemAmount.ItemClass);
					Inventory->AddStackToIndex(i, itemStack);

					ItemAmount.Amount -= AmountToAdd;

					if (ItemAmount.Amount <= 0) {
						break;
					}
				}

				if (ItemAmount.Amount <= 0) {
					break;
				}
				AttemptsToAddItem++;
			}
		}
		});

}

void FEnhancedSortingModule::StartupModule() {
#if !WITH_EDITOR
	SetupHooks();
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FEnhancedSortingModule, EnhancedSorting)