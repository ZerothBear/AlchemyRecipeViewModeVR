#pragma once

#include <atomic>

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

		void OnCraftingMenuOpened(RE::GFxMovieView* a_movie);
		void OnCraftingMenuClosed();
		void BindAlchemyMenu(AlchemyMenu* a_menu);

		void Toggle();
		void RequestToggle();

		[[nodiscard]] bool IsMenuOpen() const noexcept;
		[[nodiscard]] bool IsEnabled() const noexcept;
		[[nodiscard]] bool IsCurrentMovieAlchemy() const noexcept;
		[[nodiscard]] bool ShouldBlockCraft() const noexcept;

	private:
		void ExecuteQueuedToggle(std::uint64_t a_capturedGeneration);
		void EnableRecipeMode();
		void DisableRecipeMode();
		void BuildGhostIngredients();
		void AddGhostItemsToInventory();
		void AppendGhostEntriesToMenu();
		void RemoveGhostEntriesFromMenu();
		void RemoveGhostItemsFromInventory();
		void RestoreOriginalNames();
		void RefreshMenu();
		void SyncRootState();
		void SetCraftingBlocked(bool a_blocked);
		void FlushDeferredCleanup();

		// Main-thread-only state (never read/written from input thread)
		RE::GFxMovieView* movie_{ nullptr };
		AlchemyMenu*      alchemyMenu_{ nullptr };
		bool              uiInjected_{ false };
		bool              inventoryInjected_{ false };
		bool              menuEntriesInjected_{ false };

		// Cross-thread atomics (read from input thread, written from main thread)
		std::atomic<bool>          menuOpen_{ false };
		std::atomic<bool>          enabled_{ false };
		std::atomic<std::uint64_t> menuGeneration_{ 0 };
		std::atomic<std::int32_t>  pendingToggleCount_{ 0 };
		std::atomic<bool>          toggleTaskQueued_{ false };

		Alchemy::PlayerAlchemySnapshot    playerSnapshot_{};
		std::vector<GhostIngredient>      ghostIngredients_{};
		std::vector<GhostIngredient>      deferredCleanup_{};
	};
}
