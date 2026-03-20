#include "Hooks/AlchemyMenuHooks.h"

#include "Config/Settings.h"
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

			hooks->RememberCallback(methodName, a_method);

			auto* callback = a_method;
			if (methodName == "CraftButtonPress") {
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

			if (auto* original = ARV::AlchemyMenuHooks::GetSingleton().CallbackFor("CraftButtonPress")) {
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
				CraftBlockingProcessor interceptor(a_processor);
				_Accept(a_menu, std::addressof(interceptor));
				AlchemyMenuHooks::GetSingleton().OnAccept(a_menu);
			});

		// Hook ProcessUserEvent (vfunc slot 5) for native craft blocking
		_ProcessUserEvent = vtable.write_vfunc(
			5,
			+[](AlchemyMenu* a_menu, RE::BSFixedString* a_control) -> bool
			{
				auto& session = RecipeModeSession::GetSingleton();
				if (session.ShouldBlockCraft() && a_control) {
					// Phase 4A: Discovery -- log all controls seen during recipe mode
					spdlog::debug(
						"AlchemyMenuHooks::ProcessUserEvent: control='{}' (recipe mode active)",
						a_control->c_str());

					// Phase 4B: Block craft-trigger controls
					// "Activate" is the primary craft action in both keyboard and VR controller paths.
					// Additional controls can be added here after discovery testing.
					const std::string_view control = a_control->c_str();
					if (control == "Activate") {
						spdlog::info("AlchemyMenuHooks: blocked craft control '{}'", control);
						return true;  // handled -- do not forward
					}
				}
				return _ProcessUserEvent(a_menu, a_control);
			});

		hooks.installed_ = true;
		spdlog::info("AlchemyMenuHooks installed (Accept slot 1, ProcessUserEvent slot 5)");
	}

	void AlchemyMenuHooks::OnAccept(AlchemyMenu* a_menu)
	{
		RecipeModeSession::GetSingleton().BindAlchemyMenu(a_menu);
	}

	void AlchemyMenuHooks::RememberCallback(std::string_view a_name, RE::FxDelegateHandler::CallbackFn* a_callback)
	{
		callbacks_[std::string(a_name)] = a_callback;
	}

	RE::FxDelegateHandler::CallbackFn* AlchemyMenuHooks::CallbackFor(std::string_view a_name) const noexcept
	{
		const auto it = callbacks_.find(std::string(a_name));
		return it != callbacks_.end() ? it->second : nullptr;
	}

	void AlchemyMenuHooks::ClearCallbacks()
	{
		callbacks_.clear();
	}
}
