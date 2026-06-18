#include "cutiewaydroid.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>

// =============================================================================
// CutieWaydroidAppModel
// =============================================================================

CutieWaydroidAppModel::CutieWaydroidAppModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CutieWaydroidAppModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant CutieWaydroidAppModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return QVariant();

    const AppEntry &entry = m_entries.at(index.row());
    switch (role) {
    case PackageNameRole:
        return entry.packageName;
    case NameRole:
        return entry.name;
    case IconRole:
        return entry.icon;
    case HiddenRole:
        return entry.hidden;
    case DesktopFilePathRole:
        return entry.desktopFilePath;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> CutieWaydroidAppModel::roleNames() const
{
    return {
        { PackageNameRole, "packageName" },
        { NameRole, "name" },
        { IconRole, "icon" },
        { HiddenRole, "hidden" },
        { DesktopFilePathRole, "desktopFilePath" }
    };
}

void CutieWaydroidAppModel::refresh()
{
    beginResetModel();
    m_entries.clear();

    // Waydroid writes one .desktop file per installed app (plus a separate
    // "Waydroid.desktop" for the full-UI launcher itself) into the user's
    // writable applications directory - normally ~/.local/share/applications.
    const QString appsDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    QDir dir(appsDir);

    static const QString launchPrefix = QStringLiteral("waydroid app launch ");

    if (dir.exists()) {
        const QFileInfoList files = dir.entryInfoList(QStringList{ QStringLiteral("*.desktop") }, QDir::Files);
        for (const QFileInfo &fileInfo : files) {
            QSettings desktopFile(fileInfo.absoluteFilePath(), QSettings::IniFormat);
            const QString exec = desktopFile.value(QStringLiteral("Desktop Entry/Exec")).toString();

            // Anything that isn't "waydroid app launch <package>" is not a
            // per-app entry - this is what excludes Waydroid's own
            // "show-full-ui" launcher from the list.
            if (!exec.startsWith(launchPrefix))
                continue;

            AppEntry entry;
            entry.packageName = exec.mid(launchPrefix.length()).trimmed();
            if (entry.packageName.isEmpty())
                continue;

            entry.name = desktopFile.value(QStringLiteral("Desktop Entry/Name")).toString();
            entry.icon = desktopFile.value(QStringLiteral("Desktop Entry/Icon")).toString();
            const QString noDisplay = desktopFile.value(QStringLiteral("Desktop Entry/NoDisplay")).toString();
            entry.hidden = (noDisplay.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            entry.desktopFilePath = fileInfo.absoluteFilePath();

            m_entries.append(entry);
        }
    }

    endResetModel();
}

void CutieWaydroidAppModel::toggleHidden(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;

    AppEntry &entry = m_entries[row];
    const bool newHidden = !entry.hidden;

    // This is a deliberate, narrow text edit rather than a QSettings
    // read-modify-write. QSettings' INI writer doesn't preserve key order
    // or comments, and re-serializes the whole file from its own in-memory
    // map - which is risky here because these files commonly have a
    // second "[Desktop Action android_settings]" group (and possibly
    // more) that must survive untouched. Touching only the NoDisplay= line
    // guarantees nothing else in the file changes.
    QFile file(entry.desktopFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "CutieWaydroidAppModel: could not open" << entry.desktopFilePath << "for reading";
        return;
    }

    QStringList lines;
    bool inDesktopEntryGroup = false;
    bool wroteKey = false;
    const QString replacement = QStringLiteral("NoDisplay=%1\n").arg(newHidden ? QStringLiteral("true") : QStringLiteral("false"));

    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine());
        const QString trimmed = line.trimmed();

        if (trimmed.startsWith(QLatin1Char('['))) {
            inDesktopEntryGroup = (trimmed.compare(QStringLiteral("[Desktop Entry]"), Qt::CaseInsensitive) == 0);
            lines << line;
            continue;
        }

        if (inDesktopEntryGroup && trimmed.startsWith(QStringLiteral("NoDisplay="))) {
            line = replacement;
            wroteKey = true;
        }

        lines << line;
    }
    file.close();

    if (!wroteKey) {
        // No NoDisplay= key yet (apps default to visible, so it's often
        // simply absent) - insert one right after [Desktop Entry].
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).trimmed().compare(QStringLiteral("[Desktop Entry]"), Qt::CaseInsensitive) == 0) {
                lines.insert(i + 1, replacement);
                break;
            }
        }
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "CutieWaydroidAppModel: could not open" << entry.desktopFilePath << "for writing";
        return;
    }
    for (const QString &line : lines)
        file.write(line.toUtf8());
    file.close();

    entry.hidden = newHidden;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { HiddenRole });

    // Note: Waydroid's own background app sync can regenerate this file
    // (e.g. after an app update) and isn't aware of this toggle, so it may
    // reset NoDisplay back to its default on its own schedule. That's a
    // characteristic of how Waydroid manages these files, not a bug here.
}

int CutieWaydroidAppModel::indexOfPackage(const QString &packageName) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).packageName == packageName)
            return i;
    }
    return -1;
}

void CutieWaydroidAppModel::uninstallApp(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;

    const QString packageName = m_entries.at(row).packageName;
    if (packageName.isEmpty())
        return;

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, &QProcess::finished, this,
            [this, process, packageName](int exitCode, QProcess::ExitStatus status) {
                const bool ok = (status == QProcess::NormalExit && exitCode == 0);
                const QString output = QString::fromUtf8(process->readAllStandardOutput());
                process->deleteLater();

                if (ok) {
                    // Re-resolve by package name: other rows may have
                    // shifted (or this one may have been removed already)
                    // while this uninstall was in flight.
                    const int currentRow = indexOfPackage(packageName);
                    if (currentRow >= 0) {
                        beginRemoveRows(QModelIndex(), currentRow, currentRow);
                        m_entries.removeAt(currentRow);
                        endRemoveRows();
                    }
                }
                emit uninstallFinished(packageName, ok, ok ? QString() : output.trimmed());
            });

    connect(process, &QProcess::errorOccurred, this,
            [this, process, packageName](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    emit uninstallFinished(packageName, false, tr("The waydroid command was not found."));
                    process->deleteLater();
                }
            });

    process->start(QStringLiteral("waydroid"), { QStringLiteral("app"), QStringLiteral("remove"), packageName });
}

// =============================================================================
// CutieWaydroidManager
// =============================================================================

CutieWaydroidManager::CutieWaydroidManager(QObject *parent)
    : QObject(parent)
{
}

CutieWaydroidManager::~CutieWaydroidManager()
{
}

CutieWaydroidManager *CutieWaydroidManager::instance()
{
    static CutieWaydroidManager instance;
    return &instance;
}

QObject *CutieWaydroidManager::provider(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    return CutieWaydroidManager::instance();
}

void CutieWaydroidManager::setInitialized(bool value)
{
    if (m_initialized == value)
        return;
    m_initialized = value;
    emit initializedChanged();
}

void CutieWaydroidManager::setContainerRunning(bool value)
{
    if (m_containerRunning == value)
        return;
    const bool wasRunning = running();
    m_containerRunning = value;
    emit containerRunningChanged();
    if (running() != wasRunning)
        emit runningChanged();
}

void CutieWaydroidManager::setSessionRunning(bool value)
{
    if (m_sessionRunning == value)
        return;
    const bool wasRunning = running();
    m_sessionRunning = value;
    emit sessionRunningChanged();
    if (running() != wasRunning)
        emit runningChanged();
}

void CutieWaydroidManager::setBusy(bool value)
{
    if (m_busy == value)
        return;
    m_busy = value;
    emit busyChanged();
}

void CutieWaydroidManager::setInstallInProgress(bool value)
{
    if (m_installInProgress == value)
        return;
    m_installInProgress = value;
    emit installInProgressChanged();
}

void CutieWaydroidManager::setLastError(const QString &value)
{
    if (m_lastError == value)
        return;
    m_lastError = value;
    emit lastErrorChanged();
}

void CutieWaydroidManager::refreshInitializedFromDisk()
{
    // waydroid init writes this config once setup succeeds, and nothing
    // else plausibly creates it - a fast, no-process-spawn signal that's
    // good enough to gate the "Initialize Waydroid" button on.
    setInitialized(QFileInfo::exists(QStringLiteral("/var/lib/waydroid/waydroid.cfg")));
}

void CutieWaydroidManager::probeStatus(std::function<void()> onDone)
{
    if (m_statusCheckPending) {
        if (onDone)
            onDone();
        return;
    }
    m_statusCheckPending = true;

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, &QProcess::finished, this,
            [this, process, onDone](int, QProcess::ExitStatus) {
                const QString output = QString::fromUtf8(process->readAllStandardOutput());
                process->deleteLater();
                m_statusCheckPending = false;

                setContainerRunning(output.contains(QLatin1String("Container: RUNNING")));
                setSessionRunning(output.contains(QLatin1String("Session: RUNNING")));

                if (output.contains(QLatin1String("is not initialized"), Qt::CaseInsensitive))
                    setInitialized(false);

                if (onDone)
                    onDone();
            });

    connect(process, &QProcess::errorOccurred, this,
            [this, process, onDone](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    m_statusCheckPending = false;
                    setLastError(tr("The waydroid command was not found. Is Waydroid installed?"));
                    process->deleteLater();
                    if (onDone)
                        onDone();
                }
            });

    process->start(QStringLiteral("waydroid"), { QStringLiteral("status") });
}

void CutieWaydroidManager::refreshStatus()
{
    refreshInitializedFromDisk();
    probeStatus();
}

void CutieWaydroidManager::beginPolling(const QString &statusKey, bool waitForRunning,
                                         int maxAttempts, std::function<void(bool, QString)> onDone)
{
    m_pollKey = statusKey;
    m_pollWaitForRunning = waitForRunning;
    m_pollAttemptsRemaining = maxAttempts;
    m_pollCallback = std::move(onDone);
    pollOnce();
}

void CutieWaydroidManager::pollOnce()
{
    probeStatus([this]() {
        const bool currentValue = (m_pollKey == QLatin1String("Container")) ? m_containerRunning : m_sessionRunning;

        if (currentValue == m_pollWaitForRunning) {
            auto callback = m_pollCallback;
            m_pollCallback = nullptr;
            if (callback)
                callback(true, QString());
            return;
        }

        if (--m_pollAttemptsRemaining <= 0) {
            auto callback = m_pollCallback;
            m_pollCallback = nullptr;
            if (callback) {
                callback(false, tr("Timed out waiting for the %1 to reach the expected state.")
                                     .arg(m_pollKey == QLatin1String("Container") ? tr("container") : tr("session")));
            }
            return;
        }

        QTimer::singleShot(1000, this, &CutieWaydroidManager::pollOnce);
    });
}

void CutieWaydroidManager::initWaydroid()
{
    if (m_busy)
        return;
    setBusy(true);
    setLastError(QString());
    emit statusMessage(tr("Initializing Waydroid... this can take a while on the first run."));

    // "waydroid init" requires root. This deliberately does NOT shell out
    // through pkexec/sudo itself, because that mechanism is environment
    // specific (e.g. it needs a polkit authentication agent registered in
    // the compositor session, which a bespoke mobile shell may not have).
    // Run this module on a system where the default user already has
    // passwordless rights to run "waydroid init" (a polkit rule or
    // sudoers entry installed as part of the OS image, the same way other
    // root-only settings actions are already handled), or wrap just this
    // one QProcess::start() call with whatever elevation helper this
    // project already uses elsewhere.
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        const QStringList lines = QString::fromUtf8(process->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines)
            emit statusMessage(line.trimmed());
    });

    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
                const bool ok = (status == QProcess::NormalExit && exitCode == 0);
                process->deleteLater();
                refreshInitializedFromDisk();
                setBusy(false);
                if (!ok) {
                    setLastError(tr("Waydroid initialization failed (exit code %1). "
                                     "Run \"waydroid init\" manually to see full output, or check "
                                     "whether this requires root on this device.").arg(exitCode));
                } else {
                    emit statusMessage(tr("Waydroid initialized."));
                }
            });

    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    setBusy(false);
                    setLastError(tr("Could not run \"waydroid init\" - command not found, "
                                     "or root privileges are required."));
                    process->deleteLater();
                }
            });

    process->start(QStringLiteral("waydroid"), { QStringLiteral("init") });
}

void CutieWaydroidManager::startWaydroid()
{
    if (m_busy)
        return;
    setBusy(true);
    setLastError(QString());
    runStartStage(StartStage::Container);
}

void CutieWaydroidManager::runStartStage(StartStage stage)
{
    switch (stage) {
    case StartStage::Container: {
        if (m_containerRunning) {
            runStartStage(StartStage::Session);
            return;
        }
        emit statusMessage(tr("Starting container..."));

        // Detached on purpose: whatever process ends up supervising the
        // container must keep running for as long as Waydroid is meant to
        // be usable, independent of this settings page (or app) being
        // closed. We confirm success purely via status polling below, not
        // via this process's own exit code.
        if (!QProcess::startDetached(QStringLiteral("waydroid"), { QStringLiteral("container"), QStringLiteral("start") })) {
            setBusy(false);
            setLastError(tr("Failed to run \"waydroid container start\"."));
            return;
        }

        beginPolling(QStringLiteral("Container"), true, 30, [this](bool ok, QString error) {
            if (!ok) {
                setBusy(false);
                setLastError(error);
                return;
            }
            runStartStage(StartStage::Session);
        });
        break;
    }
    case StartStage::Session: {
        if (m_sessionRunning) {
            runStartStage(StartStage::FullUi);
            return;
        }
        emit statusMessage(tr("Starting session..."));

        if (!QProcess::startDetached(QStringLiteral("waydroid"), { QStringLiteral("session"), QStringLiteral("start") })) {
            setBusy(false);
            setLastError(tr("Failed to run \"waydroid session start\"."));
            return;
        }

        beginPolling(QStringLiteral("Session"), true, 30, [this](bool ok, QString error) {
            if (!ok) {
                setBusy(false);
                setLastError(error);
                return;
            }
            runStartStage(StartStage::FullUi);
        });
        break;
    }
    case StartStage::FullUi: {
        emit statusMessage(tr("Launching full screen UI..."));
        if (!QProcess::startDetached(QStringLiteral("waydroid"), { QStringLiteral("show-full-ui") })) {
            setLastError(tr("Failed to launch the Waydroid UI window."));
        }
        setBusy(false);
        break;
    }
    }
}

void CutieWaydroidManager::stopWaydroid()
{
    if (m_busy)
        return;
    setBusy(true);
    setLastError(QString());
    runStopStage(StopStage::Session);
}

void CutieWaydroidManager::runStopStage(StopStage stage)
{
    switch (stage) {
    case StopStage::Session: {
        if (!m_sessionRunning) {
            runStopStage(StopStage::Container);
            return;
        }
        emit statusMessage(tr("Stopping session..."));

        // "waydroid session stop" only tears down the graphical
        // connection (and with it, the full-UI window) - the container
        // can outlive it, which is exactly why this is a separate stage
        // from the container stop below.
        if (!QProcess::startDetached(QStringLiteral("waydroid"), { QStringLiteral("session"), QStringLiteral("stop") })) {
            setBusy(false);
            setLastError(tr("Failed to run \"waydroid session stop\"."));
            return;
        }

        beginPolling(QStringLiteral("Session"), false, 20, [this](bool ok, QString error) {
            if (!ok) {
                setBusy(false);
                setLastError(error);
                return;
            }
            runStopStage(StopStage::Container);
        });
        break;
    }
    case StopStage::Container: {
        if (!m_containerRunning) {
            setBusy(false);
            emit statusMessage(tr("Waydroid stopped."));
            return;
        }
        emit statusMessage(tr("Stopping container..."));

        if (!QProcess::startDetached(QStringLiteral("waydroid"), { QStringLiteral("container"), QStringLiteral("stop") })) {
            setBusy(false);
            setLastError(tr("Failed to run \"waydroid container stop\"."));
            return;
        }

        beginPolling(QStringLiteral("Container"), false, 30, [this](bool ok, QString error) {
            setBusy(false);
            if (!ok) {
                setLastError(error);
                return;
            }
            emit statusMessage(tr("Waydroid stopped."));
        });
        break;
    }
    }
}

void CutieWaydroidManager::installApp(const QString &apkPath)
{
    if (m_installInProgress)
        return;

    if (!QFileInfo::exists(apkPath)) {
        setLastError(tr("File not found: %1").arg(apkPath));
        emit installFinished(false, lastError());
        return;
    }

    setInstallInProgress(true);
    setLastError(QString());
    emit statusMessage(tr("Installing %1...").arg(apkPath));

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, &QProcess::finished, this,
            [this, process, apkPath](int exitCode, QProcess::ExitStatus status) {
                const bool ok = (status == QProcess::NormalExit && exitCode == 0);
                const QString output = QString::fromUtf8(process->readAllStandardOutput());
                process->deleteLater();
                setInstallInProgress(false);
                if (!ok)
                    setLastError(tr("Failed to install %1.").arg(apkPath));
                emit installFinished(ok, ok ? tr("Installed successfully.") : output.trimmed());
            });

    connect(process, &QProcess::errorOccurred, this,
            [this, process, apkPath](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    setInstallInProgress(false);
                    setLastError(tr("The waydroid command was not found."));
                    emit installFinished(false, lastError());
                    process->deleteLater();
                }
            });

    process->start(QStringLiteral("waydroid"), { QStringLiteral("app"), QStringLiteral("install"), apkPath });
}

CutieWaydroidAppModel *CutieWaydroidManager::fetchInstalledApps()
{
    auto *model = new CutieWaydroidAppModel(this);
    model->refresh();
    return model;
}
