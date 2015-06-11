#include "update.h"
#include <QApplication>
#include <QDir>

QString g_logfile_name;
void outputMessage(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString text;
    switch(type)
    {
    case QtDebugMsg:
        text = QString("Debug: ");
        break;

    case QtWarningMsg:
        text = QString("Warning: ");
        break;

    case QtCriticalMsg:
        text = QString("Critical: ");
        break;

    case QtFatalMsg:
        text = QString("Fatal: ");
    }

    QDateTime dateTime = QDateTime::currentDateTime();
    QString timeStr = dateTime.toString("yyyy-MM-dd hh:mm:ss");
    QString message = text.append(timeStr).append(": ").append(msg).append("\r\n");

    QFile file(g_logfile_name);
    file.open(QIODevice::WriteOnly | QIODevice::Append);
    file.write(message.toLocal8Bit().data());
    file.close();
}

void addLog()
{
    QDateTime dateTime = QDateTime::currentDateTime();
    QString dateTimeStr = dateTime.toString("yyyyMMdd_hhmmss");
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir + "/log");
    if(!dir.exists())
    {
        dir.mkpath(appDir + "/log");
    }
    g_logfile_name = appDir + "/log/log_" + dateTimeStr + ".txt";
    qInstallMessageHandler(outputMessage);
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setQuitOnLastWindowClosed(true);

    if(argc <= 2)
    {
        return -1;
    }

    QString urlPatch = argv[1];
    QString urlFull = argv[2];

    // 是否需要后台偷偷升级
    bool bHide = false;
    if(argc >= 3)
    {
        QString hide = argv[3];
        if(hide == "-hide")
        {
            bHide = true;
        }
    }

    // 启用日志功能
    addLog();
    Update u(urlPatch, urlFull, bHide);
    u.show();
    QObject::connect(&u, &Update::updateCompleted, qApp, &QApplication::quit);

    return a.exec();
}
