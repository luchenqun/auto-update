#include "Update.h"
#include "ui_Update.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QSettings>

Update::Update(QString url, QString urlFull, bool bHide, QWidget *parent) :
    QWidget(parent),
    m_url(url),
    m_urlFull(urlFull),
    m_bHide(bHide),
    m_dirSrc(""),
    ui(new Ui::Update)
{
    ui->setupUi(this);
    this->setFixedSize(this->size());

    this->setWindowTitle(QStringLiteral("积木社区升级程序"));
    this->setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint & ~Qt::WindowMaximizeButtonHint);

    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);

    ui->exit->setHidden(true);
    connect(ui->exit, &QPushButton::clicked, qApp, &QApplication::quit);

    m_dc = DownloadControl::instance();

    // 修正注册表中的路径,防止修改到不正确的路径
//    updateRegPath();

    downLoad();
}

void Update::updateRegPath()
{
    QString regPath = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\DownloadToolN1.exe";
    QString appPath = QApplication::applicationDirPath();

    QSettings *reg = new QSettings(regPath, QSettings::NativeFormat);
    qDebug() << reg->value(".", false).toString();
    qDebug() << appPath;
}

void Update::downLoad()
{
    m_dirSrc = QApplication::applicationDirPath() + "/updatetemp";
    m_dirDes = QApplication::applicationDirPath() + "/update";

    QDir dir(m_dirSrc);
    if(!dir.exists())
    {
        dir.mkdir(m_dirSrc);
    }

    dir.setPath(m_dirDes);
    dir.removeRecursively();
    if(!dir.exists())
    {
        dir.mkdir(m_dirDes);
    }

    m_dc->setDir(m_dirSrc);
    QStringList urlLst;
    urlLst << m_url;
    m_h = m_dc->createTask(urlLst);
    m_dc->delTempFile(m_h);
    m_dc->startTask(m_h);

    connect(m_dc, &DownloadControl::finished, this, &Update::unRar);
    connect(m_dc, &DownloadControl::process, this, &Update::process);
    connect(m_dc, &DownloadControl::errorOccured, this, &Update::error);
    connect(m_dc, &DownloadControl::downloadFlag, this, &Update::downloadFlag);
}

void Update::process(qint64 dltotal, qint64 dlnow, double speed, DOWNLOAD_HANDLE h)
{
    if(m_h == h)
    {
        int percentage = 0;
        if(dltotal > 0)
            percentage = dlnow * 100 / dltotal;
        qDebug() << "percentage:" << percentage << "speed:" << speed;
        ui->progressBar->setValue(percentage);
    }
}

void Update::unRar(DOWNLOAD_HANDLE h)
{
    if(h != m_h)
        return;

    qDebug() << "unRar";
    QString exePath = QCoreApplication::applicationDirPath() + "/UnRAR.exe";
    if(!QFile::exists(exePath))
    {
        qDebug() << "unrar not exist";
        reInstallTip();
    }
    else
    {
        QStringList cmd;
        cmd.append("x");
        cmd.append("-o+");
        cmd.append(m_dc->getFilePath(m_h)); // 源路径
        cmd.append(m_dirDes);// 目标文件路径

        QProcess p;
        p.start(exePath, cmd);
        p.waitForFinished(-1);

        QByteArray array =  p.readAllStandardOutput();
        qDebug() << "unrar:" << QString(array);

//        copy();
        execPatch();
    }
}

void Update::execPatch()
{
    bool ret = false;
    QDir dir(m_dirDes);

    QStringList files = dir.entryList(QDir::Files);
    for(int i = 0; i < files.count(); i++)
    {
        QString name = files[i];
        if(name.endsWith(".exe"))
        {
            QString program = m_dirDes + "/" + name;
            QStringList args;
            m_p.startDetached(program, args);
            emit updateCompleted();
            ret = true;
            qDebug() << "exec patch successed";
        }
    }

    if(!ret)
    {
        qDebug() << "exec patch failed";
        reInstallTip();
    }
}

void Update::copy()
{
    qDebug() << "copy";

    bool ret = copyDir(m_dirDes, QCoreApplication::applicationDirPath());
    ui->progressBar->setValue(120);
    if(ret)
    {
        qDebug() << "update succeed";
        QDir dir(m_dirDes);
        dir.removeRecursively();

        QString program = "DownloadToolN1.exe";
        QStringList args;
        if(m_bHide)
            args << "-hide";
        m_p.startDetached(program, args);

        emit updateCompleted();
    }
    else
    {
        reInstallTip();
    }
}

// 更新失败，提示重新安装
void Update::reInstallTip()
{
    qDebug() << "update failed";
    ui->progressBar->setHidden(true);
    ui->label->setText(QStringLiteral("更新失败。请点击以下链接，下载最新版本, 然后重新安装。"));

    QString text = "<a href=" + m_urlFull + ">" + QStringLiteral("点击下载最新版本") + "</a>";
    ui->netsite->setText(text);
    ui->netsite->setOpenExternalLinks(true);

    ui->exit->setHidden(false);
}

void Update::error(int err, DOWNLOAD_HANDLE h)
{
    if(h == m_h)
    {
        qDebug() << "update error" << err;
        if(err != 0)
        {
            m_dc->stopTask(m_h);
            reInstallTip();
        }
    }
}

void Update::downloadFlag(int flag, DOWNLOAD_HANDLE handle)
{
    if(handle == m_h)
    {
        if(flag == DownloadFarm::DF_stop)
        {
            reInstallTip();
        }
    }
}

bool Update::copyDir(QString sourceFolder, QString destFolder)
{
    QDir sourceDir(sourceFolder);
    if(!sourceDir.exists())
        return false;

    QDir destDir(destFolder);
    if(!destDir.exists())
    {
        destDir.mkdir(destFolder);
    }

    QStringList files = sourceDir.entryList(QDir::Files);
    for(int i = 0; i < files.count(); i++)
    {
        QString srcName = sourceFolder + "/" + files[i];
        QString destName = destFolder + "/" + files[i];

        QFile f(destName);
        if(f.exists())
		{
            bool ret = f.remove(destName);
            if(!ret)
            {
                qDebug() << "remove file failed:" << destName;
            }
		}
        
        bool ret = QFile::copy(srcName, destName);
        if(!ret)
        {
            qDebug() << "copy file failed:" << srcName << "to" << destName;
            ui->label->setText(destName);
            return false;
        }
    }

    files.clear();
    files = sourceDir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
    for(int i = 0; i< files.count(); i++)
    {
        QString srcName = sourceFolder + "/" + files[i];
        QString destName = destFolder + "/" + files[i];
        copyDir(srcName, destName);
    }

    return true;
}
