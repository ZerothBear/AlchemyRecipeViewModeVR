#pragma once

#include <atomic>

namespace ARV
{
	class AlchemyMenuHooks final
	{
	public:
		using AlchemyMenu = RE::CraftingSubMenus::CraftingSubMenus::AlchemyMenu;

		static AlchemyMenuHooks& GetSingleton();
		static void              Install();

		void OnAccept(AlchemyMenu* a_menu);
		void PrepareForAccept() noexcept;

		void RememberCraftButtonPress(RE::FxDelegateHandler::CallbackFn* a_callback) noexcept;
		[[nodiscard]] RE::FxDelegateHandler::CallbackFn* CraftButtonPressCallback() const noexcept;

	private:
		using ProcessUserEvent_t = bool (AlchemyMenu::*)(RE::BSFixedString*);
		static inline REL::Relocation<ProcessUserEvent_t> _ProcessUserEvent;

		std::atomic<RE::FxDelegateHandler::CallbackFn*> craftButtonPressCallback_{ nullptr };
		bool installed_{ false };
	};
}
