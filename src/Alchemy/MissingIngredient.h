#pragma once

#include <string>
#include <vector>

namespace ARV
{
	struct GhostRecipeCandidate
	{
		std::vector<RE::FormID>   partnerFormIDs{};
		std::vector<std::string>  partnerNames{};
		std::vector<RE::FormID>   effectFormIDs{};
		std::vector<std::string>  effectNames{};
		std::string               title;
		std::string               summary;
	};

	struct GhostIngredient
	{
		RE::FormID          formID{ 0 };
		RE::IngredientItem* ingredient{ nullptr };
		std::string         originalName;
		std::uint32_t       partitionFilterFlag{ 0 };
		std::vector<GhostRecipeCandidate> recipeCandidates{};
	};
}
