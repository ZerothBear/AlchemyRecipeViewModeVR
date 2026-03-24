#pragma once

#include <vector>

namespace ARV
{
	struct GhostIngredient;
}

namespace ARV::UI
{
	class AlchemyUiInjector final
	{
	public:
		static AlchemyUiInjector& GetSingleton();

		[[nodiscard]] bool IsAlchemyMovie(RE::GFxMovieView* a_movie) const;

		void SyncRootState(RE::GFxMovieView* a_movie, bool a_enabled, std::int32_t a_toggleKey, bool a_showNavButton) const;
		void PublishGhostIngredients(RE::GFxMovieView* a_movie, const std::vector<GhostIngredient>& a_ghosts) const;
		void PublishSelectedGhost(RE::GFxMovieView* a_movie, const GhostIngredient& a_ghost) const;
		void ClearGhostIngredients(RE::GFxMovieView* a_movie) const;
		void ClearSelectedGhost(RE::GFxMovieView* a_movie) const;
		void InvalidateAlchemyList(RE::GFxMovieView* a_movie) const;
		void RefreshAlchemyListPapyrusStyle(RE::GFxMovieView* a_movie, bool a_fullRebuild) const;
		void ResetAlchemySelectionUi(RE::GFxMovieView* a_movie) const;
		void InjectButtonShim(RE::GFxMovieView* a_movie, std::int32_t a_toggleKey) const;

	private:
		[[nodiscard]] bool ResolveMenuRoot(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_menu) const;
		[[nodiscard]] bool ResolveOrCreateArvState(RE::GFxMovieView* a_movie, RE::GFxValue& a_root, RE::GFxValue& a_menu, RE::GFxValue& a_arvState) const;
		void BindGhostSelectionCallback(RE::GFxMovieView* a_movie, RE::GFxValue& a_arvState) const;
		[[nodiscard]] bool InvokeGameDelegate(RE::GFxMovieView* a_movie, std::string_view a_method, const RE::GFxValue& a_arg) const;
	};
}
