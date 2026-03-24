#include "Events/InputWatcher.h"

#include "Config/Settings.h"
#include "Hooks/MainThreadPump.h"
#include "PCH/PCH.h"
#include "Runtime/RecipeModeSession.h"

namespace ARV
{
	InputWatcher& InputWatcher::GetSingleton()
	{
		static InputWatcher singleton;
		return singleton;
	}

	void InputWatcher::Install()
	{
		auto& watcher = GetSingleton();
		if (watcher.installed_) {
			spdlog::info("InputWatcher already installed");
			return;
		}

		auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
		if (!inputManager) {
			spdlog::error("InputWatcher: BSInputDeviceManager unavailable");
			return;
		}

		inputManager->AddEventSink(std::addressof(watcher));
		watcher.installed_ = true;
		spdlog::info("InputWatcher installed");
	}

	RE::BSEventNotifyControl InputWatcher::ProcessEvent(
		RE::InputEvent* const* a_event,
		RE::BSTEventSource<RE::InputEvent*>*)
	{
		if (!a_event || !Config::Settings::GetSingleton().Enabled()) {
			return RE::BSEventNotifyControl::kContinue;
		}

		auto& session = RecipeModeSession::GetSingleton();
		if (!session.IsMenuOpen()) {
			return RE::BSEventNotifyControl::kContinue;
		}

		for (auto* event = *a_event; event; event = event->next) {
			if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
				continue;
			}

			if (event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
				continue;
			}

			auto* button = event->AsButtonEvent();
			if (!button || !button->IsDown()) {
				continue;
			}

			auto* idEvent = button->AsIDEvent();
			if (!idEvent) {
				continue;
			}

			if (static_cast<std::int32_t>(idEvent->idCode) != Config::Settings::GetSingleton().ToggleKey()) {
				continue;
			}

			spdlog::info("InputWatcher: toggling recipe mode on key {} (thread {})", idEvent->idCode, GetCurrentThreadId());
			MainThreadPump::VerifyInstalled("toggle-key");
			session.RequestToggle();
		}

		return RE::BSEventNotifyControl::kContinue;
	}
}
