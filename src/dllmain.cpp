#include <API/ARK/Ark.h>
#include <mysql+++.h>
#pragma comment(lib, "ArkApi.lib")
#pragma comment(lib, "mysqlclient.lib")

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
		my.open("server","username","password","dbname"); // change these for your setup.
		

		if (!my)
		{
			Log::GetLog()->error("MYSQL connection could not be established!?");
		}
		else
		{
			Log::GetLog()->info("MYSQL connection was established sucsessfully. :)");
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
	std::string contents = "A new asteroid biome has been reached. Asteroid Biome contains: " + biomes[zone];

	try
	{
		std::string player_name = "INFO";
		std::string tribename = "Asteroid Zone";
		std::string smapname = mapname.ToString();

		if (!my)
		{
			Log::GetLog()->warn("problem in PostLatestChat with the database");
			ConnectDatabase();
		}

		Log::GetLog()->info("is DB open " + std::to_string(my.is_open()));

		Log::GetLog()->warn("Sending Asteroid Notification: " + std::to_string(zone));

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