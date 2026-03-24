#include "UI/AlchemyUiInjector.h"

#include <optional>

#include "Alchemy/MissingIngredient.h"
#include "Config/Settings.h"
#include "PCH/PCH.h"
#include "Runtime/RecipeModeSession.h"

namespace
{
	constexpr auto kShimMovieName = "AlchemyRecipeViewVR.swf"sv;
	constexpr auto kArvNamespace = "arv"sv;
	constexpr std::uint32_t kFilterIngredients = 0x00000001;
	constexpr std::uint32_t kFilterGood = 0x00000002;
	constexpr std::uint32_t kFilterBad = 0x00000004;
	constexpr std::uint32_t kFilterOther = 0x00000008;
	constexpr double        kInventoryCardTypeIngredient = 8.0;

	enum class GhostEffectCategory
	{
		kGood,
		kBad,
		kOther
	};

	struct PublishedGhostMetadata
	{
		std::uint32_t             filterFlag{ kFilterIngredients };
		std::uint32_t             numItemEffects{ 0 };
		std::array<std::string, 4> itemEffects{};
		std::string               goodEffects;
		std::string               badEffects;
		std::string               otherEffects;
		std::string               iconLabel{ "default_ingredient" };
		double                    iconColor{ 0.0 };
		bool                      hasIconColor{ false };
	};

	std::string ResolveEffectName(const RE::EffectSetting* a_effect)
	{
		if (!a_effect) {
			return {};
		}

		if (const auto* name = a_effect->GetName(); name && name[0] != '\0') {
			return name;
		}

		if (const auto* fullName = a_effect->GetFullName(); fullName && fullName[0] != '\0') {
			return fullName;
		}

		return std::format("Effect_{:08X}", a_effect->GetFormID());
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

	GhostEffectCategory ClassifyEffect(const RE::EffectSetting* a_effect)
	{
		if (!a_effect) {
			return GhostEffectCategory::kOther;
		}

		if (a_effect->IsDetrimental() || a_effect->IsHostile()) {
			return GhostEffectCategory::kBad;
		}

		if (a_effect->data.primaryAV != RE::ActorValue::kNone ||
			a_effect->data.secondaryAV != RE::ActorValue::kNone ||
			a_effect->data.resistVariable != RE::ActorValue::kNone) {
			return GhostEffectCategory::kGood;
		}

		return GhostEffectCategory::kOther;
	}

	PublishedGhostMetadata BuildGhostMetadata(const ARV::GhostIngredient& a_ghost)
	{
		PublishedGhostMetadata metadata;
		if (!a_ghost.ingredient) {
			return metadata;
		}

		const auto totalEffects =
			std::min<std::size_t>(static_cast<std::size_t>(a_ghost.ingredient->effects.size()), metadata.itemEffects.size());
		metadata.numItemEffects = static_cast<std::uint32_t>(totalEffects);

		std::vector<std::string> goodEffects;
		std::vector<std::string> badEffects;
		std::vector<std::string> otherEffects;
		std::optional<GhostEffectCategory> primaryCategory;

		for (std::size_t i = 0; i < totalEffects; ++i) {
			const auto* effect = a_ghost.ingredient->effects[static_cast<RE::BSTArray<RE::Effect*>::size_type>(i)];
			const auto* baseEffect = effect ? effect->baseEffect : nullptr;
			if (!baseEffect) {
				continue;
			}

			const auto isKnown = (a_ghost.ingredient->gamedata.knownEffectFlags & (1u << i)) != 0;
			if (!isKnown) {
				continue;
			}

			const auto effectName = ResolveEffectName(baseEffect);
			if (effectName.empty()) {
				continue;
			}

			metadata.itemEffects[i] = effectName;

			const auto category = ClassifyEffect(baseEffect);
			if (!primaryCategory.has_value()) {
				primaryCategory = category;
			}

			switch (category) {
			case GhostEffectCategory::kBad:
				badEffects.push_back(effectName);
				break;
			case GhostEffectCategory::kOther:
				otherEffects.push_back(effectName);
				break;
			case GhostEffectCategory::kGood:
			default:
				goodEffects.push_back(effectName);
				break;
			}
		}

		// Use partition-encoded filterFlag computed from native ingredientEntries.
		// This matches the format SkyUI expects: each byte is a partition index.
		// Falls back to kFilterIngredients if no partition data was computed.
		metadata.filterFlag = a_ghost.partitionFilterFlag != 0
			? a_ghost.partitionFilterFlag
			: kFilterIngredients;

		metadata.goodEffects = JoinStrings(goodEffects, "  |  ");
		metadata.badEffects = JoinStrings(badEffects, ", ");
		metadata.otherEffects = JoinStrings(otherEffects, ", ");

		switch (primaryCategory.value_or(GhostEffectCategory::kOther)) {
		case GhostEffectCategory::kBad:
			metadata.iconLabel = "harmful";
			metadata.iconColor = 0xC73636;
			metadata.hasIconColor = true;
			break;
		case GhostEffectCategory::kGood:
			metadata.iconLabel = "beneficial";
			metadata.iconColor = 0x51DB2E;
			metadata.hasIconColor = true;
			break;
		case GhostEffectCategory::kOther:
		default:
			metadata.iconLabel = "other";
			metadata.iconColor = 0x1FFBFF;
			metadata.hasIconColor = true;
			break;
		}

		return metadata;
	}

	void PopulateGhostEntryValue(RE::GFxMovieView* a_movie, RE::GFxValue& a_entry, const ARV::GhostIngredient& a_ghost)
	{
		const auto metadata = BuildGhostMetadata(a_ghost);
		const auto displayName = a_ghost.originalName + " (0)";
		a_movie->CreateObject(std::addressof(a_entry));
		a_entry.SetMember("formId", RE::GFxValue(static_cast<double>(a_ghost.formID)));
		a_entry.SetMember("text", RE::GFxValue(displayName.c_str()));
		a_entry.SetMember("displayName", RE::GFxValue(displayName.c_str()));
		a_entry.SetMember("name", RE::GFxValue(displayName.c_str()));
		a_entry.SetMember("originalName", RE::GFxValue(a_ghost.originalName.c_str()));
		a_entry.SetMember("count", RE::GFxValue(0.0));
		a_entry.SetMember("type", RE::GFxValue(kInventoryCardTypeIngredient));
		a_entry.SetMember("formType", RE::GFxValue(static_cast<double>(RE::FormType::Ingredient)));
		a_entry.SetMember("equipState", RE::GFxValue(0.0));
		a_entry.SetMember("filterFlag", RE::GFxValue(static_cast<double>(metadata.filterFlag)));
		a_entry.SetMember("weight", RE::GFxValue(static_cast<double>(a_ghost.ingredient->GetWeight())));
		a_entry.SetMember("value", RE::GFxValue(static_cast<double>(a_ghost.ingredient->GetGoldValue())));
		a_entry.SetMember("iconLabel", RE::GFxValue(metadata.iconLabel.c_str()));
		a_entry.SetMember("numItemEffects", RE::GFxValue(static_cast<double>(metadata.numItemEffects)));
		a_entry.SetMember("upperCaseName", RE::GFxValue(false));
		a_entry.SetMember("negativeEffect", RE::GFxValue(false));
		a_entry.SetMember("isGhost", RE::GFxValue(true));
		a_entry.SetMember("isARVGhost", RE::GFxValue(true));

		if (metadata.hasIconColor) {
			a_entry.SetMember("iconColor", RE::GFxValue(metadata.iconColor));
		}

		if (!metadata.goodEffects.empty()) {
			a_entry.SetMember("goodEffects", RE::GFxValue(metadata.goodEffects.c_str()));
		}

		if (!metadata.badEffects.empty()) {
			a_entry.SetMember("badEffects", RE::GFxValue(metadata.badEffects.c_str()));
		}

		if (!metadata.otherEffects.empty()) {
			a_entry.SetMember("otherEffects", RE::GFxValue(metadata.otherEffects.c_str()));
		}

		for (std::size_t i = 0; i < metadata.itemEffects.size(); ++i) {
			if (metadata.itemEffects[i].empty()) {
				continue;
			}

			a_entry.SetMember(
				std::format("itemEffect{}", i).c_str(),
				RE::GFxValue(metadata.itemEffects[i].c_str()));
		}

		RE::GFxValue recipeCandidates;
		a_movie->CreateArray(std::addressof(recipeCandidates));
		recipeCandidates.SetArraySize(static_cast<std::uint32_t>(a_ghost.recipeCandidates.size()));

		std::vector<std::string> recipeLines;
		recipeLines.reserve(a_ghost.recipeCandidates.size());

		for (std::uint32_t i = 0; i < a_ghost.recipeCandidates.size(); ++i) {
			const auto& candidate = a_ghost.recipeCandidates[i];

			RE::GFxValue candidateValue;
			a_movie->CreateObject(std::addressof(candidateValue));
			candidateValue.SetMember("title", RE::GFxValue(candidate.title.c_str()));
			candidateValue.SetMember("summary", RE::GFxValue(candidate.summary.c_str()));
			candidateValue.SetMember("partnerText", RE::GFxValue(JoinStrings(candidate.partnerNames, " + ").c_str()));
			candidateValue.SetMember("effectsText", RE::GFxValue(JoinStrings(candidate.effectNames, ", ").c_str()));
			recipeCandidates.SetElement(i, candidateValue);

			if (!candidate.title.empty() && !candidate.summary.empty()) {
				recipeLines.push_back(candidate.title + ": " + candidate.summary);
			} else if (!candidate.title.empty()) {
				recipeLines.push_back(candidate.title);
			} else if (!candidate.summary.empty()) {
				recipeLines.push_back(candidate.summary);
			}
		}

		a_entry.SetMember("recipeCandidates", recipeCandidates);
		a_entry.SetMember("recipeCount", RE::GFxValue(static_cast<double>(a_ghost.recipeCandidates.size())));
		a_entry.SetMember("recipeDescription", RE::GFxValue(JoinStrings(recipeLines, "\n").c_str()));
	}

	class GhostSelectionHandler final : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (a_params.argCount < 1) {
				if (a_params.retVal) {
					a_params.retVal->SetBoolean(false);
				}
				return;
			}

			std::uint32_t formID = 0;
			const auto type = a_params.args[0].GetType();
			if (type == RE::GFxValue::ValueType::kNumber) {
				formID = static_cast<std::uint32_t>(a_params.args[0].GetNumber());
			} else if (type == RE::GFxValue::ValueType::kString) {
				formID = static_cast<std::uint32_t>(std::strtoul(a_params.args[0].GetString(), nullptr, 10));
			}

			if (formID != 0) {
				ARV::RecipeModeSession::GetSingleton().RequestGhostSelection(formID);
			}

			if (a_params.retVal) {
				a_params.retVal->SetBoolean(formID != 0);
			}
		}
	};
}

namespace ARV::UI
{
	AlchemyUiInjector& AlchemyUiInjector::GetSingleton()
	{
		static AlchemyUiInjector singleton;
		return singleton;
	}

	bool AlchemyUiInjector::ResolveMenuRoot(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_menu) const
	{
		if (!a_movie) {
			return false;
		}

		if (!a_movie->GetVariable(std::addressof(a_root), "_root")) {
			return false;
		}

		if (!a_movie->GetVariable(std::addressof(a_menu), "_root.Menu")) {
			return false;
		}

		return a_root.IsDisplayObject() && a_menu.IsDisplayObject();
	}

	bool AlchemyUiInjector::ResolveOrCreateArvState(
		RE::GFxMovieView* a_movie,
		RE::GFxValue& a_root,
		RE::GFxValue& a_menu,
		RE::GFxValue& a_arvState) const
	{
		if (!ResolveMenuRoot(a_movie, a_root, a_menu)) {
			return false;
		}

		if (a_root.GetMember(kArvNamespace.data(), std::addressof(a_arvState)) && a_arvState.IsObject()) {
			return true;
		}

		a_movie->CreateObject(std::addressof(a_arvState));
		return a_root.SetMember(kArvNamespace.data(), a_arvState);
	}

	void AlchemyUiInjector::BindGhostSelectionCallback(RE::GFxMovieView* a_movie, RE::GFxValue& a_arvState) const
	{
		static RE::GFxFunctionHandler* handler = new GhostSelectionHandler();

		RE::GFxValue callback;
		a_movie->CreateFunction(std::addressof(callback), handler);
		a_arvState.SetMember("onGhostSelected", callback);
	}

	bool AlchemyUiInjector::InvokeGameDelegate(
		RE::GFxMovieView* a_movie,
		std::string_view a_method,
		const RE::GFxValue& a_arg) const
	{
		if (!a_movie) {
			return false;
		}

		std::array invokeArgs{ a_arg };
		RE::FxResponseArgsEx responseArgs{ invokeArgs };
		RE::FxDelegate::Invoke(a_movie, a_method.data(), responseArgs);
		return true;
	}

	bool AlchemyUiInjector::IsAlchemyMovie(RE::GFxMovieView* a_movie) const
	{
		RE::GFxValue root;
		RE::GFxValue menu;
		if (!ResolveMenuRoot(a_movie, root, menu)) {
			return false;
		}

		RE::GFxValue subtype;
		if (!menu.GetMember("_subtypeName", std::addressof(subtype)) || !subtype.IsString()) {
			return false;
		}

		const auto* subtypeName = subtype.GetString();
		return subtypeName && std::string_view(subtypeName) == "Alchemy";
	}

	void AlchemyUiInjector::SyncRootState(
		RE::GFxMovieView* a_movie,
		bool a_enabled,
		std::int32_t a_toggleKey,
		bool a_showNavButton) const
	{
		RE::GFxValue root;
		RE::GFxValue menu;
		RE::GFxValue arvState;
		if (!ResolveOrCreateArvState(a_movie, root, menu, arvState)) {
			return;
		}

		BindGhostSelectionCallback(a_movie, arvState);
		arvState.SetMember("enabled", RE::GFxValue(a_enabled));
		arvState.SetMember("toggleKey", RE::GFxValue(a_toggleKey));
		arvState.SetMember("showNavButton", RE::GFxValue(a_showNavButton));
	}

	void AlchemyUiInjector::PublishGhostIngredients(RE::GFxMovieView* a_movie, const std::vector<GhostIngredient>& a_ghosts) const
	{
		RE::GFxValue root;
		RE::GFxValue menu;
		RE::GFxValue arvState;
		if (!ResolveOrCreateArvState(a_movie, root, menu, arvState)) {
			return;
		}

		RE::GFxValue ghostByFormId;
		RE::GFxValue ghostIngredients;
		RE::GFxValue ghostOrder;
		a_movie->CreateObject(std::addressof(ghostByFormId));
		a_movie->CreateArray(std::addressof(ghostIngredients));
		a_movie->CreateArray(std::addressof(ghostOrder));
		ghostIngredients.SetArraySize(static_cast<std::uint32_t>(a_ghosts.size()));
		ghostOrder.SetArraySize(static_cast<std::uint32_t>(a_ghosts.size()));

		for (std::uint32_t i = 0; i < a_ghosts.size(); ++i) {
			const auto& ghost = a_ghosts[i];
			if (!ghost.ingredient) {
				continue;
			}

			RE::GFxValue entry;
			PopulateGhostEntryValue(a_movie, entry, ghost);

			ghostIngredients.SetElement(i, entry);
			ghostOrder.SetElement(i, RE::GFxValue(static_cast<double>(ghost.formID)));
			ghostByFormId.SetMember(std::to_string(ghost.formID).c_str(), entry);
		}

		arvState.SetMember("ghostByFormId", ghostByFormId);
		arvState.SetMember("ghostIngredients", ghostIngredients);
		arvState.SetMember("ghostOrder", ghostOrder);
		arvState.SetMember("ghostCount", RE::GFxValue(static_cast<double>(a_ghosts.size())));
	}

	void AlchemyUiInjector::PublishSelectedGhost(RE::GFxMovieView* a_movie, const GhostIngredient& a_ghost) const
	{
		RE::GFxValue root;
		RE::GFxValue menu;
		RE::GFxValue arvState;
		if (!ResolveOrCreateArvState(a_movie, root, menu, arvState)) {
			return;
		}

		RE::GFxValue selectedGhost;
		PopulateGhostEntryValue(a_movie, selectedGhost, a_ghost);
		arvState.SetMember("selectedGhost", selectedGhost);
		arvState.SetMember("selectedGhostFormId", RE::GFxValue(static_cast<double>(a_ghost.formID)));
	}

	void AlchemyUiInjector::ClearGhostIngredients(RE::GFxMovieView* a_movie) const
	{
		RE::GFxValue root;
		RE::GFxValue menu;
		RE::GFxValue arvState;
		if (!ResolveOrCreateArvState(a_movie, root, menu, arvState)) {
			return;
		}

		RE::GFxValue ghostByFormId;
		RE::GFxValue ghostIngredients;
		RE::GFxValue ghostOrder;
		a_movie->CreateObject(std::addressof(ghostByFormId));
		a_movie->CreateArray(std::addressof(ghostIngredients));
		a_movie->CreateArray(std::addressof(ghostOrder));
		arvState.SetMember("ghostByFormId", ghostByFormId);
		arvState.SetMember("ghostIngredients", ghostIngredients);
		arvState.SetMember("ghostOrder", ghostOrder);
		arvState.SetMember("ghostCount", RE::GFxValue(0.0));
	}

	void AlchemyUiInjector::ClearSelectedGhost(RE::GFxMovieView* a_movie) const
	{
		RE::GFxValue root;
		RE::GFxValue menu;
		RE::GFxValue arvState;
		if (!ResolveOrCreateArvState(a_movie, root, menu, arvState)) {
			return;
		}

		RE::GFxValue selectedGhost;
		a_movie->CreateObject(std::addressof(selectedGhost));
		arvState.SetMember("selectedGhost", selectedGhost);
		arvState.SetMember("selectedGhostFormId", RE::GFxValue(0.0));
	}

	void AlchemyUiInjector::InvalidateAlchemyList(RE::GFxMovieView* a_movie) const
	{
		RE::GFxValue root;
		RE::GFxValue menu;
		if (!ResolveMenuRoot(a_movie, root, menu)) {
			return;
		}

		RE::GFxValue categoryList;
		if (menu.GetMember("CategoryList", std::addressof(categoryList)) && categoryList.IsObject()) {
			categoryList.Invoke("InvalidateListData");
			return;
		}

		RE::GFxValue itemList;
		if (menu.GetMember("ItemList", std::addressof(itemList)) && itemList.IsObject()) {
			itemList.Invoke("InvalidateData");
			return;
		}

		spdlog::warn("AlchemyUiInjector: failed to resolve CategoryList/ItemList for alchemy invalidation");
	}

	void AlchemyUiInjector::RefreshAlchemyListPapyrusStyle(RE::GFxMovieView* a_movie, bool a_fullRebuild) const
	{
		RE::GFxValue root;
		RE::GFxValue menu;
		if (!ResolveMenuRoot(a_movie, root, menu)) {
			return;
		}

		RE::GFxValue itemList;
		RE::GFxValue selectedIndexValue;
		auto selectedIndex = -1;
		if (menu.GetMember("ItemList", std::addressof(itemList)) && itemList.IsObject() &&
			itemList.GetMember("selectedIndex", std::addressof(selectedIndexValue)) &&
			selectedIndexValue.IsNumber()) {
			selectedIndex = static_cast<std::int32_t>(selectedIndexValue.GetSInt());
		}

		auto inventoryListsInvalidated = false;
		RE::GFxValue inventoryLists;
		if (menu.GetMember("InventoryLists", std::addressof(inventoryLists)) && inventoryLists.IsObject()) {
			inventoryLists.Invoke("InvalidateListData");
			inventoryListsInvalidated = true;
		}

		std::array rebuildArgs{ RE::GFxValue(a_fullRebuild) };
		menu.Invoke("UpdateItemList", nullptr, rebuildArgs.data(), static_cast<RE::UPInt>(rebuildArgs.size()));

		if (selectedIndex >= 0 && itemList.IsObject()) {
			itemList.SetMember("selectedIndex", RE::GFxValue(-1.0));
			itemList.SetMember("selectedIndex", RE::GFxValue(static_cast<double>(selectedIndex)));

			std::array selectedArgs{ RE::GFxValue(static_cast<double>(selectedIndex)) };
			menu.Invoke("SetSelectedItem", nullptr, selectedArgs.data(), static_cast<RE::UPInt>(selectedArgs.size()));
		}

		menu.Invoke("UpdateItemDisplay");
		menu.Invoke("UpdateButtonText");

		if (!inventoryListsInvalidated) {
			InvalidateAlchemyList(a_movie);
			spdlog::warn("AlchemyUiInjector: InventoryLists unavailable, fell back to current alchemy invalidation path");
		}
	}

	void AlchemyUiInjector::ResetAlchemySelectionUi(RE::GFxMovieView* a_movie) const
	{
		RE::GFxValue root;
		RE::GFxValue menu;
		if (!ResolveMenuRoot(a_movie, root, menu)) {
			return;
		}

		RE::GFxValue itemList;
		if (menu.GetMember("ItemList", std::addressof(itemList)) && itemList.IsObject()) {
			itemList.SetMember("selectedIndex", RE::GFxValue(-1.0));
		}

		const auto chooseInvoked = InvokeGameDelegate(a_movie, "ChooseItem", RE::GFxValue(-1.0));
		const auto showItem3DInvoked = InvokeGameDelegate(a_movie, "ShowItem3D", RE::GFxValue(false));

		std::array chooseArgs{ RE::GFxValue(-1.0) };

		menu.Invoke("SetSelectedItem", nullptr, chooseArgs.data(), static_cast<RE::UPInt>(chooseArgs.size()));

		std::array fadeArgs{ RE::GFxValue(true) };
		menu.Invoke("FadeInfoCard", nullptr, fadeArgs.data(), static_cast<RE::UPInt>(fadeArgs.size()));
		menu.Invoke("UpdateButtonText");

		RE::GFxValue emptyIngredients;
		a_movie->CreateArray(std::addressof(emptyIngredients));
		std::array updateIngredientArgs{
			RE::GFxValue(""),
			emptyIngredients,
			RE::GFxValue(false)
		};
		menu.Invoke("UpdateIngredients", nullptr, updateIngredientArgs.data(), static_cast<RE::UPInt>(updateIngredientArgs.size()));

		if (Config::Settings::GetSingleton().DebugLogging()) {
			spdlog::info(
				"AlchemyUiInjector: reset alchemy selection UI (ChooseItem={} ShowItem3D={})",
				chooseInvoked,
				showItem3DInvoked);
		}
	}

	void AlchemyUiInjector::InjectButtonShim(RE::GFxMovieView* a_movie, std::int32_t a_toggleKey) const
	{
		RE::GFxValue root;
		RE::GFxValue menu;
		if (!ResolveMenuRoot(a_movie, root, menu)) {
			return;
		}

		const auto instanceName = std::format("AlchemyHelperUI_{}", a_toggleKey);

		RE::GFxValue existingClip;
		if (menu.GetMember(instanceName.c_str(), std::addressof(existingClip)) && existingClip.IsDisplayObject()) {
			return;
		}

		RE::GFxValue shimClip;
		if (!menu.CreateEmptyMovieClip(std::addressof(shimClip), instanceName.c_str(), 8543)) {
			spdlog::error("AlchemyUiInjector: failed to create shim movie clip '{}'", instanceName);
			return;
		}

		std::array args{ RE::GFxValue(kShimMovieName) };
		if (!shimClip.Invoke("loadMovie", nullptr, args.data(), static_cast<RE::UPInt>(args.size()))) {
			spdlog::error("AlchemyUiInjector: failed to load {}", kShimMovieName);
			return;
		}

		spdlog::info("AlchemyUiInjector: injected {} into '{}'", kShimMovieName, instanceName);
	}
}
