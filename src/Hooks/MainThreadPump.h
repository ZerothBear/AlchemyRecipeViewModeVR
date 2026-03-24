#pragma once

namespace ARV
{
	class MainThreadPump final
	{
	public:
		static void Install(const char* a_reason = "unspecified");
		static void VerifyInstalled(const char* a_reason);
		static void OnTick();
	};
}
