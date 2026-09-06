#include <API/ARK/Ark.h>
#include <mysql+++.h>
#include <json.hpp>
#include <fstream>
// #pragma comment(lib, "ArkApi.lib")
// #pragma comment(lib, "mysqlclient.lib")

daotk::mysql::connection my;
FString mapname("");

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

void ConnectDatabase()
{
	try
	{
		const std::string path = ArkApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/AsteroidBiomeChatNotifications/config.json";
		std::ifstream file(path);
		if (!file.is_open())
		{
			Log::GetLog()->error("Could not open config.json at {}", path);
			return;
		}

		nlohmann::json config;
		file >> config;

		const std::string host = config["Mysql"]["Host"];
		const std::string user = config["Mysql"]["User"];
		const std::string pass = config["Mysql"]["Pass"];
		const std::string db = config["Mysql"]["Db"];
		const int port = config["Mysql"]["Port"];

		daotk::mysql::connect_options options;
		options.server = host;
		options.username = user;
		options.password = pass;
		options.dbname = db;
		options.port = port;
		options.ssl_mode = 1; // 1 = SSL_MODE_DISABLED

		Log::GetLog()->info("Connecting to DB at {}:{} with user '{}'...", host, port, user);

		my.open(options);

		if (!my)
		{
			Log::GetLog()->info("Raw check: host='{}', user='{}', db='{}', port={}", host, user, db, port);
			// Try a raw connection just to capture the error message for the user if the wrapper fails
			MYSQL* temp = mysql_init(nullptr);
			if (temp) {
				my_bool b = 0; 
				mysql_options(temp, MYSQL_OPT_SSL_ENFORCE, &b);
				mysql_options(temp, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &b);
				mysql_ssl_set(temp, nullptr, nullptr, nullptr, nullptr, nullptr);

				if (mysql_real_connect(temp, host.c_str(), user.c_str(), pass.c_str(), db.c_str(), (unsigned int)port, nullptr, 0) == nullptr)
				{
					Log::GetLog()->error("Raw Check Failed! Error: {} (Code: {})", mysql_error(temp), mysql_errno(temp));
				}
				else {
					Log::GetLog()->info("Raw Check Succeeded! (SSL was disabled manually)");
				}
				mysql_close(temp);
			}
		}
		else
		{
			Log::GetLog()->info("MYSQL connection was established sucsessfully! :)");
		}
	}
	catch (const std::exception& ex)
	{
		Log::GetLog()->warn("problem in ConnectDatabase:");
		Log::GetLog()->warn(ex.what());
	}
}

// list of all possible biomes as of 09/09/2023
std::string biomes[] = {
	"Ambergris, mutagel, Metal and white supply crates.",
	"Crystal, metal and green supply crates.",
	"Metal, obsidian, sulfer and cyan supply crates.",
	"Element shards, metal and purple supply crates.",
	"Metal, gems, obsidian and yellow supply crates.",
	"Metal, oil and red supply crates.",
	"Element dust, metal and cyan supply crates.",
	"Black pearls, mutagel, obsidian, metal and orange supply crates."
};

// send the new asteroid biome notification to the databse.
void SendAsteroidNotification(int zone)
{
	// `zone` arrives straight from the game's NetEndWarp "index" parameter and is
	// used to index a fixed-size array. Anything outside the known biomes would be
	// an out-of-bounds read, so refuse it rather than take the chance.
	if (zone < 0 || zone >= static_cast<int>(sizeof(biomes) / sizeof(biomes[0])))
	{
		Log::GetLog()->error("Asteroid zone index out of range: " + std::to_string(zone));
		return;
	}

	std::string contents = "A new asteroid biome has been reached. Asteroid Biome contains: " + biomes[zone];

	try
	{
		std::string player_name = "INFO";
		std::string tribename = "Asteroid Zone";
		std::string smapname = mapname.ToString();

		// A connection that was established and has since been dropped - the
		// database restarting, a timeout, a network blip - still tests truthy,
		// so `!my` on its own never triggers a reconnect. is_open() is what
		// actually reflects the state of the socket.
		if (!my || !my.is_open())
		{
			Log::GetLog()->warn("Database connection is not open, reconnecting...");
			ConnectDatabase();
		}

		// If it is still down, give up quietly rather than carrying on.
		// Constructing a prepared_stmt against a dead connection makes
		// mysql_stmt_init() return NULL, which the wrapper then dereferences.
		// That is an access violation rather than a C++ exception, so the catch
		// below cannot intercept it and the entire server process dies.
		if (!my || !my.is_open())
		{
			Log::GetLog()->error("Database unavailable - skipping asteroid notification for zone " + std::to_string(zone));
			return;
		}

		Log::GetLog()->info("Sending Asteroid Notification: " + std::to_string(zone));

		daotk::mysql::prepared_stmt stmt(my, "INSERT into messages (`name`,`tribe`,`contents`,`originating_map`) VALUES (?,?,?,?);");
		stmt.bind_param(player_name, tribename, contents, smapname);
		stmt.execute();		
	}
	catch (const std::exception& ex)
	{
		Log::GetLog()->error(ex.what());
	}
}

// Find the NetEndWarp UFunction during your plugin's init and save its InternalIndex
int NetEndWarp = -1;
void ServerReadyInit()
{
	
	FString path = "Blueprint'/Game/Genesis2/CoreBlueprints/Environment/DayCycleManager_Gen2.DayCycleManager_Gen2'";
	UClass* DayManagerClass = UVictoryCore::BPLoadClass(&path);
	if (!DayManagerClass)
		return;

	UFunction* function = DayManagerClass->FindFunctionByName(FName("NetEndWarp", EFindName::FNAME_Find), EIncludeSuperFlag::ExcludeSuper);
	if (!function)
		return;
	Log::GetLog()->warn("2 found triggerWarp index");
	NetEndWarp = function->InternalIndexField();
}

DECLARE_HOOK(AShooterGameMode_BeginPlay, void, AShooterGameMode*);
void Hook_AShooterGameMode_BeginPlay(AShooterGameMode* _this)
{
	AShooterGameMode_BeginPlay_original(_this);
	ServerReadyInit();
}

DECLARE_HOOK(UObject_ProcessInternal, void, UObject*, FFrame*, void* const);
void Hook_UObject_ProcessInternal(UObject* _this, FFrame* Stack, void* const Result)
{
	// The Stack's Node field is the UFunction that this FFrame was constructed for
	if (Stack->NodeField()->InternalIndexField() != NetEndWarp) {
		// Process the function normally and return if this is not the function we're looking for
		
		UObject_ProcessInternal_original(_this, Stack, Result);
		return;
	}

	int* parmIndex = nullptr;

	// Get the pointer to the "index" input parameter
	for (UProperty* prop = Stack->NodeField()->PropertyLinkField(); prop; prop = prop->PropertyLinkNextField())
	{
		if (prop->NameField().ToString().Equals("index"))
		{
			parmIndex = reinterpret_cast<int*>(Stack->LocalsField() + prop->Offset_InternalField());
			break;
		}
	}

	if (parmIndex)
	{
		Log::GetLog()->info("A warp happened with zone: "  + std::to_string(*parmIndex) );
		SendAsteroidNotification(*parmIndex);
	}
	else
	{
		Log::GetLog()->error("\"index\" parm not found");
	}

	UObject_ProcessInternal_original(_this, Stack, Result);
}

DECLARE_HOOK(AShooterGameMode_InitGame, void, AShooterGameMode*, FString*, FString*, FString*);
void Hook_AShooterGameMode_InitGame(AShooterGameMode* a_shooter_game_mode, FString* map_name, FString* options,FString* error_message)
{
	AShooterGameMode_InitGame_original(a_shooter_game_mode, map_name, options, error_message);

	Log::GetLog()->info("Server is ready. attempting to connect to DB...");

	ConnectDatabase();
	mapname = *map_name;
}

void Load()
{
	Log::Get().Init("Asteroid Biome Notifications V1 ");
	Log::GetLog()->info("Asteroid Biome Notifications V1");

	ArkApi::GetHooks().SetHook("AShooterGameMode.InitGame", &Hook_AShooterGameMode_InitGame,&AShooterGameMode_InitGame_original);
	ArkApi::GetHooks().SetHook("AShooterGameMode.BeginPlay", &Hook_AShooterGameMode_BeginPlay, &AShooterGameMode_BeginPlay_original);
	ArkApi::GetHooks().SetHook("UObject.ProcessInternal", &Hook_UObject_ProcessInternal, &UObject_ProcessInternal_original);
	
}
void Unload()
{

}

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		Load();
		break;
	case DLL_PROCESS_DETACH:
		Unload();
		break;
	}
	return TRUE;
}