// AsteroidBiomeChatNotifications - announces the Genesis 2 asteroid biome in
// cluster chat when the space biome rotates.
//
// WHAT CHANGED, AND WHY IT MATTERS MORE THAN IT LOOKS
//
// This plugin used to open its own MariaDB connection and INSERT a row into
// ArkCrossChat's `messages` table. One INSERT, no reads, no state - and for
// that it carried a database library, credentials in its own config.json,
// reconnect logic, and a socket opened on the game thread from inside a
// UFunction hook. A slow database stalled the server mid-dispatch.
//
// It now asks ArkCrossChat to say the line instead. That removes a writer from
// the messages table, which is the whole point: a table with fewer writers is a
// table that can be moved between database engines without coordinating several
// programs at once. It also means this plugin never needs porting again for any
// backend, and the game thread never touches the network.
//
// Three other defects went with it:
//
//   Hooks were installed from DllMain, under the Windows loader lock, and
//   Unload() was EMPTY. Three live detours pointed into freed code the moment
//   the DLL was unloaded - a map hang with nothing in the log to explain it.
//   Hooks now go in from Plugin_Init and come out in Plugin_Unload.
//
//   The biome list was compiled in. It describes game content, which changes
//   when the game does, so it lives in config.json now - with the list below as
//   the default, so a missing config file is not a failure.
//
//   A database connection was attempted whether or not this was even a
//   Genesis 2 map.
//
// ON A MAP THAT IS NOT GENESIS 2 this plugin is inert: the DayCycleManager
// blueprint does not load, the watched function index stays -1, and the hook
// below is a compare-and-forward.
#include <API/ARK/Ark.h>

#include <json.hpp>

#include <atomic>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>

// ---------------------------------------------------------------- engine ABI
// Enough of FFrame to reach the executing UFunction and its parameters. These
// are layout accessors over the live object, not a redefinition of the engine's
// types.
struct FOutParmRec
{
	UProperty*& PropertyField() { return *GetNativePointerField<UProperty**>(this, "FOutParmRec.Property"); }
	unsigned __int8*& PropAddrField() { return *GetNativePointerField<unsigned __int8**>(this, "FOutParmRec.PropAddr"); }
	FOutParmRec*& NextOutParmField() { return *GetNativePointerField<FOutParmRec**>(this, "FOutParmRec.NextOutParm"); }
};

struct FFrame : FOutputDevice
{
	UFunction*& NodeField() { return *GetNativePointerField<UFunction**>(this, "FFrame.Node"); }
	UObject*& ObjectField() { return *GetNativePointerField<UObject**>(this, "FFrame.Object"); }
	unsigned __int8*& CodeField() { return *GetNativePointerField<unsigned __int8**>(this, "FFrame.Code"); }
	unsigned __int8*& LocalsField() { return *GetNativePointerField<unsigned __int8**>(this, "FFrame.Locals"); }
	UProperty*& MostRecentPropertyField() { return *GetNativePointerField<UProperty**>(this, "FFrame.MostRecentProperty"); }
	unsigned __int8*& MostRecentPropertyAddressField() { return *GetNativePointerField<unsigned __int8**>(this, "FFrame.MostRecentPropertyAddress"); }
	FFrame*& PreviousFrameField() { return *GetNativePointerField<FFrame**>(this, "FFrame.PreviousFrame"); }
	FOutParmRec*& OutParmsField() { return *GetNativePointerField<FOutParmRec**>(this, "FFrame.OutParms"); }
	UField*& PropertyChainForCompiledInField() { return *GetNativePointerField<UField**>(this, "FFrame.PropertyChainForCompiledIn"); }
	UFunction*& CurrentNativeFunctionField() { return *GetNativePointerField<UFunction**>(this, "FFrame.CurrentNativeFunction"); }
};

namespace
{

// The Genesis 2 space biomes, in the order NetEndWarp indexes them. Defaults
// only: config.json replaces the whole list if it provides one. Content, not
// code - last checked against the game on 2023-09-09.
const char* kDefaultBiomes[] = {
	"Ambergris, mutagel, metal and white supply crates.",
	"Crystal, metal and green supply crates.",
	"Metal, obsidian, sulfur and cyan supply crates.",
	"Element shards, metal and purple supply crates.",
	"Metal, gems, obsidian and yellow supply crates.",
	"Metal, oil and red supply crates.",
	"Element dust, metal and cyan supply crates.",
	"Black pearls, mutagel, obsidian, metal and orange supply crates.",
};

std::vector<std::string> g_biomes;
std::string g_sender = "INFO";
std::string g_tribe = "Asteroid Zone";
std::string g_prefix = "A new asteroid biome has been reached. Asteroid Biome contains: ";

// The InternalIndex of DayCycleManager_Gen2.NetEndWarp, or -1 until the game
// mode has come up and told us. Atomic because the hot hook reads it on the
// game thread while BeginPlay writes it.
std::atomic<int> g_net_end_warp{ -1 };

// Tracked per hook, not as one flag: if only one of the two attached, only that
// one may be detached, and passing an unattached detour to DisableHook is not
// something to find out about during shutdown.
std::atomic<bool> g_hooks_installed{ false };
std::atomic<bool> g_begin_play_hooked{ false };
std::atomic<bool> g_process_internal_hooked{ false };

// The retry tick is not a hook, but it comes off the same way and for the same
// reason: a callback left on ArkApi's list after this module is freed is a call
// into freed code, exactly like a live detour.
std::atomic<bool> g_tick_installed{ false };
const char* const kTickId = "AsteroidBiome.Tick";
std::string g_module_dir;

// ArkCrossChat's exported announcer. Resolved at EVERY call and never cached.
//
// This is not defensiveness for its own sake: ArkApi hot-reloads plugin DLLs,
// so a pointer taken once is a pointer into code that may since have been
// freed, and calling it hangs the map. The same rule already governs how
// ArkShop and our shop UI talk to each other.
typedef bool (*CrossChatPostFn)(const char*, const char*, const char*, const char*);

bool Announce(const std::string& contents)
{
	const HMODULE cc = GetModuleHandleA("ArkCrossChat.dll");
	if (cc == nullptr)
	{
		Log::GetLog()->warn("[asteroid] ArkCrossChat is not loaded - notification not sent");
		return false;
	}

	const auto post = reinterpret_cast<CrossChatPostFn>(GetProcAddress(cc, "CrossChat_Post"));
	if (post == nullptr)
	{
		Log::GetLog()->warn("[asteroid] ArkCrossChat is loaded but has no CrossChat_Post export - "
		                    "it is older than this plugin expects; notification not sent");
		return false;
	}

	// The map is left empty on purpose. ArkCrossChat fills in the server it is
	// running on, which is the map this event happened on, and that saves
	// hooking InitGame here just to learn our own name.
	if (!post(g_sender.c_str(), g_tribe.c_str(), contents.c_str(), ""))
	{
		Log::GetLog()->warn("[asteroid] ArkCrossChat refused the notification - "
		                    "its relay is probably not started yet");
		return false;
	}
	return true;
}

void AnnounceZone(const int zone)
{
	// `zone` arrives straight from the game's NetEndWarp "index" parameter and
	// indexes the biome list. Anything outside it would be an out-of-bounds
	// read, so refuse rather than take the chance.
	if (zone < 0 || zone >= static_cast<int>(g_biomes.size()))
	{
		Log::GetLog()->error("[asteroid] zone index out of range: " + std::to_string(zone) +
		                     " (know " + std::to_string(g_biomes.size()) + " biomes)");
		return;
	}

	if (Announce(g_prefix + g_biomes[static_cast<size_t>(zone)]))
	{
		Log::GetLog()->info("[asteroid] zone " + std::to_string(zone) + " announced");
	}
}

// config.json is entirely optional. Every key has a working default, so a
// missing or partial file is normal rather than an error - there are no
// credentials in it any more, and nothing to get wrong by guessing.
void LoadConfig()
{
	g_biomes.assign(std::begin(kDefaultBiomes), std::end(kDefaultBiomes));

	const std::string path = g_module_dir + "config.json";
	std::ifstream file(path);
	if (!file.is_open())
	{
		Log::GetLog()->info("[asteroid] no config.json at " + path + " - using built-in defaults");
		return;
	}

	try
	{
		nlohmann::json cfg;
		file >> cfg;

		if (cfg.contains("sender_name") && cfg["sender_name"].is_string()) g_sender = cfg["sender_name"].get<std::string>();
		if (cfg.contains("tribe_name") && cfg["tribe_name"].is_string()) g_tribe = cfg["tribe_name"].get<std::string>();
		if (cfg.contains("message_prefix") && cfg["message_prefix"].is_string()) g_prefix = cfg["message_prefix"].get<std::string>();

		// Replaced wholesale, not merged: a partial list would silently shift
		// every index after the one that changed and announce the wrong biome.
		//
		// And for the same reason, ONE bad entry rejects the WHOLE list. An
		// earlier version of this function skipped non-string entries and kept
		// going, which quietly did the exact thing the paragraph above says must
		// not happen: a list of eight with a stray number at position two became
		// a list of seven, and every biome after it announced the wrong one.
		// There is no safe way to take part of this list.
		if (cfg.contains("biomes") && cfg["biomes"].is_array())
		{
			std::vector<std::string> from_config;
			bool all_strings = true;
			for (const auto& b : cfg["biomes"])
			{
				if (!b.is_string()) { all_strings = false; break; }
				from_config.push_back(b.get<std::string>());
			}

			if (!all_strings)
			{
				Log::GetLog()->error("[asteroid] config.json \"biomes\" contains a non-string entry - "
				                     "ignoring the whole list and using built-in defaults, because "
				                     "dropping one entry would shift every index after it");
			}
			else if (from_config.empty())
			{
				Log::GetLog()->warn("[asteroid] config.json \"biomes\" is empty - using built-in defaults");
			}
			else
			{
				g_biomes = from_config;
				Log::GetLog()->info("[asteroid] biome list from config.json: " +
				                    std::to_string(g_biomes.size()) + " entries");
			}
		}
	}
	catch (const std::exception& ex)
	{
		// Keep the defaults rather than starting with nothing to say.
		Log::GetLog()->error(std::string("[asteroid] config.json could not be read: ") + ex.what() +
		                     " - using built-in defaults");
		g_biomes.assign(std::begin(kDefaultBiomes), std::end(kDefaultBiomes));
	}
}

// Find the blueprint function whose call we want to notice. Only Genesis 2 has
// it; anywhere else the lookup fails and the plugin stays inert, which is
// intended and not an error.
//
// NEVER CALL THIS FROM Plugin_Init. That is not a style preference - it took
// Gen2 down for 44 minutes on 2026-09-22.
//
// Plugin_Init runs at UEngine::Init, long before the level is loaded. Calling
// BPLoadClass there does not fail harmlessly as one might hope: it succeeded,
// took about two minutes, logged the index - and the server then stopped dead
// partway through its own startup, sitting at zero CPU with a 131 MB working
// set where a loaded Gen2 holds about 10 GB. It never finished loading and
// never answered RCON. Reverting the plugin fixed it; the same binary without
// this call is what now runs.
//
// So it is reached from two places that both run after the world exists: the
// BeginPlay hook, and a tick that covers the hot-reload case, where BeginPlay
// has already been and gone - the same shape ArkCrossChat uses to start its
// relay, which has been running on thirteen maps without trouble.
//
// Returns true once the index has been found. `quiet` suppresses the two
// "nothing to watch here" lines, so the tick can retry without writing the same
// line to the log on every attempt.
bool FindWatchedFunction(bool quiet)
{
	if (g_net_end_warp.load() >= 0) return true;

	FString path("Blueprint'/Game/Genesis2/CoreBlueprints/Environment/DayCycleManager_Gen2.DayCycleManager_Gen2'");
	UClass* day_manager = UVictoryCore::BPLoadClass(&path);
	if (day_manager == nullptr)
	{
		if (!quiet)
			Log::GetLog()->info("[asteroid] no Genesis 2 day cycle manager on this map - staying idle");
		return false;
	}

	UFunction* fn = day_manager->FindFunctionByName(FName("NetEndWarp", EFindName::FNAME_Find),
	                                                EIncludeSuperFlag::ExcludeSuper);
	if (fn == nullptr)
	{
		if (!quiet)
			Log::GetLog()->warn("[asteroid] DayCycleManager_Gen2 has no NetEndWarp - "
			                    "the game may have renamed it; staying idle");
		return false;
	}

	g_net_end_warp.store(fn->InternalIndexField());
	Log::GetLog()->info("[asteroid] watching NetEndWarp (index " +
	                    std::to_string(g_net_end_warp.load()) + ")");
	return true;
}

// How long the retry runs before giving up, and how often it tries inside that
// window. Thirty seconds is generous for a world that is already up - this only
// does any work on a hot reload - and the interval keeps a failing lookup off
// the game thread on all but one frame in thirty.
constexpr float kLookupWindowSeconds = 30.0f;
constexpr float kLookupIntervalSeconds = 1.0f;

void StopTick()
{
	if (g_tick_installed.exchange(false))
		ArkApi::GetCommands().RemoveOnTickCallback(kTickId);
}

// Covers the hot-reload case: if this plugin is swapped in while the map is
// already running, BeginPlay has been and gone. UWorld::Tick is the earliest
// callback guaranteed to run with the world up, which is the entire point - see
// the banner above.
//
// IT MUST STOP ITSELF. On the twelve maps that are not Genesis 2 the lookup
// never succeeds, so a tick that merely retried would call BPLoadClass and write
// a log line on every frame for the life of the process. It gets a budget
// instead: one quiet attempt a second for kLookupWindowSeconds, then it takes
// itself off the list whatever the answer, logging once on the way out. On a
// cold start BeginPlay settles the question and removes it before it ever runs.
//
// Removing a callback from inside the tick is safe: CheckOnTickCallbacks
// iterates a copy of the vector, so the erase cannot invalidate it.
void OnTick(float delta_seconds)
{
	// UWorld::Tick is the only caller, so these need no synchronisation.
	static float elapsed = 0.0f;
	static float since_attempt = 0.0f;

	if (g_net_end_warp.load(std::memory_order_relaxed) >= 0)
	{
		StopTick();
		return;
	}

	elapsed += delta_seconds;
	since_attempt += delta_seconds;

	const bool last_attempt = elapsed >= kLookupWindowSeconds;
	if (since_attempt < kLookupIntervalSeconds && !last_attempt) return;
	since_attempt = 0.0f;

	bool found = false;
	try
	{
		found = FindWatchedFunction(!last_attempt);
	}
	catch (...)
	{
		// A throw out of a tick callback would take the map with it.
	}

	if (found || last_attempt) StopTick();
}

DECLARE_HOOK(AShooterGameMode_BeginPlay, void, AShooterGameMode*);
void Hook_AShooterGameMode_BeginPlay(AShooterGameMode* _this)
{
	AShooterGameMode_BeginPlay_original(_this);
	try
	{
		FindWatchedFunction(false);

		// The world is up, so the answer is settled either way and the hot-reload
		// retry has nothing left to do. On a cold start this is what stops the
		// tick before its first frame.
		StopTick();
	}
	catch (const std::exception& ex)
	{
		Log::GetLog()->error(std::string("[asteroid] BeginPlay: ") + ex.what());
	}
	catch (...)
	{
		Log::GetLog()->error("[asteroid] BeginPlay: unknown error");
	}
}

DECLARE_HOOK(UObject_ProcessInternal, void, UObject*, FFrame*, void* const);
void Hook_UObject_ProcessInternal(UObject* _this, FFrame* Stack, void* const Result)
{
	// THE HOTTEST PATH IN THE PROCESS. Every blueprint function call in the
	// game arrives here, so everything before the passthrough is paid for by
	// the whole server, continuously.
	//
	// NodeField() IS NOT A MEMBER READ, which is the whole reason this is
	// written the way it is. It expands to GetNativePointerField -> GetAddress,
	// a dllimport call into ArkApi that constructs a std::string from a literal
	// and hashes it in an unordered_map. So it is a function call and a hash
	// lookup, per invocation, and the compiler cannot fold two of them together
	// because GetAddress is opaque and each call materialises its own string.
	//
	// A previous version of this rewrite called it TWICE - once to null-check
	// and once to dereference - while a comment above claimed the arrangement
	// was "the cheapest possible rejection". It was the opposite: three lookups
	// per call where the code it replaced paid two, on the hottest path in the
	// process, on the only map this plugin is installed on. Resolve once.
	//
	// The atomic is read first so that a map without Genesis 2 pays nothing at
	// all: no call, no string, no hash.
	const int watched = g_net_end_warp.load(std::memory_order_relaxed);
	UFunction* const node = (watched >= 0 && Stack != nullptr) ? Stack->NodeField() : nullptr;
	if (node == nullptr || node->InternalIndexField() != watched)
	{
		UObject_ProcessInternal_original(_this, Stack, Result);
		return;
	}

	int zone = -1;
	bool found = false;
	try
	{
		// `node`, not Stack->NodeField() again - see the note above about what
		// that accessor actually costs.
		for (UProperty* prop = node->PropertyLinkField(); prop != nullptr;
		     prop = prop->PropertyLinkNextField())
		{
			// .Equals rather than ==: this SDK's FString has no operator== for a
			// narrow string literal, only for TCHAR.
			if (prop->NameField().ToString().Equals("index"))
			{
				zone = *reinterpret_cast<int*>(Stack->LocalsField() + prop->Offset_InternalField());
				found = true;
				break;
			}
		}
	}
	catch (...)
	{
		found = false;
	}

	// The original runs FIRST. Announcing is our business and must not delay,
	// or if it ever threw prevent, the game's own handling of the warp.
	UObject_ProcessInternal_original(_this, Stack, Result);

	if (!found)
	{
		Log::GetLog()->error("[asteroid] NetEndWarp fired but has no \"index\" parameter");
		return;
	}

	try
	{
		AnnounceZone(zone);
	}
	catch (const std::exception& ex)
	{
		Log::GetLog()->error(std::string("[asteroid] announce: ") + ex.what());
	}
	catch (...)
	{
		Log::GetLog()->error("[asteroid] announce: unknown error");
	}
}

}  // namespace

// ArkApi calls this after LoadLibrary returns - outside the loader lock, which
// is where a detour may safely be installed and where another module's logging
// containers may safely be touched.
extern "C" __declspec(dllexport) void Plugin_Init()
{
	try
	{
		Log::Get().Init("AsteroidBiome");
		LoadConfig();

		// SetHook returns bool, and both results matter. Discarding them and
		// reporting "ready" regardless means a failed detour looks exactly like
		// a working one, and the first sign of trouble is an announcement that
		// never arrives weeks later.
		//
		// g_hooks_installed is set if EITHER attached, because Plugin_Unload
		// must then still take that one out - a single live detour into freed
		// code hangs the map just as thoroughly as three.
		const bool begin_play_ok = ArkApi::GetHooks().SetHook("AShooterGameMode.BeginPlay",
		                                                      &Hook_AShooterGameMode_BeginPlay,
		                                                      &AShooterGameMode_BeginPlay_original);
		const bool process_internal_ok = ArkApi::GetHooks().SetHook("UObject.ProcessInternal",
		                                                            &Hook_UObject_ProcessInternal,
		                                                            &UObject_ProcessInternal_original);
		g_begin_play_hooked = begin_play_ok;
		g_process_internal_hooked = process_internal_ok;
		g_hooks_installed = begin_play_ok || process_internal_ok;

		if (!begin_play_ok)
			Log::GetLog()->warn("[asteroid] could not hook AShooterGameMode.BeginPlay - "
			                    "the retry tick will have to find the watched function "
			                    "instead, a moment later than it otherwise would");
		if (!process_internal_ok)
			Log::GetLog()->error("[asteroid] could not hook UObject.ProcessInternal - "
			                     "NOTHING WILL EVER BE ANNOUNCED on this server");

		// DELIBERATELY NOT LOOKED UP HERE. Plugin_Init runs inside UEngine::Init,
		// before the world exists, and forcing that blueprint to load at this point
		// is what hung Gen2 - see the banner on FindWatchedFunction. The hot-reload
		// case is covered by the tick, which runs from UWorld::Tick and so cannot
		// fire until the world is up.
		ArkApi::GetCommands().AddOnTickCallback(kTickId, &OnTick);
		g_tick_installed = true;

		if (process_internal_ok)
		{
			Log::GetLog()->info("[asteroid] ready; announcements go through ArkCrossChat, "
			                    "this plugin holds no database connection");
		}
	}
	catch (const std::exception& ex)
	{
		Log::GetLog()->error(std::string("[asteroid] Plugin_Init: ") + ex.what());
	}
	catch (...)
	{
		Log::GetLog()->error("[asteroid] Plugin_Init: unknown error");
	}
}

extern "C" __declspec(dllexport) void Plugin_Unload()
{
	try
	{
		// Order matters: the tick comes off first, so nothing of ours can still be
		// running while the detours are removed.
		if (g_tick_installed.exchange(false))
			ArkApi::GetCommands().RemoveOnTickCallback(kTickId);

		// The detours must not be live when this module is freed. The previous
		// version left all three installed, which is a crash waiting for the
		// next call into freed code rather than an error anybody would see.
		if (g_hooks_installed.exchange(false))
		{
			if (g_process_internal_hooked.exchange(false))
				ArkApi::GetHooks().DisableHook("UObject.ProcessInternal", &Hook_UObject_ProcessInternal);
			if (g_begin_play_hooked.exchange(false))
				ArkApi::GetHooks().DisableHook("AShooterGameMode.BeginPlay", &Hook_AShooterGameMode_BeginPlay);
		}
	}
	catch (...)
	{
		// Nothing useful can be done here, and throwing would be worse.
	}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/)
{
	// Nothing but recording where we live. This runs under the Windows loader
	// lock, where installing a detour, starting a thread, or reaching into
	// another module is a good way to deadlock a map.
	if (reason == DLL_PROCESS_ATTACH)
	{
		char path[MAX_PATH] = { 0 };
		if (GetModuleFileNameA(module, path, MAX_PATH) != 0)
		{
			std::string full(path);
			const size_t slash = full.find_last_of("\\/");
			if (slash != std::string::npos) g_module_dir = full.substr(0, slash + 1);
		}
	}
	return TRUE;
}
