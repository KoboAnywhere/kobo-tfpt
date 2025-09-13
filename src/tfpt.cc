#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <cstddef>
#include <cstdlib>
#include <sys/stat.h>

#include <linux/uinput.h>

#include <NickelHook.h>

#include <QApplication>
#include <QScreen>
#include <QWidget>
#include <QDir>
#include <QFileSystemWatcher>
#include <QFile>
#include <QTextStream>
#include <QThread>

#include "tfpt.h"

static bool (*Device_supportsStylus)(void *);
static QString (*Device_getDeviceClassString)(void *);
static QRect* (*MainWindowController_uiGeometry)(void *);
static void (*WirelessEnabler_setEnabled)(void *, bool);

static void *(*MainWindowController_sharedInstance)();
static QWidget *(*MainWindowController_currentView)(void *);

static QObject *(*PowerManager_sharedInstance)();
static int (*PowerManager_filter)(QObject *, QObject *, QEvent *);
static QEvent::Type (*TimeEvent_eventType)();
static int (*WindowSystemInterface_handleScreenOrientationChange)(QScreen *, Qt::ScreenOrientation);

static std::pair<int, std::string> system_with_output(const std::string& command)
{
	FILE* pipe = popen(command.c_str(), "r");
	if (!pipe) {
		return {1, ""};
	}

	std::string output;
	char buffer[128];
	while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
		output += buffer;
	}

	int exit_code = pclose(pipe);
	if (exit_code == -1) {
		return {1, ""};
	}

	exit_code = WEXITSTATUS(exit_code);

	return {exit_code, output};
}

extern "C" __attribute__((visibility("default"))) bool _Device_supportsStylus(void* _this)
{
	int exit_code;
	std::string script_output;
	std::tie(exit_code, script_output) = system_with_output("kobo_supports_stylus");
	return exit_code == 0 ? script_output == "true" : Device_supportsStylus(_this);
}

extern "C" __attribute__((visibility("default"))) QString _Device_getDeviceClassString(void * _this)
{
	int exit_code;
	std::string script_output;
	std::tie(exit_code, script_output) = system_with_output("kobo_device_class");
	return exit_code == 0 ? QString::fromStdString(script_output) : Device_getDeviceClassString(_this);
}

extern "C" __attribute__((visibility("default"))) QRect* _MainWindowController_uiGeometry(void * _this)
{
	QRect* g = MainWindowController_uiGeometry(_this);
	QScreen *screen = QGuiApplication::primaryScreen();
	if (screen) {
		*g = screen->geometry();
	}
	return g;
}

extern "C" __attribute__((visibility("default"))) void _WirelessEnabler_setEnabled(void * _this, bool enabled)
{
	if (enabled) {
		system("kobo_toggle_wifi true");
	}
	WirelessEnabler_setEnabled(_this, enabled);
	if (!enabled) {
		system("kobo_toggle_wifi false");
	}
}

static void invokeMainWindowController(const char *method)
{
	QString name = QString();
	void *mwc = MainWindowController_sharedInstance();
	if (!mwc) {
		nh_log("invalid MainWindowController");
		return;
	}
	QWidget *cv = MainWindowController_currentView(mwc);
	if (!cv) {
		nh_log("invalid View");
		return;
	}
	name = cv->objectName();
	if (name == "ReadingView") {
		nh_log(method);
		QMetaObject::invokeMethod(cv, method, Qt::QueuedConnection);
	}
	else {
		nh_log("not reading view");
	}
}

static void triggerScreenOrientationChange(Qt::ScreenOrientation orientation)
{
	// We captured the QWindowSystemInterface::handleScreenOrientationChange function pointer
	if (WindowSystemInterface_handleScreenOrientationChange != NULL)
	{
		// Get the primary screen
		QScreen *screen = QGuiApplication::primaryScreen();
		if (screen != NULL)
		{
			// Force the wanted orientation
			WindowSystemInterface_handleScreenOrientationChange(screen, orientation);
		}
	}
}

void TriggerFilePageTurner::directoryChanged(const QString &path)
{
	// Lock the mutex
	mutex.lock();

	// Create a directory object
	QDir dir(path);

	// List of trigger files
	QStringList triggers = {"nextPage", "prevPage", "rotatePrimary", "rotate0", "rotate90", "rotate180", "rotate270"};
	for (const QString &trigger : triggers)
	{
		QString filePath = dir.filePath(trigger);
		QFile file(filePath);

		if (file.exists())
		{
			// Keep the device awake for a while
			emit notify();

			// Navigate pages
			if (trigger == "nextPage" || trigger == "prevPage")
			{
				invokeMainWindowController(trigger.toStdString().c_str());
			}

			// Rotate the screen
			else if (trigger == "rotatePrimary")
			{
				triggerScreenOrientationChange(Qt::PrimaryOrientation);
			}
			else if (trigger == "rotate0")
			{
				triggerScreenOrientationChange(Qt::PortraitOrientation);
			}
			else if (trigger == "rotate90")
			{
				triggerScreenOrientationChange(Qt::LandscapeOrientation);
			}
			else if (trigger == "rotate180")
			{
				triggerScreenOrientationChange(Qt::InvertedPortraitOrientation);
			}
			else if (trigger == "rotate270")
			{
				triggerScreenOrientationChange(Qt::InvertedLandscapeOrientation);
			}

			// Log the invocation
			nh_log(("Invoking " + trigger).toStdString().c_str());

			// Delete the trigger file
			if (!file.remove())
			{
				nh_log(("Failed to remove trigger file: " + filePath).toStdString().c_str());
			}
		}
	}

	// Unlock the mutex
	mutex.unlock();
}

void TimeLastUsedUpdater::notify()
{
	/* Update PowerManager::timeLastUsed to prevent sleep */
	QEvent timeEvent(TimeEvent_eventType());

	QObject *pm = PowerManager_sharedInstance();
	if (pm == NULL) {
		nh_log("invalid PowerManager");
	}

	PowerManager_filter(pm, this, &timeEvent);
}

TriggerFilePageTurner::TriggerFilePageTurner(QObject *parent)
	: QObject(parent)
{
	// Watch for trigger files
	QObject::connect(
		&watcher, &QFileSystemWatcher::directoryChanged,
		this, &TriggerFilePageTurner::directoryChanged);
	watcher.addPath("/tmp");
}

static int tfpt_init()
{
	// Connect to a few necessary Nickel components
	TimeLastUsedUpdater *timeLastUsedUpdater = new TimeLastUsedUpdater();
	TriggerFilePageTurner *tfpt = new TriggerFilePageTurner();
	QObject::connect(
		tfpt, &TriggerFilePageTurner::notify,
		timeLastUsedUpdater, &TimeLastUsedUpdater::notify,
		Qt::QueuedConnection);

	return 0;
}

static struct nh_info tfpt_info = {
	.name           = "TriggerFilePageTurner",
	.desc           = "Turn pages using trigger files",
};

static struct nh_hook tfpt_hook[] = {
	{
		.sym = "_ZNK6Device14supportsStylusEv",
		.sym_new = "_Device_supportsStylus",
		.lib = "libnickel.so.1.0.0",
		.out = nh_symoutptr(Device_supportsStylus)
	},
	{
		.sym = "_ZNK6Device20getDeviceClassStringEv",
		.sym_new = "_Device_getDeviceClassString",
		.lib = "libnickel.so.1.0.0",
		.out = nh_symoutptr(Device_getDeviceClassString)
	},
	{
		.sym = "_ZNK20MainWindowController10uiGeometryEv",
		.sym_new = "_MainWindowController_uiGeometry",
		.lib = "libnickel.so.1.0.0",
		.out = nh_symoutptr(MainWindowController_uiGeometry)
	},
	{
		.sym = "_ZN15WirelessEnabler10setEnabledEb",
		.sym_new = "_WirelessEnabler_setEnabled",
		.lib = "libnickel.so.1.0.0",
		.out = nh_symoutptr(WirelessEnabler_setEnabled)
	},
	{0},
};

static struct nh_dlsym tfpt_dlsym[] = {
	{
		.name = "_ZN20MainWindowController14sharedInstanceEv",
		.out = nh_symoutptr(MainWindowController_sharedInstance)
	},
	{
		.name = "_ZNK20MainWindowController11currentViewEv",
		.out = nh_symoutptr(MainWindowController_currentView)
	},
	{
		.name = "_ZN9TimeEvent9eventTypeEv",
		.out = nh_symoutptr(TimeEvent_eventType)
	},
	{
		.name = "_ZN12PowerManager14sharedInstanceEv",
		.out = nh_symoutptr(PowerManager_sharedInstance)
	},
	{
		.name = "_ZN12PowerManager6filterEP7QObjectP6QEvent",
		.out = nh_symoutptr(PowerManager_filter)
	},
	{
		.name = "_ZN22QWindowSystemInterface29handleScreenOrientationChangeEP7QScreenN2Qt17ScreenOrientationE",
		.out = nh_symoutptr(WindowSystemInterface_handleScreenOrientationChange)
	},
	{0},
};

NickelHook(
	.init  = tfpt_init,
	.info  = &tfpt_info,
	.hook  = tfpt_hook,
	.dlsym = tfpt_dlsym,
)
