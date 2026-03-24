#include "Config/Settings.h"

#include "PCH/PCH.h"

#include <Windows.h>

namespace
{
	constexpr auto kDefaultsPath = "Data/MCM/Config/Alchemy Helper/settings.ini"sv;
	constexpr auto kUserPath     = "Data/MCM/Settings/Alchemy Helper.ini"sv;
	constexpr auto kLegacyPath   = "Data/SKSE/Plugins/AlchemyRecipeViewVR.ini"sv;

	constexpr auto kSentinel = "__UNSET__";

	bool FileExists(const char* a_path)
	{
		return ::GetFileAttributesA(a_path) != INVALID_FILE_ATTRIBUTES;
	}

	std::optional<bool> TryReadBool(const char* a_path, const char* a_section, const char* a_key)
	{
		char buffer[32]{};
		::GetPrivateProfileStringA(a_section, a_key, kSentinel, buffer,
			static_cast<DWORD>(std::size(buffer)), a_path);

		if (std::strcmp(buffer, kSentinel) == 0) {
			return std::nullopt;
		}

		std::string value = buffer;
		std::ranges::transform(value, value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});

		if (value == "1" || value == "true" || value == "yes" || value == "on") {
			return true;
		}
		if (value == "0" || value == "false" || value == "no" || value == "off") {
			return false;
		}
		return std::nullopt;
	}

	std::optional<std::int32_t> TryReadInt(const char* a_path, const char* a_section, const char* a_key)
	{
		char buffer[32]{};
		::GetPrivateProfileStringA(a_section, a_key, kSentinel, buffer,
			static_cast<DWORD>(std::size(buffer)), a_path);

		if (std::strcmp(buffer, kSentinel) == 0) {
			return std::nullopt;
		}

		try {
			return static_cast<std::int32_t>(std::stol(buffer));
		} catch (...) {
			return std::nullopt;
		}
	}

	bool ReadBoolCascade(
		const char* a_section, const char* a_key, bool a_default,
		const char* a_legacySection, const char* a_legacyKey,
		const char* a_userPath)
	{
		bool result = a_default;

		// Layer 1: MCM defaults
		if (auto v = TryReadBool(kDefaultsPath.data(), a_section, a_key)) {
			result = *v;
		}

		// Layer 2: user overrides (MCM or legacy)
		if (a_userPath) {
			if (a_userPath == kLegacyPath.data()) {
				if (auto v = TryReadBool(a_userPath, a_legacySection, a_legacyKey)) {
					result = *v;
				}
			} else {
				if (auto v = TryReadBool(a_userPath, a_section, a_key)) {
					result = *v;
				}
			}
		}

		return result;
	}

	std::int32_t ReadIntCascade(
		const char* a_section, const char* a_key, std::int32_t a_default,
		const char* a_legacySection, const char* a_legacyKey,
		const char* a_userPath)
	{
		std::int32_t result = a_default;

		// Layer 1: MCM defaults
		if (auto v = TryReadInt(kDefaultsPath.data(), a_section, a_key)) {
			result = *v;
		}

		// Layer 2: user overrides (MCM or legacy)
		if (a_userPath) {
			if (a_userPath == kLegacyPath.data()) {
				if (auto v = TryReadInt(a_userPath, a_legacySection, a_legacyKey)) {
					result = *v;
				}
			} else {
				if (auto v = TryReadInt(a_userPath, a_section, a_key)) {
					result = *v;
				}
			}
		}

		return result;
	}

	struct ParsedSettingName
	{
		std::string key;
		std::string section;
	};

	std::optional<ParsedSettingName> ParseSettingName(std::string_view a_name)
	{
		const auto sep = a_name.rfind(':');
		if (sep == std::string_view::npos || sep == 0 || sep == a_name.size() - 1) {
			return std::nullopt;
		}
		return ParsedSettingName{
			std::string(a_name.substr(0, sep)),
			std::string(a_name.substr(sep + 1))
		};
	}
}

namespace ARV::Config
{
	Settings& Settings::GetSingleton()
	{
		static Settings singleton;
		return singleton;
	}

	void Settings::Load()
	{
		// Determine which user-override file to use
		const char* userPath = nullptr;
		if (FileExists(kUserPath.data())) {
			userPath = kUserPath.data();
			spdlog::info("Settings: using MCM user overrides ({})", kUserPath);
		} else if (FileExists(kLegacyPath.data())) {
			userPath = kLegacyPath.data();
			spdlog::info("Settings: using legacy INI ({})", kLegacyPath);
		} else {
			spdlog::info("Settings: no user overrides found, using defaults only");
		}

		// MCM settings (three-tier cascade: defaults -> user overrides)
		data_.enable = ReadBoolCascade(
			"General", "bEnable", data_.enable,
			"Main", "bEnable", userPath);

		data_.debugLogging = ReadBoolCascade(
			"General", "bDebugLogging", data_.debugLogging,
			"Main", "bDebugLogging", userPath);

		data_.toggleKey = ReadIntCascade(
			"Controls", "iToggleKey", data_.toggleKey,
			"Main", "iToggleKey", userPath);

		data_.showNavButton = ReadBoolCascade(
			"Behavior", "bShowNavButton", data_.showNavButton,
			"Main", "bShowNavButton", userPath);

		data_.blockCraftWhileEnabled = ReadBoolCascade(
			"Behavior", "bBlockCraftWhileEnabled", data_.blockCraftWhileEnabled,
			"Main", "bBlockCraftWhileEnabled", userPath);

		// Probes: legacy INI only (not exposed in MCM)
		if (FileExists(kLegacyPath.data())) {
			if (auto v = TryReadBool(kLegacyPath.data(), "Main", "bNativeAddItemProbe")) {
				data_.nativeAddItemProbe = *v;
			}
			if (auto v = TryReadBool(kLegacyPath.data(), "Main", "bPapyrusRefreshProbe")) {
				data_.papyrusRefreshProbe = *v;
			}
		}

		spdlog::info(
			"Settings: enable={} debugLogging={} toggleKey={} showNavButton={} blockCraftWhileEnabled={} nativeAddItemProbe={} papyrusRefreshProbe={}",
			data_.enable,
			data_.debugLogging,
			data_.toggleKey,
			data_.showNavButton,
			data_.blockCraftWhileEnabled,
			data_.nativeAddItemProbe,
			data_.papyrusRefreshProbe);
	}

	const RuntimeSettings& Settings::Get() const noexcept
	{
		return data_;
	}

	bool Settings::Enabled() const noexcept
	{
		return data_.enable;
	}

	bool Settings::DebugLogging() const noexcept
	{
		return data_.debugLogging;
	}

	std::int32_t Settings::ToggleKey() const noexcept
	{
		return data_.toggleKey;
	}

	bool Settings::ShowNavButton() const noexcept
	{
		return data_.showNavButton;
	}

	bool Settings::BlockCraftWhileEnabled() const noexcept
	{
		return data_.blockCraftWhileEnabled;
	}

	bool Settings::NativeAddItemProbe() const noexcept
	{
		return data_.nativeAddItemProbe;
	}

	bool Settings::PapyrusRefreshProbe() const noexcept
	{
		return data_.papyrusRefreshProbe;
	}

	bool Settings::TryGetDefaultBool(std::string_view a_settingName, bool& a_value)
	{
		const auto parsed = ParseSettingName(a_settingName);
		if (!parsed) {
			spdlog::warn("Settings: invalid setting name '{}'", a_settingName);
			return false;
		}

		auto result = TryReadBool(kDefaultsPath.data(), parsed->section.c_str(), parsed->key.c_str());
		if (!result) {
			spdlog::warn("Settings: default bool '{}' not found in '{}'", a_settingName, kDefaultsPath);
			return false;
		}

		a_value = *result;
		return true;
	}

	bool Settings::TryGetDefaultInt(std::string_view a_settingName, std::int32_t& a_value)
	{
		const auto parsed = ParseSettingName(a_settingName);
		if (!parsed) {
			spdlog::warn("Settings: invalid setting name '{}'", a_settingName);
			return false;
		}

		auto result = TryReadInt(kDefaultsPath.data(), parsed->section.c_str(), parsed->key.c_str());
		if (!result) {
			spdlog::warn("Settings: default int '{}' not found in '{}'", a_settingName, kDefaultsPath);
			return false;
		}

		a_value = *result;
		return true;
	}
}
