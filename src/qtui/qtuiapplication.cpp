/***************************************************************************
 *   Copyright (C) 2005-2016 by the Quassel Project                        *
 *   devel@quassel-irc.org                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3.                                           *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "qtuiapplication.h"

#include <QIcon>
#include <QDir>
#include <QStringList>

#ifdef HAVE_KDE4
#  include <KStandardDirs>
#endif

#include "client.h"
#include "cliparser.h"
#include "mainwin.h"
#include "qtui.h"
#include "qtuisettings.h"

QtUiApplication::QtUiApplication(int &argc, char **argv)
#ifdef HAVE_KDE4
    : KApplication(),  // KApplication is deprecated in KF5
#else
    : QApplication(argc, argv),
#endif
    Quassel(),
    _aboutToQuit(false)
{
#ifdef HAVE_KDE4
    Q_UNUSED(argc); Q_UNUSED(argv);

    // Setup KDE's data dirs
    // Because we can't use KDE stuff in (the class) Quassel directly, we need to do this here...
    QStringList dataDirs = KGlobal::dirs()->findDirs("data", "");

    // Just in case, also check our install prefix
    dataDirs << QCoreApplication::applicationDirPath() + "/../share/apps/";

    // Normalize and append our application name
    for (int i = 0; i < dataDirs.count(); i++)
        dataDirs[i] = QDir::cleanPath(dataDirs.at(i)) + "/quassel/";

    // Add resource path and just in case.
    // Workdir should have precedence
    dataDirs.prepend(QCoreApplication::applicationDirPath() + "/data/");
    dataDirs.append(":/data/");

    // Append trailing '/' and check for existence
    auto iter = dataDirs.begin();
    while (iter != dataDirs.end()) {
        if (!iter->endsWith(QDir::separator()) && !iter->endsWith('/'))
            iter->append(QDir::separator());
        if (!QFile::exists(*iter))
            iter = dataDirs.erase(iter);
        else
            ++iter;
    }

    dataDirs.removeDuplicates();
    setDataDirPaths(dataDirs);

#else /* HAVE_KDE4 */

    setDataDirPaths(findDataDirPaths());

#endif /* HAVE_KDE4 */

#if defined(HAVE_KDE4) || defined(Q_OS_MAC)
    disableCrashhandler();
#endif /* HAVE_KDE4 || Q_OS_MAC */
    setRunMode(Quassel::ClientOnly);

#if QT_VERSION < 0x050000
    qInstallMsgHandler(Client::logMessage);
#else
    qInstallMessageHandler(Client::logMessage);
    connect(this, &QGuiApplication::commitDataRequest, this, &QtUiApplication::commitData, Qt::DirectConnection);
    connect(this, &QGuiApplication::saveStateRequest, this, &QtUiApplication::saveState, Qt::DirectConnection);
#endif

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
    QGuiApplication::setFallbackSessionManagementEnabled(false);
#endif
}


bool QtUiApplication::init()
{
    if (Quassel::init()) {
        // FIXME: MIGRATION 0.3 -> 0.4: Move database and core config to new location
        // Move settings, note this does not delete the old files
#ifdef Q_OS_MAC
        QSettings newSettings("quassel-irc.org", "quasselclient");
#else

# ifdef Q_OS_WIN
        QSettings::Format format = QSettings::IniFormat;
# else
        QSettings::Format format = QSettings::NativeFormat;
# endif

        QString newFilePath = Quassel::configDirPath() + "quasselclient"
                              + ((format == QSettings::NativeFormat) ? QLatin1String(".conf") : QLatin1String(".ini"));
        QSettings newSettings(newFilePath, format);
#endif /* Q_OS_MAC */

        if (newSettings.value("Config/Version").toUInt() == 0) {
#     ifdef Q_OS_MAC
            QString org = "quassel-irc.org";
#     else
            QString org = "Quassel Project";
#     endif
            QSettings oldSettings(org, "Quassel Client");
            if (oldSettings.allKeys().count()) {
                qWarning() << "\n\n*** IMPORTANT: Config and data file locations have changed. Attempting to auto-migrate your client settings...";
                foreach(QString key, oldSettings.allKeys())
                newSettings.setValue(key, oldSettings.value(key));
                newSettings.setValue("Config/Version", 1);
                qWarning() << "*   Your client settings have been migrated to" << newSettings.fileName();
                qWarning() << "*** Migration completed.\n\n";
            }
        }

        // MIGRATION end

        // Settings upgrade/downgrade handling
        if (!migrateSettings()) {
            qCritical() << "Could not load or upgrade client settings, terminating!";
            return false;
        }

        // Checking if settings Icon Theme is valid
        QString savedIcontheme = QtUiSettings().value("IconTheme", QVariant("")).toString();
#ifndef WITH_OXYGEN
        if (savedIcontheme == "oxygen")
            QtUiSettings().remove("IconTheme");
#endif
#ifndef WITH_BREEZE
        if (savedIcontheme == "breeze")
            QtUiSettings().remove("IconTheme");
#endif
#ifndef WITH_BREEZE_DARK
        if (savedIcontheme == "breezedark")
            QtUiSettings().remove("IconTheme");
#endif

        // Set the icon theme
        if (Quassel::isOptionSet("icontheme"))
            QIcon::setThemeName(Quassel::optionValue("icontheme"));
        else if (QtUiSettings().value("IconTheme", QVariant("")).toString() != "")
            QIcon::setThemeName(QtUiSettings().value("IconTheme").toString());
        else if (QIcon::themeName().isEmpty())
            // Some platforms don't set a default icon theme; chances are we can find our bundled theme though
            QIcon::setThemeName("breeze");

        // session resume
        QtUi *gui = new QtUi();
        Client::init(gui);
        // init gui only after the event loop has started
        // QTimer::singleShot(0, gui, SLOT(init()));
        gui->init();
        resumeSessionIfPossible();
        return true;
    }
    return false;
}


QtUiApplication::~QtUiApplication()
{
    Client::destroy();
}


void QtUiApplication::quit()
{
    QtUi::mainWindow()->quit();
}


bool QtUiApplication::migrateSettings()
{
    // --------
    // Check major settings version.  This represents incompatible changes between settings
    // versions.  So far, we only have 1.
    QtUiSettings s;
    uint versionMajor = s.version();
    if (versionMajor != 1) {
        qCritical() << qPrintable(QString("Invalid client settings version '%1'")
                                  .arg(versionMajor));
        return false;
    }

    // --------
    // Check minor settings version, handling upgrades/downgrades as needed
    // Current minor version
    //
    // NOTE:  If you increase the minor version, you MUST ALSO add new version upgrade logic in
    // applySettingsMigration()!  Otherwise, settings upgrades will fail.
    const uint VERSION_MINOR_CURRENT = 6;
    // Stored minor version
    uint versionMinor = s.versionMinor();

    if (versionMinor == VERSION_MINOR_CURRENT) {
        // At latest version, no need to migrate defaults or other settings
        return true;
    } else if (versionMinor == 0) {
        // New configuration, store as current version
        qDebug() << qPrintable(QString("Set up new client settings v%1.%2")
                               .arg(versionMajor).arg(VERSION_MINOR_CURRENT));
        s.setVersionMinor(VERSION_MINOR_CURRENT);

        // Update the settings stylesheet for first setup.  We don't know if older content exists,
        // if the configuration got erased separately, etc.
        QtUiStyle qtUiStyle;
        qtUiStyle.generateSettingsQss();
        return true;
    } else if (versionMinor < VERSION_MINOR_CURRENT) {
        // We're upgrading - apply the neccessary upgrades from each interim version
        // curVersion will never equal VERSION_MINOR_CURRENT, as it represents the version before
        // the most recent applySettingsMigration() call.
        for (uint curVersion = versionMinor; curVersion < VERSION_MINOR_CURRENT; curVersion++) {
            if (!applySettingsMigration(s, curVersion + 1)) {
                // Something went wrong, time to bail out
                qCritical() << qPrintable(QString("Could not migrate client settings from v%1.%2 "
                                                  "to v%1.%3")
                                          .arg(versionMajor).arg(curVersion).arg(curVersion + 1));
                // Keep track of the last successful upgrade to avoid repeating it on next start
                s.setVersionMinor(curVersion);
                return false;
            }
        }
        // Migration successful!
        qDebug() << qPrintable(QString("Successfully migrated client settings from v%1.%2 to "
                                       "v%1.%3")
                               .arg(versionMajor).arg(versionMinor).arg(VERSION_MINOR_CURRENT));
        // Store the new minor version
        s.setVersionMinor(VERSION_MINOR_CURRENT);
        return true;
    } else {
        // versionMinor > VERSION_MINOR_CURRENT
        // The user downgraded to an older version of Quassel.  Let's hope for the best.
        // Don't change the minorVersion as the newer version's upgrade logic has already run.
        qWarning() << qPrintable(QString("Client settings v%1.%2 is newer than latest known v%1.%3,"
                                         " things might not work!")
                                 .arg(versionMajor).arg(versionMinor).arg(VERSION_MINOR_CURRENT));
        return true;
    }
}


bool QtUiApplication::applySettingsMigration(QtUiSettings settings, const uint newVersion)
{
    switch (newVersion) {
    // Version 0 and 1 aren't valid upgrade paths - one represents no version, the other is the
    // oldest version.  Ignore those, start from 2 and higher.
    // Each missed version will be called in sequence.  E.g. to upgrade from '1' to '3', this
    // function will be called with '2', then '3'.
    // Use explicit scope via { ... } to avoid cross-initialization
    //
    // In most cases, the goal is to preserve the older default values for keys that haven't been
    // saved.  Exceptions will be noted below.
    // NOTE:  If you add new upgrade logic here, you MUST ALSO increase VERSION_MINOR_CURRENT in
    // migrateSettings()!  Otherwise, your upgrade logic won't ever be called.
    case 6:
    {
        // New default changes: sender colors switched around to Tango-ish theme

        // --------
        // QtUiStyle settings
        QtUiStyleSettings settingsUiStyleColors("Colors");
        // Preserve the old default values for all variants
        const QColor oldDefaultSenderColorSelf = QColor(0, 0, 0);
        const QList<QColor> oldDefaultSenderColors = QList<QColor> {
            QColor(204,  13, 127),  /// Sender00
            QColor(142,  85, 233),  /// Sender01
            QColor(179,  14,  14),  /// Sender02
            QColor( 23, 179,  57),  /// Sender03
            QColor( 88, 175, 179),  /// Sender04
            QColor(157,  84, 179),  /// Sender05
            QColor(179, 151, 117),  /// Sender06
            QColor( 49, 118, 179),  /// Sender07
            QColor(233,  13, 127),  /// Sender08
            QColor(142,  85, 233),  /// Sender09
            QColor(179,  14,  14),  /// Sender10
            QColor( 23, 179,  57),  /// Sender11
            QColor( 88, 175, 179),  /// Sender12
            QColor(157,  84, 179),  /// Sender13
            QColor(179, 151, 117),  /// Sender14
            QColor( 49, 118, 179),  /// Sender15
        };
        if (!settingsUiStyleColors.valueExists("SenderSelf")) {
            // Preserve the old default sender color if none set
            settingsUiStyleColors.setValue("SenderSelf", oldDefaultSenderColorSelf);
        }
        QString senderColorId;
        for (int i = 0; i < oldDefaultSenderColors.count(); i++) {
            // Get the sender color ID for each available color
            QString dez = QString::number(i);
            if (dez.length() == 1) dez.prepend('0');
            senderColorId = QString("Sender" + dez);
            if (!settingsUiStyleColors.valueExists(senderColorId)) {
                // Preserve the old default sender color if none set
                settingsUiStyleColors.setValue(senderColorId, oldDefaultSenderColors[i]);
            }
        }

        // Update the settings stylesheet with old defaults
        QtUiStyle qtUiStyle;
        qtUiStyle.generateSettingsQss();
        // --------

        // Migration complete!
        return true;
    }
    case 5:
    {
        // New default changes: sender colors apply to nearly all messages with nicks

        // --------
        // QtUiStyle settings
        QtUiStyleSettings settingsUiStyleColors("Colors");
        const QString useNickGeneralColorsId = "UseNickGeneralColors";
        if (!settingsUiStyleColors.valueExists(useNickGeneralColorsId)) {
            // New default is true, preserve previous behavior by setting to false
            settingsUiStyleColors.setValue(useNickGeneralColorsId, false);
        }

        // Update the settings stylesheet with old defaults
        QtUiStyle qtUiStyle;
        qtUiStyle.generateSettingsQss();
        // --------

        // Migration complete!
        return true;
    }
    case 4:
    {
        // New default changes: system locale used to generate a timestamp format string, deciding
        // 24-hour or 12-hour timestamp.

        // --------
        // ChatView settings
        const QString useCustomTimestampFormatId = "ChatView/__default__/UseCustomTimestampFormat";
        if (!settings.valueExists(useCustomTimestampFormatId)) {
            // New default value is false, preserve previous behavior by setting to true
            settings.setValue(useCustomTimestampFormatId, true);
        }
        // --------

        // Migration complete!
        return true;
    }
    case 3:
    {
        // New default changes: per-chat history and line wrapping enabled by default.

        // --------
        // InputWidget settings
        UiSettings settingsInputWidget("InputWidget");
        const QString enableInputPerChatId = "EnablePerChatHistory";
        if (!settingsInputWidget.valueExists(enableInputPerChatId)) {
            // New default value is true, preserve previous behavior by setting to false
            settingsInputWidget.setValue(enableInputPerChatId, false);
        }

        const QString enableInputLinewrap = "EnableLineWrap";
        if (!settingsInputWidget.valueExists(enableInputLinewrap)) {
            // New default value is true, preserve previous behavior by setting to false
            settingsInputWidget.setValue(enableInputLinewrap, false);
        }
        // --------

        // Migration complete!
        return true;
    }
    case 2:
    {
        // New default changes: sender <nick> brackets disabled, sender colors and sender CTCP
        // colors enabled.

        // --------
        // ChatView settings
        const QString timestampFormatId = "ChatView/__default__/TimestampFormat";
        if (!settings.valueExists(timestampFormatId)) {
            // New default value is " hh:mm:ss", preserve old default of "[hh:mm:ss]"
            settings.setValue(timestampFormatId, "[hh:mm:ss]");
        }

        const QString showSenderBracketsId = "ChatView/__default__/ShowSenderBrackets";
        if (!settings.valueExists(showSenderBracketsId)) {
            // New default is false, preserve previous behavior by setting to true
            settings.setValue(showSenderBracketsId, true);
        }
        // --------

        // --------
        // QtUiStyle settings
        QtUiStyleSettings settingsUiStyleColors("Colors");
        const QString useSenderColorsId = "UseSenderColors";
        if (!settingsUiStyleColors.valueExists(useSenderColorsId)) {
            // New default is true, preserve previous behavior by setting to false
            settingsUiStyleColors.setValue(useSenderColorsId, false);
        }

        const QString useSenderActionColorsId = "UseSenderActionColors";
        if (!settingsUiStyleColors.valueExists(useSenderActionColorsId)) {
            // New default is true, preserve previous behavior by setting to false
            settingsUiStyleColors.setValue(useSenderActionColorsId, false);
        }

        // Update the settings stylesheet with old defaults
        QtUiStyle qtUiStyle;
        qtUiStyle.generateSettingsQss();
        // --------

        // Migration complete!
        return true;
    }
    default:
        // Something unexpected happened
        return false;
    }
}


void QtUiApplication::commitData(QSessionManager &manager)
{
    Q_UNUSED(manager)
    _aboutToQuit = true;
}


void QtUiApplication::saveState(QSessionManager &manager)
{
    //qDebug() << QString("saving session state to id %1").arg(manager.sessionId());
    // AccountId activeCore = Client::currentCoreAccount().accountId(); // FIXME store this!
    SessionSettings s(manager.sessionId());
    s.setSessionAge(0);
    QtUi::mainWindow()->saveStateToSettings(s);
}


void QtUiApplication::resumeSessionIfPossible()
{
    // load all sessions
    if (isSessionRestored()) {
        qDebug() << QString("restoring from session %1").arg(sessionId());
        SessionSettings s(sessionId());
        s.sessionAging();
        s.setSessionAge(0);
        QtUi::mainWindow()->restoreStateFromSettings(s);
        s.cleanup();
    }
    else {
        SessionSettings s(QString("1"));
        s.sessionAging();
        s.cleanup();
    }
}
