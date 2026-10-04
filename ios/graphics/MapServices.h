#pragma once
#include "tier1/interface.h"
#include <stddef.h>
void *QueryMapService(const char *name);
bool InitializeMapServices(const char *modules, CreateInterfaceFn factory, char *detail, size_t capacity);
void ShutdownMapServices();
bool MountRequestedWorldData(char *detail, size_t capacity);
bool LoadRequestedWorldMap(char *detail, size_t capacity);
bool HasLoadedWorldMap();
class IMaterialSystem;

bool InitializeGameServices(const char *modules,CreateInterfaceFn factory,char *detail,size_t capacity);
bool InitializePortalServer(CreateInterfaceFn gameFactory,char *detail,size_t capacity);

bool InitializePortalClient(CSysModule *module,char *detail,size_t capacity);
bool ShutdownPortalServer();

void PollGameInput();


bool StartGameSession(char *detail,size_t capacity);

bool HasLivePortalGame();
bool IsGameLoading();
bool AdvancePortalGame(char *detail,size_t capacity);
bool DrawPortalGame(int width,int height,char *detail,size_t capacity);
