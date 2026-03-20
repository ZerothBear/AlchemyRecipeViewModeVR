#pragma once

namespace ARV
{
	struct GhostIngredient
	{
		RE::FormID          formID{ 0 };
		RE::IngredientItem* ingredient{ nullptr };
		std::string         originalName;
	};
}
