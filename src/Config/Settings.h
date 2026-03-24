#pragma once

namespace ARV::Config
{
	struct RuntimeSettings
	{
		bool         enable{ true };
		bool         debugLogging{ false };
		std::int32_t toggleKey{ 43 };
		bool         showNavButton{ true };
		bool         blockCraftWhileEnabled{ true };
		bool         nativeAddItemProbe{ false };
		bool         papyrusRefreshProbe{ false };
	};

	class Settings final
	{
	public:
		static Settings& GetSingleton();

		void Load();

		[[nodiscard]] const RuntimeSettings& Get() const noexcept;
		[[nodiscard]] bool                   Enabled() const noexcept;
		[[nodiscard]] bool                   DebugLogging() const noexcept;
		[[nodiscard]] std::int32_t           ToggleKey() const noexcept;
		[[nodiscard]] bool                   ShowNavButton() const noexcept;
		[[nodiscard]] bool                   BlockCraftWhileEnabled() const noexcept;
		[[nodiscard]] bool                   NativeAddItemProbe() const noexcept;
		[[nodiscard]] bool                   PapyrusRefreshProbe() const noexcept;

		static bool TryGetDefaultBool(std::string_view a_settingName, bool& a_value);
		static bool TryGetDefaultInt(std::string_view a_settingName, std::int32_t& a_value);

	private:
		RuntimeSettings data_{};
	};
}
