#include "downloadcontrol.h"
#include <QDebug>
#include <QFileInfo>
#include <QApplication>
#include <QDir>
#include <QMutexLocker>

quint64 getDiskFreeSpace(QString drive)
{
#ifdef Q_OS_WIN
    LPCWSTR  lpDrive = (LPCWSTR )drive.utf16();
    if(GetDriveType(lpDrive) == DRIVE_FIXED)
    {
        ULARGE_INTEGER freeBytesAvailable;
        ULARGE_INTEGER totalNumberOfBytes;
        ULARGE_INTEGER totalNumberOfFreeBytes;
        if(GetDiskFreeSpaceEx(lpDrive, &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes))
        {
            qDebug() << drive << ": " << totalNumberOfFreeBytes.QuadPart;
            return (quint64)totalNumberOfFreeBytes.QuadPart;
        }
    }
    return 0;
#else

#endif
}

bool isDiskSpaceFree(QString path, qint64 size)
{
    QString drive = path.left(2);
    quint64 space = getDiskFreeSpace(drive);
    if(size <= 0)
        return true;

    return (space > size);
}

size_t DownloadFarm::writeDataFuncHeader(void* buffer, size_t size, size_t n, void *user)
{
    DownloadFarm* d = (DownloadFarm*)user;
    qint64 len = d->m_fileHeader.write((char*)buffer, size*n);
    return len;
}

int DownloadFarm::processFuncHeader(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    DownloadFarm* d = (DownloadFarm*)p;
    if(d->m_stopFlag)
    {
        return CURLE_ABORTED_BY_CALLBACK;
    }

    return 0;
}

size_t DownloadFarm::writeDataFunc(void* buffer, size_t size, size_t n, void *user)
{
    DownloadFarm* d = (DownloadFarm*)user;
    qint64 len = d->m_file.write((char*)buffer, size*n);
    return len;
}

int DownloadFarm::processFunc(void *p,
                    curl_off_t dltotal, curl_off_t dlnow,
                    curl_off_t ultotal, curl_off_t ulnow)
{
    DownloadFarm* d = (DownloadFarm*)p;

    int time = d->m_time.elapsed();
    if(((dltotal > dlnow && time >= 1000) || dltotal == dlnow) && dltotal > 0)
    {
        double speed = 0;
        if(dlnow - d->m_lastTotalSize > 0)
        {
            speed = (dlnow - d->m_lastTotalSize) * 1000 / time;  // Bytes / s
        }
        d->m_downloadSize = dlnow+d->m_breakPoint;
        if(d->m_state != DS_paused)
        {
            emit d->process(dltotal+d->m_breakPoint, dlnow+d->m_breakPoint, speed);
        }

        d->m_lastTotalSize = dlnow;
        d->m_time.restart();
    }

    if(d->m_stopFlag)
    {
        return CURLE_ABORTED_BY_CALLBACK;
    }

    return 0;
}

DownloadFarm::DownloadFarm(const QStringList &urlStrLst, const QString dir, QObject *parent) :
    QObject(parent),
    m_curl(NULL),
    m_fileName(INVALID_STRING),
    m_filePath(INVALID_STRING),
    m_urlStrLst(urlStrLst),
    m_urlStr(""),
    m_urlStrFirst(""),
    m_currentUrlIndex(0),
    m_fileDir(dir),
    m_fileSize(0),
    m_downloadSize(0),
    m_breakPoint(0),
    m_lastTotalSize(0),
    m_error(0),
    m_state(DS_ready),
    m_flag(DF_null),
    m_stopFlag(false),
    m_redirectCnt(0),
    m_retryCnt(0)
{
    if(m_urlStrLst.size() > m_currentUrlIndex)
    {
        m_urlStr =  m_urlStrLst.at(m_currentUrlIndex);
        m_urlStrFirst = m_urlStr;
        qDebug() << "url" << m_currentUrlIndex << m_urlStr;
    }

    m_url = QUrl(m_urlStr);
    QFileInfo fileInfo(m_url.toString());
    m_fileName = fileInfo.fileName();
    m_filePath = m_fileDir + "/" + m_fileName;
    setDir(m_fileDir);

    m_curl = curl_easy_init();

    connect(this, &DownloadFarm::retry, this, &DownloadFarm::execute, Qt::QueuedConnection); // 必须队列方式连接信号槽
}

DownloadFarm::~DownloadFarm()
{
    qDebug() << "~Download()";
    stop();
    QMutexLocker locker(&m_mutex);
    curl_easy_cleanup(m_curl);
}

bool DownloadFarm::detectDiskSpace()
{
    if(!isDiskSpaceFree(m_filePath, m_fileSize-m_downloadSize))
    {
        m_error = DE_destinationSpaceNotEnough;
        qDebug() << "no enough disk space. m_fileSize:" << m_fileSize << "m_downloadSize:" << m_downloadSize;;
        emit errorOccured(m_error);
        return false;
    }
    else if(m_error == DE_destinationSpaceNotEnough)
    {
        m_error = CURLE_OK;
    }
    return true;
}

bool DownloadFarm::fileInit()
{    
    if(!QFile::exists(m_filePath+".part.cfg"))
    {
        QFile::remove(m_filePath + ".part");
    }
    else if(m_urlStrFirst == m_urlStr)
    {
        m_fileCfg.setFileName(m_filePath + ".part.cfg");
        m_fileCfg.open(QIODevice::ReadWrite);
        QDataStream in(&m_fileCfg);
        QString urlStr;
        in >> urlStr;
        if(urlStr != m_urlStrFirst)
        {
            // 删除旧版本的文件
            QFile::remove(m_filePath + ".part");
        }
        m_fileCfg.close();
    }

    // 头
    QString headerDir = m_fileDir + "/DownloadHeader";
    QDir headerDirector(headerDir);
    if(!headerDirector.exists())
        headerDirector.mkpath(headerDir);
    m_fileHeader.setFileName(headerDir+"/"+m_fileName+".header");
    m_fileHeader.open(QIODevice::WriteOnly | QIODevice::Append);

    m_file.setFileName(m_filePath+".part");
    bool ret = m_file.open(QIODevice::WriteOnly | QIODevice::Append);
    if(ret == false)
    {
        m_error = DE_createFileFailed;
        qDebug() << "m_error:" << m_error;
        emit errorOccured(m_error);
        return false;
    }

    m_breakPoint = m_file.size();
    m_downloadSize = m_breakPoint;

    if(m_urlStrFirst == m_urlStr)// 重定向地址不写
    {
        m_fileCfg.setFileName(m_filePath + ".part.cfg");
        m_fileCfg.open(QIODevice::ReadWrite);
        QDataStream out(&m_fileCfg);
        out << m_urlStrFirst;
        m_fileCfg.close();
    }

    return true;
}

void DownloadFarm::fileUninit()
{
    m_fileHeader.flush();
    m_fileHeader.close();
    m_file.flush();
    m_file.close();
}

// true  正常
// false 错误
bool DownloadFarm::responseCodeHandle(int responseCode)
{
    if(301 == responseCode || 302 == responseCode || 303 == responseCode)
    {
        char *newUrl = 0;
        CURLcode returnCode = curl_easy_getinfo(m_curl, CURLINFO_REDIRECT_URL, &newUrl);
        qDebug() << "curl_easy_getinfo CURLINFO_REDIRECT_URL return code" << returnCode;
        if(returnCode == CURLE_OK && newUrl)
        {
            qDebug() << "new url" << newUrl << "redirectCnt" << m_redirectCnt;
            if(m_redirectCnt < 5)
            {
                m_urlStr = QString::fromLocal8Bit(newUrl);
                m_url = QUrl(m_urlStr);
                m_retryCnt = 0;
                m_redirectCnt++;
                emit retry();
            }
            else
            {
                m_error = DE_tooManyRedirects;
                emit errorOccured(m_error);
            }
        }
        return false;
    }
    else if(responseCode >= 400 && responseCode <= 510)
    {
        nextUrl(responseCode);
        return false;
    }

    return true;
}

void DownloadFarm::nextUrl(int code)
{
    qDebug() << "m_currentUrlIndex" << m_currentUrlIndex;
    if(m_urlStrLst.size() > m_currentUrlIndex + 1)
    {
        m_currentUrlIndex++;
        m_urlStr =  m_urlStrLst.at(m_currentUrlIndex);
        m_url = QUrl(m_urlStr);
        m_retryCnt = 0;
        m_redirectCnt = 0;
        qDebug() << "url" << m_currentUrlIndex << m_urlStr;
        emit retry();
    }
    else
    {
        m_error = code;
        emit errorOccured(m_error);
    }
}

void DownloadFarm::execute()
{
    QMutexLocker locker(&m_mutex);
    m_stopFlag = false;

    // 初始化文件
    if(!fileInit())
    {
        m_state = DS_ready;
        return;
    }

    curl_easy_reset(m_curl); // import, reset curl handle
    curl_easy_setopt(m_curl, CURLOPT_URL, m_urlStr.toLocal8Bit().data());
//    curl_easy_setopt(m_curl, CURLOPT_MAXREDIRS, 5);
//    curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1);// 重定向
    QString useragent("Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/34.0.1847.131 Safari/537.36");
    curl_easy_setopt(m_curl, CURLOPT_USERAGENT, useragent.toLocal8Bit().data());

    curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_LIMIT, 10);
    curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_TIME, TIMEOUT_VALUE);    // 300s的时间内速度小于10b/s,则取消下载

    // 获得文件大小
    m_state = DS_header;
    curl_easy_setopt(m_curl, CURLOPT_HEADER, 1);
    curl_easy_setopt(m_curl, CURLOPT_NOBODY, 1);
    curl_easy_setopt(m_curl, CURLOPT_XFERINFOFUNCTION, processFuncHeader);
    curl_easy_setopt(m_curl, CURLOPT_HEADERFUNCTION, writeDataFuncHeader);
    curl_easy_setopt(m_curl, CURLOPT_HEADERDATA, this);

    CURLcode code = curl_easy_perform(m_curl);
	qDebug() << "curl_easy_perform code header:" << code;
    int responseCode = -1;
    curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &responseCode);
    qDebug() << "header responseCode:" << responseCode;
    if(code == CURLE_ABORTED_BY_CALLBACK)
    {
        // 手动退出
        m_flag = DF_stop;
        emit downloadFlag(DF_stop);
        fileUninit();
        m_retryCnt = 0;
        m_redirectCnt = 0;
        m_state = DS_ready;
        return;
    }
    else if(code != CURLE_OK)
    {
        if(code != CURLE_OPERATION_TIMEDOUT)
        {
            qDebug() << "m_retryCnt" << m_retryCnt;
            if(m_retryCnt < 3)
            {
                m_retryCnt++;
                emit retry();
            }
            else
            {
                m_retryCnt = 0;
                nextUrl(code);
            }
        }
        else
        {
            m_retryCnt = 0;
            m_error = code;
            emit errorOccured(m_error);
        }

        m_fileSize = 0;
        fileUninit();
        m_state = DS_ready;
        return;
    }
    else if(code == CURLE_OK)
    {
        double size = 0;
        curl_easy_getinfo(m_curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &size);
        m_fileSize = size;
        qDebug() << "curl_easy_getinfo size:" << (int)size;
        emit fileSize(m_fileSize);

        // 处理http状态码
        if(!responseCodeHandle(responseCode))
        {
            fileUninit();
            m_state = DS_ready;
            return;
        }
    }

    // 检测磁盘大小
    if(!detectDiskSpace())
    {
        fileUninit();
        m_state = DS_ready;
        return;
    }

    // 下载
    m_state = DS_downloading;
    curl_easy_setopt(m_curl, CURLOPT_HEADER, 0);
    curl_easy_setopt(m_curl, CURLOPT_NOBODY, 0);
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &writeDataFunc);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, this);

    curl_easy_setopt(m_curl, CURLOPT_RESUME_FROM_LARGE, m_breakPoint); // 断点续传

    curl_easy_setopt(m_curl, CURLOPT_XFERINFOFUNCTION, processFunc);
    curl_easy_setopt(m_curl, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(m_curl, CURLOPT_NOPROGRESS, 0L);

    curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(m_curl, CURLOPT_FORBID_REUSE, 1);

    m_time.start();
    m_lastTotalSize = 0;
    // 可以暂停
    m_flag = DF_canPause;
    emit downloadFlag(DF_canPause);
    code = curl_easy_perform(m_curl);
    qDebug() << "curl_easy_perform code body:" << code;

    responseCode = -1;
    curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &responseCode);
    qDebug() << "body responseCode:" << responseCode;

    // 关闭文件
    fileUninit();

    if(code == CURLE_ABORTED_BY_CALLBACK)
    {
        // 手动退出
        m_flag = DF_stop;
        m_retryCnt = 0;
        m_redirectCnt = 0;
        emit downloadFlag(DF_stop);
    }
    else if(code != CURLE_OK)
    {
        if(code != CURLE_OPERATION_TIMEDOUT)
        {
            qDebug() << "m_retryCnt" << m_retryCnt;
            if(m_retryCnt < 3)
            {
                qDebug() << "now retry";
                m_retryCnt++;
                emit retry();
            }
            else
            {
                m_retryCnt = 0;
                nextUrl(code);
            }
        }
        else
        {
            m_retryCnt = 0;
            m_error = code;
            emit errorOccured(m_error);
        }
    }
    else if(code == CURLE_OK)
    {
        // 处理http状态码
        if(responseCodeHandle(responseCode))
        {
            finish();
        }
    }

    m_state = DS_ready;
}

void DownloadFarm::finish()
{
    qDebug() << "Download::finished()" << m_filePath;

    QFile::remove(m_filePath);
    QFile::remove(m_filePath + ".part.cfg");
    bool bRet = false;
    bRet = QFile::rename(m_filePath + ".part", m_filePath);
    qDebug() << "rename ret:" << bRet << "filepath:" << m_filePath;
    if(bRet)
    {
        QFileInfo info(m_filePath);
        qDebug() << "file size:" << info.size();
        emit finished();
    }
    else
    {
        QFile::remove(m_filePath + ".part");
    }
}

void DownloadFarm::stop()
{
    qDebug() << "Download::stop()" << m_state;
    m_stopFlag = true;
}

bool DownloadFarm::pause()
{
    qDebug() << "Download::pause()" << m_state;
    if(m_state == DS_downloading)
    {
        CURLcode code = curl_easy_pause(m_curl, CURLPAUSE_RECV);
        curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_LIMIT, 0);
        curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_TIME, 0);
        if(code == CURLE_OK)
        {
            m_state = DS_paused;
            return true;
        }
        m_error = code;
        qDebug() << "pause error:" << m_error;
    }

    return false;
}

bool DownloadFarm::resume()
{
    qDebug() << "Download::pause()" << m_state;
    if(m_state == DS_paused)
    {
        CURLcode code = curl_easy_pause(m_curl, CURLPAUSE_CONT);
        curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_LIMIT, 10);
        curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_TIME, TIMEOUT_VALUE);
        if(code == CURLE_OK)
        {
            m_state = DS_downloading;
            detectDiskSpace();
            return true;
        }
        m_error = code;
        qDebug() << "resume error:" << m_error;
    }

    return false;
}

bool DownloadFarm::delTempFile()
{
    qDebug() << "Download::delTempFile()" << m_state;
    if(m_state == DS_ready)
    {
        bool ret1 = QFile::remove(m_filePath + ".part");
        bool ret2 = QFile::remove(m_filePath + ".part.cfg");
        qDebug() << "Download::delTempFile()" << m_fileName << ".part" << ret1 << ".part.cfg" << ret2;
        if(ret1 && ret2)
        {
            return true;
        }
    }

    return false;
}

bool DownloadFarm::setFileName(const QString fileName)
{
    qDebug() << "Download::setFileName()" << m_state;
    if(m_state == DS_ready)
    {
        m_fileName = fileName;
        m_filePath = m_fileDir + "/" + m_fileName;
        return true;
    }
    return false;
}

QString DownloadFarm::getFileName() const
{
    return m_fileName;
}

QString DownloadFarm::getFilePath() const
{
    return m_filePath;
}

void DownloadFarm::setDir(const QString dir)
{
    m_fileDir = dir;
    m_filePath = m_fileDir + "/" + m_fileName;
    QDir directory;
    directory.setPath(m_fileDir);
    if(!directory.exists())
    {
        directory.mkpath(m_fileDir);
    }
}

QString DownloadFarm::getDir() const
{
    return m_fileDir;
}

QString DownloadFarm::getUrlStr() const
{
    return m_urlStr;
}


DownloadThread::DownloadThread(const QStringList &urlStrLst, const QString dir, QObject *parent):
    QObject(parent),
    m_d(NULL),
    m_urlStrLst(urlStrLst),
    m_fileDir(dir)
{
    m_d = new DownloadFarm(m_urlStrLst, m_fileDir);
    m_d->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_d, &QObject::deleteLater);
    connect(this, &DownloadThread::execute, m_d, &DownloadFarm::execute, Qt::QueuedConnection);
    connect(m_d, &DownloadFarm::process, this, &DownloadThread::processSlot, Qt::QueuedConnection);
    connect(m_d, &DownloadFarm::finished, this, &DownloadThread::finishSlot, Qt::QueuedConnection);
    connect(m_d, &DownloadFarm::errorOccured, this, &DownloadThread::errorOccuredSlot, Qt::QueuedConnection);
    connect(m_d, &DownloadFarm::downloadFlag, this, &DownloadThread::downloadFlagSlot, Qt::QueuedConnection);
    connect(m_d, &DownloadFarm::fileSize, this, &DownloadThread::fileSizeSlot, Qt::QueuedConnection);
    connect(m_d, &DownloadFarm::retry, this, &DownloadThread::retrySlot, Qt::QueuedConnection);
}

DownloadThread::~DownloadThread()
{
    qDebug() << "thread quit";
    delete m_d;
    m_thread.quit();
    bool ret = m_thread.wait(1000);
    if(ret == false)
    {
        qDebug() << "thread quit falied, now terminate it";
        m_thread.terminate();
        m_thread.wait();
    }
}

void DownloadThread::start()
{
    if(!m_thread.isRunning())
        m_thread.start();
    emit execute();
}

void DownloadThread::stop()
{
    m_d->stop();
}

bool DownloadThread::pause()
{
    return m_d->pause();
}

bool DownloadThread::resume()
{
    return m_d->resume();
}

bool DownloadThread::delTempFile()
{
    return m_d->delTempFile();
}

void DownloadThread::processSlot(qint64 dltotal, qint64 dlnow, double speed)
{
    emit process(dltotal, dlnow, speed, this);
}

void DownloadThread::finishSlot()
{
    emit finished(this);
}

void DownloadThread::errorOccuredSlot(int error)
{
    emit errorOccured(error, this);
}

void DownloadThread::downloadFlagSlot(int flag)
{
    emit downloadFlag(flag, this);
}

void DownloadThread::fileSizeSlot(qint64 size)
{
    emit fileSize(size, this);
}

void DownloadThread::retrySlot()
{
    emit retry(this);
}

QString DownloadThread::getFileName() const
{
    if(m_d != NULL)
        return m_d->getFileName();

    return INVALID_STRING;
}

bool DownloadThread::setFileName(const QString fileName)
{
    return m_d->setFileName(fileName);
}

QString DownloadThread::getFilePath() const
{
    if(m_d != NULL)
        return m_d->getFilePath();

    return INVALID_STRING;
}

QStringList DownloadThread::getUrlStr() const
{
    return m_urlStrLst;
}

void DownloadThread::setDir(const QString dir)
{
    m_d->setDir(dir);
}

QString DownloadThread::getDir() const
{
    return m_d->getDir();
}

DownloadFarm::DownlodState DownloadThread::getState() const
{
    return m_d->m_state;
}

DownloadControl::DownloadControl(QObject *parent):
    QObject(parent)
{
    curl_global_init(CURL_GLOBAL_ALL);
    m_dir = QApplication::applicationDirPath();
}

DownloadControl* DownloadControl::instance()
{
    static DownloadControl dc;
    return &dc;
}

DownloadControl::~DownloadControl()
{
    qDebug() << "~DownloadControl()";

    foreach(DownloadThread* dt, m_taskList)
    {
        disconnect(dt, &DownloadThread::finished, this, &DownloadControl::finished);
        disconnect(dt, &DownloadThread::process, this, &DownloadControl::process);
        disconnect(dt, &DownloadThread::errorOccured, this, &DownloadControl::errorOccured);
        disconnect(dt, &DownloadThread::downloadFlag, this, &DownloadControl::downloadFlag);
        disconnect(dt, &DownloadThread::fileSize, this, &DownloadControl::fileSize);
        disconnect(dt, &DownloadThread::retry, this, &DownloadControl::retry);

        dt->deleteLater();
    }

    curl_global_cleanup();
}

void DownloadControl::setDir(const QString &dir)
{
    m_dir = dir;
    QDir directory;
    directory.setPath(m_dir);
    if(!directory.exists())
    {
        directory.mkpath(m_dir);
    }
}

QString DownloadControl::getDir() const
{
    return m_dir;
}

bool DownloadControl::setTaskDir(DOWNLOAD_HANDLE handle, const QString dir)
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            qDebug() << "setTaskDir:" << handle->getFileName();
            handle->setDir(dir);
            return true;
        }
    }

    return false;
}

QString DownloadControl::getTaskDir(DOWNLOAD_HANDLE handle) const
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            return handle->getDir();
        }
    }

    return INVALID_STRING;
}

bool DownloadControl::setFileName(DOWNLOAD_HANDLE handle, const QString fileName)
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            return handle->setFileName(fileName);
        }
    }

    return false;
}

QString DownloadControl::getFileName(DOWNLOAD_HANDLE handle) const
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            return handle->getFileName();
        }
    }

    return INVALID_STRING;
}

QString DownloadControl::getFilePath(DOWNLOAD_HANDLE handle) const
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            return handle->getFilePath();
        }
    }

    return INVALID_STRING;
}

QStringList DownloadControl::getUrlStr(DOWNLOAD_HANDLE handle) const
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            return handle->getUrlStr();
        }
    }

    return QStringList();
}

int DownloadControl::getTaskCntDownloading() const
{
    int cnt = 0;
    foreach(DownloadThread* d, m_taskList)
    {
        if(d != NULL && (d->getState() == DownloadFarm::DS_downloading || d->getState() == DownloadFarm::DS_header))
        {
            cnt++;
        }
    }

    return cnt;
}

int DownloadControl::getTaskCntPaused() const
{
    int cnt = 0;
    foreach(DownloadThread* d, m_taskList)
    {
        if(d != NULL && d->getState() == DownloadFarm::DS_paused)
        {
            cnt++;
        }
    }

    return cnt;
}

// 任务创建后，是不会马上就下载的，需要调用startTask异步执行。
DOWNLOAD_HANDLE DownloadControl::createTask(const QStringList &urlStrLst)
{
    qDebug() << "createTask:" << urlStrLst;

    DownloadThread *d = new DownloadThread(urlStrLst, m_dir, this);
    connect(d, &DownloadThread::finished, this, &DownloadControl::finished);
    connect(d, &DownloadThread::process, this, &DownloadControl::process);
    connect(d, &DownloadThread::errorOccured, this, &DownloadControl::errorOccured);
    connect(d, &DownloadThread::downloadFlag, this, &DownloadControl::downloadFlag);
    connect(d, &DownloadThread::fileSize, this, &DownloadControl::fileSize);
    connect(d, &DownloadThread::retry, this, &DownloadControl::retry);

    m_taskList.append(d);
    return d;
}

// 销毁任务，释放任务在运行期间申请的资源
bool DownloadControl::deleteTask(DOWNLOAD_HANDLE handle)
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            qDebug() << "now delete task:" << handle->getFileName();
            handle->deleteLater();
            m_taskList.removeOne(handle);
            return true;
        }
    }

    return false;
}

// 开始下载。任务创建成功后，不会马上开始下载，需调用此接口才会开始下载。
bool DownloadControl::startTask(DOWNLOAD_HANDLE handle)
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            qDebug() << "startTask" << handle->getFileName();
            handle->start();
            return true;
        }
    }

    return false;
}

// 停止下载
bool DownloadControl::stopTask(DOWNLOAD_HANDLE handle)
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            qDebug() << "stopTask:" << handle->getFileName();
            handle->stop();
            return true;
        }
    }

    return false;
}

bool DownloadControl::pauseTask(DOWNLOAD_HANDLE handle)
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            qDebug() << "pauseTask:" << handle->getFileName();
            return handle->pause();
        }
    }

    return false;
}

bool DownloadControl::resumeTask(DOWNLOAD_HANDLE handle)
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            qDebug() << "resumeTask:" << handle->getFileName();
            return handle->resume();
        }
    }

    return false;
}

// 删除任务的临时文件。下载过程中会创建 .part 、 .part.cfg后缀的文件用来保存已下载的数据。
bool DownloadControl::delTempFile(DOWNLOAD_HANDLE handle)
{
    foreach(DownloadThread* d, m_taskList)
    {
        if(d == handle && handle != NULL)
        {
            qDebug() << "delTempFile:" << handle->getFileName();
            return handle->delTempFile();
        }
    }

    return false;
}
