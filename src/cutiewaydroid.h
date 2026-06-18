#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QAbstractListModel>
#include <QProcess>
#include <QQmlEngine>
#include <QJSEngine>
#include <functional>

// ---- 1. Per-app list model ----
//
// Lists installed Waydroid (Android) apps by reading the .desktop launcher
// files Waydroid itself writes to the user's applications directory,
// instead of shelling out to "waydroid app list". That command needs a
// live session and is known to hang/error ("Failed to get service
// waydroidplatform") while the platform service is still coming up, so
// reading the desktop files directly lets the list populate immediately,
// even before the session/container are started.
class CutieWaydroidAppModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PackageNameRole = Qt::UserRole + 1,
        NameRole,
        IconRole,
        HiddenRole,
        DesktopFilePathRole
    };

    explicit CutieWaydroidAppModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // (Re)scans the desktop file directory and resets the model.
    Q_INVOKABLE void refresh();

    // Flips NoDisplay for the row and rewrites just that one line of the
    // underlying .desktop file - see the .cpp for why this is done with a
    // targeted text edit instead of QSettings.
    Q_INVOKABLE void toggleHidden(int row);

    // Runs "waydroid app remove <packageName>" and removes the row from
    // the model once that finishes successfully.
    Q_INVOKABLE void uninstallApp(int row);

signals:
    // Fired when an uninstall kicked off by uninstallApp() completes,
    // success or not. Identified by package name (not row) since the row
    // index can no longer be trusted by the time an async call returns.
    void uninstallFinished(const QString &packageName, bool success, const QString &message);

private:
    struct AppEntry {
        QString packageName;
        QString name;
        QString icon;
        bool hidden = false;
        QString desktopFilePath;
    };

    int indexOfPackage(const QString &packageName) const;

    QList<AppEntry> m_entries;
};

// ---- 2. Lifecycle manager singleton ----
//
// Exposed to QML as the "CutieWaydroidManager" singleton (Cutie.Waydroid
// 1.0). Wraps the waydroid CLI via QProcess to report init/container/
// session status and to drive init/start/stop/install.
class CutieWaydroidManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool containerRunning READ containerRunning NOTIFY containerRunningChanged)
    Q_PROPERTY(bool sessionRunning READ sessionRunning NOTIFY sessionRunningChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool installInProgress READ installInProgress NOTIFY installInProgressChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit CutieWaydroidManager(QObject *parent = nullptr);
    ~CutieWaydroidManager();

    bool initialized() const { return m_initialized; }
    bool containerRunning() const { return m_containerRunning; }
    bool sessionRunning() const { return m_sessionRunning; }
    bool running() const { return m_containerRunning && m_sessionRunning; }
    bool busy() const { return m_busy; }
    bool installInProgress() const { return m_installInProgress; }
    QString lastError() const { return m_lastError; }

    // 1. Status check - call when the settings page is opened. Cheap: a
    // local file check plus one "waydroid status" call.
    Q_INVOKABLE void refreshStatus();

    // 1b. Bound to the "Initialize Waydroid" button, shown while !initialized.
    Q_INVOKABLE void initWaydroid();

    // 2. Toggle ON: checks container/session status, starts whichever of
    // them isn't already up, then launches the full-screen UI.
    Q_INVOKABLE void startWaydroid();

    // Toggle OFF: mirrors startWaydroid() in reverse (session, then container).
    Q_INVOKABLE void stopWaydroid();

    // 4. Sideload an APK by absolute path.
    Q_INVOKABLE void installApp(const QString &apkPath);

    // 3. Returns a freshly scanned app list model. Mirrors
    // CutieDesktopFileParser::fetchAllEntriesModel(): caller owns the
    // returned pointer through normal QML/JS GC rules.
    Q_INVOKABLE CutieWaydroidAppModel *fetchInstalledApps();

    static CutieWaydroidManager *instance();
    static QObject *provider(QQmlEngine *engine, QJSEngine *scriptEngine);

signals:
    void initializedChanged();
    void containerRunningChanged();
    void sessionRunningChanged();
    void runningChanged();
    void busyChanged();
    void installInProgressChanged();
    void lastErrorChanged();

    // Transient progress text ("Starting container...", a raw line from
    // "waydroid init", etc). Purely informational, safe to ignore in QML.
    void statusMessage(const QString &message);

    void installFinished(bool success, const QString &message);

private:
    enum class StartStage { Container, Session, FullUi };
    enum class StopStage { Session, Container };

    void setInitialized(bool value);
    void setContainerRunning(bool value);
    void setSessionRunning(bool value);
    void setBusy(bool value);
    void setInstallInProgress(bool value);
    void setLastError(const QString &value);

    void refreshInitializedFromDisk();

    // Runs "waydroid status" once and updates containerRunning/sessionRunning
    // (and, on the explicit "not initialized" error, initialized). Calls
    // onDone() afterwards either way; never blocks the event loop.
    void probeStatus(std::function<void()> onDone = nullptr);

    // Polls "waydroid status" once a second until the given key ("Container"
    // or "Session") reaches the desired RUNNING/STOPPED state, or until
    // maxAttempts is exhausted.
    void beginPolling(const QString &statusKey, bool waitForRunning,
                       int maxAttempts, std::function<void(bool ok, QString error)> onDone);
    void pollOnce();

    void runStartStage(StartStage stage);
    void runStopStage(StopStage stage);

    bool m_initialized = false;
    bool m_containerRunning = false;
    bool m_sessionRunning = false;
    bool m_busy = false;
    bool m_installInProgress = false;
    bool m_statusCheckPending = false;
    QString m_lastError;

    // Poll-loop state (only one start/stop sequence runs at a time, gated
    // by m_busy, so a single set of fields is enough).
    QString m_pollKey;
    bool m_pollWaitForRunning = true;
    int m_pollAttemptsRemaining = 0;
    std::function<void(bool, QString)> m_pollCallback;
};
