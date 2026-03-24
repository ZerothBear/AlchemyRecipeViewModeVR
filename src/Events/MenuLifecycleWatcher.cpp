#include "Events/MenuLifecycleWatcher.h"

#include "Hooks/MainThreadPump.h"
#include "PCH/PCH.h"
#include "Runtime/RecipeModeSession.h"
#include "UI/AlchemyUiInjector.h"

namespace ARV
{
	MenuLifecycleWatcher& MenuLifecycleWatcher::GetSingleton()
	{
		static MenuLifecycleWatcher singleton;
		return singleton;
	}

	void MenuLifecycleWatcher::Install()
	{
		auto& watcher = GetSingleton();
		if (watcher.installed_) {
			spdlog::info("MenuLifecycleWatcher already installed");
			return;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			spdlog::error("MenuLifecycleWatcher: UI singleton unavailable");
			return;
		}

		ui->AddEventSink<RE::MenuOpenCloseEvent>(std::addressof(watcher));
		watcher.installed_ = true;
		spdlog::info("MenuLifecycleWatcher installed");
	}

	RE::BSEventNotifyControl MenuLifecycleWatcher::ProcessEvent(
		const RE::MenuOpenCloseEvent* a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (!a_event || a_event->menuName != RE::CraftingMenu::MENU_NAME) {
			return RE::BSEventNotifyControl::kContinue;
		}

		if (!a_event->opening) {
			MainThreadPump::VerifyInstalled("alchemy-close");
			RecipeModeSession::GetSingleton().PublishCraftingMenuClosed();
			return RE::BSEventNotifyControl::kContinue;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const auto craftingMenu = ui->GetMenu<RE::CraftingMenu>();
		if (!craftingMenu || !craftingMenu->uiMovie) {
			spdlog::warn("MenuLifecycleWatcher: CraftingMenu opened without uiMovie");
			return RE::BSEventNotifyControl::kContinue;
		}

		if (!UI::AlchemyUiInjector::GetSingleton().IsAlchemyMovie(craftingMenu->uiMovie.get())) {
			spdlog::info("MenuLifecycleWatcher: Crafting Menu opened in non-alchemy subtype");
			return RE::BSEventNotifyControl::kContinue;
		}

		MainThreadPump::Install("alchemy-open");
		RecipeModeSession::GetSingleton().PublishCraftingMenuOpened(craftingMenu->uiMovie.get());
		return RE::BSEventNotifyControl::kContinue;
	}
}
