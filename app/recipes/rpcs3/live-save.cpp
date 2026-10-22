#include "stdafx.h"
#include "Utilities/sync.h"
#include "Emu/IdManager.h"
#include "Emu/System.h"
#include "Emu/system_config.h"
#include "Emu/VFS.h"
#include "Emu/savestate_utils.hpp"
#include "Emu/Cell/timers.hpp"
#include "Emu/Cell/Modules/cellAudio.h"
#include "Emu/Cell/Modules/cellCamera.h"
#include "Emu/Cell/Modules/cellDmuxPamf.h"
#include "Emu/Cell/Modules/cellSysutil.h"
#include "Emu/RSX/RSXThread.h"
#include "Emu/Cell/PPUThread.h"
#include "Emu/Cell/PPUAnalyser.h"
#include "Emu/Cell/PPUInterpreter.h"
#include "Emu/Cell/SPUThread.h"
#include "Emu/Cell/lv2/sys_cond.h"
#include "Emu/Cell/lv2/sys_event.h"
#include "Emu/Cell/lv2/sys_event_flag.h"
#include "Emu/Cell/lv2/sys_lwcond.h"
#include "Emu/Cell/lv2/sys_lwmutex.h"
#include "Emu/Cell/lv2/sys_mutex.h"
#include "Emu/Cell/lv2/sys_rwlock.h"
#include "Emu/Cell/lv2/sys_semaphore.h"
#include "Emu/Cell/lv2/sys_rsxaudio.h"
#include "Emu/Cell/lv2/sys_spu.h"
#include "Emu/Cell/lv2/sys_timer.h"
#include "Emu/Cell/lv2/sys_net/network_context.h"
#include "Emu/Cell/lv2/sys_net/sys_net_helpers.h"
#include "Emu/NP/np_handler.h"
#include "Emu/Memory/vm.h"
#include "Emu/Memory/vm_locking.h"
#include "Loader/ISO.h"
#include "Loader/TAR.h"
#include "rpcs3_version.h"
#include "util/serialization_ext.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <queue>
#include <span>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <thread>
#include <dirent.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zstd.h>

extern std::deque<std::pair<u64, cpu_thread*>> scheduled_timeouts asm("_ZL9g_waiting");
extern std::deque<cpu_thread*> threads_to_sleep asm("_ZL10g_to_sleep");
extern atomic_t<bool> scheduler_ready asm("_ZL17g_scheduler_ready");
extern bool try_lock_spu_threads_in_a_state_compatible_with_savestates(bool revert_lock, std::vector<std::pair<shared_ptr<named_thread<spu_thread>>, u32>>* out_list);
extern void resume_spu_thread_group_from_waiting(spu_thread& spu, std::array<shared_ptr<named_thread<spu_thread>>, 8>& notify_spus);
extern bool check_if_vdec_contexts_exist();
extern const ppu_decoder<ppu_itype> g_ppu_itype;
extern void using_global_version_serialization();
extern void qt_events_aware_op(int repeat_duration_ms, std::function<bool()> wrapped_op);

struct live_save_member
{
	const char* owner;
	usz offset;
};

extern "C"
{
	extern void* live_save_group_join_original;
	extern u32 live_save_nested_calls;
	extern s32 live_save_stopping;
	extern s32 live_save_draining;
	extern const live_save_member live_save_gem_config_mutex;
	extern const live_save_member live_save_gem_tracker_mutex;
	extern const live_save_member live_save_usb_event_mutex;
	extern const live_save_member live_save_usb_event_waiters;
	extern const live_save_member live_save_usb_events;
	extern const live_save_member live_save_sysutil_pending_callbacks;
}

template <auto Member, typename Tag>
struct private_member
{
	friend constexpr auto member(Tag) { return Member; }
};

struct emulator_state { friend constexpr auto member(emulator_state); };
struct emulator_game_dir { friend constexpr auto member(emulator_game_dir); };
struct emulator_extension_flags { friend constexpr auto member(emulator_extension_flags); };
template struct private_member<&Emulator::m_state, emulator_state>;
template struct private_member<&Emulator::m_game_dir, emulator_game_dir>;
template struct private_member<&Emulator::m_savestate_extension_flags1, emulator_extension_flags>;

struct lv2_run_queue { friend constexpr auto member(lv2_run_queue); };
struct lv2_pending_suspends { friend constexpr auto member(lv2_pending_suspends); };
template struct private_member<&lv2_obj::g_ppu, lv2_run_queue>;
template struct private_member<&lv2_obj::g_pending, lv2_pending_suspends>;

struct np_rpcn_mutex { friend constexpr auto member(np_rpcn_mutex); };
struct np_memory { friend constexpr auto member(np_memory); };
struct np_memory_mutex { friend constexpr auto member(np_memory_mutex); };
template struct private_member<&np::np_handler::mutex_rpcn, np_rpcn_mutex>;
template struct private_member<&np::np_handler::np_memory, np_memory>;
template struct private_member<&np::memory_allocator::m_mutex, np_memory_mutex>;

template <typename>
struct bit_set_enum;

template <typename Enum>
struct bit_set_enum<bs_t<Enum>>
{
	using type = Enum;
};

template <typename Class, typename Member>
Member member_type(Member Class::*);

namespace
{
	using namespace std::chrono_literals;
	using extension_flags = bit_set_enum<decltype(member_type(member(emulator_extension_flags{})))>::type;

	constexpr auto measured_stop = 42ms;
	constexpr auto settle_deadline = 3 * measured_stop;
	constexpr auto measured_replay = 5ms;
	constexpr auto replay_deadline = 3 * measured_replay;
	constexpr auto measured_capture = 5874ms;
	constexpr auto capture_deadline = 3 * measured_capture;
	constexpr auto measured_privatize = 123ms;
	constexpr auto privatize_deadline = 3 * measured_privatize;
	constexpr auto measured_blocking_hle_call = 3070ms;
	constexpr auto blocked_save_deadline = 3 * measured_blocking_hle_call;
	constexpr int graceful_shutdown_event_poll_ms = 5;
	constexpr std::string_view unrestorable_mount = "/app_home/";
	constexpr std::string_view host_root_mount = "/host_root/";
	std::vector<std::pair<shared_ptr<named_thread<spu_thread>>, u32>> paused_spus;
	std::vector<u32> stopped_threads;
	std::vector<u32> executing_threads;
	std::mutex settle_mutex;
	std::condition_variable settle_changed;
	std::vector<u32> awaited_threads;
	std::vector<u32> unwound_threads;
	std::vector<u32> late_threads;
	std::vector<u32> held_threads;
	std::vector<u32> tasking_threads;
	u32 replay_sleeps;
	bool replays_running;
	std::mutex deferred_mutex;
	struct deferred_event
	{
		u32 receiver;
		u32 queue;
		lv2_event event;
	};
	std::vector<deferred_event> deferred_events;
	std::shared_mutex capture_gate;
	std::jthread capture_reaper;
	constexpr auto classified_helpers = std::to_array<std::string_view>({
		"(anonymous namespace)::jit_module_manager",
		"id_manager::id_map<lv2_socket>",
		"id_manager::id_map<named_thread<dmux_pamf_spu_context>>",
		"id_manager::id_map<named_thread<ppu_thread>>",
		"id_manager::id_map<named_thread<spu_thread>>",
		"id_manager::id_map<ppu_thread>",
		"named_thread<VKGSRender>",
		"named_thread<camera_context>",
		"named_thread<cell_audio_thread>",
		"named_thread<cpu_prof>",
		"named_thread<gdb_thread>",
		"named_thread<gun_handler>",
		"named_thread<lv2_timer_thread>",
		"named_thread<mic_context>",
		"named_thread<msg_dlg_thread_info>",
		"named_thread<netstart_hack>",
		"named_thread<network_thread>",
		"named_thread<np::np_handler>",
		"named_thread<p2p_thread>",
		"named_thread<pad_thread>",
		"named_thread<perf_monitor>",
		"named_thread<ppu_syscall_usage>",
		"named_thread<progress_dialog_server>",
		"named_thread<rsx::overlays::media_list_dialog_thread>",
		"named_thread<rsxaudio_backend_thread>",
		"named_thread<rsxaudio_data_thread>",
		"named_thread<signaling_handler>",
		"named_thread<spu_llvm>",
		"named_thread<tcp_timeout_monitor>",
		"named_thread<vdec_context_reaper>",
		"named_thread<vk::driver_manager_t>",
		"named_thread<vuart_av_thread>",
		"named_thread<watchdog_t>",
		"ppu_thread_cleaner",
		"rsx::vblank_thread",
	});
	constexpr std::array located_helpers{&live_save_gem_config_mutex, &live_save_gem_tracker_mutex, &live_save_usb_event_mutex};

	enum class blocker
	{
		none,
		entering_task,
		nested_call,
		unreturned_thread,
		other,
	};

	struct shared_mapping
	{
		uptr start;
		uptr end;
		off_t offset;
		int protection;
		dev_t device;
		ino_t inode;
	};

	long long microseconds_since(std::chrono::steady_clock::time_point start)
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
	}

	template <typename Word>
	void wake_all(Word& word)
	{
		futex(&word, FUTEX_WAKE_PRIVATE, INT_MAX);
	}

	template <typename Word>
	void wait_for_change(Word& word, Word seen, std::chrono::steady_clock::time_point give_up)
	{
		const auto seconds = std::chrono::floor<std::chrono::seconds>(give_up.time_since_epoch());
		const timespec until{seconds.count(), std::chrono::nanoseconds{give_up.time_since_epoch() - seconds}.count()};
		futex(&word, FUTEX_WAIT_BITSET_PRIVATE, seen, &until, FUTEX_BITSET_MATCH_ANY);
	}

	void note_replay_sleep(cpu_thread& cpu)
	{
		std::erase(threads_to_sleep, &cpu);
		std::atomic_ref{replay_sleeps}.fetch_add(1);
		wake_all(replay_sleeps);
	}

	template <typename T>
	T* located(const live_save_member& location)
	{
		for (const auto& [info, object] : *g_fxo)
			if (info.name == location.owner) return reinterpret_cast<T*>(static_cast<uchar*>(object) + location.offset);
		return nullptr;
	}

	bool executing(ppu_thread& ppu)
	{
		switch (lv2_obj::ppu_state(&ppu, false).first)
		{
		case PPU_THREAD_STATUS_SLEEP:
		case PPU_THREAD_STATUS_RUNNABLE:
		case PPU_THREAD_STATUS_ONPROC:
			return true;
		case PPU_THREAD_STATUS_STOP:
			return !ppu.is_interrupt_thread;
		default:
			return false;
		}
	}

	ppu_intrp_func step_return = {[](ppu_thread& ppu, ppu_opcode_t, be_t<u32>* this_op, ppu_intrp_func*)
	{
		ppu.cia = vm::get_addr(this_op);
	}};

	bool parked(const cpu_thread& cpu)
	{
		return !!(cpu.state & cpu_flag::again);
	}

	s32 unlink_parked(ppu_thread*& queue)
	{
		s32 unlinked = 0;
		for (ppu_thread* cpu = queue; cpu;)
		{
			ppu_thread* const next = cpu->next_cpu;
			if (parked(*cpu))
			{
				lv2_obj::unqueue(queue, cpu);
				unlinked++;
			}
			cpu = next;
		}
		return unlinked;
	}

	template <typename Object, typename Repair>
	void repair_all(Repair repair)
	{
		idm::select<lv2_obj, Object>([&](u32, Object& object) { repair(object); });
	}

	template <typename Visit>
	void for_each_wait_list(Visit visit)
	{
		repair_all<lv2_cond>([&](lv2_cond& cond) { std::lock_guard lock(cond.mutex->mutex); visit(cond.sq); });
		repair_all<lv2_event_flag>([&](lv2_event_flag& flag) { std::lock_guard lock(flag.mutex); visit(flag.sq); });
		repair_all<lv2_event_queue>([&](lv2_event_queue& queue) { std::lock_guard lock(queue.mutex); visit(queue.pq); });
		repair_all<lv2_lwcond>([&](lv2_lwcond& cond) { std::lock_guard lock(cond.mutex); visit(cond.sq); });
		repair_all<lv2_lwmutex>([&](lv2_lwmutex& mutex)
		{
			std::lock_guard lock(mutex.mutex);
			mutex.lv2_control.atomic_op([&](lv2_lwmutex::control_data_t& data) { visit(data.sq); });
		});
		repair_all<lv2_mutex>([&](lv2_mutex& mutex)
		{
			std::lock_guard lock(mutex.mutex);
			mutex.control.atomic_op([&](lv2_mutex::control_data_t& data) { visit(data.sq); });
		});
		repair_all<lv2_rwlock>([&](lv2_rwlock& rwlock) { std::lock_guard lock(rwlock.mutex); visit(rwlock.rq); visit(rwlock.wq); });
		repair_all<lv2_sema>([&](lv2_sema& sema) { std::lock_guard lock(sema.mutex); sema.val += visit(sema.sq); });
		std::lock_guard lock(*located<shared_mutex>(live_save_usb_event_mutex));
		visit(*located<ppu_thread*>(live_save_usb_event_waiters));
	}

	bool still_waiting(const ppu_thread& ppu)
	{
		bool waiting = false;
		for_each_wait_list([&](ppu_thread*& queue) -> s32
		{
			for (const ppu_thread* cpu = queue; cpu; cpu = cpu->next_cpu) waiting |= cpu == &ppu;
			return 0;
		});
		return waiting;
	}

	void prepare_replay(ppu_thread& ppu)
	{
		if (lv2_obj::unqueue(*member(lv2_run_queue{}), &ppu, &ppu_thread::next_ppu) && std::exchange(ppu.ack_suspend, false)) (*member(lv2_pending_suspends{}))--;
		lv2_obj::set_future_sleep(&ppu);
	}

	void replay_syscall(ppu_thread& ppu)
	{
		ppu.cmd_list
		({
			{ppu_cmd::ptr_call, 0},

			+[](ppu_thread& ppu) -> bool
			{
				const u32 syscall = ppu.cia;
				const u32 op = vm::read32(syscall);
				{
					std::lock_guard lock(lv2_obj::g_mutex);
					if (!std::ranges::contains(threads_to_sleep, &ppu)) prepare_replay(ppu);
					ppu.state -= cpu_flag::suspend;
				}
				if (ppu.optional_savestate_state->data.empty()) ppu.optional_savestate_state->clear();
				ppu.optional_savestate_state->set_reading_state();
				ppu.loaded_from_savestate = true;
				ppu.prio.raw().preserve_bit = 1;
				g_fxo->get<ppu_interpreter_rt>().decode(op)(ppu, {op}, vm::_ptr<u32>(syscall), &step_return);
				ppu.prio.raw().preserve_bit = 0;
				ppu.loaded_from_savestate = false;
				bool replay_parked;
				{
					std::lock_guard lock(lv2_obj::g_mutex);
					note_replay_sleep(ppu);
					replay_parked = parked(ppu);
					if (!replay_parked) ppu.state += cpu_flag::suspend;
				}
				if (replay_parked)
				{
					std::memcpy(&ppu.gpr[3], ppu.syscall_args, sizeof(ppu.syscall_args));
					ppu.cia = syscall;
					return true;
				}
				ppu.optional_savestate_state->clear();
				ppu.check_state();
				ppu.cmd_push({ppu_cmd::cia_call, 0});
				return true;
			}
		});
	}

	void deliver_deferred_events()
	{
		std::vector<deferred_event> events;
		{
			std::lock_guard lock(deferred_mutex);
			events.swap(deferred_events);
		}
		for (const auto& [receiver, queue_id, event] : events)
		{
			const auto ppu = idm::get_unlocked<named_thread<ppu_thread>>(receiver);
			if (g_ppu_itype.decode(vm::read32(ppu->cia)) != ppu_itype::SC)
			{
				const auto queue = idm::get_unlocked<lv2_obj, lv2_event_queue>(queue_id);
				std::lock_guard queue_lock(queue->mutex);
				queue->events.push_front(event);
				continue;
			}
			std::tie(ppu->gpr[4], ppu->gpr[5], ppu->gpr[6], ppu->gpr[7]) = event;
			ppu->gpr[3] = CELL_OK;
			ppu->cia += 4;
			ppu->state -= cpu_flag::again;
			lv2_obj::awake(ppu.get());
		}
	}

	void repair_parked_threads()
	{
		for_each_wait_list(unlink_parked);
		deliver_deferred_events();
		std::lock_guard lock(lv2_obj::g_mutex);
		std::erase_if(scheduled_timeouts, [](const auto& timeout) { return parked(*timeout.second); });
	}

	void continue_thread(ppu_thread& ppu)
	{
		if (parked(ppu))
		{
			replay_syscall(ppu);
			ppu.state -= cpu_flag::notify;
		}
		else ppu.cmd_push({ppu_cmd::cia_call, 0});
	}

	void allow_nested_calls()
	{
		std::atomic_ref{live_save_stopping}.store(0);
		wake_all(live_save_stopping);
	}

	void wait_for_replays_to_sleep()
	{
		const auto give_up = std::chrono::steady_clock::now() + replay_deadline;
		for (u32 seen = std::atomic_ref{replay_sleeps}.load(); !lv2_obj::is_scheduler_ready() && std::chrono::steady_clock::now() < give_up; seen = std::atomic_ref{replay_sleeps}.load())
			wait_for_change(replay_sleeps, seen, give_up);
	}

	void release_replays(const std::vector<ppu_thread*>& replaying)
	{
		{
			std::lock_guard lock(lv2_obj::g_mutex);
			scheduler_ready.release(false);
			std::atomic_ref{replays_running}.store(true);
			for (ppu_thread* ppu : replaying)
			{
				ppu->state -= cpu_flag::stop + cpu_flag::again + cpu_flag::suspend + cpu_flag::signal;
				ppu->state.notify_one();
			}
		}
		wait_for_replays_to_sleep();
		std::atomic_ref{replays_running}.store(false);
		{
			std::lock_guard lock(lv2_obj::g_mutex);
			for (cpu_thread* cpu : threads_to_sleep)
				std::fprintf(stderr, "live-save: PPU 0x%x did not wait again within %lld ms of its replay\n", cpu->id, static_cast<long long>(std::chrono::milliseconds{replay_deadline}.count()));
			threads_to_sleep.clear();
		}
		lv2_obj::make_scheduler_ready();
	}

	void resume_threads()
	{
		std::vector<ppu_thread*> unwound;
		std::vector<u32> late;
		{
			std::lock_guard lock(settle_mutex);
			idm::select<named_thread<ppu_thread>>([&](u32 id, ppu_thread& ppu)
			{
				if (!std::ranges::contains(executing_threads, id)) return;
				if (std::ranges::contains(unwound_threads, id)) unwound.push_back(&ppu);
				else late.push_back(id);
			});
			late_threads.insert(late_threads.end(), late.begin(), late.end());
		}
		repair_parked_threads();
		std::vector<ppu_thread*> replaying;
		{
			std::lock_guard lock(lv2_obj::g_mutex);
			for (ppu_thread* ppu : unwound)
			{
				if (!parked(*ppu)) continue;
				prepare_replay(*ppu);
				replaying.push_back(ppu);
			}
		}
		for (ppu_thread* ppu : unwound) continue_thread(*ppu);
		release_replays(replaying);
		idm::select<named_thread<ppu_thread>>([&](u32 id, ppu_thread& ppu)
		{
			if (!std::ranges::contains(stopped_threads, id) || std::ranges::contains(late, id)) return;
			ppu.state -= cpu_flag::stop + cpu_flag::again;
			ppu.state.notify_one();
		});
		allow_nested_calls();
		if (!paused_spus.empty()) try_lock_spu_threads_in_a_state_compatible_with_savestates(true, nullptr);
		paused_spus.clear();
		stopped_threads.clear();
		executing_threads.clear();
		std::lock_guard lock(settle_mutex);
		awaited_threads.clear();
		unwound_threads.clear();
	}

	std::vector<u32> joining_threads()
	{
		std::vector<u32> joining;
		idm::select<named_thread<ppu_thread>>([&](u32, ppu_thread& ppu)
		{
			if (const ppu_join_status joiner = ppu.joiner; joiner >= ppu_join_status::max) joining.push_back(static_cast<u32>(joiner));
		});
		return joining;
	}

	bool entering_task()
	{
		return static_cast<bool>(idm::select<named_thread<ppu_thread>>([](u32 id, ppu_thread& ppu) { return executing(ppu) && !std::ranges::contains(tasking_threads, id); }));
	}

	void report_unsettled()
	{
		std::lock_guard lock(settle_mutex);
		idm::select<named_thread<ppu_thread>>([](u32 id, ppu_thread& ppu)
		{
			if (std::ranges::contains(awaited_threads, id) && !std::ranges::contains(unwound_threads, id))
				std::fprintf(stderr, "live-save: PPU 0x%x %s did not return (cia=0x%x state=0x%x commands=%u)\n", id, ppu.ppu_tname.load()->c_str(), ppu.cia, static_cast<u32>(ppu.state.load()), ppu.cmd_queue.size());
		});
	}

	bool has_pending_work(ppu_thread& ppu)
	{
		return ppu.cmd_queue.size() > 1 || (ppu.cmd_get(0) && ppu.cmd_get(0).arg1<ppu_cmd>() != ppu_cmd::cia_call);
	}

	std::vector<std::unique_lock<shared_mutex>> hold_network_loops()
	{
		std::vector<std::unique_lock<shared_mutex>> loops;
		if (const auto network = g_fxo->try_get<network_context>()) loops.emplace_back(network->mutex_thread_loop);
		if (const auto p2p = g_fxo->try_get<p2p_context>()) loops.emplace_back(p2p->mutex_thread_loop);
		return loops;
	}

	void park_unwoken_waiters(const std::vector<u32>& unwound)
	{
		std::vector<ppu_thread*> after_syscall;
		idm::select<named_thread<ppu_thread>>([&](u32 id, ppu_thread& ppu)
		{
			if (std::ranges::contains(unwound, id) && !parked(ppu) && vm::check_addr(ppu.cia - 4) && g_ppu_itype.decode(vm::read32(ppu.cia - 4)) == ppu_itype::SC) after_syscall.push_back(&ppu);
		});
		for (ppu_thread* ppu : after_syscall)
		{
			if (!network_clear_queue(*ppu) && !still_waiting(*ppu)) continue;
			clear_ppu_to_awake(*ppu);
			std::memcpy(&ppu->gpr[3], ppu->syscall_args, sizeof(ppu->syscall_args));
			ppu->cia -= 4;
			ppu->state += cpu_flag::again;
		}
	}

	blocker stop_threads()
	{
		{
			std::lock_guard lock(settle_mutex);
			if (entering_task()) return blocker::entering_task;
		}
		if (const auto queued = idm::select<named_thread<ppu_thread>>([](u32, ppu_thread& ppu) { return executing(ppu) && has_pending_work(ppu); }))
		{
			std::fprintf(stderr, "live-save: refused, PPU 0x%x still has queued commands\n", queued->id);
			return blocker::other;
		}
		{
			std::lock_guard lock(settle_mutex);
			late_threads.clear();
		}
		std::atomic_ref{live_save_stopping}.store(1);
		if (std::atomic_ref{live_save_nested_calls}.load())
		{
			allow_nested_calls();
			std::fprintf(stderr, "live-save: refused, a PPU thread is inside a call from an HLE function\n");
			return blocker::nested_call;
		}
		const std::vector<u32> joining = joining_threads();
		const auto network_loops = hold_network_loops();
		idm::select<named_thread<ppu_thread>>([&](u32 id, ppu_thread& ppu)
		{
			if (executing(ppu))
			{
				if (!ppu.is_interrupt_thread) ppu.interrupt_thread_executing = true;
				executing_threads.push_back(id);
				std::lock_guard lock(settle_mutex);
				if (!std::ranges::contains(joining, id)) awaited_threads.push_back(id);
			}
			if (!ppu.state.test_and_set(cpu_flag::stop)) stopped_threads.push_back(id);
			ppu.state.notify_one();
		});
		std::unique_lock lock(settle_mutex);
		const bool all_settled = settle_changed.wait_for(lock, settle_deadline, [] { return std::ranges::all_of(awaited_threads, [](u32 id) { return std::ranges::contains(unwound_threads, id); }) || !held_threads.empty(); }) && held_threads.empty();
		const std::vector<u32> unwound = unwound_threads;
		lock.unlock();
		park_unwoken_waiters(unwound);
		if (!all_settled)
		{
			report_unsettled();
			resume_threads();
			return blocker::unreturned_thread;
		}
		if (idm::select<named_thread<spu_thread>>([](u32, spu_thread&) {}) && !try_lock_spu_threads_in_a_state_compatible_with_savestates(false, &paused_spus))
		{
			std::fprintf(stderr, "live-save: SPU threads are not in a savable state\n");
			resume_threads();
			return blocker::other;
		}
		return blocker::none;
	}

	bool drain_nested_calls(std::chrono::steady_clock::time_point give_up)
	{
		std::atomic_ref draining{live_save_draining};
		std::atomic_ref nested_calls{live_save_nested_calls};
		draining.store(1);
		for (u32 calls; (calls = nested_calls.load()) && std::chrono::steady_clock::now() < give_up;)
			wait_for_change(live_save_nested_calls, calls, give_up);
		draining.store(0);
		return !nested_calls.load();
	}

	bool wait_out(blocker blocked, std::chrono::steady_clock::time_point give_up)
	{
		if (blocked == blocker::nested_call) return drain_nested_calls(give_up);
		std::unique_lock lock(settle_mutex);
		if (blocked == blocker::entering_task) return settle_changed.wait_until(lock, give_up, [] { return !entering_task(); });
		return blocked == blocker::unreturned_thread && settle_changed.wait_until(lock, give_up, [] { return late_threads.empty(); });
	}

	class synchronous_zstd_file_handler final : public utils::serialization_file_handler
	{
	public:
		explicit synchronous_zstd_file_handler(const fs::file& file)
			: m_file(file)
		{
		}

		~synchronous_zstd_file_handler() override
		{
			ZSTD_freeCCtx(m_context);
		}

		bool handle_file_op(utils::serial& ar, usz pos, usz size, const void*) override
		{
			ensure(ar.pos == ar.data_offset + ar.data.size());
			compress(ar.data, ZSTD_e_continue);
			ar.data_offset = ar.pos;
			ar.data.clear();
			if (pos == umax && size == umax) m_file.sync();
			return true;
		}

		usz get_size(const utils::serial&, usz) const override
		{
			return m_file.size();
		}

		void finalize(utils::serial& ar) override
		{
			handle_file_op(ar, 0, umax, nullptr);
			compress({}, ZSTD_e_end);
			m_file.sync();
		}

	private:
		void compress(std::span<const u8> input, ZSTD_EndDirective directive)
		{
			ZSTD_inBuffer source{input.data(), input.size(), 0};
			for (bool pending = true; pending;)
			{
				ZSTD_outBuffer target{m_output.data(), m_output.size(), 0};
				const usz remaining = ZSTD_compressStream2(m_context, &target, &source, directive);
				ensure(!ZSTD_isError(remaining));
				m_file.write(m_output.data(), target.pos);
				pending = directive == ZSTD_e_end ? remaining != 0 : source.pos < source.size;
			}
		}

		const fs::file& m_file;
		ZSTD_CCtx* const m_context = ZSTD_createCCtx();
		std::vector<u8> m_output = std::vector<u8>(ZSTD_CStreamOutSize());
	};

	void capture(const fs::file& file, u64 timestamp)
	{
		utils::serial ar;
		ar.m_file_handler = std::make_unique<synchronous_zstd_file_handler>(file);

		read_used_savestate_versions();
		using_global_version_serialization();

		auto save_tar = [&](const std::string& directory)
		{
			if (!fs::is_dir(directory))
			{
				ar(usz{});
				return;
			}
			std::vector<fs::dir_entry> entries;
			utils::serial ar_null;
			ar_null.m_file_handler = make_null_serialization_file_handler();
			tar_object::save_directory(directory, ar_null, {}, std::move(entries), false);
			ar(ar_null.pos);
			ar.breathe();
			const usz old_pos = ar.seek_end();
			tar_object::save_directory(directory, ar, {}, std::move(entries), true);
			ensure(ar.seek_end() - old_pos == ar_null.pos);
		};

		ar("RPCS3SAV"_u64);
		ar(std::endian::native == std::endian::little);
		ar(g_cfg.savestate.state_inspection_mode.get());
		ar(usz{10 + sizeof(usz) + sizeof(u8)});
		{
			utils::serial ar_temp;
			ar_temp.m_file_handler = make_null_serialization_file_handler();
			g_fxo->save(ar_temp);
			ar(u8{1});
			ar(read_used_savestate_versions());
		}
		ar(u8{1});
		ar(rpcs3::get_verbose_version());
		ar(fmt::format("%s", std::chrono::system_clock::now()));
		ar(Emu.GetTitleAndTitleID());
		ar(std::string{});
		ar(std::array<u8, 32>{});

		const std::string& boot = Emu.GetBoot();
		const std::string& title_id = Emu.GetTitleID();
		const auto restorable = [&boot]
		{
			std::string mapped = vfs::retrieve(boot);
			return mapped.starts_with(unrestorable_mount) ? std::string{host_root_mount} + boot : mapped;
		};
		if (boot.starts_with(iso_device::virtual_device_name + "/"))
		{
			const auto device = ensure(fs::get_virtual_device(iso_device::virtual_device_name + "/"));
			ar(boot.substr(iso_device::virtual_device_name.size() + 1));
			ar(ensure(dynamic_cast<const iso_device*>(device.get()))->get_loaded_iso());
		}
		else if (auto dir = vfs::get("/dev_bdvd/PS3_GAME"); fs::is_dir(dir) && !fs::is_file(fs::get_parent_dir(dir) + "/PS3_DISC.SFB"))
		{
			ensure(vfs::unmount("/dev_bdvd/PS3_GAME"));
			ar(restorable());
			ar(vfs::retrieve(Emu.disc));
			ensure(vfs::mount("/dev_bdvd/PS3_GAME", dir));
		}
		else
		{
			ar(restorable());
			ar(!title_id.empty() && !vfs::get("/dev_bdvd").empty() ? title_id : vfs::retrieve(Emu.disc));
		}
		ar(Emu.klic.empty() ? std::array<u8, 16>{} : std::bit_cast<std::array<u8, 16>>(Emu.klic[0]));
		ar(Emu.*member(emulator_game_dir{}));

		const std::string hdd1 = vfs::get("/dev_hdd1");
		const std::string_view hdd1_path = std::string_view{hdd1}.substr(0, hdd1.find_last_not_of(fs::delim) + 1);
		ar(std::string{hdd1_path.substr(hdd1_path.find_last_of(fs::delim) + 1)});
		if (!hdd1.empty()) save_tar(hdd1);

		if (g_cfg.savestate.save_disc_game_data)
		{
			const std::string games = vfs::get("/dev_hdd0/game/");
			for (auto& entry : fs::dir(games))
			{
				if (!entry.is_directory || entry.name == "." || entry.name == "..") continue;
				if (auto sfo = psf::load(games + entry.name + "/PARAM.SFO"); sfo && psf::get_string(sfo.sfo, "TITLE_ID") == title_id && psf::get_string(sfo.sfo, "CATEGORY") == "GD")
				{
					ar(entry.name);
					save_tar(games + entry.name);
				}
			}
		}
		ar(std::string{});
		ar(std::array<u8, 32>{});

		vm::save(ar);
		g_fxo->save(ar);

		bs_t<extension_flags> flags{extension_flags::SupportsMenuOpenResume};
		if (g_fxo->get<SysutilMenuOpenStatus>().active) flags += extension_flags::ShouldCloseMenu;
		ar(flags);
		ar(std::array<u8, 32>{});
		ar(timestamp);
		ar.seek_end();
		ar.m_file_handler->finalize(ar);
	}

	[[noreturn]] void capture_failed(const char* what)
	{
		std::fprintf(stderr, "live-save: capture failed to %s: %s\n", what, std::strerror(errno));
		_exit(EXIT_FAILURE);
	}

	std::vector<shared_mapping> shared_mappings()
	{
		std::vector<shared_mapping> mappings;
		FILE* const maps = std::fopen("/proc/self/maps", "re");
		if (!maps) capture_failed("read /proc/self/maps");
		char* line = nullptr;
		usz size = 0;
		while (getline(&line, &size, maps) > 0)
		{
			unsigned long start, end, offset, inode;
			unsigned major, minor;
			char permissions[5];
			if (std::sscanf(line, "%lx-%lx %4s %lx %x:%x %lu", &start, &end, permissions, &offset, &major, &minor, &inode) != 7 || permissions[3] != 's') continue;
			const int protection = (permissions[0] == 'r' ? PROT_READ : 0) | (permissions[1] == 'w' ? PROT_WRITE : 0) | (permissions[2] == 'x' ? PROT_EXEC : 0);
			mappings.push_back({start, end, static_cast<off_t>(offset), protection, makedev(major, minor), inode});
		}
		std::free(line);
		std::fclose(maps);
		return mappings;
	}

	struct extent
	{
		int source;
		int copy;
		off_t start;
		off_t end;
	};

	void add_extents(int source, int copy, std::vector<extent>& extents)
	{
		off_t data = lseek(source, 0, SEEK_DATA);
		for (off_t hole; data >= 0; data = lseek(source, hole, SEEK_DATA))
		{
			hole = lseek(source, data, SEEK_HOLE);
			if (hole < 0) capture_failed("find the end of data in shared memory");
			extents.push_back({source, copy, data, hole});
		}
		if (errno != ENXIO) capture_failed("find data in shared memory");
	}

	void copy_extents(const std::vector<extent>& extents)
	{
		for (const auto& [source, copy, start, end] : extents)
		{
			for (loff_t from = start, to = start; from < end;)
			{
				if (copy_file_range(source, &from, copy, &to, end - from, 0) <= 0) capture_failed("copy shared memory");
			}
		}
	}

	void copy_on_every_core(const std::vector<extent>& extents)
	{
		const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
		off_t total = 0;
		for (const extent& piece : extents) total += piece.end - piece.start;
		const off_t share = (total + cores - 1) / cores;
		std::vector<std::vector<extent>> slices(cores);
		usz core = 0;
		off_t filled = 0;
		for (extent piece : extents)
		{
			while (piece.start < piece.end)
			{
				const off_t taken = std::min(piece.end - piece.start, share - filled);
				slices[core].push_back({piece.source, piece.copy, piece.start, piece.start + taken});
				piece.start += taken;
				filled += taken;
				if (filled == share)
				{
					core++;
					filled = 0;
				}
			}
		}
		std::vector<std::jthread> copiers;
		for (const auto& slice : slices) copiers.emplace_back([&slice] { copy_extents(slice); });
	}

	void privatize_shared_memory()
	{
		const std::vector<shared_mapping> mappings = shared_mappings();
		std::map<std::pair<dev_t, ino_t>, int> copies;
		std::vector<int> sources;
		std::vector<extent> extents;
		DIR* const descriptors = opendir("/proc/self/fd");
		if (!descriptors) capture_failed("list /proc/self/fd");
		for (const dirent* entry; (entry = readdir(descriptors));)
		{
			struct stat status;
			if (entry->d_type != DT_LNK || fstatat(dirfd(descriptors), entry->d_name, &status, 0) || !S_ISREG(status.st_mode)) continue;
			const std::pair key{status.st_dev, status.st_ino};
			if (copies.contains(key) || std::ranges::none_of(mappings, [&](const shared_mapping& mapping) { return std::pair{mapping.device, mapping.inode} == key; })) continue;
			const int source = openat(dirfd(descriptors), entry->d_name, O_RDONLY | O_CLOEXEC);
			const int copy = memfd_create("live-save", MFD_CLOEXEC);
			if (source < 0 || copy < 0 || ftruncate(copy, status.st_size)) capture_failed("open a copy of shared memory");
			add_extents(source, copy, extents);
			sources.push_back(source);
			copies.emplace(key, copy);
		}
		closedir(descriptors);
		copy_on_every_core(extents);
		for (int source : sources) close(source);
		for (const shared_mapping& mapping : mappings)
		{
			const auto copy = copies.find({mapping.device, mapping.inode});
			if (copy == copies.end()) continue;
			if (mmap(reinterpret_cast<void*>(mapping.start), mapping.end - mapping.start, mapping.protection, MAP_SHARED | MAP_FIXED, copy->second, mapping.offset) == MAP_FAILED) capture_failed("map a copy of shared memory");
		}
	}

	[[noreturn]] void capture_in_child(const fs::file& file, u64 timestamp)
	{
		logs::silence();
		if (const auto pending = located<u64>(live_save_sysutil_pending_callbacks); !pending || *pending)
		{
			std::fprintf(stderr, "live-save: refused, system callbacks are pending\n");
			_exit(EXIT_FAILURE);
		}
		Emu.*member(emulator_state{}) = system_state::stopping;
		for (u32 id : stopped_threads) idm::check_unlocked<named_thread<ppu_thread>>(id)->state -= cpu_flag::stop;
		named_thread capture_thread("Emu State Capture Thread", [&]
		{
			capture(file, timestamp);
			_exit(EXIT_SUCCESS);
		});
		capture_thread();
		std::unreachable();
	}

	bool wait_for_capture(pid_t child, int finished)
	{
		pollfd capture{finished, POLLIN, 0};
		const int ready = poll(&capture, 1, static_cast<int>(std::chrono::milliseconds{capture_deadline}.count()));
		if (ready <= 0)
		{
			std::fprintf(stderr, "live-save: capture did not finish within %lld ms, killing it\n", static_cast<long long>(std::chrono::milliseconds{capture_deadline}.count()));
			kill(child, SIGKILL);
		}
		int status = 0;
		waitpid(child, &status, 0);
		if (WIFSIGNALED(status)) std::fprintf(stderr, "live-save: capture died of signal %d\n", WTERMSIG(status));
		return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
	}

	pid_t fork_capture(const fs::file& file, int& finished, std::unique_lock<std::shared_mutex>& gate)
	{
		const u64 timestamp = get_timebased_time();
		int privatized[2], exited[2];
		if (pipe2(privatized, O_CLOEXEC)) return -1;
		if (pipe2(exited, O_CLOEXEC))
		{
			close(privatized[0]);
			close(privatized[1]);
			return -1;
		}
		const pid_t child = fork();
		if (child == 0)
		{
			close(privatized[0]);
			close(exited[0]);
			privatize_shared_memory();
			close(privatized[1]);
			capture_in_child(file, timestamp);
		}
		gate.unlock();
		close(privatized[1]);
		close(exited[1]);
		pollfd handshake{privatized[0], POLLIN, 0};
		if (child > 0 && poll(&handshake, 1, static_cast<int>(privatize_deadline.count())) <= 0)
		{
			std::fprintf(stderr, "live-save: capture did not copy shared memory within %lld ms, killing it\n", static_cast<long long>(privatize_deadline.count()));
			kill(child, SIGKILL);
		}
		close(privatized[0]);
		finished = exited[0];
		return child;
	}

	std::vector<std::unique_lock<shared_mutex>> hold_helpers()
	{
		auto locks = hold_network_loops();
		idm::select<lv2_obj, lv2_timer>([&](u32, lv2_timer& timer) { locks.emplace_back(timer.mutex); });
		if (const auto audio = g_fxo->try_get<cell_audio>()) locks.emplace_back(audio->mutex);
		idm::select<lv2_obj, lv2_rsxaudio>([&](u32, lv2_rsxaudio& rsxaudio) { locks.emplace_back(rsxaudio.mutex); });
		if (const auto camera = g_fxo->try_get<camera_thread>()) locks.emplace_back(camera->mutex);
		for (const live_save_member* helper : located_helpers) locks.emplace_back(*ensure(located<shared_mutex>(*helper)));
		if (const auto np = g_fxo->try_get<named_thread<np::np_handler>>())
		{
			locks.emplace_back(np->*member(np_rpcn_mutex{}));
			locks.emplace_back(np->*member(np_memory{}).*member(np_memory_mutex{}));
		}
		return locks;
	}

	bool unknown_helpers()
	{
		bool unknown = false;
		for (const auto& [info, object] : *g_fxo)
		{
			if (!info.thread_op || std::ranges::contains(classified_helpers, info.name) || std::ranges::any_of(located_helpers, [&](const live_save_member* helper) { return info.name == helper->owner; })) continue;
			std::fprintf(stderr, "live-save: refused, helper %.*s is not known to be safe to save beside\n", static_cast<int>(info.name.size()), info.name.data());
			unknown = true;
		}
		return unknown;
	}

	bool helpers_missing()
	{
		const auto missing = std::ranges::find(located_helpers, nullptr, [](const live_save_member* helper) { return located<shared_mutex>(*helper); });
		if (missing == located_helpers.end()) return false;
		std::fprintf(stderr, "live-save: refused, helper %s was not found\n", (*missing)->owner);
		return true;
	}

	bool refused()
	{
		if (check_if_vdec_contexts_exist())
		{
			std::fprintf(stderr, "live-save: refused, video decoder contexts exist\n");
			return true;
		}
		if (idm::select<named_thread<dmux_pamf_spu_context>>([](u32, dmux_pamf_spu_context&) {}))
		{
			std::fprintf(stderr, "live-save: refused, demuxer contexts exist\n");
			return true;
		}
		if (unknown_helpers()) return true;
		if (!g_fxo->get<hle_locks_t>().lock_val.compare_and_swap_test(0, hle_locks_t::waiting_for_evaluation))
		{
			std::fprintf(stderr, "live-save: refused, an HLE function holds a savestate lock\n");
			return true;
		}
		return false;
	}

	void wait_until_booted()
	{
		const auto& state = Emu.*member(emulator_state{});
		for (system_state now = state; now < system_state::running || now > system_state::frozen; now = state) state.wait(now);
	}

	void release_late_threads()
	{
		std::lock_guard lock(settle_mutex);
		idm::select<named_thread<ppu_thread>>([](u32 id, ppu_thread& ppu)
		{
			if (!std::ranges::contains(late_threads, id)) return;
			ppu.state -= cpu_flag::stop;
			ppu.state.notify_one();
		});
	}

	std::optional<std::chrono::steady_clock::time_point> freeze_threads(std::chrono::steady_clock::time_point give_up)
	{
		auto start = std::chrono::steady_clock::now();
		for (blocker blocked; (blocked = stop_threads()) != blocker::none; start = std::chrono::steady_clock::now())
		{
			if (wait_out(blocked, give_up)) continue;
			release_late_threads();
			return std::nullopt;
		}
		return start;
	}

	void save(const std::string& path)
	{
		if (capture_reaper.joinable()) capture_reaper.join();
		wait_until_booted();
		if (helpers_missing()) return;
		auto pending = std::make_shared<fs::pending_file>(path);
		if (!pending->file)
		{
			std::fprintf(stderr, "live-save: failed to save %s, could not create its file\n", path.c_str());
			return;
		}
		const auto frozen = freeze_threads(std::chrono::steady_clock::now() + blocked_save_deadline);
		if (!frozen) return;
		const auto start = *frozen;
		if (refused())
		{
			resume_threads();
			return;
		}
		auto locks = hold_helpers();
		const auto rsx = g_fxo->try_get<rsx::thread>();
		const bool rsx_runs_commands = rsx && rsx->rsx_thread_running;
		if (rsx_runs_commands) rsx->pause();
		std::unique_lock gate(capture_gate);
		deliver_deferred_events();
		int finished = -1;
		const pid_t child = fork_capture(pending->file, finished, gate);
		if (rsx_runs_commands) rsx->unpause();
		locks.clear();
		auto& hle = g_fxo->get<hle_locks_t>();
		hle.lock_val.compare_and_swap_test(hle_locks_t::waiting_for_evaluation, 0);
		hle.lock_val.notify_all();
		resume_threads();
		const long long stopped = microseconds_since(start);
		capture_reaper = std::jthread([=]
		{
			const bool saved = child > 0 && wait_for_capture(child, finished) && pending->commit();
			close(finished);
			std::fprintf(stderr, "live-save: %s %s, stopped %lld us, written in %lld us\n", saved ? "saved" : "failed to save", path.c_str(), stopped, microseconds_since(start));
		});
	}

	void forget_stopped_emulation()
	{
		{
			std::lock_guard lock(deferred_mutex);
			deferred_events.clear();
		}
		paused_spus.clear();
		stopped_threads.clear();
		executing_threads.clear();
		std::lock_guard lock(settle_mutex);
		awaited_threads.clear();
		unwound_threads.clear();
		late_threads.clear();
		held_threads.clear();
	}

	void load(const std::string& path)
	{
		if (!Emu.IsStopped())
		{
			wait_until_booted();
			if (!freeze_threads(std::chrono::steady_clock::now() + blocked_save_deadline)) std::fprintf(stderr, "live-save: could not stop the PPU threads before loading %s, stopping the emulation with them running\n", path.c_str());
			allow_nested_calls();
		}
		atomic_t<u32> booted = 0;
		Emu.CallFromMainThread([path]
		{
			const auto session = Emu.GetEmulationIdentifier();
			Emu.SetContinuousMode(true);
			Emu.Kill(false);
			idm::select<named_thread<ppu_thread>>([](u32, ppu_thread& ppu) { ppu.state.notify_one(); });
			qt_events_aware_op(graceful_shutdown_event_poll_ms, [session] { return Emu.GetEmulationIdentifier() != session || Emu.IsStopped(true); });
			forget_stopped_emulation();
			if (const game_boot_result error = Emu.BootGame(path, "", true); error != game_boot_result::no_errors)
				std::fprintf(stderr, "live-save: failed to load %s (error %u)\n", path.c_str(), static_cast<u32>(error));
		}, &booted);
		while (!booted) booted.wait(0);
	}
}

extern "C"
{
	extern const u32 live_save_exiting_loop_mask = static_cast<u32>(cpu_flag::exit + cpu_flag::again + cpu_flag::req_exit);
	extern const u32 live_save_parked_loop_mask = static_cast<u32>(+cpu_flag::req_exit);
}

extern "C" CellError live_save_send_event(lv2_event_queue& queue, lv2_event event, bool* notified_thread, lv2_event_port* port)
{
	if (notified_thread)
	{
		*notified_thread = false;
	}

	struct notify_spus_t
	{
		std::array<shared_ptr<named_thread<spu_thread>>, 8> spus;

		~notify_spus_t() noexcept
		{
			for (auto& spu : spus)
			{
				if (spu && spu->state & cpu_flag::wait)
				{
					spu->state.notify_one();
				}
			}
		}
	} notify_spus{};

	std::shared_lock gate(capture_gate);
	std::lock_guard lock(queue.mutex);

	if (!queue.exists)
	{
		return CELL_ENOTCONN;
	}

	cpu_thread* const receiver = queue.type == SYS_PPU_QUEUE ? static_cast<cpu_thread*>(lv2_obj::schedule<ppu_thread>(queue.pq, queue.protocol)) : lv2_obj::schedule<spu_thread>(queue.sq, queue.protocol);

	if (queue.type == SYS_PPU_QUEUE && receiver && parked(*receiver))
	{
		lv2_obj::unqueue(queue.pq, static_cast<ppu_thread*>(receiver));
		std::lock_guard deferred(deferred_mutex);
		deferred_events.push_back({receiver->id, queue.id, event});
		return {};
	}

	if (!receiver || parked(*receiver))
	{
		if (queue.events.size() >= queue.size + 0u)
		{
			return CELL_EBUSY;
		}

		queue.events.emplace_back(event);
		return {};
	}

	if (queue.type == SYS_PPU_QUEUE)
	{
		auto& ppu = static_cast<ppu_thread&>(*receiver);
		std::tie(ppu.gpr[4], ppu.gpr[5], ppu.gpr[6], ppu.gpr[7]) = event;
		lv2_obj::awake(&ppu);

		if (port && ppu.prio.load().prio < ensure(cpu_thread::get_current<ppu_thread>())->prio.load().prio)
		{
			port->is_busy++;
			ensure(notified_thread);
			*notified_thread = true;
		}
	}
	else
	{
		auto& spu = static_cast<spu_thread&>(*receiver);
		spu.ch_in_mbox.set_values(4, CELL_OK, static_cast<u32>(std::get<1>(event)), static_cast<u32>(std::get<2>(event)), static_cast<u32>(std::get<3>(event)));
		resume_spu_thread_group_from_waiting(spu, notify_spus.spus);
	}

	return {};
}

extern "C" void live_save_usb_add_event(void*, u64 arg1, u64 arg2, u64 arg3)
{
	std::lock_guard lock(*ensure(located<shared_mutex>(live_save_usb_event_mutex)));
	ppu_thread*& waiters = *located<ppu_thread*>(live_save_usb_event_waiters);
	unlink_parked(waiters);
	if (const auto ppu = lv2_obj::schedule<ppu_thread>(waiters, SYS_SYNC_PRIORITY))
	{
		std::tie(ppu->gpr[4], ppu->gpr[5], ppu->gpr[6]) = std::tie(arg1, arg2, arg3);
		lv2_obj::awake(ppu);
		return;
	}
	located<std::queue<std::tuple<u64, u64, u64>>>(live_save_usb_events)->emplace(arg1, arg2, arg3);
}

extern "C" error_code live_save_group_join(ppu_thread& ppu, u32 id, vm::ptr<u32> cause, vm::ptr<u32> status)
{
	const error_code result = reinterpret_cast<decltype(&sys_spu_thread_group_join)>(live_save_group_join_original)(ppu, id, cause, status);
	if (!parked(ppu)) return result;
	const auto group = idm::get_unlocked<lv2_spu_group>(id);
	std::lock_guard lock(group->mutex);
	if (group->waiter == &ppu) group->waiter = nullptr;
	else group->join_state.release(static_cast<u32>(ppu.gpr[4]));
	return result;
}

extern "C" void live_save_slept(void* thread)
{
	if (std::atomic_ref{replays_running}.load()) note_replay_sleep(*static_cast<cpu_thread*>(thread));
}

extern "C" void live_save_task_entered(void* thread)
{
	std::lock_guard lock(settle_mutex);
	tasking_threads.push_back(static_cast<ppu_thread*>(thread)->id);
	settle_changed.notify_all();
}

extern "C" void live_save_task_returned(void* thread)
{
	auto& ppu = *static_cast<ppu_thread*>(thread);
	{
		std::lock_guard lock(settle_mutex);
		std::erase(tasking_threads, ppu.id);
		if (!std::ranges::contains(late_threads, ppu.id))
		{
			if (!std::ranges::contains(awaited_threads, ppu.id) || std::ranges::contains(unwound_threads, ppu.id)) return;
			vm::temporary_unlock(ppu);
			unwound_threads.push_back(ppu.id);
			settle_changed.notify_all();
			return;
		}
	}
	repair_parked_threads();
	continue_thread(ppu);
	ppu.state -= cpu_flag::again + cpu_flag::stop;
	std::lock_guard lock(settle_mutex);
	std::erase(late_threads, ppu.id);
	settle_changed.notify_all();
}

extern "C" void live_save_call_held(void* thread)
{
	std::lock_guard lock(settle_mutex);
	held_threads.push_back(static_cast<ppu_thread*>(thread)->id);
	settle_changed.notify_all();
}

extern "C" void live_save_call_released(void* thread)
{
	auto& ppu = *static_cast<ppu_thread*>(thread);
	std::lock_guard lock(settle_mutex);
	std::erase(held_threads, ppu.id);
	if (std::erase(late_threads, ppu.id)) ppu.state -= cpu_flag::stop;
	settle_changed.notify_all();
}

extern "C" void live_save_sfo_dir(std::string& sfo_dir, const std::string& game_path)
{
	const std::string archive = iso_device::virtual_device_name + "/";
	if (game_path.starts_with(archive)) sfo_dir = archive + Emu.*member(emulator_game_dir{});
}

extern "C" void live_save_run_finalized(void*)
{
	(Emu.*member(emulator_state{})).notify_all();
}

extern "C" void live_save_command(const char* command)
{
	const std::string_view line = command;
	const usz split = line.find(' ');
	const std::string_view name = line.substr(0, split);
	const std::string argument{line.substr(std::min(split, line.size() - 1) + 1)};
	if (name == "save") save(argument);
	else if (name == "load") load(argument);
	else std::fprintf(stderr, "live-save: unknown command %s\n", command);
}
