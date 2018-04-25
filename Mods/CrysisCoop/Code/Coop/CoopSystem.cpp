#include <StdAfx.h>
#include <IAISystem.h>
#include <IGame.h>
#include <IGameFramework.h>
#include <IActorSystem.h>
#include <IAgent.h>
#include <IItemSystem.h>
#include "CoopSystem.h"
#include "CoopCutsceneSystem.h"

#include "Coop/DialogSystem/DialogSystem.h"

#include <IVehicleSystem.h>
#include <IConsole.h>
#include <INetworkService.h>
#include <IEntityClass.h>

// Static CCoopSystem class instance forward declaration.
CCoopSystem CCoopSystem::s_instance = CCoopSystem();

CCoopSystem::CCoopSystem() :
	m_nInitialized(0),
	m_pReadability(NULL)
	, m_aiEntities()
{
}

CCoopSystem::~CCoopSystem()
{
}

// Summary:
//	Initializes the CCoopSystem instance.
bool CCoopSystem::Initialize()
{
	gEnv->pGame->GetIGameFramework()->GetILevelSystem()->AddListener(this);
	m_pReadability = new CCoopReadability();

	CCoopCutsceneSystem::GetInstance()->Register();

	IScriptSystem *pSS = gEnv->pScriptSystem;
	if (pSS->ExecuteFile("Scripts/Coop/AI.lua", true, true))
	{
		pSS->BeginCall("Init");
		pSS->EndCall();
	}

	gEnv->pSystem->GetLocalizationManager()->LoadExcelXmlSpreadsheet("Languages/game_text_messages.xml");

	InitCvars();

	m_pDialogSystem = new CDialogSystem();
	m_pDialogSystem->Init();

	return true;
}

void CCoopSystem::InitCvars()
{
	ICVar* pAIUpdateAlways = gEnv->pConsole->GetCVar("ai_UpdateAllAlways");
	ICVar* pCheatCvar = gEnv->pConsole->GetCVar("sv_cheatprotection");
	ICVar* pGameRules = gEnv->pConsole->GetCVar("sv_gamerules");

	if (pAIUpdateAlways)
		pAIUpdateAlways->ForceSet("1");

	if (pCheatCvar)
		pCheatCvar->ForceSet("0");

	if (pGameRules)
		pGameRules->ForceSet("coop");

	// TODO :: Hijack the map command to force DX10 and immersiveness to be enabled

}

void CCoopSystem::CompleteInit()
{
	gEnv->pSystem->SetIDialogSystem(m_pDialogSystem);
}

// Summary:
//	Shuts down the CCoopSystem instance.
void CCoopSystem::Shutdown()
{
	IScriptSystem *pSS = gEnv->pScriptSystem;
	if (pSS->ExecuteFile("Scripts/Coop/AI.lua", true, true))
	{
		pSS->BeginCall("Shutdown");
		pSS->EndCall();
	}

	CCoopCutsceneSystem::GetInstance()->Unregister();

	gEnv->pGame->GetIGameFramework()->GetILevelSystem()->RemoveListener(this);
	SAFE_DELETE(m_pReadability);

	if (m_pDialogSystem)
		m_pDialogSystem->Shutdown();

	SAFE_DELETE(m_pDialogSystem);
}

// Summary:
//	Updates the CCoopSystem instance.
void CCoopSystem::Update(float fFrameTime)
{
	gEnv->pAISystem->Enable(gEnv->bServer);
	if (m_pDialogSystem)
		m_pDialogSystem->Update(fFrameTime);

	// Registers vehicles into the AI system
	if (gEnv->bServer)
	{
		IVehicleIteratorPtr iter = gEnv->pGame->GetIGameFramework()->GetIVehicleSystem()->CreateVehicleIterator();
		while (IVehicle* pVehicle = iter->Next())
		{
			if (IEntity *pEntity = pVehicle->GetEntity())
			{
				if (!pEntity->GetAI())
				{
					gEnv->bMultiplayer = false;

					HSCRIPTFUNCTION scriptFunction(0);
					IScriptSystem*	pIScriptSystem = gEnv->pScriptSystem;
					if (IScriptTable* pScriptTable = pEntity->GetScriptTable())
					{
						if (pScriptTable->GetValue("ForceCoopAI", scriptFunction))
						{
							Script::Call(pIScriptSystem, scriptFunction, pScriptTable, false);
							pIScriptSystem->ReleaseFunc(scriptFunction);
						}
					}


					gEnv->bMultiplayer = true;
				}
			}
		}
		iter->Release();
	}
	CCoopCutsceneSystem::GetInstance()->Update(fFrameTime);

	// Disable server time elapsing on the client ( server synced only )
	if (!gEnv->bServer)
	{
		ITimeOfDay::SAdvancedInfo advancedinfo;
		advancedinfo.fAnimSpeed = 0.f;
		gEnv->p3DEngine->GetTimeOfDay()->SetAdvancedInfo(advancedinfo);
	}
}

void CCoopSystem::OnLoadingStart(ILevelInfo *pLevel)
{
	// Shared initialization.
	ICVar* pSystemUpdate = gEnv->pConsole->GetCVar("ai_systemupdate");
	pSystemUpdate->SetFlags(pSystemUpdate->GetFlags() | EVarFlags::VF_NOT_NET_SYNCED);
	pSystemUpdate->Set(gEnv->bServer ? 1 : 0);

	// Server-only initialization.
	if (gEnv->bEditor || !gEnv->bServer) 
		return;

	m_nInitialized = 0;
	CryLogAlways("[CCoopSystem] Initializing AI System...");


	gEnv->bMultiplayer = false;
	if (!gEnv->pAISystem->Init())
		CryLogAlways("[CCoopSystem] AI System Initialization Failed");

	gEnv->pAISystem->FlushSystem();
	gEnv->pAISystem->Enable();
	gEnv->pAISystem->LoadNavigationData(pLevel->GetPath(), "mission0");

	
}

void CCoopSystem::OnLoadingComplete(ILevel *pLevel)
{
	m_pDialogSystem->Reset();

	if (CDialogSystem::sAutoReloadScripts != 0)
		m_pDialogSystem->ReloadScripts();

	if (gEnv->bEditor) return;

	/*CryLogAlways("Making smart objects non-removable.");
	IGameFramework* pGameFramework = gEnv->pGame->GetIGameFramework();
	IEntityIt* pIterator = gEnv->pEntitySystem->GetEntityIterator();
	pIterator->Release();
	int nInitialized = 0;
	gEnv->bMultiplayer = false;
	m_nInitialized = 1;

	// Why?
	// gEnv->pAISystem->Reset(IAISystem::RESET_ENTER_GAME);
	gEnv->bMultiplayer = true;*/
}

void LogEntities()
{
	IEntityIt* pIterator = gEnv->pEntitySystem->GetEntityIterator();
	while (!pIterator->IsEnd())
	{
		IEntity* pEntity = nullptr;
		if ((pEntity = pIterator->This()))
		{
			CryLogAlways("[%d] %s", pEntity->GetId(), pEntity->GetName());
		}
		pIterator->Next();
	} pIterator->Release();
}

// Summary:
//	Called before the game rules have reseted entities.
void CCoopSystem::OnPreResetEntities()
{
	if (!gEnv->bServer)
		return;

	gEnv->bMultiplayer = true;

	/*const char* sRecreateEntityClasses[] = { "SmartObject", "AIAnchor" };
	const int nRecreateEntityClasses = sizeof(sRecreateEntityClasses) / sizeof(const char*);
	IEntityIt* pIterator = gEnv->pEntitySystem->GetEntityIterator();
	while (!pIterator->IsEnd())
	{
		IEntity* pEntity = nullptr;
		if ((pEntity = pIterator->This()) && pEntity->GetSmartObject())
		{
			const char* sClassName = pEntity->GetClass() ? pEntity->GetClass()->GetName() : "";
			for (int nIndex = 0; nIndex < nRecreateEntityClasses; ++nIndex)
			{
				if (strcmp(sRecreateEntityClasses[nIndex], sClassName) == 0)
				{
					CryLogAlways("[CCoopSystem] Queued entity %s of class %s for re-initialization...", pEntity->GetName(), pEntity->GetClass()->GetName());
					gEnv->pAISystem->RemoveSmartObject(pEntity);
					if (pEntity->GetAI())
						gEnv->pAISystem->RemoveObject(pEntity->GetAI());
					pEntity->SetSmartObject(nullptr);
				}
			}
		}
		pIterator->Next();
	} pIterator->Release();*/
	/*IGameFramework* pGameFramework = gEnv->pGame->GetIGameFramework();
	IEntityIt* pIterator = gEnv->pEntitySystem->GetEntityIterator();
	while (!pIterator->IsEnd())
	{
		IEntity* pEntity = nullptr;
		if ((pEntity = pIterator->This()) && (pEntity->GetSmartObject()))
		{
			pEntity->SetFlags(pEntity->GetFlags() | ENTITY_FLAG_UNREMOVABLE);
			
		}
		pIterator->Next();
	}
	*/
	//gEnv->pAISystem->Reset(IAISystem::RESET_EXIT_GAME);
}

IScriptTable* DeserializeEntityProperties(IScriptTable* pBase, XmlNodeRef pNode)
{
	if (!pNode)
		return nullptr;
	if(!pBase)
		pBase = gEnv->pScriptSystem->CreateTable(true);
	pBase->AddRef();
	IScriptTable* pScriptTable = gEnv->pScriptSystem->CreateTable(true);
	pScriptTable->AddRef();
	for (int i = 0; i < pNode->getNumAttributes(); ++i)
	{
		const char* attributeName = 0;
		const char* attributeValue = 0;
		pNode->getAttributeByIndex(i, &attributeName, &attributeValue);
		pScriptTable->SetValue(attributeName, attributeValue);
	}

	for (int i = 0; i < pNode->getChildCount(); ++i)
	{
		DeserializeEntityProperties(pScriptTable, pNode->getChild(i));
	}
	

	pBase->SetValue(pNode->getTag(), pScriptTable);
	//pScriptTable->Release();

	return pBase;
	//pScriptTable->cxt

	//
	//pScriptTable.child
}

// Summary:
//	Called after the game rules have reseted entities.
void CCoopSystem::OnPostResetEntities()
{
	if (gEnv->bEditor)
		return;

	if (!gEnv->bServer)
	{
		gEnv->pAISystem->Enable(false);
		return;
	}

	CryLogAlways("Post-reseting entities.");

	gEnv->bMultiplayer = false;

	gEnv->pAISystem->Enable();
	
	CryLogAlways("[CCoopSystem] Gathering list of entities to re-initialize...");
	const char* sRecreateEntityClasses[] = { "SmartObject", "AIAnchor" };
	const int nRecreateEntityClasses = sizeof(sRecreateEntityClasses) / sizeof(const char*);
	std::map<EntityId, _smart_ptr<IScriptTable>> recreateObjects = std::map<EntityId, _smart_ptr<IScriptTable>>();
	std::list<EntityId> networkBoundObjects = std::list<EntityId>();

	// Iterate entities to be re-created...
	IEntityIt* pIterator = gEnv->pEntitySystem->GetEntityIterator();
	while (!pIterator->IsEnd())
	{
		IEntity* pEntity = nullptr;
		if ((pEntity = pIterator->This()))
		{
			if (pEntity->GetSmartObject())
			{
				const char* sClassName = pEntity->GetClass() ? pEntity->GetClass()->GetName() : "";
				for (int nIndex = 0; nIndex < nRecreateEntityClasses; ++nIndex)
				{
					if (strcmp(sRecreateEntityClasses[nIndex], sClassName) == 0)
					{
						CryLogAlways("[CCoopSystem] Queued entity %s of class %s for re-initialization...", pEntity->GetName(), pEntity->GetClass()->GetName());
						gEnv->pAISystem->RemoveSmartObject(pEntity);
						if (pEntity->GetAI())
							gEnv->pAISystem->RemoveObject(pEntity->GetAI());
						pEntity->SetSmartObject(nullptr);
						
						recreateObjects.emplace(std::make_pair(pEntity->GetId(), _smart_ptr<IScriptTable>(pEntity->GetScriptTable())));
					}
				}
			}
		}
		pIterator->Next();
	} pIterator->Release();

	CryLogAlways("[CCoopSystem] Gathering list of entities to re-initialize...");
	// Remove entities to be re-created...
	for (std::map<EntityId, _smart_ptr<IScriptTable>>::iterator it = recreateObjects.begin(); it != recreateObjects.end(); ++it)
	{
		gEnv->pEntitySystem->RemoveEntity(it->first, true);
	}


	CryLogAlways("[CCoopSystem] Re-initializing entities...");
	ILevel*	pLevel = gEnv->pGame->GetIGameFramework()->GetILevelSystem()->GetCurrentLevel();
	const char* sLevelName = gEnv->pGame->GetIGameFramework()->GetLevelName();
	ILevelInfo* pLevelInfo = gEnv->pGame->GetIGameFramework()->GetILevelSystem()->GetLevelInfo(sLevelName);
	gEnv->pEntitySystem->DeletePendingEntities();

	string sMissionXml = pLevelInfo->GetDefaultGameType()->xmlFile;
	string sXmlFile = string(pLevelInfo->GetPath()) + "/" + sMissionXml;

	gEnv->pAISystem->LoadNavigationData(pLevelInfo->GetPath(), "mission0");

	XmlNodeRef pMissionNode = GetISystem()->LoadXmlFile(sXmlFile.c_str());
	XmlNodeRef pRecreateObjectsNode = gEnv->pSystem->CreateXmlNode("Objects");

	if (pMissionNode)
	{
		XmlNodeRef pObjectsNode = pMissionNode->findChild("Objects");
		for (int nObject = 0; nObject < pObjectsNode->getChildCount(); ++nObject)
		{
			XmlNodeRef pObjectNode = pObjectsNode->getChild(nObject);
			if (pObjectNode->isTag("Entity"))
			{
				const char* sClassName = pObjectNode->getAttr("EntityClass");
				for (int nIndex = 0; nIndex < nRecreateEntityClasses; ++nIndex)
				{
					if (strcmp(sRecreateEntityClasses[nIndex], sClassName) == 0)
					{
						pRecreateObjectsNode->addChild(pObjectNode);
					}
				}
			}
		}
	}

	for (int nObject = 0; nObject < pRecreateObjectsNode->getChildCount(); ++nObject)
	{
		XmlNodeRef pObjectNode = pRecreateObjectsNode->getChild(nObject);
		SEntitySpawnParams sSpawnParams = SEntitySpawnParams();
		sSpawnParams.pClass = gEnv->pEntitySystem->GetClassRegistry()->FindClass(pObjectNode->getAttr("EntityClass"));
		sSpawnParams.sName = pObjectNode->getAttr("Name");
		sSpawnParams.sLayerName = pObjectNode->getAttr("Layer");
		pObjectNode->getAttr("Pos", sSpawnParams.vPosition);
		pObjectNode->getAttr("Rotate", sSpawnParams.qRotation);
		pObjectNode->getAttr("Scale", sSpawnParams.vScale);
		pObjectNode->getAttr("EntityId", sSpawnParams.id);
		pObjectNode->getAttr("EntityGuid", sSpawnParams.guid);

		bool bCastShadow = true;
		bool bGoodOccluder = false;
		bool bOutdoorOnly = false;

		pObjectNode->getAttr("CastShadow", bCastShadow);
		pObjectNode->getAttr("GoodOccluder", bGoodOccluder);
		pObjectNode->getAttr("OutdoorOnly", bOutdoorOnly);
		
		sSpawnParams.nFlags |= bCastShadow ? ENTITY_FLAG_CASTSHADOW : 0;
		sSpawnParams.nFlags |= bGoodOccluder ? ENTITY_FLAG_GOOD_OCCLUDER : 0;
		sSpawnParams.nFlags |= bOutdoorOnly ? ENTITY_FLAG_OUTDOORONLY : 0;

		const char* sArchetype = pObjectNode->getAttr("Archetype");
		if (sArchetype[0])
		{
			sSpawnParams.pArchetype = gEnv->pEntitySystem->LoadEntityArchetype(sArchetype);
		}

		IEntity* pEntity = gEnv->pEntitySystem->SpawnEntity(sSpawnParams, false);
		if (!pEntity)
		{
			CryLogAlways("Something went wrong.");
			continue;
		}

		auto instance = recreateObjects.find(sSpawnParams.id);
		if(instance != recreateObjects.end())
			sSpawnParams.pPropertiesTable = instance->second;
		gEnv->pEntitySystem->InitEntity(pEntity, sSpawnParams);
		
	}

	gEnv->bMultiplayer = true;

	m_aiEntities.clear();
}

// Summary:
//	Dumps debug information about entities to the console.
void CCoopSystem::DumpEntityDebugInformation()
{
	// List of used entity classes.
	std::list<string> stlUsedClasses = std::list<string>();

	#define AITypeCase(Name) case Name: sType = #Name; break;
	#define EntityFlagState(Name) if((nFlags & EEntityFlags::Name) != 0) sFlags += #Name " | ";
	IEntityIt* pIterator = gEnv->pEntitySystem->GetEntityIterator();
	while (!pIterator->IsEnd())
	{
		if (IEntity* pEntity = pIterator->This())
		{
			string sFlags = "";
			uint nFlags = pEntity->GetFlags();

			// Push entity class to the used entity classes list.
			stlUsedClasses.push_back(pEntity->GetClass()->GetName());

			EntityFlagState(ENTITY_FLAG_CASTSHADOW);
			EntityFlagState(ENTITY_FLAG_UNREMOVABLE);
			EntityFlagState(ENTITY_FLAG_GOOD_OCCLUDER);
			EntityFlagState(ENTITY_FLAG_WRITE_ONLY);
			EntityFlagState(ENTITY_FLAG_NOT_REGISTER_IN_SECTORS);
			EntityFlagState(ENTITY_FLAG_CALC_PHYSICS);
			EntityFlagState(ENTITY_FLAG_CLIENT_ONLY);
			EntityFlagState(ENTITY_FLAG_SERVER_ONLY);
			EntityFlagState(ENTITY_FLAG_CUSTOM_VIEWDIST_RATIO);
			EntityFlagState(ENTITY_FLAG_CALCBBOX_USEALL);
			EntityFlagState(ENTITY_FLAG_VOLUME_SOUND);
			EntityFlagState(ENTITY_FLAG_HAS_AI);
			EntityFlagState(ENTITY_FLAG_TRIGGER_AREAS);
			EntityFlagState(ENTITY_FLAG_NO_SAVE);
			EntityFlagState(ENTITY_FLAG_NET_PRESENT);
			EntityFlagState(ENTITY_FLAG_CLIENTSIDE_STATE);
			EntityFlagState(ENTITY_FLAG_SEND_RENDER_EVENT);
			EntityFlagState(ENTITY_FLAG_NO_PROXIMITY);
			EntityFlagState(ENTITY_FLAG_ON_RADAR);
			EntityFlagState(ENTITY_FLAG_UPDATE_HIDDEN);
			EntityFlagState(ENTITY_FLAG_NEVER_NETWORK_STATIC);
			EntityFlagState(ENTITY_FLAG_IGNORE_PHYSICS_UPDATE);
			EntityFlagState(ENTITY_FLAG_SPAWNED);
			EntityFlagState(ENTITY_FLAG_SLOTS_CHANGED);
			EntityFlagState(ENTITY_FLAG_MODIFIED_BY_PHYSICS);
			EntityFlagState(ENTITY_FLAG_OUTDOORONLY);
			EntityFlagState(ENTITY_FLAG_SEND_NOT_SEEN_TIMEOUT);
			EntityFlagState(ENTITY_FLAG_RECVWIND);
			EntityFlagState(ENTITY_FLAG_LOCAL_PLAYER);
			EntityFlagState(ENTITY_FLAG_AI_HIDEABLE);
			
			CryLogAlways("[Entity] Name: %s, Id: %d, Class: %s, Archetype: %s, Active: %s, Initialized: %s, Hidden: %s, Flags: %s", 
				pEntity->GetName(), 
				pEntity->GetId(), 
				pEntity->GetClass() ? pEntity->GetClass()->GetName() : "NULL", 
				pEntity->GetArchetype() ? pEntity->GetArchetype()->GetName() : "NULL",
				pEntity->IsActive() ? "Yes" : "No",
				pEntity->IsInitialized() ? "Yes" : "No",
				pEntity->IsHidden() ? "Yes" : "No",
				sFlags.c_str());

			
			if (IAIObject* pAI = pEntity->GetAI())
			{
				const char* sType = "Unknown";

				switch (pAI->GetAIType())
				{
					AITypeCase(AIOBJECT_NONE)
						AITypeCase(AIOBJECT_DUMMY)
						AITypeCase(AIOBJECT_AIACTOR)
						AITypeCase(AIOBJECT_CAIACTOR)
						AITypeCase(AIOBJECT_PIPEUSER)
						AITypeCase(AIOBJECT_CPIPEUSER)
						AITypeCase(AIOBJECT_PUPPET)
						AITypeCase(AIOBJECT_CPUPPET)
						AITypeCase(AIOBJECT_VEHICLE)
						AITypeCase(AIOBJECT_CVEHICLE)
						AITypeCase(AIOBJECT_AWARE)
						AITypeCase(AIOBJECT_ATTRIBUTE)
						AITypeCase(AIOBJECT_WAYPOINT)
						AITypeCase(AIOBJECT_HIDEPOINT)
						AITypeCase(AIOBJECT_SNDSUPRESSOR)
						AITypeCase(AIOBJECT_HELICOPTER)
						AITypeCase(AIOBJECT_CAR)
						AITypeCase(AIOBJECT_BOAT)
						AITypeCase(AIOBJECT_AIRPLANE)
						AITypeCase(AIOBJECT_2D_FLY)
						AITypeCase(AIOBJECT_MOUNTEDWEAPON)
						AITypeCase(AIOBJECT_GLOBALALERTNESS)
						AITypeCase(AIOBJECT_LEADER)
						AITypeCase(AIOBJECT_ORDER)
						AITypeCase(AIOBJECT_PLAYER)
						AITypeCase(AIOBJECT_GRENADE)
						AITypeCase(AIOBJECT_RPG)
				}
				
				CryLogAlways("[AI] Name: %s, Type: %s", pAI->GetName(), sType);
			}

			CryLogAlways("[SmartObject] %s", pEntity->GetSmartObject() ? "Yes" : "No");
			CryLogAlways("[Network] Bound: %s", gEnv->pGame->GetIGameFramework()->GetNetContext()->IsBound(pEntity->GetId()) ? "Yes" : "No");
		}
		pIterator->Next();
	}

	stlUsedClasses.unique();
	stlUsedClasses.sort();

	CryLogAlways("[Classes]");
	for (std::list<string>::iterator it = stlUsedClasses.begin(); it != stlUsedClasses.end(); ++it)
	{
		CryLogAlways(" - %s", it->c_str());
	}

}