#ifndef LIBCURLPARSER_H
#define LIBCURLPARSER_H

#include "curl.h"
#include <QThread>
#include <QFile>
#include <QUrl>
#include <QTime>
#include <QMutex>
#include <QStringList>

#define TIMEOUT_VALUE 300
#define INVALID_STRING "invalidstring"
class DownloadThread;
typedef DownloadThread*  DOWNLOAD_HANDLE;

class DownloadFarm : public QObject
{
    Q_OBJECT
public:
    static size_t writeDataFuncHeader(void* buffer, size_t size, size_t n, void *user);
    static int processFuncHeader(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    static size_t writeDataFunc(void* buffer, size_t size, size_t n, void *user);
    static int processFunc(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

    explicit DownloadFarm(const QStringList &urlStrLst, const QString dir = "", QObject *parent = 0);
    ~DownloadFarm();

    enum DownloadError{
        DE_custom = 1000,
        DE_destinationPathNotExists,  // 目标路径不存在
        DE_destinationSpaceNotEnough, // 目标路径空间不足
        DE_createFileFailed,          // 创建文件失败
        DE_tooManyRedirects,          // 重定向太多
    };

    typedef enum _DownlodFlag{
        DF_null,     // 空
        DF_stop,     // 手动退出
        DF_canPause, // 可以暂停
    }DownlodFlag;

    typedef enum _DownlodState{
        DS_ready,
        DS_header,
        DS_downloading,
        DS_paused,
    }DownlodState;

    QFile m_fileHeader;
    QFile m_file;
    QTime m_time;
    qint64   m_fileSize;       // 文件大小
    qint64   m_downloadSize;   // 已下载
    qint64   m_breakPoint;     // 断点
    qint64   m_lastTotalSize;  // 用于计算下载速度
    DownlodState m_state;
    DownlodFlag  m_flag;
    int m_error;
    bool m_stopFlag;

    void stop();
    bool pause();
    bool resume();
    bool delTempFile();

    void execute();

    bool setFileName(const QString fileName);
    QString getFileName() const;
    QString getFilePath() const;
    void setDir(const QString dir);
    QString getDir() const;
    QString getUrlStr() const;

signals:
    void process(qint64 dltotal, qint64 dlnow, double speed);
    void finished();
    void errorOccured(int error);
    void downloadFlag(int flag);
    void fileSize(qint64 size);
    void retry();

public slots:


private:
    CURL* m_curl;
    QUrl m_url;
    QString m_fileName;
    QString m_filePath;
    QStringList m_urlStrLst;
    QString m_urlStr;
    QString m_fileDir;

    QFile m_fileCfg;
    QString m_urlStrFirst;
    int   m_redirectCnt;
    QMutex m_mutex;
    int m_currentUrlIndex;
    int m_retryCnt;

    bool fileInit();
    void fileUninit();
    void finish();
    bool detectDiskSpace();

    bool responseCodeHandle(int responseCode);
    void nextUrl(int code);
};

class DownloadThread : public QObject
{
    Q_OBJECT
public:
    explicit DownloadThread(const QStringList &urlStrLst, const QString dir = "", QObject *parent = 0);
    ~DownloadThread();

    bool setFileName(const QString fileName);
    QString getFileName() const;
    QString getFilePath() const;
    QStringList getUrlStr() const;
    void setDir(const QString dir);
    QString getDir() const;

    DownloadFarm::DownlodState getState() const;

    void start();
    void stop();
    bool pause();
    bool resume();
    bool delTempFile();

signals:
    void execute();
    void process(qint64 dltotal, qint64 dlnow, double speed, DOWNLOAD_HANDLE handle);
    void finished(DOWNLOAD_HANDLE handle);
    void errorOccured(int error, DOWNLOAD_HANDLE handle);
    void downloadFlag(int flag, DOWNLOAD_HANDLE handle);
    void fileSize(qint64 size, DOWNLOAD_HANDLE handle);
    void retry(DOWNLOAD_HANDLE handle);

private slots:
    void processSlot(qint64 dltotal, qint64 dlnow, double speed);
    void finishSlot();
    void errorOccuredSlot(int error);
    void downloadFlagSlot(int flag);
    void fileSizeSlot(qint64 size);

    void retrySlot();

private:
    QThread m_thread;
    DownloadFarm *m_d;

    QStringList m_urlStrLst;
    QString m_fileDir;
};

class DownloadControl : public QObject
{
    Q_OBJECT
public:
    static DownloadControl *instance();
    ~DownloadControl();

    DOWNLOAD_HANDLE createTask(const QStringList &urlStrLst);
    bool deleteTask(DOWNLOAD_HANDLE handle);
    bool startTask(DOWNLOAD_HANDLE handle);
    bool stopTask(DOWNLOAD_HANDLE handle);
    bool pauseTask(DOWNLOAD_HANDLE handle);
    bool resumeTask(DOWNLOAD_HANDLE handle);
    bool delTempFile(DOWNLOAD_HANDLE handle);

    bool setTaskDir(DOWNLOAD_HANDLE handle, const QString dir);
    QString getTaskDir(DOWNLOAD_HANDLE handle) const;
    bool setFileName(DOWNLOAD_HANDLE handle, const QString fileName);
    QString getFileName(DOWNLOAD_HANDLE handle) const;
    QString getFilePath(DOWNLOAD_HANDLE handle) const;
    QStringList getUrlStr(DOWNLOAD_HANDLE handle) const;

    int getTaskCntDownloading() const;
    int getTaskCntPaused() const;

    void setDir(const QString &dir);
    QString getDir() const;

signals:
    void process(qint64 dltotal, qint64 dlnow, double speed, DOWNLOAD_HANDLE handle);
    void finished(DOWNLOAD_HANDLE handle);
    void errorOccured(int error, DOWNLOAD_HANDLE handle);
    void downloadFlag(int flag, DOWNLOAD_HANDLE handle);
    void fileSize(qint64 size, DOWNLOAD_HANDLE handle);
    void retry(DOWNLOAD_HANDLE handle);

private:
    explicit DownloadControl(QObject *parent = 0);
    DownloadControl(const DownloadControl &) Q_DECL_EQ_DELETE;
    DownloadControl &operator =(DownloadControl rhs) Q_DECL_EQ_DELETE;

    QString m_dir;
    QList<DownloadThread*> m_taskList;
};

#endif // LIBCURLPARSER_H
