#include "Papyrus/PapyrusBridge.h"

#include "Config/Settings.h"
#include "PCH/PCH.h"

#include <Windows.h>

namespace ARV::Papyrus
{
	namespace
	{
		void ReloadSettings([[maybe_unused]] RE::TESQuest* a_self)
		{
			spdlog::info("Papyrus: ReloadSettings called from MCM");
			Config::Settings::GetSingleton().Load();
		}

		bool GetDefaultBoolSetting([[maybe_unused]] RE::TESQuest* a_self, RE::BSFixedString a_settingName)
		{
			bool value = false;
			const auto found = Config::Settings::TryGetDefaultBool(
				a_settingName.c_str() ? a_settingName.c_str() : "", value);
			if (!found) {
				spdlog::warn("Papyrus: failed to resolve default bool setting '{}'",
					a_settingName.c_str() ? a_settingName.c_str() : "");
			}
			return value;
		}

		std::int32_t GetDefaultIntSetting([[maybe_unused]] RE::TESQuest* a_self, RE::BSFixedString a_settingName)
		{
			std::int32_t value = 0;
			const auto found = Config::Settings::TryGetDefaultInt(
				a_settingName.c_str() ? a_settingName.c_str() : "", value);
			if (!found) {
				spdlog::warn("Papyrus: failed to resolve default int setting '{}'",
					a_settingName.c_str() ? a_settingName.c_str() : "");
			}
			return value;
		}
	}

	bool Register(RE::BSScript::IVirtualMachine* a_vm)
	{
		a_vm->RegisterFunction("ReloadSettings"sv, McmScriptName, ReloadSettings);
		a_vm->RegisterFunction("GetDefaultBoolSetting"sv, McmScriptName, GetDefaultBoolSetting);
		a_vm->RegisterFunction("GetDefaultIntSetting"sv, McmScriptName, GetDefaultIntSetting);
		spdlog::info("Papyrus: registered native bindings for {}", McmScriptName);
		return true;
	}
}
