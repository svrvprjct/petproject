#include "Project.h"
#include "Engine/Simulation.h"
#include "Platform/Win32/WinEntry.h"

class Project : public Engine::Simulation
{
public:
	// Application Constructor
	Project();

	// Application Deconstructor
	~Project();

public:
	// Called to Setup our pergame settings
	VOID SetupPerGameSettings();

	// Called to Preinitialize the Application
	VOID PreInitialize();

	// Called to Initialize the Application
	VOID Initialize();

	// Game Loop - Called on a loop while the Application is running
	VOID Update();
};

ENTRYAPP(Project)

Project::Project()
{
}

Project::~Project()
{
}

VOID Project::SetupPerGameSettings()
{
	::Simulation::SetupPerGameSettings();
	PerGameSettings::SetGameName(IDS_PERGAMENAME);
	PerGameSettings::SetShortName(IDS_SHORTNAME);
	PerGameSettings::SetMainIcon(IDI_MAINICON);
	PerGameSettings::SetSplashURL(IDS_SPLASHURL);
}

VOID Project::PreInitialize()
{
	::Simulation::PreInitialize();
}

VOID Project::Initialize()
{
	::Simulation::Initialize();
}


VOID Project::Update()
{
	::Simulation::Update();
}
