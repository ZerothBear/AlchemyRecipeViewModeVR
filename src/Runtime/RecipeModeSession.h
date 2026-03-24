#pragma once

#include <atomic>
#include <unordered_map>

#include "Alchemy/IngredientRegistry.h"
#include "Alchemy/MissingIngredient.h"
#include "Alchemy/PlayerAlchemySnapshot.h"

namespace ARV
{
	class RecipeModeSession final
	{
	public:
		using AlchemyMenu = RE::CraftingSubMenus::CraftingSubMenus::AlchemyMenu;

		static RecipeModeSession& GetSingleton();

		void PublishCraftingMenuOpened(RE::GFxMovieView* a_movie);
		void PublishCraftingMenuClosed();
		void PublishAlchemyMenuBound(AlchemyMenu* a_menu);
		void RequestToggle();
		void RequestGhostSelection(std::uint32_t a_formID);
		void TickOwnerThread();

		[[nodiscard]] bool IsMenuOpen() const noexcept;
		[[nodiscard]] bool IsEnabled() const noexcept;
		[[nodiscard]] bool IsCurrentMovieAlchemy() const noexcept;
		[[nodiscard]] bool ShouldBlockCraft() const noexcept;

	private:
		bool ConsumePublishedState();
		void ProcessPendingGhostSelection();
		void ProcessPendingToggle();
		void ProcessDeferredCleanup();
		void HandlePublishedOpen(RE::GFxMovieView* a_movie);
		void HandlePublishedClose();
		void HandlePublishedBind(AlchemyMenu* a_menu);
		void Toggle();
		void EnableRecipeMode();
		void DisableRecipeMode();
		void BuildGhostIngredients();
		void BuildGhostRecipeCandidates();
		void BuildEffectPartitionMap();
		void ComputeGhostPartitionFlags();
		void PopulateGhostMenuEntries();
		void RemoveGhostMenuEntries();
		void ApplyGhostDisplayNames();
		void AddGhostItemsToInventory();
		void RemoveGhostItemsFromInventory(std::vector<GhostIngredient>& a_ghosts);
		void RestoreOriginalNames(std::vector<GhostIngredient>& a_ghosts);
		void RefreshMenu();
		void SyncRootState();
		void SetCraftingBlocked(bool a_blocked);
		void ClearSelectionState();

		// Owner-thread-only state. Only TickOwnerThread and its helpers may mutate these.
		RE::GFxMovieView* movie_{ nullptr };
		AlchemyMenu*      alchemyMenu_{ nullptr };
		bool              uiInjected_{ false };
		bool              inventoryInjected_{ false };
		bool              deferredCleanupPending_{ false };
		std::uint32_t     selectedGhostFormID_{ 0 };

		// Cross-thread mailbox state. Producer hooks publish into these fields.
		std::atomic<bool>          menuOpen_{ false };
		std::atomic<bool>          enabled_{ false };
		std::atomic<std::uint64_t> menuGeneration_{ 0 };
		std::atomic<std::int32_t>  pendingToggleCount_{ 0 };
		std::atomic<bool>          pendingGhostSelectionRequested_{ false };
		std::atomic<std::uint32_t> pendingGhostSelectionFormID_{ 0 };
		std::atomic<bool>          publishedOpenRequested_{ false };
		std::atomic<bool>          publishedCloseRequested_{ false };
		std::atomic<bool>          publishedBindRequested_{ false };
		std::atomic<RE::GFxMovieView*> publishedMovie_{ nullptr };
		std::atomic<AlchemyMenu*>      publishedAlchemyMenu_{ nullptr };

		Alchemy::PlayerAlchemySnapshot    playerSnapshot_{};
		std::vector<GhostIngredient>      ghostIngredients_{};
		std::vector<GhostIngredient>      deferredCleanup_{};
		std::unordered_map<RE::FormID, std::uint8_t> effectPartitionMap_{};
	};
}
