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
    ui(new Ui::Update)
{
    ui->setupUi(this);
    this->setFixedSize(this->size());

    this->setWindowTitle(QStringLiteral("程序增量升级测试"));
    this->setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint & ~Qt::WindowMaximizeButtonHint);

    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);

    ui->exit->setHidden(true);
    connect(ui->exit, &QPushButton::clicked, qApp, &QApplication::quit);

    m_dc = DownloadControl::instance();

    m_dirDown = QApplication::applicationDirPath();

    downLoad();
}


void Update::downLoad()
{
    QDir dir(m_dirDown);
    if(!dir.exists())
    {
        dir.mkdir(m_dirDown);
    }

    m_dc->setDir(m_dirDown);
    QStringList urlLst;
    urlLst << m_url;
    m_h = m_dc->createTask(urlLst);
    m_dc->delTempFile(m_h);
    m_dc->startTask(m_h);

    connect(m_dc, &DownloadControl::finished, this, &Update::execPatch);
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

void Update::execPatch(DOWNLOAD_HANDLE h)
{
    QString filePath = h->getFilePath();
    if(filePath.endsWith(".exe"))
    {
        QStringList args;
        m_p.startDetached(filePath, args);
        emit updateCompleted();
        qDebug() << "exec patch successed";
    }
    else
    {
        qDebug() << "exec patch failed";
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
