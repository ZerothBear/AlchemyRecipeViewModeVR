#include "Hooks/AlchemyMenuHooks.h"

#include "Alchemy/IngredientRegistry.h"
#include "Config/Settings.h"
#include "PCH/PCH.h"
#include "Runtime/RecipeModeSession.h"

namespace
{
	using AlchemyMenu = RE::CraftingSubMenus::CraftingSubMenus::AlchemyMenu;

	class InterceptingProcessor final : public RE::FxDelegateHandler::CallbackProcessor
	{
	public:
		explicit InterceptingProcessor(RE::FxDelegateHandler::CallbackProcessor* a_inner) :
			inner_(a_inner)
		{}

		void Process(const RE::GString& a_methodName, RE::FxDelegateHandler::CallbackFn* a_method) override
		{
			auto* hooks = std::addressof(ARV::AlchemyMenuHooks::GetSingleton());
			const std::string_view methodName = a_methodName.c_str() ? a_methodName.c_str() : "";

			if (ARV::Config::Settings::GetSingleton().DebugLogging()) {
				spdlog::info("AlchemyMenuHooks: Accept registered callback '{}'", methodName);
			}

			hooks->RememberCallback(methodName, a_method);

			auto* callback = a_method;
			if (methodName == "CraftButtonPress") {
				callback = &CraftButtonPressCallback;
			} else if (methodName == "CraftSelectedItem") {
				callback = &CraftSelectedItemCallback;
			} else if (methodName == "ChooseItem") {
				callback = &ChooseItemCallback;
			} else if (methodName == "SetSelectedItem") {
				callback = &SetSelectedItemCallback;
			} else if (methodName == "SetSelectedCategory") {
				callback = &SetSelectedCategoryCallback;
			} else if (methodName == "ShowItem3D") {
				callback = &ShowItem3DCallback;
			}

			inner_->Process(a_methodName, callback);
		}

	private:
		static void ForwardOrBlock(std::string_view a_name, const RE::FxDelegateArgs& a_args, bool a_blockWhenRecipeModeEnabled, bool a_refreshAfter)
		{
			auto& session = ARV::RecipeModeSession::GetSingleton();
			if (a_blockWhenRecipeModeEnabled && session.ShouldBlockCraft()) {
				spdlog::info("AlchemyMenuHooks: blocked callback '{}'", a_name);
				session.OnSelectionChanged();
				return;
			}

			if (auto* original = ARV::AlchemyMenuHooks::GetSingleton().CallbackFor(a_name)) {
				(*original)(a_args);
			}

			if (a_refreshAfter) {
				session.OnSelectionChanged();
			}
		}

		static void CraftButtonPressCallback(const RE::FxDelegateArgs& a_args)
		{
			ForwardOrBlock("CraftButtonPress", a_args, true, true);
		}

		static void CraftSelectedItemCallback(const RE::FxDelegateArgs& a_args)
		{
			ForwardOrBlock("CraftSelectedItem", a_args, true, true);
		}

		static void ChooseItemCallback(const RE::FxDelegateArgs& a_args)
		{
			ForwardOrBlock("ChooseItem", a_args, false, true);
		}

		static void SetSelectedItemCallback(const RE::FxDelegateArgs& a_args)
		{
			ForwardOrBlock("SetSelectedItem", a_args, false, true);
		}

		static void SetSelectedCategoryCallback(const RE::FxDelegateArgs& a_args)
		{
			ForwardOrBlock("SetSelectedCategory", a_args, false, true);
		}

		static void ShowItem3DCallback(const RE::FxDelegateArgs& a_args)
		{
			ForwardOrBlock("ShowItem3D", a_args, true, false);
		}

		RE::FxDelegateHandler::CallbackProcessor* inner_{ nullptr };
	};

	struct NativeAlchemyListEntry
	{
		RE::InventoryEntryData* data;        // 00
		std::uint32_t           filterFlag;  // 08
		std::uint8_t            bEquipped;   // 0C
		std::uint8_t            bEnabled;    // 0D
		std::uint16_t           pad0E;       // 0E
	};
	static_assert(sizeof(NativeAlchemyListEntry) == 0x10);
}

namespace ARV
{
	namespace
	{
		using Accept_t = void (AlchemyMenu::*)(RE::FxDelegateHandler::CallbackProcessor*);
		using ProcessUserEvent_t = bool (AlchemyMenu::*)(RE::BSFixedString*);
		using SetData_t = void (*)(RE::GFxValue*, NativeAlchemyListEntry*, AlchemyMenu*);

		inline REL::Relocation<Accept_t> _Accept;
		inline REL::Relocation<ProcessUserEvent_t> _ProcessUserEvent;
		inline REL::Relocation<SetData_t> _SetData;

		constexpr REL::VariantOffset kAlchemySetDataBase{ 0, 0, 0x00890300 };
		constexpr std::ptrdiff_t kAlchemySetDataCallOffset = 0xDD;

		void SetDataHook(RE::GFxValue* a_dataContainer, NativeAlchemyListEntry* a_entry, AlchemyMenu* a_menu)
		{
			_SetData(a_dataContainer, a_entry, a_menu);

			if (!a_dataContainer || !a_entry || !a_entry->data) {
				return;
			}

			auto* object = a_entry->data->object;
			if (!object) {
				return;
			}

			const auto formID = object->GetFormID();
			const auto& session = RecipeModeSession::GetSingleton();
			const bool synthetic = session.IsSyntheticEntry(a_entry->data);

			const auto formType = object->GetFormType();
			if (!synthetic && formType != RE::FormType::Ingredient) {
				return;
			}

			const auto* ingredientRecord = Alchemy::IngredientRegistry::GetSingleton().Find(formID);
			if (!synthetic && !ingredientRecord) {
				return;
			}

			std::string text = ingredientRecord ? ingredientRecord->displayName : (object->GetName() ? object->GetName() : "");
			if (synthetic) {
				text += " (0)";
				a_dataContainer->SetMember("count", RE::GFxValue(0));
				a_dataContainer->SetMember("enabled", RE::GFxValue(false));
			}

			a_dataContainer->SetMember("arvSynthetic", RE::GFxValue(synthetic));
			a_dataContainer->SetMember("arvFormID", RE::GFxValue(static_cast<double>(formID)));
			a_dataContainer->SetMember("text", RE::GFxValue(text));
		}
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
				InterceptingProcessor interceptor(a_processor);
				_Accept(a_menu, std::addressof(interceptor));
				AlchemyMenuHooks::GetSingleton().OnAccept(a_menu);
			});

		_ProcessUserEvent = vtable.write_vfunc(
			5,
			+[](AlchemyMenu* a_menu, RE::BSFixedString* a_control)
			{
				AlchemyMenuHooks::GetSingleton().OnProcessUserEvent(a_menu, a_control);
				return _ProcessUserEvent(a_menu, a_control);
			});

		auto& trampoline = SKSE::GetTrampoline();
		const auto setDataCall = REL::Relocation<std::uintptr_t>(kAlchemySetDataBase).address() + kAlchemySetDataCallOffset;
		_SetData = trampoline.write_call<5>(setDataCall, SetDataHook);

		hooks.installed_ = true;
		spdlog::info("AlchemyMenuHooks installed");
	}

	void AlchemyMenuHooks::RememberCallback(std::string_view a_name, RE::FxDelegateHandler::CallbackFn* a_callback)
	{
		callbacks_[std::string(a_name)] = a_callback;
	}

	void AlchemyMenuHooks::OnAccept(AlchemyMenu* a_menu)
	{
		RecipeModeSession::GetSingleton().BindAlchemyMenu(a_menu);
	}

	void AlchemyMenuHooks::OnProcessUserEvent(AlchemyMenu*, RE::BSFixedString* a_control)
	{
		RecipeModeSession::GetSingleton().OnUserEvent(a_control);
	}

	RE::FxDelegateHandler::CallbackFn* AlchemyMenuHooks::CallbackFor(std::string_view a_name) const noexcept
	{
		const auto it = callbacks_.find(std::string(a_name));
		return it != callbacks_.end() ? it->second : nullptr;
	}
}
