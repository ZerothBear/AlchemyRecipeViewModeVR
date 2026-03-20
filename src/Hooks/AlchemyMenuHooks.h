#pragma once

namespace ARV
{
	class AlchemyMenuHooks final
	{
	public:
		using AlchemyMenu = RE::CraftingSubMenus::CraftingSubMenus::AlchemyMenu;

		static AlchemyMenuHooks& GetSingleton();
		static void              Install();

		void OnAccept(AlchemyMenu* a_menu);

		void RememberCallback(std::string_view a_name, RE::FxDelegateHandler::CallbackFn* a_callback);
		[[nodiscard]] RE::FxDelegateHandler::CallbackFn* CallbackFor(std::string_view a_name) const noexcept;
		void ClearCallbacks();

	private:
		using ProcessUserEvent_t = bool (AlchemyMenu::*)(RE::BSFixedString*);
		static inline REL::Relocation<ProcessUserEvent_t> _ProcessUserEvent;

		std::unordered_map<std::string, RE::FxDelegateHandler::CallbackFn*> callbacks_{};
		bool installed_{ false };
	};
}
