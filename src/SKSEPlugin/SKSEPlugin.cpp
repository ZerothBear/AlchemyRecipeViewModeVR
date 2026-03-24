#include "Alchemy/IngredientRegistry.h"
#include "Config/Settings.h"
#include "Events/InputWatcher.h"
#include "Events/MenuLifecycleWatcher.h"
#include "Hooks/AlchemyMenuHooks.h"
#include "Papyrus/PapyrusBridge.h"
#include "PCH/PCH.h"

namespace
{
	void InitializeLog()
	{
#ifndef NDEBUG
		auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
		const auto level = spdlog::level::trace;
#else
		const auto path = std::filesystem::path("Data") / "SKSE" /
			std::format("{}.log", Plugin::NAME);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		const auto level = spdlog::level::info;
#endif

		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(level);
		log->flush_on(level);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("%s(%#): [%^%l%$] %v");
	}
}

extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	spdlog::info("========================================");
	spdlog::info("{} v{} loading", Plugin::NAME, Plugin::VERSION.string());
	spdlog::info("========================================");

	try {
		SKSE::Init(a_skse);
		SKSE::AllocTrampoline(64);

		auto* papyrus = SKSE::GetPapyrusInterface();
		papyrus->Register(ARV::Papyrus::Register);

		auto* messaging = SKSE::GetMessagingInterface();
		messaging->RegisterListener(
			[](SKSE::MessagingInterface::Message* a_message)
			{
				if (!a_message) {
					spdlog::warn("Received null SKSE message");
					return;
				}

				switch (a_message->type) {
				case SKSE::MessagingInterface::kDataLoaded:
					spdlog::info("DataLoaded received");
					ARV::Config::Settings::GetSingleton().Load();
					ARV::Alchemy::IngredientRegistry::GetSingleton().Build();
					ARV::AlchemyMenuHooks::Install();
					ARV::MenuLifecycleWatcher::Install();
					ARV::InputWatcher::Install();
					break;
				default:
					break;
				}
			});
	} catch (const std::exception& e) {
		spdlog::critical("Exception during plugin load: {}", e.what());
		return false;
	} catch (...) {
		spdlog::critical("Unknown exception during plugin load");
		return false;
	}

	spdlog::info("{} loaded successfully", Plugin::NAME);
	return true;
}
