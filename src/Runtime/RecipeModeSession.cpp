#include "Runtime/RecipeModeSession.h"

#include "Alchemy/IngredientRegistry.h"
#include "Config/Settings.h"
#include "Hooks/AlchemyMenuHooks.h"
#include "PCH/PCH.h"
#include "UI/AlchemyUiInjector.h"

namespace ARV
{
	RecipeModeSession& RecipeModeSession::GetSingleton()
	{
		static RecipeModeSession singleton;
		return singleton;
	}

	void RecipeModeSession::OnCraftingMenuOpened(RE::GFxMovieView* a_movie)
	{
		FlushDeferredCleanup();
		menuGeneration_.fetch_add(1, std::memory_order_release);

		const auto preserveAlchemyMenu = alchemyMenu_ && alchemyMenu_->view == a_movie;

		menuOpen_.store(true, std::memory_order_release);
		movie_ = a_movie;
		if (!preserveAlchemyMenu) {
			alchemyMenu_ = nullptr;
		}
		uiInjected_ = false;
		enabled_.store(false, std::memory_order_release);
		inventoryInjected_ = false;
		menuEntriesInjected_ = false;
		ghostIngredients_.clear();

		SyncRootState();

		const auto& settings = Config::Settings::GetSingleton();
		if (movie_ && settings.ShowNavButton()) {
			UI::AlchemyUiInjector::GetSingleton().InjectButtonShim(movie_, settings.ToggleKey());
			uiInjected_ = true;
		}

		spdlog::info(
			"RecipeModeSession: crafting menu opened (alchemy movie={} preserveAlchemyMenu={} generation={})",
			IsCurrentMovieAlchemy(),
			preserveAlchemyMenu,
			menuGeneration_.load(std::memory_order_acquire));
	}

	void RecipeModeSession::OnCraftingMenuClosed()
	{
		// Bump generation FIRST so any queued toggle task becomes stale
		menuGeneration_.fetch_add(1, std::memory_order_release);

		// Move ghost state into deferred cleanup buffer instead of cleaning up inline.
		// This avoids modifying inventory/forms while menu event listeners (moreHUD etc.)
		// may still be processing the closing menu.
		if (enabled_.load(std::memory_order_acquire) || inventoryInjected_ || menuEntriesInjected_) {
			if (!ghostIngredients_.empty()) {
				deferredCleanup_ = std::move(ghostIngredients_);
				ghostIngredients_.clear();
			}
			enabled_.store(false, std::memory_order_release);
			inventoryInjected_ = false;
			menuEntriesInjected_ = false;
		}

		AlchemyMenuHooks::GetSingleton().ClearCallbacks();

		menuOpen_.store(false, std::memory_order_release);
		movie_ = nullptr;
		alchemyMenu_ = nullptr;
		uiInjected_ = false;

		// Queue deferred cleanup on the main thread for the next frame
		if (!deferredCleanup_.empty()) {
			SKSE::GetTaskInterface()->AddTask([this]() {
				FlushDeferredCleanup();
			});
		}

		spdlog::info(
			"RecipeModeSession: crafting menu closed (generation={} deferredCleanup={})",
			menuGeneration_.load(std::memory_order_acquire),
			deferredCleanup_.size());
	}

	void RecipeModeSession::BindAlchemyMenu(AlchemyMenu* a_menu)
	{
		if (menuOpen_.load(std::memory_order_acquire) && alchemyMenu_ == a_menu && a_menu) {
			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: skipping duplicate AlchemyMenu bind for {}",
					static_cast<const void*>(a_menu));
			}
			return;
		}

		// New menu instance -- clear stale callback pointers from previous session
		AlchemyMenuHooks::GetSingleton().ClearCallbacks();

		alchemyMenu_ = a_menu;
		if (!alchemyMenu_) {
			return;
		}

		if (!movie_) {
			movie_ = alchemyMenu_->view;
			SyncRootState();
		}

		spdlog::info(
			"RecipeModeSession: bound AlchemyMenu={}",
			static_cast<const void*>(alchemyMenu_));
	}

	void RecipeModeSession::Toggle()
	{
		if (!menuOpen_.load(std::memory_order_acquire) || !IsCurrentMovieAlchemy()) {
			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::warn(
					"RecipeModeSession: toggle ignored (menuOpen={} alchemyMovie={})",
					menuOpen_.load(std::memory_order_acquire),
					IsCurrentMovieAlchemy());
			}
			return;
		}

		if (enabled_.load(std::memory_order_acquire)) {
			DisableRecipeMode();
		} else {
			EnableRecipeMode();
		}
	}

	void RecipeModeSession::RequestToggle()
	{
		pendingToggleCount_.fetch_add(1, std::memory_order_acq_rel);

		// Coalesce: only one AddTask in flight at a time
		bool expected = false;
		if (!toggleTaskQueued_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::debug("RecipeModeSession: toggle request coalesced (task already queued)");
			}
			return;
		}

		const auto generation = menuGeneration_.load(std::memory_order_acquire);

		SKSE::GetTaskInterface()->AddTask([this, generation]() {
			ExecuteQueuedToggle(generation);
		});

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::debug(
				"RecipeModeSession: queued toggle task (generation={})",
				generation);
		}
	}

	void RecipeModeSession::ExecuteQueuedToggle(std::uint64_t a_capturedGeneration)
	{
		toggleTaskQueued_.store(false, std::memory_order_release);

		const auto count = pendingToggleCount_.exchange(0, std::memory_order_acq_rel);

		if (count % 2 == 0) {
			spdlog::info(
				"RecipeModeSession: toggle cancelled (even count={}, net no-op)",
				count);
			return;
		}

		if (menuGeneration_.load(std::memory_order_acquire) != a_capturedGeneration) {
			spdlog::info(
				"RecipeModeSession: stale toggle task rejected (captured={} current={})",
				a_capturedGeneration,
				menuGeneration_.load(std::memory_order_acquire));
			return;
		}

		if (!menuOpen_.load(std::memory_order_acquire)) {
			spdlog::info("RecipeModeSession: toggle task rejected (menu closed)");
			return;
		}

		if (!movie_ || !alchemyMenu_) {
			spdlog::info("RecipeModeSession: toggle task rejected (null movie/menu)");
			return;
		}

		if (!IsCurrentMovieAlchemy()) {
			spdlog::info("RecipeModeSession: toggle task rejected (not alchemy movie)");
			return;
		}

		spdlog::info(
			"RecipeModeSession: executing queued toggle on thread {} (count={} generation={})",
			GetCurrentThreadId(),
			count,
			a_capturedGeneration);

		Toggle();
	}

	void RecipeModeSession::EnableRecipeMode()
	{
		if (enabled_.load(std::memory_order_acquire)) {
			return;
		}

		FlushDeferredCleanup();

		if (!movie_ || !alchemyMenu_) {
			spdlog::warn(
				"RecipeModeSession: enable skipped (movie={} alchemyMenu={})",
				static_cast<const void*>(movie_),
				static_cast<const void*>(alchemyMenu_));
			return;
		}

		playerSnapshot_.Rebuild(Alchemy::IngredientRegistry::GetSingleton());
		BuildGhostIngredients();

		if (ghostIngredients_.empty()) {
			spdlog::info("RecipeModeSession: no missing known ingredients found");
			return;
		}

		AddGhostItemsToInventory();
		inventoryInjected_ = true;

		// RAII guard: if anything below fails, roll back inventory injection
		auto guard = SKSE::stl::scope_exit([this]() {
			spdlog::warn("RecipeModeSession: enable failed, rolling back ghost state");
			RemoveGhostItemsFromInventory();
			RestoreOriginalNames();
			ghostIngredients_.clear();
			inventoryInjected_ = false;
			menuEntriesInjected_ = false;
		});

		AppendGhostEntriesToMenu();
		menuEntriesInjected_ = true;

		enabled_.store(true, std::memory_order_release);
		SyncRootState();
		SetCraftingBlocked(true);
		RefreshMenu();

		guard.release();  // success -- don't undo

		spdlog::info(
			"RecipeModeSession: enabled recipe mode with {} ghost ingredients",
			ghostIngredients_.size());
	}

	void RecipeModeSession::DisableRecipeMode()
	{
		if (!ghostIngredients_.empty()) {
			if (inventoryInjected_) {
				RemoveGhostItemsFromInventory();
			}
			RestoreOriginalNames();
			if (menuEntriesInjected_) {
				RemoveGhostEntriesFromMenu();
			}
		}

		ghostIngredients_.clear();
		enabled_.store(false, std::memory_order_release);
		inventoryInjected_ = false;
		menuEntriesInjected_ = false;

		SyncRootState();
		SetCraftingBlocked(false);

		if (alchemyMenu_) {
			RefreshMenu();
		}

		spdlog::info("RecipeModeSession: disabled recipe mode");
	}

	void RecipeModeSession::BuildGhostIngredients()
	{
		ghostIngredients_.clear();

		const auto& registry = Alchemy::IngredientRegistry::GetSingleton();

		for (const auto& [formID, record] : registry.Records()) {
			if (!record.ingredient) {
				continue;
			}

			if (!playerSnapshot_.HasKnownEffects(formID)) {
				continue;
			}

			if (playerSnapshot_.Count(formID) > 0) {
				continue;
			}

			GhostIngredient ghost{};
			ghost.formID = formID;
			ghost.ingredient = record.ingredient;
			ghost.originalName = record.ingredient->GetName() ? record.ingredient->GetName() : "";
			ghostIngredients_.push_back(std::move(ghost));
		}

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info(
				"RecipeModeSession: built {} ghost ingredients from {} registry records",
				ghostIngredients_.size(),
				registry.Records().size());
		}
	}

	void RecipeModeSession::AddGhostItemsToInventory()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			spdlog::error("RecipeModeSession: PlayerCharacter unavailable");
			return;
		}

		for (auto& ghost : ghostIngredients_) {
			const auto renamedName = ghost.originalName + " (0)";
			ghost.ingredient->fullName = renamedName;

			player->AddObjectToContainer(ghost.ingredient, nullptr, 1, nullptr);

			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: added ghost '{}' (formID={:08X})",
					renamedName,
					ghost.formID);
			}
		}
	}

	void RecipeModeSession::AppendGhostEntriesToMenu()
	{
		if (!alchemyMenu_) {
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		auto* changes = player->GetInventoryChanges();
		if (!changes || !changes->entryList) {
			spdlog::warn("RecipeModeSession: no inventory changes to scan");
			return;
		}

		std::uint32_t appended = 0;
		for (auto& ghost : ghostIngredients_) {
			RE::InventoryEntryData* foundEntry = nullptr;

			for (auto* entry : *changes->entryList) {
				if (entry && entry->object == ghost.ingredient) {
					foundEntry = entry;
					break;
				}
			}

			if (!foundEntry) {
				spdlog::warn(
					"RecipeModeSession: could not find inventory entry for ghost '{}'",
					ghost.originalName);
				continue;
			}

			AlchemyMenu::MenuIngredientEntry menuEntry{};
			menuEntry.ingredient = foundEntry;
			menuEntry.effect1FilterID = 0;
			menuEntry.effect2FilterID = 0;
			menuEntry.effect3FilterID = 0;
			menuEntry.effect4FilterID = 0;
			menuEntry.isSelected = 0;
			menuEntry.isNotGreyed = 1;

			alchemyMenu_->ingredientEntries.push_back(menuEntry);
			++appended;
		}

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info(
				"RecipeModeSession: appended {} ghost entries to ingredientEntries (total={})",
				appended,
				alchemyMenu_->ingredientEntries.size());
		}
	}

	void RecipeModeSession::RemoveGhostEntriesFromMenu()
	{
		if (!alchemyMenu_) {
			return;
		}

		// Build set of ghost ingredient pointers for fast lookup
		std::unordered_set<RE::TESBoundObject*> ghostObjects;
		for (const auto& ghost : ghostIngredients_) {
			ghostObjects.insert(ghost.ingredient);
		}

		// Filter ingredientEntries to remove ghost entries
		auto& entries = alchemyMenu_->ingredientEntries;
		RE::BSTArray<AlchemyMenu::MenuIngredientEntry> filtered;

		for (std::uint32_t i = 0; i < entries.size(); ++i) {
			if (entries[i].ingredient && entries[i].ingredient->object &&
				ghostObjects.contains(entries[i].ingredient->object)) {
				continue;
			}
			filtered.push_back(entries[i]);
		}

		entries.clear();
		for (auto& entry : filtered) {
			entries.push_back(entry);
		}

		// Clear any selected indexes that pointed at ghost entries
		alchemyMenu_->selectedIndexes.clear();
	}

	void RecipeModeSession::RemoveGhostItemsFromInventory()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			spdlog::error("RecipeModeSession: PlayerCharacter unavailable for removal");
			return;
		}

		for (auto& ghost : ghostIngredients_) {
			player->RemoveItem(ghost.ingredient, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);

			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: removed ghost '{}' (formID={:08X})",
					ghost.ingredient->GetName() ? ghost.ingredient->GetName() : "<null>",
					ghost.formID);
			}
		}
	}

	void RecipeModeSession::RestoreOriginalNames()
	{
		for (auto& ghost : ghostIngredients_) {
			ghost.ingredient->fullName = ghost.originalName;

			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: restored name '{}' (formID={:08X})",
					ghost.originalName,
					ghost.formID);
			}
		}
	}

	void RecipeModeSession::FlushDeferredCleanup()
	{
		if (deferredCleanup_.empty()) {
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			spdlog::error("RecipeModeSession: PlayerCharacter unavailable for deferred cleanup");
			deferredCleanup_.clear();
			return;
		}

		for (auto& ghost : deferredCleanup_) {
			player->RemoveItem(ghost.ingredient, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			ghost.ingredient->fullName = ghost.originalName;

			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: deferred cleanup '{}' (formID={:08X})",
					ghost.originalName,
					ghost.formID);
			}
		}

		spdlog::info(
			"RecipeModeSession: deferred cleanup complete ({} ghosts)",
			deferredCleanup_.size());

		deferredCleanup_.clear();
	}

	void RecipeModeSession::RefreshMenu()
	{
		if (!alchemyMenu_ || !alchemyMenu_->craftingMenu.IsDisplayObject()) {
			return;
		}

		std::array args{ RE::GFxValue(true) };
		alchemyMenu_->craftingMenu.Invoke("UpdateItemList", nullptr, args.data(), static_cast<RE::UPInt>(args.size()));
		alchemyMenu_->craftingMenu.Invoke("UpdateItemDisplay");
		alchemyMenu_->craftingMenu.Invoke("UpdateButtonText");

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info("RecipeModeSession: menu refreshed");
		}
	}

	void RecipeModeSession::SetCraftingBlocked(bool a_blocked)
	{
		if (!movie_) {
			return;
		}

		movie_->SetVariable("_root.Menu._bCanCraft", RE::GFxValue(!a_blocked));

		if (a_blocked) {
			movie_->SetVariable("_root.Menu.ItemList._alpha", RE::GFxValue(70.0));
		} else {
			movie_->SetVariable("_root.Menu.ItemList._alpha", RE::GFxValue(100.0));
		}
	}

	void RecipeModeSession::SyncRootState()
	{
		if (!movie_) {
			return;
		}

		const auto& settings = Config::Settings::GetSingleton();
		UI::AlchemyUiInjector::GetSingleton().SyncRootState(
			movie_,
			enabled_,
			settings.ToggleKey(),
			settings.ShowNavButton());
	}

	bool RecipeModeSession::IsMenuOpen() const noexcept
	{
		return menuOpen_.load(std::memory_order_acquire);
	}

	bool RecipeModeSession::IsEnabled() const noexcept
	{
		return enabled_.load(std::memory_order_acquire);
	}

	bool RecipeModeSession::IsCurrentMovieAlchemy() const noexcept
	{
		return UI::AlchemyUiInjector::GetSingleton().IsAlchemyMovie(movie_);
	}

	bool RecipeModeSession::ShouldBlockCraft() const noexcept
	{
		return enabled_;
	}
}
