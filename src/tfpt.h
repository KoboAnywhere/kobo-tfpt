#ifndef TFPT_H
#define TFPT_H

#include <linux/input.h>
#include <linux/uinput.h>

#include <QFileSystemWatcher>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QPair>
#include <QThread>
#include <QWaitCondition>

class TriggerFilePageTurner : public QObject
{
	Q_OBJECT

private:
	QFileSystemWatcher watcher;
	QMutex mutex;

public:
	explicit TriggerFilePageTurner(QObject *parent = nullptr);

public slots:
	void directoryChanged(const QString &path);

signals:
	void notify();
};

class TimeLastUsedUpdater : public QObject
{
	Q_OBJECT

public slots:
	void notify();
};

#endif /* TFPT_H */
