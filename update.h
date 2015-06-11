#ifndef UPDATE_H_736108F8FDEC71BBE7113B069558DA32
#define UPDATE_H_736108F8FDEC71BBE7113B069558DA32

#include "downloadcontrol.h"
#include <QWidget>
#include <QProcess>
#include <QPushButton>

namespace Ui{
class Update;
}

class Update : public QWidget
{
    Q_OBJECT
public:
    explicit Update(QString url, QString urlFull, bool bHide, QWidget *parent = 0);
signals:
    void updateCompleted();

public slots:
    void downLoad();
    void process(qint64 dltotal, qint64 dlnow, double speed, DOWNLOAD_HANDLE h);
    void error(int err, DOWNLOAD_HANDLE h);
    void downloadFlag(int flag, DOWNLOAD_HANDLE handle);
    void execPatch(DOWNLOAD_HANDLE h);

private:
    DownloadControl* m_dc;
    DOWNLOAD_HANDLE m_h;
    QString m_url;
    QString m_urlFull;
    QString m_dirDown;
    bool m_bHide;
    QProcess m_p;

    Ui::Update* ui;

    void reInstallTip();
};

#endif // UPDATE_H_736108F8FDEC71BBE7113B069558DA32
