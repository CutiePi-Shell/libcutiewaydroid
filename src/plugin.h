#pragma once
#include <QDebug>
#include <QtQuick>
#include <QtQml/qqml.h>
#include <QtQml/QQmlExtensionPlugin>

#include "cutiewaydroid.h"

class CutieWaydroidPlugin : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlEngineExtensionInterface_iid FILE
                      "cutiewaydroid.json")

public:
    explicit CutieWaydroidPlugin() {}

    void registerTypes(const char *uri) override;
};
