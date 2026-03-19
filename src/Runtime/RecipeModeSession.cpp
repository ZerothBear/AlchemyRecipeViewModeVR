#include "Runtime/RecipeModeSession.h"

#include "Alchemy/IngredientRegistry.h"
#include "Config/Settings.h"
#include "PCH/PCH.h"
#include "UI/AlchemyUiInjector.h"

namespace
{
	using AlchemyMenu = RE::CraftingSubMenus::CraftingSubMenus::AlchemyMenu;
	using MenuIngredientEntry = AlchemyMenu::MenuIngredientEntry;

	std::vector<MenuIngredientEntry> CopyIngredientEntries(const RE::BSTArray<MenuIngredientEntry>& a_entries)
	{
		std::vector<MenuIngredientEntry> copy;
		copy.reserve(a_entries.size());
		for (const auto& entry : a_entries) {
			copy.push_back(entry);
		}
		return copy;
	}
}

namespace ARV
{
	RecipeModeSession& RecipeModeSession::GetSingleton()
	{
		static RecipeModeSession singleton;
		return singleton;
	}

	void RecipeModeSession::OnCraftingMenuOpened(RE::GFxMovieView* a_movie)
	{
		const auto preserveAlchemyMenu = alchemyMenu_ && alchemyMenu_->view == a_movie;

		menuOpen_ = true;
		movie_ = a_movie;
		if (!preserveAlchemyMenu) {
			alchemyMenu_ = nullptr;
		}
		uiInjected_ = false;
		vanillaCaptured_ = false;
		enabled_ = false;

		ClearSyntheticEntries();
		vanillaIngredientEntries_.clear();

		SyncRootState();

		const auto& settings = Config::Settings::GetSingleton();
		if (movie_ && settings.ShowNavButton()) {
			UI::AlchemyUiInjector::GetSingleton().InjectButtonShim(movie_, settings.ToggleKey());
			uiInjected_ = true;
		}

		if (preserveAlchemyMenu) {
			CaptureVanillaIngredientEntries();
			ApplyUiState();
		}

		spdlog::info(
			"RecipeModeSession: crafting menu opened (alchemy movie={} preserveAlchemyMenu={})",
			IsCurrentMovieAlchemy(),
			preserveAlchemyMenu);
	}

	void RecipeModeSession::OnCraftingMenuClosed()
	{
		if (enabled_) {
			DisableRecipeMode();
		}

		menuOpen_ = false;
		movie_ = nullptr;
		alchemyMenu_ = nullptr;
		uiInjected_ = false;
		vanillaCaptured_ = false;
		vanillaIngredientEntries_.clear();
		ClearSyntheticEntries();

		spdlog::info("RecipeModeSession: crafting menu closed");
	}

	void RecipeModeSession::BindAlchemyMenu(AlchemyMenu* a_menu)
	{
		if (menuOpen_ && alchemyMenu_ == a_menu && a_menu) {
			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: skipping duplicate AlchemyMenu bind for {} while menu is already open",
					static_cast<const void*>(a_menu));
			}
			return;
		}

		alchemyMenu_ = a_menu;
		if (!alchemyMenu_) {
			return;
		}

		if (!movie_) {
			movie_ = alchemyMenu_->view;
			SyncRootState();
		}

		CaptureVanillaIngredientEntries();
		ApplyUiState();

		spdlog::info(
			"RecipeModeSession: bound AlchemyMenu={} ingredientEntries={}",
			static_cast<const void*>(alchemyMenu_),
			alchemyMenu_->ingredientEntries.size());
	}

	void RecipeModeSession::OnSelectionChanged()
	{
		if (!enabled_) {
			return;
		}

		ApplyUiState();
	}

	void RecipeModeSession::OnUserEvent(const RE::BSFixedString* a_control)
	{
		if (!Config::Settings::GetSingleton().DebugLogging()) {
			return;
		}

		const auto* controlName = a_control ? a_control->c_str() : "<null>";
		spdlog::info("RecipeModeSession: ProcessUserEvent control='{}'", controlName ? controlName : "<null>");
	}

	void RecipeModeSession::Toggle()
	{
		if (!menuOpen_ || !IsCurrentMovieAlchemy()) {
			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::warn(
					"RecipeModeSession: toggle ignored (menuOpen={} alchemyMovie={})",
					menuOpen_,
					IsCurrentMovieAlchemy());
			}
			return;
		}

		if (enabled_) {
			DisableRecipeMode();
		} else {
			EnableRecipeMode();
		}
	}

	void RecipeModeSession::EnableRecipeMode()
	{
		if (enabled_) {
			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::warn("RecipeModeSession: enable skipped because recipe mode is already enabled");
			}
			return;
		}

		if (!alchemyMenu_) {
			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::warn(
					"RecipeModeSession: enable skipped because alchemyMenu is null (menuOpen={} alchemyMovie={} movie={})",
					menuOpen_,
					IsCurrentMovieAlchemy(),
					static_cast<const void*>(movie_));
			}
			return;
		}

		CaptureVanillaIngredientEntries();
		playerSnapshot_.Rebuild(Alchemy::IngredientRegistry::GetSingleton());
		BuildSyntheticIngredientEntries();
		ApplySyntheticIngredientEntries();
		enabled_ = true;
		ApplyUiState();
		RefreshMenu(true);

		spdlog::info(
			"RecipeModeSession: enabled recipe mode with {} synthetic entries (vanilla={} selected={})",
			syntheticIngredientEntries_.size(),
			vanillaIngredientEntries_.size(),
			alchemyMenu_->selectedIndexes.size());
	}

	void RecipeModeSession::DisableRecipeMode()
	{
		if (!alchemyMenu_) {
			enabled_ = false;
			ClearSyntheticEntries();
			return;
		}

		RestoreVanillaIngredientEntries();
		enabled_ = false;
		ClearSyntheticEntries();
		ApplyUiState();
		RefreshMenu(true);

		spdlog::info("RecipeModeSession: disabled recipe mode");
	}

	void RecipeModeSession::CaptureVanillaIngredientEntries()
	{
		if (!alchemyMenu_ || vanillaCaptured_) {
			return;
		}

		vanillaIngredientEntries_ = CopyIngredientEntries(alchemyMenu_->ingredientEntries);
		vanillaCaptured_ = true;
	}

	void RecipeModeSession::BuildSyntheticIngredientEntries()
	{
		ClearSyntheticEntries();

		const auto& registry = Alchemy::IngredientRegistry::GetSingleton();
		syntheticIngredientEntries_.reserve(registry.Records().size());
		syntheticInventoryEntries_.reserve(registry.Records().size());

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

			auto entryData = std::make_unique<RE::InventoryEntryData>(record.ingredient, 0);
			auto* entryPtr = entryData.get();

			MenuIngredientEntry menuEntry{};
			menuEntry.ingredient = entryPtr;
			menuEntry.effect1FilterID = 0;
			menuEntry.effect2FilterID = 0;
			menuEntry.effect3FilterID = 0;
			menuEntry.effect4FilterID = 0;
			menuEntry.isSelected = 0;
			menuEntry.isNotGreyed = 1;

			syntheticFormIDs_.insert(formID);
			syntheticEntryPointers_.insert(entryPtr);
			syntheticInventoryEntries_.push_back(std::move(entryData));
			syntheticIngredientEntries_.push_back(menuEntry);
		}

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info(
				"RecipeModeSession: built {} synthetic ingredient entries from {} registry records",
				syntheticIngredientEntries_.size(),
				registry.Records().size());
		}
	}

	void RecipeModeSession::ApplySyntheticIngredientEntries()
	{
		if (!alchemyMenu_) {
			return;
		}

		alchemyMenu_->ingredientEntries.clear();
		for (const auto& entry : vanillaIngredientEntries_) {
			alchemyMenu_->ingredientEntries.push_back(entry);
		}

		for (const auto& entry : syntheticIngredientEntries_) {
			alchemyMenu_->ingredientEntries.push_back(entry);
		}

		alchemyMenu_->potionCreationData.ingredientEntries = std::addressof(alchemyMenu_->ingredientEntries);

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info(
				"RecipeModeSession: applied synthetic entries (vanilla={} synthetic={} total={})",
				vanillaIngredientEntries_.size(),
				syntheticIngredientEntries_.size(),
				alchemyMenu_->ingredientEntries.size());
		}
	}

	void RecipeModeSession::RestoreVanillaIngredientEntries()
	{
		if (!alchemyMenu_ || !vanillaCaptured_) {
			return;
		}

		RE::BSTSmallArray<std::uint32_t, 4> filteredSelectedIndexes;
		for (const auto selectedIndex : alchemyMenu_->selectedIndexes) {
			if (selectedIndex < vanillaIngredientEntries_.size()) {
				filteredSelectedIndexes.push_back(selectedIndex);
			}
		}

		alchemyMenu_->ingredientEntries.clear();
		for (const auto& entry : vanillaIngredientEntries_) {
			alchemyMenu_->ingredientEntries.push_back(entry);
		}
		alchemyMenu_->potionCreationData.ingredientEntries = std::addressof(alchemyMenu_->ingredientEntries);

		alchemyMenu_->selectedIndexes = filteredSelectedIndexes;
		if (alchemyMenu_->currentIngredientIdx >= vanillaIngredientEntries_.size()) {
			alchemyMenu_->currentIngredientIdx = vanillaIngredientEntries_.empty() ?
				0u :
				static_cast<std::uint32_t>(vanillaIngredientEntries_.size() - 1);
		}
	}

	void RecipeModeSession::ClearSyntheticEntries()
	{
		syntheticIngredientEntries_.clear();
		syntheticInventoryEntries_.clear();
		syntheticFormIDs_.clear();
		syntheticEntryPointers_.clear();
	}

	void RecipeModeSession::ApplyUiState()
	{
		SyncRootState();

		if (!movie_) {
			return;
		}

		if (enabled_) {
			movie_->SetVariable("_root.Menu.bCanCraft", RE::GFxValue(false));
		}

		if (alchemyMenu_ && alchemyMenu_->craftingMenu.IsDisplayObject()) {
			alchemyMenu_->craftingMenu.Invoke("UpdateButtonText");
		}
	}

	void RecipeModeSession::RefreshMenu(bool a_fullRebuild)
	{
		if (!alchemyMenu_ || !alchemyMenu_->craftingMenu.IsDisplayObject()) {
			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::warn(
					"RecipeModeSession: RefreshMenu skipped (alchemyMenu={} craftingMenuDisplayObject={})",
					static_cast<const void*>(alchemyMenu_),
					alchemyMenu_ ? alchemyMenu_->craftingMenu.IsDisplayObject() : false);
			}
			return;
		}

		std::array args{ RE::GFxValue(a_fullRebuild) };
		alchemyMenu_->craftingMenu.Invoke("UpdateItemList", nullptr, args.data(), static_cast<RE::UPInt>(args.size()));
		alchemyMenu_->craftingMenu.Invoke("UpdateItemDisplay");
		alchemyMenu_->craftingMenu.Invoke("UpdateButtonText");

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info("RecipeModeSession: RefreshMenu invoked (fullRebuild={})", a_fullRebuild);
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
		return menuOpen_;
	}

	bool RecipeModeSession::IsEnabled() const noexcept
	{
		return enabled_;
	}

	bool RecipeModeSession::HasAlchemyMenu() const noexcept
	{
		return alchemyMenu_ != nullptr;
	}

	bool RecipeModeSession::ShouldBlockCraft() const noexcept
	{
		return enabled_;
	}

	bool RecipeModeSession::IsSyntheticEntry(const RE::InventoryEntryData* a_entryData) const noexcept
	{
		return a_entryData && syntheticEntryPointers_.contains(a_entryData);
	}

	bool RecipeModeSession::IsSyntheticForm(RE::FormID a_formID) const noexcept
	{
		return syntheticFormIDs_.contains(a_formID);
	}

	bool RecipeModeSession::IsCurrentMovieAlchemy() const noexcept
	{
		return UI::AlchemyUiInjector::GetSingleton().IsAlchemyMovie(movie_);
	}
}
