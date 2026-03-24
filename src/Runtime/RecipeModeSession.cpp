#include "Runtime/RecipeModeSession.h"

#include "Alchemy/IngredientRegistry.h"
#include "Config/Settings.h"
#include "Hooks/AlchemyMenuHooks.h"
#include "PCH/PCH.h"
#include "RE/S/SendUIMessage.h"
#include "UI/AlchemyUiInjector.h"

namespace
{
	void NotifyGhostInventoryMutation(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_item, bool a_added)
	{
		if (!a_player || !a_item || !ARV::Config::Settings::GetSingleton().NativeAddItemProbe()) {
			return;
		}

		RE::SendUIMessage::SendInventoryUpdateMessage(a_player, a_item);

		if (a_added) {
			a_player->AddPlayerAddItemEvent(a_item, nullptr, nullptr, RE::AQUIRE_TYPE::kNone);
		}

		if (ARV::Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info(
				"RecipeModeSession: native add-item probe notified inventory mutation for {:08X} (added={})",
				a_item->GetFormID(),
				a_added);
		}
	}

	std::string JoinStrings(const std::vector<std::string>& a_values, std::string_view a_separator)
	{
		if (a_values.empty()) {
			return {};
		}

		std::string joined = a_values.front();
		for (std::size_t i = 1; i < a_values.size(); ++i) {
			joined += a_separator;
			joined += a_values[i];
		}

		return joined;
	}

	std::string ResolveIngredientName(const ARV::Alchemy::IngredientRecord& a_record)
	{
		if (!a_record.displayName.empty()) {
			return a_record.displayName;
		}

		if (a_record.ingredient) {
			if (const auto* name = a_record.ingredient->GetName(); name && name[0] != '\0') {
				return name;
			}
		}

		return std::format("Ingredient_{:08X}", a_record.formID);
	}

	std::string ResolveEffectName(RE::FormID a_formID)
	{
		const auto* effect = RE::TESForm::LookupByID<RE::EffectSetting>(a_formID);
		if (!effect) {
			return std::format("Effect_{:08X}", a_formID);
		}

		if (const auto* name = effect->GetName(); name && name[0] != '\0') {
			return name;
		}

		if (const auto* fullName = effect->GetFullName(); fullName && fullName[0] != '\0') {
			return fullName;
		}

		return std::format("Effect_{:08X}", a_formID);
	}

	std::vector<RE::FormID> GetKnownEffectFormIDs(const ARV::Alchemy::IngredientRecord& a_record)
	{
		std::vector<RE::FormID> knownEffects;
		if (!a_record.ingredient) {
			return knownEffects;
		}

		const auto knownMask = a_record.ingredient->gamedata.knownEffectFlags;
		for (std::size_t i = 0; i < a_record.effectFormIDs.size(); ++i) {
			if ((knownMask & (1u << i)) == 0) {
				continue;
			}

			const auto effectFormID = a_record.effectFormIDs[i];
			if (effectFormID == 0) {
				continue;
			}

			knownEffects.push_back(effectFormID);
		}

		std::sort(knownEffects.begin(), knownEffects.end());
		return knownEffects;
	}

	std::vector<RE::FormID> BuildRecipeEffectSet(const std::vector<std::vector<RE::FormID>>& a_sources)
	{
		std::unordered_map<RE::FormID, std::uint32_t> counts;
		for (const auto& source : a_sources) {
			for (const auto formID : source) {
				++counts[formID];
			}
		}

		std::vector<RE::FormID> sharedEffects;
		sharedEffects.reserve(counts.size());
		for (const auto& [formID, count] : counts) {
			if (count >= 2) {
				sharedEffects.push_back(formID);
			}
		}

		std::sort(sharedEffects.begin(), sharedEffects.end());
		return sharedEffects;
	}

	std::string BuildCandidateKey(const std::vector<RE::FormID>& a_partnerIDs, const std::vector<RE::FormID>& a_effectIDs)
	{
		std::string key;
		for (const auto formID : a_partnerIDs) {
			key += std::format("{:08X}|", formID);
		}
		key += '#';
		for (const auto formID : a_effectIDs) {
			key += std::format("{:08X}|", formID);
		}
		return key;
	}

	std::string BuildCandidateSummary(const std::vector<std::string>& a_effectNames)
	{
		return JoinStrings(a_effectNames, ", ");
	}
}

namespace ARV
{
	RecipeModeSession& RecipeModeSession::GetSingleton()
	{
		static RecipeModeSession singleton;
		return singleton;
	}

	void RecipeModeSession::PublishCraftingMenuOpened(RE::GFxMovieView* a_movie)
	{
		publishedMovie_.store(a_movie, std::memory_order_release);
		publishedOpenRequested_.store(true, std::memory_order_release);
		menuOpen_.store(true, std::memory_order_release);

		spdlog::info(
			"RecipeModeSession: published crafting menu open on thread {} (movie={})",
			GetCurrentThreadId(),
			static_cast<const void*>(a_movie));
	}

	void RecipeModeSession::PublishCraftingMenuClosed()
	{
		publishedMovie_.store(nullptr, std::memory_order_release);
		publishedAlchemyMenu_.store(nullptr, std::memory_order_release);
		publishedOpenRequested_.store(false, std::memory_order_release);
		publishedBindRequested_.store(false, std::memory_order_release);
		publishedCloseRequested_.store(true, std::memory_order_release);
		pendingToggleCount_.store(0, std::memory_order_release);
		menuOpen_.store(false, std::memory_order_release);

		spdlog::info("RecipeModeSession: published crafting menu close on thread {}", GetCurrentThreadId());
	}

	void RecipeModeSession::PublishAlchemyMenuBound(AlchemyMenu* a_menu)
	{
		publishedAlchemyMenu_.store(a_menu, std::memory_order_release);
		publishedBindRequested_.store(true, std::memory_order_release);

		spdlog::info(
			"RecipeModeSession: published AlchemyMenu bind={} on thread {}",
			static_cast<const void*>(a_menu),
			GetCurrentThreadId());
	}

	void RecipeModeSession::RequestToggle()
	{
		pendingToggleCount_.fetch_add(1, std::memory_order_acq_rel);
	}

	void RecipeModeSession::RequestGhostSelection(std::uint32_t a_formID)
	{
		pendingGhostSelectionFormID_.store(a_formID, std::memory_order_release);
		pendingGhostSelectionRequested_.store(true, std::memory_order_release);
	}

	void RecipeModeSession::TickOwnerThread()
	{
		if (ConsumePublishedState()) {
			return;
		}

		ProcessPendingGhostSelection();
		ProcessPendingToggle();
	}

	bool RecipeModeSession::ConsumePublishedState()
	{
		if (publishedCloseRequested_.exchange(false, std::memory_order_acq_rel)) {
			HandlePublishedClose();
			return true;
		}

		if (deferredCleanupPending_) {
			ProcessDeferredCleanup();
		}

		if (publishedOpenRequested_.exchange(false, std::memory_order_acq_rel)) {
			HandlePublishedOpen(publishedMovie_.exchange(nullptr, std::memory_order_acq_rel));
		}

		if (publishedBindRequested_.exchange(false, std::memory_order_acq_rel)) {
			HandlePublishedBind(publishedAlchemyMenu_.exchange(nullptr, std::memory_order_acq_rel));
		}

		return false;
	}

	void RecipeModeSession::ProcessPendingToggle()
	{
		if (!menuOpen_.load(std::memory_order_acquire)) {
			return;
		}

		if (!movie_ || !alchemyMenu_) {
			return;
		}

		if (!IsCurrentMovieAlchemy()) {
			const auto dropped = pendingToggleCount_.exchange(0, std::memory_order_acq_rel);
			if (dropped > 0) {
				spdlog::info("RecipeModeSession: dropped {} pending toggle(s) for non-alchemy movie", dropped);
			}
			return;
		}

		const auto count = pendingToggleCount_.exchange(0, std::memory_order_acq_rel);
		if (count == 0) {
			return;
		}

		if (count % 2 == 0) {
			spdlog::info("RecipeModeSession: toggle cancelled (even count={}, net no-op)", count);
			return;
		}

		spdlog::info(
			"RecipeModeSession: processing queued toggle on owner thread {} (count={} generation={})",
			GetCurrentThreadId(),
			count,
			menuGeneration_.load(std::memory_order_acquire));

		Toggle();
	}

	void RecipeModeSession::ProcessPendingGhostSelection()
	{
		if (!pendingGhostSelectionRequested_.exchange(false, std::memory_order_acq_rel)) {
			return;
		}

		const auto formID = pendingGhostSelectionFormID_.exchange(0, std::memory_order_acq_rel);
		if (!formID) {
			return;
		}

		if (!movie_ || !enabled_.load(std::memory_order_acquire) || !menuOpen_.load(std::memory_order_acquire)) {
			selectedGhostFormID_ = 0;
			if (movie_) {
				UI::AlchemyUiInjector::GetSingleton().ClearSelectedGhost(movie_);
			}
			return;
		}

		const auto it = std::find_if(
			ghostIngredients_.begin(),
			ghostIngredients_.end(),
			[formID](const GhostIngredient& a_ghost) {
				return a_ghost.formID == formID;
			});

		if (it == ghostIngredients_.end()) {
			selectedGhostFormID_ = 0;
			UI::AlchemyUiInjector::GetSingleton().ClearSelectedGhost(movie_);
			spdlog::info(
				"RecipeModeSession: ignored ghost selection for unknown formID={:08X}",
				formID);
			return;
		}

		selectedGhostFormID_ = formID;
		UI::AlchemyUiInjector::GetSingleton().PublishSelectedGhost(movie_, *it);

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info(
				"RecipeModeSession: selected ghost ingredient '{}' (formID={:08X})",
				it->originalName,
				it->formID);
		}
	}

	void RecipeModeSession::HandlePublishedOpen(RE::GFxMovieView* a_movie)
	{
		if (!a_movie) {
			menuOpen_.store(false, std::memory_order_release);
			spdlog::warn("RecipeModeSession: published open missing movie pointer");
			return;
		}

		movie_ = a_movie;
		alchemyMenu_ = nullptr;
		uiInjected_ = false;
		inventoryInjected_ = false;
		enabled_.store(false, std::memory_order_release);
		++menuGeneration_;
		ghostIngredients_.clear();
		selectedGhostFormID_ = 0;
		pendingGhostSelectionRequested_.store(false, std::memory_order_release);
		pendingGhostSelectionFormID_.store(0, std::memory_order_release);

		SyncRootState();

		const auto& settings = Config::Settings::GetSingleton();
		if (settings.ShowNavButton()) {
			UI::AlchemyUiInjector::GetSingleton().InjectButtonShim(movie_, settings.ToggleKey());
			uiInjected_ = true;
		}

		spdlog::info(
			"RecipeModeSession: crafting menu opened (alchemy movie={} generation={})",
			IsCurrentMovieAlchemy(),
			menuGeneration_.load(std::memory_order_acquire));
	}

	void RecipeModeSession::HandlePublishedClose()
	{
		++menuGeneration_;
		pendingToggleCount_.store(0, std::memory_order_release);
		pendingGhostSelectionRequested_.store(false, std::memory_order_release);
		pendingGhostSelectionFormID_.store(0, std::memory_order_release);
		menuOpen_.store(false, std::memory_order_release);
		selectedGhostFormID_ = 0;

		if (!ghostIngredients_.empty()) {
			deferredCleanup_ = std::move(ghostIngredients_);
			ghostIngredients_.clear();
			deferredCleanupPending_ = !deferredCleanup_.empty();
		}

		enabled_.store(false, std::memory_order_release);
		inventoryInjected_ = false;
		uiInjected_ = false;
		effectPartitionMap_.clear();
		movie_ = nullptr;
		alchemyMenu_ = nullptr;

		spdlog::info(
			"RecipeModeSession: crafting menu closed (generation={} deferredCleanup={})",
			menuGeneration_.load(std::memory_order_acquire),
			deferredCleanup_.size());
	}

	void RecipeModeSession::HandlePublishedBind(AlchemyMenu* a_menu)
	{
		if (!a_menu) {
			return;
		}

		if (alchemyMenu_ == a_menu) {
			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: skipping duplicate AlchemyMenu bind for {}",
					static_cast<const void*>(a_menu));
			}
			return;
		}

		alchemyMenu_ = a_menu;
		if (!movie_) {
			movie_ = alchemyMenu_->view;
			SyncRootState();
		}

		spdlog::info("RecipeModeSession: bound AlchemyMenu={}", static_cast<const void*>(alchemyMenu_));
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

	void RecipeModeSession::EnableRecipeMode()
	{
		if (enabled_.load(std::memory_order_acquire)) {
			return;
		}

		if (!movie_ || !alchemyMenu_) {
			spdlog::warn(
				"RecipeModeSession: enable skipped (movie={} alchemyMenu={})",
				static_cast<const void*>(movie_),
				static_cast<const void*>(alchemyMenu_));
			return;
		}

		playerSnapshot_.Rebuild(Alchemy::IngredientRegistry::GetSingleton());
		BuildGhostIngredients();
		BuildGhostRecipeCandidates();

		if (ghostIngredients_.empty()) {
			spdlog::info("RecipeModeSession: no missing known ingredients found");
			return;
		}

		BuildEffectPartitionMap();
		ComputeGhostPartitionFlags();
		ApplyGhostDisplayNames();
		AddGhostItemsToInventory();
		inventoryInjected_ = true;

		// RAII guard: if anything below fails, roll back any partial ghost state.
		auto guard = SKSE::stl::scope_exit([this]() {
			spdlog::warn("RecipeModeSession: enable failed, rolling back ghost state");
			RemoveGhostMenuEntries();
			if (inventoryInjected_) {
				RemoveGhostItemsFromInventory(ghostIngredients_);
			}
			RestoreOriginalNames(ghostIngredients_);
			ghostIngredients_.clear();
			inventoryInjected_ = false;
		});

		PopulateGhostMenuEntries();

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
		if (movie_) {
			UI::AlchemyUiInjector::GetSingleton().ResetAlchemySelectionUi(movie_);
		}

		ClearSelectionState();
		RemoveGhostMenuEntries();

		if (!ghostIngredients_.empty()) {
			RemoveGhostItemsFromInventory(ghostIngredients_);
			RestoreOriginalNames(ghostIngredients_);
		}

		ghostIngredients_.clear();
		selectedGhostFormID_ = 0;
		pendingGhostSelectionRequested_.store(false, std::memory_order_release);
		pendingGhostSelectionFormID_.store(0, std::memory_order_release);
		enabled_.store(false, std::memory_order_release);
		inventoryInjected_ = false;

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

	void RecipeModeSession::BuildGhostRecipeCandidates()
	{
		if (ghostIngredients_.empty()) {
			return;
		}

		const auto& registry = Alchemy::IngredientRegistry::GetSingleton();

		struct OwnedKnownIngredient
		{
			const Alchemy::IngredientRecord* record{ nullptr };
			std::vector<RE::FormID>          knownEffects{};
		};

		std::vector<OwnedKnownIngredient> ownedKnownIngredients;
		for (const auto& [formID, record] : registry.Records()) {
			if (!record.ingredient || !playerSnapshot_.HasKnownEffects(formID) || playerSnapshot_.Count(formID) <= 0) {
				continue;
			}

			auto knownEffects = GetKnownEffectFormIDs(record);
			if (knownEffects.empty()) {
				continue;
			}

			ownedKnownIngredients.push_back(OwnedKnownIngredient{ std::addressof(record), std::move(knownEffects) });
		}

		for (auto& ghost : ghostIngredients_) {
			ghost.recipeCandidates.clear();

			const auto* ghostRecord = registry.Find(ghost.formID);
			if (!ghostRecord || !ghostRecord->ingredient) {
				continue;
			}

			const auto ghostKnownEffects = GetKnownEffectFormIDs(*ghostRecord);
			if (ghostKnownEffects.empty()) {
				continue;
			}

			std::unordered_set<std::string> seenKeys;

			auto appendCandidate =
				[&ghost, &seenKeys](std::vector<const Alchemy::IngredientRecord*> a_partners, std::vector<RE::FormID> a_effectIDs) {
					if (a_partners.empty() || a_effectIDs.empty()) {
						return;
					}

					std::vector<RE::FormID> partnerIDs;
					std::vector<std::string> partnerNames;
					std::vector<std::string> effectNames;
					partnerIDs.reserve(a_partners.size());
					partnerNames.reserve(a_partners.size());
					effectNames.reserve(a_effectIDs.size());

					for (const auto* partner : a_partners) {
						if (!partner) {
							return;
						}
						partnerIDs.push_back(partner->formID);
						partnerNames.push_back(ResolveIngredientName(*partner));
					}

					for (const auto effectFormID : a_effectIDs) {
						effectNames.push_back(ResolveEffectName(effectFormID));
					}

					const auto key = BuildCandidateKey(partnerIDs, a_effectIDs);
					if (!seenKeys.insert(key).second) {
						return;
					}

					GhostRecipeCandidate candidate;
					candidate.partnerFormIDs = std::move(partnerIDs);
					candidate.partnerNames = std::move(partnerNames);
					candidate.effectFormIDs = std::move(a_effectIDs);
					candidate.effectNames = std::move(effectNames);
					candidate.title = JoinStrings(candidate.partnerNames, " + ");
					candidate.summary = BuildCandidateSummary(candidate.effectNames);

					ghost.recipeCandidates.push_back(std::move(candidate));
				};

			for (std::size_t i = 0; i < ownedKnownIngredients.size(); ++i) {
				const auto& first = ownedKnownIngredients[i];

				auto pairEffects = BuildRecipeEffectSet({ ghostKnownEffects, first.knownEffects });
				appendCandidate({ first.record }, std::move(pairEffects));

				for (std::size_t j = i + 1; j < ownedKnownIngredients.size(); ++j) {
					const auto& second = ownedKnownIngredients[j];
					auto tripleEffects = BuildRecipeEffectSet({ ghostKnownEffects, first.knownEffects, second.knownEffects });
					appendCandidate({ first.record, second.record }, std::move(tripleEffects));
				}
			}

			std::sort(
				ghost.recipeCandidates.begin(),
				ghost.recipeCandidates.end(),
				[](const GhostRecipeCandidate& a_left, const GhostRecipeCandidate& a_right) {
					if (a_left.partnerNames.size() != a_right.partnerNames.size()) {
						return a_left.partnerNames.size() < a_right.partnerNames.size();
					}

					if (a_left.title != a_right.title) {
						return a_left.title < a_right.title;
					}

					return a_left.summary < a_right.summary;
				});

			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: built {} recipe candidates for ghost '{}' ({:08X})",
					ghost.recipeCandidates.size(),
					ghost.originalName,
					ghost.formID);
			}
		}
	}

	void RecipeModeSession::BuildEffectPartitionMap()
	{
		effectPartitionMap_.clear();

		if (!alchemyMenu_) {
			return;
		}

		for (const auto& entry : alchemyMenu_->ingredientEntries) {
			if (!entry.ingredient || !entry.ingredient->object) {
				continue;
			}

			const auto* ingredient = entry.ingredient->object->As<RE::IngredientItem>();
			if (!ingredient) {
				continue;
			}

			const std::uint8_t filterIDs[4] = {
				entry.effect1FilterID,
				entry.effect2FilterID,
				entry.effect3FilterID,
				entry.effect4FilterID
			};

			const auto effectCount = std::min<std::size_t>(4, ingredient->effects.size());
			for (std::size_t i = 0; i < effectCount; ++i) {
				if (filterIDs[i] == 0) {
					continue;
				}

				const auto* effect = ingredient->effects[static_cast<RE::BSTArray<RE::Effect*>::size_type>(i)];
				if (!effect || !effect->baseEffect) {
					continue;
				}

				effectPartitionMap_[effect->baseEffect->GetFormID()] = filterIDs[i];
			}
		}

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info(
				"RecipeModeSession: built effect partition map with {} entries from {} ingredient entries",
				effectPartitionMap_.size(),
				alchemyMenu_->ingredientEntries.size());
		}
	}

	void RecipeModeSession::ComputeGhostPartitionFlags()
	{
		for (auto& ghost : ghostIngredients_) {
			if (!ghost.ingredient) {
				continue;
			}

			std::uint8_t partitionIDs[4] = { 0, 0, 0, 0 };
			const auto effectCount = std::min<std::size_t>(4, ghost.ingredient->effects.size());

			for (std::size_t i = 0; i < effectCount; ++i) {
				const auto* effect = ghost.ingredient->effects[static_cast<RE::BSTArray<RE::Effect*>::size_type>(i)];
				if (!effect || !effect->baseEffect) {
					continue;
				}

				const auto it = effectPartitionMap_.find(effect->baseEffect->GetFormID());
				if (it != effectPartitionMap_.end()) {
					partitionIDs[i] = it->second;
				}
			}

			ghost.partitionFilterFlag =
				static_cast<std::uint32_t>(partitionIDs[0]) |
				(static_cast<std::uint32_t>(partitionIDs[1]) << 8) |
				(static_cast<std::uint32_t>(partitionIDs[2]) << 16) |
				(static_cast<std::uint32_t>(partitionIDs[3]) << 24);

			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: ghost '{}' ({:08X}) partitionFilterFlag={:08X} ids=[{},{},{},{}]",
					ghost.originalName,
					ghost.formID,
					ghost.partitionFilterFlag,
					partitionIDs[0], partitionIDs[1], partitionIDs[2], partitionIDs[3]);
			}
		}
	}

	void RecipeModeSession::PopulateGhostMenuEntries()
	{
		if (!alchemyMenu_ || ghostIngredients_.empty()) {
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		auto* inventoryChanges = player->GetInventoryChanges();
		if (!inventoryChanges || !inventoryChanges->entryList) {
			spdlog::warn("RecipeModeSession: no inventory changes available for ghost menu entries");
			return;
		}

		const auto sizeBefore = alchemyMenu_->ingredientEntries.size();

		for (auto& ghost : ghostIngredients_) {
			RE::InventoryEntryData* entryData = nullptr;

			for (auto* entry : *inventoryChanges->entryList) {
				if (entry && entry->object == ghost.ingredient) {
					entryData = entry;
					break;
				}
			}

			if (!entryData) {
				if (Config::Settings::GetSingleton().DebugLogging()) {
					spdlog::warn(
						"RecipeModeSession: no InventoryEntryData found for ghost '{}' ({:08X})",
						ghost.originalName, ghost.formID);
				}
				continue;
			}

			AlchemyMenu::MenuIngredientEntry newEntry{};
			std::memset(&newEntry, 0, sizeof(newEntry));
			newEntry.ingredient = entryData;
			newEntry.isSelected = 0;
			newEntry.isNotGreyed = 1;

			const auto effectCount = std::min<std::size_t>(4, ghost.ingredient->effects.size());
			for (std::size_t i = 0; i < effectCount; ++i) {
				const auto* effect = ghost.ingredient->effects[static_cast<RE::BSTArray<RE::Effect*>::size_type>(i)];
				if (!effect || !effect->baseEffect) {
					continue;
				}

				const auto it = effectPartitionMap_.find(effect->baseEffect->GetFormID());
				if (it == effectPartitionMap_.end()) {
					continue;
				}

				switch (i) {
				case 0: newEntry.effect1FilterID = it->second; break;
				case 1: newEntry.effect2FilterID = it->second; break;
				case 2: newEntry.effect3FilterID = it->second; break;
				case 3: newEntry.effect4FilterID = it->second; break;
				}
			}

			alchemyMenu_->ingredientEntries.push_back(newEntry);

			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: appended ghost '{}' ({:08X}) to ingredientEntries [{}->{}] filterIDs=[{},{},{},{}]",
					ghost.originalName, ghost.formID,
					sizeBefore, alchemyMenu_->ingredientEntries.size(),
					newEntry.effect1FilterID, newEntry.effect2FilterID,
					newEntry.effect3FilterID, newEntry.effect4FilterID);
			}
		}
	}

	void RecipeModeSession::RemoveGhostMenuEntries()
	{
		if (!alchemyMenu_ || ghostIngredients_.empty()) {
			return;
		}

		// Build a set of InventoryEntryData* pointers that belong to ghost ingredients.
		// Matching by pointer is robust against the game rebuilding ingredientEntries
		// (e.g. after crafting), which would invalidate position-based tracking.
		std::unordered_set<RE::TESBoundObject*> ghostObjects;
		ghostObjects.reserve(ghostIngredients_.size());
		for (const auto& ghost : ghostIngredients_) {
			if (ghost.ingredient) {
				ghostObjects.insert(ghost.ingredient);
			}
		}

		auto& entries = alchemyMenu_->ingredientEntries;
		const auto sizeBefore = entries.size();

		// Erase-remove: walk backwards to avoid iterator invalidation issues with BSTArray.
		for (std::int32_t i = static_cast<std::int32_t>(entries.size()) - 1; i >= 0; --i) {
			const auto& entry = entries[static_cast<std::uint32_t>(i)];
			if (entry.ingredient && entry.ingredient->object &&
				ghostObjects.contains(entry.ingredient->object)) {
				entries.erase(entries.begin() + i);
			}
		}

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info(
				"RecipeModeSession: removed ghost menu entries ({}->{})",
				sizeBefore, entries.size());
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
			player->AddObjectToContainer(ghost.ingredient, nullptr, 1, nullptr);
			NotifyGhostInventoryMutation(player, ghost.ingredient, true);

			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: added ghost '{}' (formID={:08X})",
					ghost.ingredient->GetName() ? ghost.ingredient->GetName() : ghost.originalName,
					ghost.formID);
			}
		}
	}

	void RecipeModeSession::ApplyGhostDisplayNames()
	{
		for (auto& ghost : ghostIngredients_) {
			ghost.ingredient->fullName = ghost.originalName + " (0)";
		}
	}

	void RecipeModeSession::RemoveGhostItemsFromInventory(std::vector<GhostIngredient>& a_ghosts)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			spdlog::error("RecipeModeSession: PlayerCharacter unavailable for removal");
			return;
		}

		for (auto& ghost : a_ghosts) {
			const auto inventoryBefore = player->GetInventory();
			const auto it = inventoryBefore.find(ghost.ingredient);
			if (it == inventoryBefore.end() || it->second.first <= 0) {
				if (Config::Settings::GetSingleton().DebugLogging()) {
					spdlog::info(
						"RecipeModeSession: ghost '{}' (formID={:08X}) already absent from inventory",
						ghost.originalName,
						ghost.formID);
				}
				continue;
			}

			const auto countToRemove = static_cast<std::int32_t>(it->second.first);
			player->RemoveItem(ghost.ingredient, countToRemove, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			NotifyGhostInventoryMutation(player, ghost.ingredient, false);

			auto remainingCount = 0;
			const auto inventoryAfter = player->GetInventory();
			if (const auto afterIt = inventoryAfter.find(ghost.ingredient); afterIt != inventoryAfter.end()) {
				remainingCount = afterIt->second.first;
			}

			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: removed ghost '{}' (formID={:08X}) count={} remaining={}",
					ghost.ingredient->GetName() ? ghost.ingredient->GetName() : "<null>",
					ghost.formID,
					countToRemove,
					remainingCount);
			}

			if (remainingCount > 0) {
				spdlog::warn(
					"RecipeModeSession: ghost '{}' (formID={:08X}) still present after cleanup (remaining={})",
					ghost.originalName,
					ghost.formID,
					remainingCount);
			}
		}
	}

	void RecipeModeSession::RestoreOriginalNames(std::vector<GhostIngredient>& a_ghosts)
	{
		for (auto& ghost : a_ghosts) {
			ghost.ingredient->fullName = ghost.originalName;

			if (Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info(
					"RecipeModeSession: restored name '{}' (formID={:08X})",
					ghost.originalName,
					ghost.formID);
			}
		}
	}

	void RecipeModeSession::ProcessDeferredCleanup()
	{
		deferredCleanupPending_ = false;

		if (deferredCleanup_.empty()) {
			return;
		}

		RemoveGhostItemsFromInventory(deferredCleanup_);
		RestoreOriginalNames(deferredCleanup_);

		spdlog::info(
			"RecipeModeSession: deferred cleanup complete ({} ghosts)",
			deferredCleanup_.size());

		deferredCleanup_.clear();
	}

	void RecipeModeSession::RefreshMenu()
	{
		if (!movie_) {
			return;
		}

		if (Config::Settings::GetSingleton().PapyrusRefreshProbe()) {
			UI::AlchemyUiInjector::GetSingleton().RefreshAlchemyListPapyrusStyle(movie_, true);
		} else if (alchemyMenu_ && alchemyMenu_->craftingMenu.IsDisplayObject()) {
			std::array args{ RE::GFxValue(true) };
			alchemyMenu_->craftingMenu.Invoke("UpdateItemList", nullptr, args.data(), static_cast<RE::UPInt>(args.size()));
			alchemyMenu_->craftingMenu.Invoke("UpdateItemDisplay");
			alchemyMenu_->craftingMenu.Invoke("UpdateButtonText");
			UI::AlchemyUiInjector::GetSingleton().InvalidateAlchemyList(movie_);
		} else {
			UI::AlchemyUiInjector::GetSingleton().InvalidateAlchemyList(movie_);
		}

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info(
				"RecipeModeSession: alchemy list rebuilt using {} refresh path",
				Config::Settings::GetSingleton().PapyrusRefreshProbe() ? "papyrus-style" : "current");
		}
	}

	void RecipeModeSession::SetCraftingBlocked(bool a_blocked)
	{
		if (!movie_) {
			return;
		}

		const auto effectiveBlocked = a_blocked && Config::Settings::GetSingleton().BlockCraftWhileEnabled();

		RE::GFxValue menu;
		if (movie_->GetVariable(std::addressof(menu), "_root.Menu") && menu.IsObject()) {
			menu.SetMember("bCanCraft", RE::GFxValue(!effectiveBlocked));
			menu.Invoke("UpdateButtonText");
		}

		RE::GFxValue itemList;
		if (movie_->GetVariable(std::addressof(itemList), "_root.Menu.ItemList") && itemList.IsObject()) {
			itemList.SetMember("_alpha", RE::GFxValue(effectiveBlocked ? 70.0 : 100.0));
		}
	}

	void RecipeModeSession::SyncRootState()
	{
		if (!movie_) {
			return;
		}

		const auto& settings = Config::Settings::GetSingleton();
		auto& injector = UI::AlchemyUiInjector::GetSingleton();
		injector.SyncRootState(
			movie_,
			enabled_,
			settings.ToggleKey(),
			settings.ShowNavButton());

		if (ghostIngredients_.empty()) {
			injector.ClearGhostIngredients(movie_);
		} else {
			injector.PublishGhostIngredients(movie_, ghostIngredients_);
		}

		if (selectedGhostFormID_ == 0) {
			injector.ClearSelectedGhost(movie_);
			return;
		}

		const auto it = std::find_if(
			ghostIngredients_.begin(),
			ghostIngredients_.end(),
			[this](const GhostIngredient& a_ghost) {
				return a_ghost.formID == selectedGhostFormID_;
			});

		if (it == ghostIngredients_.end()) {
			selectedGhostFormID_ = 0;
			injector.ClearSelectedGhost(movie_);
			return;
		}

		injector.PublishSelectedGhost(movie_, *it);
	}

	void RecipeModeSession::ClearSelectionState()
	{
		if (!alchemyMenu_) {
			return;
		}

		alchemyMenu_->selectedIndexes.clear();
		alchemyMenu_->currentIngredientIdx = static_cast<std::uint32_t>(-1);
		alchemyMenu_->resultPotionEntry = nullptr;
		alchemyMenu_->resultPotion = nullptr;
		alchemyMenu_->unknownPotion = nullptr;
		alchemyMenu_->potionCreationData.usableEffectsMaps.clear();

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info("RecipeModeSession: cleared alchemy selection state");
		}
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
		return enabled_ && Config::Settings::GetSingleton().BlockCraftWhileEnabled();
	}
}
