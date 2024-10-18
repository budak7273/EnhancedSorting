#include "EnhancedSorting.h"
#include "Patching/NativeHookManager.h"
#include "FGItemDescriptor.h"
#include "FGInventoryComponent.h"
#include "ItemAmount.h"

#define LOCTEXT_NAMESPACE "FEnhancedSortingModule"

void FEnhancedSortingModule::ShutdownModule() {
}

void SortInventoryStacksByName(TArray<FInventoryStack> inArray, TArray<FInventoryStack>& outArray) {

	TArray<FString> ItemNames;

	bool Descending = false;

	for (int originalIndex = 0; originalIndex < inArray.Num(); originalIndex++) {
		auto stack = inArray[originalIndex];
		auto ItemName = UFGItemDescriptor::GetItemName(stack.Item.GetItemClass());

		ItemNames.Add(ItemName.ToString());
	}

	ItemNames.Sort(); // Sort array using built in function (sorts A-Z)

	if (Descending == true) {
		TArray<FString> holding; // Define "temp" holding array
		int             index = ItemNames.Num() - 1;

		while (index > -1) {
			holding.Add(ItemNames[index]);
			// loop through A-Z sorted array and remove element from back and place it in beginning of "temp" array
			--index;
		}

		ItemNames = holding; // Set reference array to "temp" array order, array is now Z-A
	}

	TArray<FInventoryStack> newInventoryStacks;
	newInventoryStacks.AddZeroed(ItemNames.Num());

	for (int outputIndex = 0; outputIndex < ItemNames.Num(); outputIndex++) {
		FString ItemName = ItemNames[outputIndex];
		for (int originalIndex = 0; originalIndex < inArray.Num(); originalIndex++) {
			auto stack = inArray[originalIndex];
			auto TempItemName = UFGItemDescriptor::GetItemName(stack.Item.GetItemClass());

			if (TempItemName.ToString() == ItemName) {
				newInventoryStacks[outputIndex] = stack;
			}
		}
	}

	outArray = newInventoryStacks;
}

void SetupHooks() {
	UFGInventoryComponent* DefaultInventoryClass = GetMutableDefault<UFGInventoryComponent>();

	SUBSCRIBE_METHOD_VIRTUAL(UFGInventoryComponent::SortInventory, DefaultInventoryClass, [&](auto& Scope, UFGInventoryComponent* Inventory) {
		// Replace the vanilla function implementation
		Scope.Cancel();

		if (!IsValid(Inventory)) return;

		// Clients should do the vanilla implementation (let the server handle it)
		// Yes this is correct, the client is calling Server_SortInventory, that's how CSS did it
		if (!Inventory->HasAuthority()) {
			Inventory->Server_SortInventory();
			return;
		}

		// Cache all Inventory Items and condense them
		TArray<FInventoryStack> InventoryCachedInventoryStacks;
		for (int inventorySlotIndex = 0; inventorySlotIndex < Inventory->GetSizeLinear(); inventorySlotIndex++) {
			FInventoryStack outStack;
			Inventory->GetStackFromIndex(inventorySlotIndex, outStack);

			if (!outStack.HasItems()) continue;

			bool HasExistingCachedItem = false;

			// condense stack if the item doesn't have an ItemState
			// If the item has ItemState then skip condense as most likely its an equipment and cant stack anyways.
			if (!outStack.Item.HasState()) {

				for (auto& cachedInventoryStack : InventoryCachedInventoryStacks) {

					if (cachedInventoryStack.Item.GetItemClass() == outStack.Item.GetItemClass()) {
						cachedInventoryStack.NumItems += outStack.NumItems;
						HasExistingCachedItem = true;
					}
				}
			}

			if (!HasExistingCachedItem) {
				InventoryCachedInventoryStacks.Add(outStack);
			}
		}

		// Empty Inventory (the items are held in InventoryCachedInventoryStacks)
		Inventory->Empty();

		//Sort ItemAmounts Alphabetically
		TArray<FInventoryStack> SortedInventoryStacks;
		SortInventoryStacksByName(InventoryCachedInventoryStacks, SortedInventoryStacks);

		// Re-add the items
		for (FInventoryStack& InventoryStack : SortedInventoryStacks) {
			int AttemptsToAddItem = 0;

			while (InventoryStack.NumItems > 0) {
				if (AttemptsToAddItem > 10) {
					// TODO how to handle this case cleanly?
					break;
				}

				for (int inventorySlotIndex = 0; inventorySlotIndex < Inventory->GetSizeLinear(); inventorySlotIndex++) {

					if (!Inventory->IsItemAllowed(InventoryStack.Item.GetItemClass(), inventorySlotIndex)) {
						continue;
					}

					FInventoryStack outStack;
					Inventory->GetStackFromIndex(inventorySlotIndex, outStack);

					int slotSize = Inventory->GetSlotSize(inventorySlotIndex, InventoryStack.Item.GetItemClass());

					int RemainingSpace = 0;

					if (outStack.HasItems()) {
						if (outStack.Item.GetItemClass() != InventoryStack.Item.GetItemClass()) continue;
						RemainingSpace = slotSize - outStack.NumItems;
					} else {
						RemainingSpace = slotSize;
					}

					if (RemainingSpace <= 0) continue;

					int AmountToAdd = InventoryStack.NumItems;

					if (AmountToAdd > RemainingSpace) {
						AmountToAdd = RemainingSpace;
					}

					FInventoryStack itemStack;
					itemStack.NumItems = AmountToAdd;
					itemStack.Item = InventoryStack.Item; // Important for persisting item state
					Inventory->AddStackToIndex(inventorySlotIndex, itemStack);

					InventoryStack.NumItems -= AmountToAdd;

					if (InventoryStack.NumItems <= 0) {
						break;
					}
				}

				if (InventoryStack.NumItems <= 0) {
					break;
				}
				AttemptsToAddItem++;
			}
		}

		Inventory->MarkInventoryContentsDirty();
	});
}

void FEnhancedSortingModule::StartupModule() {
#if !WITH_EDITOR
	SetupHooks();
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FEnhancedSortingModule, EnhancedSorting)