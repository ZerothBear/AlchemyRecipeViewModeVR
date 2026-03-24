#include "Hooks/AlchemyMenuHooks.h"

#include "PCH/PCH.h"
#include "Runtime/RecipeModeSession.h"

namespace
{
	using AlchemyMenu = RE::CraftingSubMenus::CraftingSubMenus::AlchemyMenu;

	class CraftBlockingProcessor final : public RE::FxDelegateHandler::CallbackProcessor
	{
	public:
		explicit CraftBlockingProcessor(RE::FxDelegateHandler::CallbackProcessor* a_inner) :
			inner_(a_inner)
		{}

		void Process(const RE::GString& a_methodName, RE::FxDelegateHandler::CallbackFn* a_method) override
		{
			auto* hooks = std::addressof(ARV::AlchemyMenuHooks::GetSingleton());
			const std::string_view methodName = a_methodName.c_str() ? a_methodName.c_str() : "";

			auto* callback = a_method;
			if (methodName == "CraftButtonPress") {
				hooks->RememberCraftButtonPress(a_method);
				callback = &CraftButtonPressCallback;
			}

			inner_->Process(a_methodName, callback);
		}

	private:
		static void CraftButtonPressCallback(const RE::FxDelegateArgs& a_args)
		{
			auto& session = ARV::RecipeModeSession::GetSingleton();
			if (session.ShouldBlockCraft()) {
				spdlog::info("AlchemyMenuHooks: blocked CraftButtonPress (recipe mode)");
				return;
			}

			if (auto* original = ARV::AlchemyMenuHooks::GetSingleton().CraftButtonPressCallback()) {
				(*original)(a_args);
			}
		}

		RE::FxDelegateHandler::CallbackProcessor* inner_{ nullptr };
	};
}

namespace ARV
{
	namespace
	{
		using Accept_t = void (AlchemyMenu::*)(RE::FxDelegateHandler::CallbackProcessor*);
		inline REL::Relocation<Accept_t> _Accept;
	}

	AlchemyMenuHooks& AlchemyMenuHooks::GetSingleton()
	{
		static AlchemyMenuHooks singleton;
		return singleton;
	}

	void AlchemyMenuHooks::Install()
	{
		auto& hooks = GetSingleton();
		if (hooks.installed_) {
			spdlog::info("AlchemyMenuHooks already installed");
			return;
		}

		auto vtable = REL::Relocation<std::uintptr_t>(AlchemyMenu::VTABLE[0]);

		_Accept = vtable.write_vfunc(
			1,
			+[](AlchemyMenu* a_menu, RE::FxDelegateHandler::CallbackProcessor* a_processor)
			{
				AlchemyMenuHooks::GetSingleton().PrepareForAccept();
				CraftBlockingProcessor interceptor(a_processor);
				_Accept(a_menu, std::addressof(interceptor));
				AlchemyMenuHooks::GetSingleton().OnAccept(a_menu);
			});

		_ProcessUserEvent = vtable.write_vfunc(
			5,
			+[](AlchemyMenu* a_menu, RE::BSFixedString* a_control) -> bool
			{
				auto& session = RecipeModeSession::GetSingleton();
				if (session.ShouldBlockCraft() && a_control) {
					spdlog::info(
						"AlchemyMenuHooks::ProcessUserEvent: control='{}' selectedIndexes={} currentIngredientIdx={} (recipe mode active)",
						a_control->c_str(),
						a_menu ? a_menu->selectedIndexes.size() : 0,
						a_menu ? a_menu->currentIngredientIdx : 0);

					const std::string_view control = a_control->c_str();
					if (control == "Activate" || control == "XButton") {
						spdlog::info("AlchemyMenuHooks: blocked craft control '{}'", control);
						return true;
					}
					if (control == "Cancel") {
						RE::DebugNotification("Deactivate Recipe View mode first");
						spdlog::info("AlchemyMenuHooks: blocked menu close (recipe mode active)");
						return true;
					}
				}
				return _ProcessUserEvent(a_menu, a_control);
			});

		hooks.installed_ = true;
		spdlog::info("AlchemyMenuHooks installed (Accept slot 1, ProcessUserEvent slot 5)");
	}

	void AlchemyMenuHooks::OnAccept(AlchemyMenu* a_menu)
	{
		RecipeModeSession::GetSingleton().PublishAlchemyMenuBound(a_menu);
	}

	void AlchemyMenuHooks::PrepareForAccept() noexcept
	{
		craftButtonPressCallback_.store(nullptr, std::memory_order_release);
	}

	void AlchemyMenuHooks::RememberCraftButtonPress(RE::FxDelegateHandler::CallbackFn* a_callback) noexcept
	{
		craftButtonPressCallback_.store(a_callback, std::memory_order_release);
	}

	RE::FxDelegateHandler::CallbackFn* AlchemyMenuHooks::CraftButtonPressCallback() const noexcept
	{
		return craftButtonPressCallback_.load(std::memory_order_acquire);
	}
}
