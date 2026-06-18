#include "plugin.h"

void CutieWaydroidPlugin::registerTypes(const char *uri)
{
    qmlRegisterSingletonType<CutieWaydroidManager>(
        uri, 1, 0, "CutieWaydroidManager",
        &CutieWaydroidManager::provider);
}
