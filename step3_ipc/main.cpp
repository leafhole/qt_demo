#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QDebug>
#include <QDateTime>
#include <QLabel>
#include <QCheckBox>
#include <QHBoxLayout>

// ============================================================================
// Qt 进程间通讯演示
// ============================================================================
// 使用 QLocalServer / QLocalSocket 实现 Unix 域套接字通讯
// ============================================================================

class ProcessMonitor : public QWidget
{
    Q_OBJECT

public:
    explicit ProcessMonitor(const QString& processName, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_processName(processName)
        , m_server(nullptr)
    {
        setupUI();
        setupServer();
        startHeartbeatTimer();
    }

    ~ProcessMonitor()
    {
        // 清理：关闭所有连接的 socket
        for (QLocalSocket* socket : m_clientSockets) {
            socket->disconnectFromServer();
            socket->deleteLater();
        }
        if (m_server) {
            m_server->close();
            m_server->deleteLater();
        }
    }

    // 发送心跳给指定进程
    void sendHeartbeatTo(const QString& targetProcess)
    {
        if (m_connections.contains(targetProcess)) {
            QLocalSocket* socket = m_connections[targetProcess];
            if (socket && socket->state() == QLocalSocket::ConnectedState) {
                QString msg = QString("[%1] HEARTBEAT from %2")
                                 .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                                 .arg(m_processName);
                socket->write(msg.toUtf8());
                socket->flush();
                logMessage(QString("发送心跳到 %1").arg(targetProcess));
            }
        }
    }

    // 连接到其他进程
    void connectToProcess(const QString& targetProcess)
    {
        QString serverName = QString("ipc_demo_%1").arg(targetProcess);

        QLocalSocket* socket = new QLocalSocket(this);
        connect(socket, &QLocalSocket::connected, this, [this, socket, targetProcess]() {
            m_connections[targetProcess] = socket;
            m_lastHeartbeat[targetProcess] = QDateTime::currentDateTime();
            logMessage(QString("连接到进程 %1 成功").arg(targetProcess));
        });

        connect(socket, &QLocalSocket::readyRead, this, [this, socket, targetProcess]() {
            QByteArray data = socket->readAll();
            QString msg = QString::fromUtf8(data);
            logMessage(QString("收到来自 %1: %2").arg(targetProcess).arg(msg));

            // 更新最后心跳时间
            m_lastHeartbeat[targetProcess] = QDateTime::currentDateTime();
            m_lastAlive[targetProcess] = true;
        });

        connect(socket, &QLocalSocket::disconnected, this, [this, targetProcess]() {
            logMessage(QString("与进程 %1 断开连接").arg(targetProcess));
            m_connections.remove(targetProcess);
            m_lastAlive[targetProcess] = false;
        });

        socket->connectToServer(serverName);
    }

    // 启动其他进程
    void startProcess(const QString& targetProcess)
    {
        QStringList args;
        args << targetProcess;

        QProcess* process = new QProcess(this);
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, targetProcess](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::CrashExit) {
                logMessage(QString("进程 %1 崩溃，正在重启...").arg(targetProcess));
                QTimer::singleShot(1000, this, [this, targetProcess]() {
                    startProcess(targetProcess);
                });
            }
        });

        process->start(QCoreApplication::applicationFilePath(), args);
        logMessage(QString("启动进程 %1").arg(targetProcess));
    }

signals:
    void processDied(const QString& processName);
    void processAlive(const QString& processName);

private slots:
    // 检查其他进程是否还活着
    void checkOtherProcesses()
    {
        QStringList allProcesses = {"A", "B", "C"};
        allProcesses.removeAll(m_processName);

        for (const QString& proc : allProcesses) {
            if (m_lastHeartbeat.contains(proc)) {
                QDateTime lastBeat = m_lastHeartbeat[proc];
                qint64 secondsSince = lastBeat.secsTo(QDateTime::currentDateTime());

                if (secondsSince > 5) {  // 超过5秒没收到心跳
                    if (m_lastAlive.value(proc, true)) {
                        logMessage(QString("进程 %1 已死亡! (最后心跳: %2秒前)")
                                     .arg(proc).arg(secondsSince));
                        m_lastAlive[proc] = false;
                        emit processDied(proc);
                    }
                } else {
                    if (!m_lastAlive.value(proc, false)) {
                        logMessage(QString("进程 %1 恢复存活").arg(proc));
                        m_lastAlive[proc] = true;
                        emit processAlive(proc);
                    }
                }
            }
        }
    }

    // 发送心跳
    void sendHeartbeats()
    {
        QStringList allProcesses = {"A", "B", "C"};
        allProcesses.removeAll(m_processName);

        for (const QString& proc : allProcesses) {
            sendHeartbeatTo(proc);
        }
    }

private:
    void setupUI()
    {
        setWindowTitle(QString("进程 %1 - IPC 演示").arg(m_processName));
        resize(500, 400);

        QVBoxLayout* layout = new QVBoxLayout(this);

        // 标题
        QLabel* title = new QLabel(QString("=== 进程 %1 ===").arg(m_processName));
        title->setStyleSheet("font-size: 18px; font-weight: bold; color: #2c3e50;");
        layout->addWidget(title);

        // 状态显示
        QHBoxLayout* statusLayout = new QHBoxLayout();
        QLabel* aStatus = new QLabel("进程 A: <span style='color:gray'>未连接</span>");
        QLabel* bStatus = new QLabel("进程 B: <span style='color:gray'>未连接</span>");
        QLabel* cStatus = new QLabel("进程 C: <span style='color:gray'>未连接</span>");
        aStatus->setObjectName("statusA");
        bStatus->setObjectName("statusB");
        cStatus->setObjectName("statusC");
        statusLayout->addWidget(aStatus);
        statusLayout->addWidget(bStatus);
        statusLayout->addWidget(cStatus);
        layout->addLayout(statusLayout);

        // 日志显示
        QLabel* logLabel = new QLabel("日志:");
        layout->addWidget(logLabel);

        m_logEdit = new QTextEdit();
        m_logEdit->setReadOnly(true);
        m_logEdit->setStyleSheet("background-color: #1e1e1e; color: #00ff00; font-family: monospace;");
        layout->addWidget(m_logEdit);

        // 控制按钮
        QHBoxLayout* btnLayout = new QHBoxLayout();
        QPushButton* startABtn = new QPushButton("启动进程 A");
        QPushButton* startBBtn = new QPushButton("启动进程 B");
        QPushButton* startCBtn = new QPushButton("启动进程 C");

        connect(startABtn, &QPushButton::clicked, this, [this]() { startProcess("A"); });
        connect(startBBtn, &QPushButton::clicked, this, [this]() { startProcess("B"); });
        connect(startCBtn, &QPushButton::clicked, this, [this]() { startProcess("C"); });

        btnLayout->addWidget(startABtn);
        btnLayout->addWidget(startBBtn);
        btnLayout->addWidget(startCBtn);
        layout->addLayout(btnLayout);

        // 自动重启按钮
        m_autoRestart = new QCheckBox("自动重启死亡的进程");
        m_autoRestart->setChecked(true);
        layout->addWidget(m_autoRestart);

        connect(this, &ProcessMonitor::processDied, this, [this](const QString& proc) {
            logMessage(QString("进程 %1 已死亡!").arg(proc));
            if (m_autoRestart->isChecked()) {
                QTimer::singleShot(2000, this, [this, proc]() {
                    logMessage(QString("正在自动重启 %1...").arg(proc));
                    startProcess(proc);
                });
            }
        });
    }

    void setupServer()
    {
        // 监听其他进程的连接
        QString serverName = QString("ipc_demo_%1").arg(m_processName);

        // 如果服务器已存在，先移除
        QLocalServer::removeServer(serverName);

        m_server = new QLocalServer(this);
        connect(m_server, &QLocalServer::newConnection, this, [this]() {
            QLocalSocket* socket = m_server->nextPendingConnection();
            if (socket) {
                logMessage("收到新的连接请求");
                m_clientSockets.append(socket);

                connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
                    QByteArray data = socket->readAll();
                    QString msg = QString::fromUtf8(data);
                    logMessage(QString("收到消息: %1").arg(msg));
                });

                connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
                    m_clientSockets.removeAll(socket);
                    socket->deleteLater();
                });
            }
        });

        if (m_server->listen(serverName)) {
            logMessage(QString("服务器启动成功，监听: %1").arg(serverName));
        } else {
            logMessage(QString("服务器启动失败: %1").arg(m_server->errorString()));
        }
    }

    void startHeartbeatTimer()
    {
        // 每2秒检查一次其他进程状态
        QTimer* checkTimer = new QTimer(this);
        connect(checkTimer, &QTimer::timeout, this, &ProcessMonitor::checkOtherProcesses);
        checkTimer->start(2000);

        // 每1秒发送一次心跳
        QTimer* heartbeatTimer = new QTimer(this);
        connect(heartbeatTimer, &QTimer::timeout, this, &ProcessMonitor::sendHeartbeats);
        heartbeatTimer->start(1000);

        // 延迟3秒后连接到其他进程
        QTimer::singleShot(3000, this, [this]() {
            logMessage("开始连接到其他进程...");
            connectToProcess("A");
            connectToProcess("B");
            connectToProcess("C");
        });
    }

    void logMessage(const QString& msg)
    {
        QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
        m_logEdit->append(QString("[%1] %2").arg(time).arg(msg));
        qDebug() << QString("[%1] %2").arg(m_processName).arg(msg);
    }
private:
    QString m_processName;
    QLocalServer* m_server;
    QList<QLocalSocket*> m_clientSockets;
    QMap<QString, QLocalSocket*> m_connections;
    QMap<QString, QDateTime> m_lastHeartbeat;
    QMap<QString, bool> m_lastAlive;
    QTextEdit* m_logEdit;
    QCheckBox* m_autoRestart;
};

// ============================================================================
// 主函数：根据命令行参数决定启动哪个进程
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 获取进程名称（命令行第一个参数）
    QString processName = "A";  // 默认进程 A
    if (argc > 1) {
        processName = QString(argv[1]);
    }

    ProcessMonitor monitor(processName);
    monitor.show();

    return app.exec();
}

#include "main.moc"
