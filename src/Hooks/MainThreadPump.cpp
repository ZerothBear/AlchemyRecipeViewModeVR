#include "Hooks/MainThreadPump.h"

#include "PCH/PCH.h"
#include "Runtime/RecipeModeSession.h"

namespace ARV
{
	namespace
	{
		constexpr std::size_t kUpdateSlot = 0xAF;

		struct State
		{
			std::atomic_bool installed{ false };
			std::atomic_bool loggedThunkEntry{ false };
			std::atomic_bool loggedOwnerThread{ false };
			std::atomic_flag inTick = ATOMIC_FLAG_INIT;
		};

		State& GetState()
		{
			static State state;
			return state;
		}

		using Update_t = void (RE::PlayerCharacter::*)(float);
		static REL::Relocation<Update_t> updatePlayer;

		const char* NormalizeReason(const char* a_reason)
		{
			return a_reason ? a_reason : "unspecified";
		}

		REL::Relocation<std::uintptr_t> GetPlayerVTable()
		{
			return REL::Relocation<std::uintptr_t>(RE::PlayerCharacter::VTABLE[0]);
		}

		std::uintptr_t* GetPlayerVTableBase()
		{
			return reinterpret_cast<std::uintptr_t*>(GetPlayerVTable().address());
		}

		std::uintptr_t GetSlotAddress()
		{
			return reinterpret_cast<std::uintptr_t>(std::addressof(GetPlayerVTableBase()[kUpdateSlot]));
		}

		std::uintptr_t ReadCurrentTarget()
		{
			return GetPlayerVTableBase()[kUpdateSlot];
		}

		void UpdateThunk(RE::PlayerCharacter* a_player, float a_delta);

		std::uintptr_t GetThunkAddress()
		{
			return reinterpret_cast<std::uintptr_t>(&UpdateThunk);
		}

		void UpdateThunk(RE::PlayerCharacter* a_player, float a_delta)
		{
			auto& state = GetState();
			const auto playerAddress = reinterpret_cast<std::uintptr_t>(a_player);
			const auto singletonAddress = reinterpret_cast<std::uintptr_t>(RE::PlayerCharacter::GetSingleton());

			if (!state.loggedThunkEntry.exchange(true, std::memory_order_acq_rel)) {
				spdlog::info(
					"MainThreadPump: first thunk entry thread={} player={:#x} singleton={:#x} delta={} slotTarget={:#x}",
					GetCurrentThreadId(),
					playerAddress,
					singletonAddress,
					a_delta,
					ReadCurrentTarget());
			}

			updatePlayer(a_player, a_delta);

			if (a_player == RE::PlayerCharacter::GetSingleton()) {
				MainThreadPump::OnTick();
			}
		}
	}

	void MainThreadPump::Install(const char* a_reason)
	{
		auto& state = GetState();
		const auto reason = NormalizeReason(a_reason);
		const auto* vtableBase = GetPlayerVTableBase();
		const auto slotAddress = GetSlotAddress();
		const auto previousTarget = ReadCurrentTarget();
		const auto thunkAddress = GetThunkAddress();

		spdlog::info(
			"MainThreadPump install requested (reason='{}', thread={}, vtable={:#x}, slot={:#x}, currentTarget={:#x}, thunk={:#x})",
			reason,
			GetCurrentThreadId(),
			reinterpret_cast<std::uintptr_t>(vtableBase),
			slotAddress,
			previousTarget,
			thunkAddress);

		if (previousTarget == thunkAddress) {
			state.installed.store(true, std::memory_order_release);
			spdlog::info(
				"MainThreadPump install skipped; thunk already active (reason='{}', slot={:#x}, target={:#x})",
				reason,
				slotAddress,
				previousTarget);
			return;
		}

		auto vtable = GetPlayerVTable();
		updatePlayer = vtable.write_vfunc(
			kUpdateSlot,
			UpdateThunk);
		state.installed.store(true, std::memory_order_release);

		const auto currentTarget = ReadCurrentTarget();
		spdlog::info(
			"MainThreadPump installed on PlayerCharacter::Update slot {:#x} (reason='{}', previousTarget={:#x}, currentTarget={:#x})",
			kUpdateSlot,
			reason,
			previousTarget,
			currentTarget);
	}

	void MainThreadPump::VerifyInstalled(const char* a_reason)
	{
		const auto reason = NormalizeReason(a_reason);
		const auto expectedTarget = GetThunkAddress();
		const auto currentTarget = ReadCurrentTarget();
		const auto slotAddress = GetSlotAddress();
		const auto installed = GetState().installed.load(std::memory_order_acquire);

		if (currentTarget != expectedTarget) {
			spdlog::warn(
				"MainThreadPump verify failed (reason='{}', thread={}, installed={}, slot={:#x}, currentTarget={:#x}, expectedTarget={:#x})",
				reason,
				GetCurrentThreadId(),
				installed,
				slotAddress,
				currentTarget,
				expectedTarget);
			return;
		}

		spdlog::info(
			"MainThreadPump verify ok (reason='{}', thread={}, installed={}, slot={:#x}, currentTarget={:#x})",
			reason,
			GetCurrentThreadId(),
			installed,
			slotAddress,
			currentTarget);
	}

	void MainThreadPump::OnTick()
	{
		auto& state = GetState();
		if (state.inTick.test_and_set(std::memory_order_acquire)) {
			return;
		}

		auto guard = SKSE::stl::scope_exit([&state]() {
			state.inTick.clear(std::memory_order_release);
		});

		if (!state.loggedOwnerThread.exchange(true, std::memory_order_acq_rel)) {
			spdlog::info(
				"MainThreadPump: owner thread is {} (slot={:#x}, currentTarget={:#x})",
				GetCurrentThreadId(),
				GetSlotAddress(),
				ReadCurrentTarget());
		}

		RecipeModeSession::GetSingleton().TickOwnerThread();
	}
}
