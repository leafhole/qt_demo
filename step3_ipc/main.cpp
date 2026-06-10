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
// Qt 进程间通讯 (IPC) 演示
// ============================================================================
//
// 本示例演示如何使用 QLocalServer / QLocalSocket 实现进程间通讯
//
// 核心概念：
//   1. QLocalServer - 类似于 TCP 服务器，监听客户端连接
//   2. QLocalSocket - 类似于 TCP 套接字，用于发送/接收数据
//   3. 心跳机制 - 定期发送消息检测对方是否存活
//   4. 自动重启 - 检测到进程死亡后自动启动新进程
//
// 架构图：
//
//   ┌─────────────┐     连接      ┌─────────────┐
//   │   进程 A     │◄────────────►│   进程 B     │
//   │ (QLocalSrv) │              │ (QLocalSrv) │
//   └─────────────┘              └─────────────┘
//          │                            │
//          │         心跳包             │
//          └────────────────────────────┘
//                   ↕ ↕ ↕
//          ┌────────────────────────────┐
//          │      Unix Domain Socket    │
//          │   (同一机器高速通讯)       │
//          └────────────────────────────┘
//
// 使用方法：
//   ./ipc_demo A    # 启动进程 A
//   ./ipc_demo B    # 启动进程 B
//   ./ipc_demo C    # 启动进程 C
//
// ============================================================================

/**
 * ProcessMonitor - 进程监控器类
 *
 * 职责：
 *   1. 作为服务器监听其他进程的连接
 *   2. 作为客户端连接到其他进程的服务器
 *   3. 定期发送心跳包检测其他进程是否存活
 *   4. 检测到进程死亡后自动重启
 *
 * 成员变量说明：
 *   - m_processName: 当前进程的名称 (A/B/C)
 *   - m_server: 本地服务器，用于接收其他进程的连接
 *   - m_clientSockets: 连接到本进程的所有客户端socket列表
 *   - m_connections: 保存到其他进程的连接 (进程名 -> socket)
 *   - m_lastHeartbeat: 记录每个进程最后发送心跳的时间
 *   - m_lastAlive: 记录每个进程的存活状态
 *   - m_logEdit: 日志显示文本框
 *   - m_autoRestart: 自动重启复选框
 */
class ProcessMonitor : public QWidget
{
    Q_OBJECT  // 必须添加 Q_OBJECT 以支持信号槽

public:
    /**
     * 构造函数
     * @param processName 进程名称 (A/B/C)
     * @param parent 父窗口
     *
     * 初始化流程：
     *   1. 设置UI界面
     *   2. 启动本地服务器监听
     *   3. 启动心跳检测定时器
     */
    explicit ProcessMonitor(const QString& processName, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_processName(processName)
        , m_server(nullptr)
    {
        setupUI();          // 初始化UI界面
        setupServer();      // 启动服务器，监听连接
        startHeartbeatTimer();  // 启动心跳和检测定时器
    }

    /**
     * 析构函数 - 清理资源
     *
     * 清理内容：
     *   1. 断开并删除所有客户端socket
     *   2. 关闭本地服务器
     */
    ~ProcessMonitor()
    {
        // 遍历所有客户端连接，断开连接并释放内存
        for (QLocalSocket* socket : m_clientSockets) {
            socket->disconnectFromServer();  // 断开连接
            socket->deleteLater();           // 延迟删除
        }
        if (m_server) {
            m_server->close();              // 关闭服务器
            m_server->deleteLater();        // 延迟删除
        }
    }

    /**
     * sendHeartbeatTo - 向指定进程发送心跳包
     *
     * @param targetProcess 目标进程名称
     *
     * 工作流程：
     *   1. 查找与目标进程的socket连接
     *   2. 检查socket是否已连接
     *   3. 构造心跳消息 (包含时间戳和发送者名称)
     *   4. 通过socket发送消息
     *   5. 调用flush()确保消息立即发送
     *
     * 心跳消息格式：
     *   "[HH:MM:SS] HEARTBEAT from X"
     *   例如: "[14:30:25] HEARTBEAT from A"
     */
    void sendHeartbeatTo(const QString& targetProcess)
    {
        // 检查是否存在到目标进程的连接
        if (m_connections.contains(targetProcess)) {
            QLocalSocket* socket = m_connections[targetProcess];
            // 确保socket有效且处于已连接状态
            if (socket && socket->state() == QLocalSocket::ConnectedState) {
                // 构造心跳消息
                QString msg = QString("[%1] HEARTBEAT from %2")
                                 .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                                 .arg(m_processName);
                // 发送数据 (需要转换为字节数组)
                socket->write(msg.toUtf8());
                // 立即刷新缓冲区，确保数据发送出去
                socket->flush();
                logMessage(QString("发送心跳到 %1").arg(targetProcess));
            }
        }
    }

    /**
     * connectToProcess - 连接到其他进程的服务器
     *
     * @param targetProcess 目标进程名称
     *
     * 工作流程：
     *   1. 根据目标进程名称构造服务器名称
     *      例如: "ipc_demo_B" 表示进程B的服务器
     *   2. 创建新的socket
     *   3. 设置信号槽连接 (connected/readyRead/disconnected)
     *   4. 尝试连接到目标进程的服务器
     *
     * 信号槽说明：
     *   - connected:       连接成功时触发
     *   - readyRead:       收到数据时触发
     *   - disconnected:    连接断开时触发
     *
     * 服务器命名规则：
     *   每个进程的服务器名称 = "ipc_demo_" + 进程名称
     */
    void connectToProcess(const QString& targetProcess)
    {
        // 构造目标进程的服务器名称
        // 例如: 进程B的服务器名称为 "ipc_demo_B"
        QString serverName = QString("ipc_demo_%1").arg(targetProcess);

        // 创建新的socket用于连接
        QLocalSocket* socket = new QLocalSocket(this);

        /**
         * connected 信号 - 连接成功
         *
         * 触发时机：成功与目标进程的服务器建立连接
         * 执行操作：
         *   1. 保存socket连接到m_connections映射
         *   2. 记录连接建立时间
         *   3. 输出日志
         */
        connect(socket, &QLocalSocket::connected, this, [this, socket, targetProcess]() {
            m_connections[targetProcess] = socket;
            m_lastHeartbeat[targetProcess] = QDateTime::currentDateTime();
            logMessage(QString("连接到进程 %1 成功").arg(targetProcess));
        });

        /**
         * readyRead 信号 - 收到数据
         *
         * 触发时机：socket收到对方发送的数据
         * 执行操作：
         *   1. 读取所有可用数据 (readAll)
         *   2. 转换为字符串
         *   3. 更新最后心跳时间
         *   4. 标记进程为存活状态
         */
        connect(socket, &QLocalSocket::readyRead, this, [this, socket, targetProcess]() {
            // 读取所有收到的数据
            QByteArray data = socket->readAll();
            // 转换为UTF-8字符串
            QString msg = QString::fromUtf8(data);
            logMessage(QString("收到来自 %1: %2").arg(targetProcess).arg(msg));

            // 更新最后心跳时间，用于判断进程是否存活
            m_lastHeartbeat[targetProcess] = QDateTime::currentDateTime();
            m_lastAlive[targetProcess] = true;
        });

        /**
         * disconnected 信号 - 连接断开
         *
         * 触发时机：与目标进程的连接断开
         * 执行操作：
         *   1. 从连接映射中移除
         *   2. 标记进程为非存活状态
         */
        connect(socket, &QLocalSocket::disconnected, this, [this, targetProcess]() {
            logMessage(QString("与进程 %1 断开连接").arg(targetProcess));
            m_connections.remove(targetProcess);
            m_lastAlive[targetProcess] = false;
        });

        // 开始连接到服务器
        socket->connectToServer(serverName);
    }

    /**
     * startProcess - 启动另一个进程
     *
     * @param targetProcess 要启动的进程名称
     *
     * 工作流程：
     *   1. 创建 QProcess 对象
     *   2. 设置进程结束信号槽 (检测崩溃)
     *   3. 启动进程，传递进程名称作为命令行参数
     *
     * QProcess 说明：
     *   - QProcess 用于启动外部程序
     *   - start() 异步启动，不会阻塞
     *   - finished 信号在进程结束时触发
     *
     * 崩溃检测：
     *   如果进程以 CrashExit 状态结束，说明进程异常崩溃
     *   此时自动调用 startProcess 重新启动
     */
    void startProcess(const QString& targetProcess)
    {
        // 命令行参数 = 进程名称
        QStringList args;
        args << targetProcess;

        // 创建进程对象
        QProcess* process = new QProcess(this);

        /**
         * finished 信号 - 进程结束
         *
         * 参数：
         *   - exitCode: 进程退出码
         *   - exitStatus: 退出状态 (NormalExit/CrashExit)
         *
         * CrashExit 说明：
         *   表示进程异常退出 (崩溃、被信号终止等)
         *   正常退出会是 NormalExit
         */
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, targetProcess](int exitCode, QProcess::ExitStatus exitStatus) {
            // 检测到崩溃
            if (exitStatus == QProcess::CrashExit) {
                logMessage(QString("进程 %1 崩溃，正在重启...").arg(targetProcess));
                // 延迟1秒后重启
                QTimer::singleShot(1000, this, [this, targetProcess]() {
                    startProcess(targetProcess);
                });
            }
        });

        // 启动进程，传递命令行参数
        process->start(QCoreApplication::applicationFilePath(), args);
        logMessage(QString("启动进程 %1").arg(targetProcess));
    }

signals:
    /**
     * processDied - 进程死亡信号
     *
     * 触发时机：检测到某个进程超过5秒没有发送心跳
     * 用途：通知UI更新状态，触发自动重启
     */
    void processDied(const QString& processName);

    /**
     * processAlive - 进程恢复存活信号
     *
     * 触发时机：之前死亡的进程重新发送心跳
     * 用途：通知UI更新状态
     */
    void processAlive(const QString& processName);

private slots:
    /**
     * checkOtherProcesses - 检查其他进程是否存活
     *
     * 工作原理：
     *   每2秒执行一次，检查所有已知进程的最后心跳时间
     *
     * 存活判断标准：
     *   当前时间 - 最后心跳时间 < 5秒 → 存活
     *   当前时间 - 最后心跳时间 >= 5秒 → 死亡
     *
     * 状态转换：
     *   存活 → 死亡：输出警告，发送 processDied 信号
     *   死亡 → 存活：输出恢复消息，发送 processAlive 信号
     */
    void checkOtherProcesses()
    {
        // 获取所有进程列表 (A, B, C)
        QStringList allProcesses = {"A", "B", "C"};
        // 移除自己，不检查自己
        allProcesses.removeAll(m_processName);

        // 遍历所有其他进程
        for (const QString& proc : allProcesses) {
            // 只检查已连接的进程
            if (m_lastHeartbeat.contains(proc)) {
                QDateTime lastBeat = m_lastHeartbeat[proc];
                // 计算距离最后心跳过去了多少秒
                qint64 secondsSince = lastBeat.secsTo(QDateTime::currentDateTime());

                // ===== 死亡检测 =====
                if (secondsSince > 5) {  // 超过5秒没收到心跳
                    // 如果之前是存活状态
                    if (m_lastAlive.value(proc, true)) {
                        logMessage(QString("进程 %1 已死亡! (最后心跳: %2秒前)")
                                     .arg(proc).arg(secondsSince));
                        m_lastAlive[proc] = false;  // 更新存活状态
                        emit processDied(proc);       // 发送死亡信号
                    }
                } else {
                    // ===== 恢复检测 =====
                    // 如果之前是死亡状态，现在恢复了
                    if (!m_lastAlive.value(proc, false)) {
                        logMessage(QString("进程 %1 恢复存活").arg(proc));
                        m_lastAlive[proc] = true;
                        emit processAlive(proc);
                    }
                }
            }
        }
    }

    /**
     * sendHeartbeats - 向所有已连接的进程发送心跳
     *
     * 工作流程：
     *   每1秒执行一次
     *   遍历所有其他进程，调用 sendHeartbeatTo()
     *
     * 注意：
     *   如果socket未连接，sendHeartbeatTo()会静默忽略
     */
    void sendHeartbeats()
    {
        QStringList allProcesses = {"A", "B", "C"};
        allProcesses.removeAll(m_processName);

        for (const QString& proc : allProcesses) {
            sendHeartbeatTo(proc);
        }
    }

private:
    /**
     * setupUI - 初始化用户界面
     *
     * 界面布局：
     *   ┌─────────────────────────────────────┐
     *   │  === 进程 A ===                     │ ← 标题
     *   ├─────────────────────────────────────┤
     *   │  进程A: 未连接  进程B: 未连接       │ ← 状态
     *   ├─────────────────────────────────────┤
     *   │  日志:                              │ ← 日志区
     *   │  [14:30:25] 连接到进程B成功        │
     *   │  [14:30:26] 发送心跳到B            │
     *   ├─────────────────────────────────────┤
     *   │  [启动A] [启动B] [启动C]           │ ← 按钮
     *   ├─────────────────────────────────────┤
     *   │  ☑ 自动重启死亡的进程              │ ← 复选框
     *   └─────────────────────────────────────┘
     */
    void setupUI()
    {
        setWindowTitle(QString("进程 %1 - IPC 演示").arg(m_processName));
        resize(500, 400);

        // 主布局 (垂直)
        QVBoxLayout* layout = new QVBoxLayout(this);

        // ===== 标题栏 =====
        QLabel* title = new QLabel(QString("=== 进程 %1 ===").arg(m_processName));
        title->setStyleSheet("font-size: 18px; font-weight: bold; color: #2c3e50;");
        layout->addWidget(title);

        // ===== 进程状态显示 =====
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

        // ===== 日志显示区 =====
        QLabel* logLabel = new QLabel("日志:");
        layout->addWidget(logLabel);

        m_logEdit = new QTextEdit();
        m_logEdit->setReadOnly(true);  // 只读
        m_logEdit->setStyleSheet("background-color: #1e1e1e; color: #00ff00; font-family: monospace;");
        layout->addWidget(m_logEdit);

        // ===== 控制按钮 =====
        QHBoxLayout* btnLayout = new QHBoxLayout();
        QPushButton* startABtn = new QPushButton("启动进程 A");
        QPushButton* startBBtn = new QPushButton("启动进程 B");
        QPushButton* startCBtn = new QPushButton("启动进程 C");

        // 按钮点击事件
        connect(startABtn, &QPushButton::clicked, this, [this]() { startProcess("A"); });
        connect(startBBtn, &QPushButton::clicked, this, [this]() { startProcess("B"); });
        connect(startCBtn, &QPushButton::clicked, this, [this]() { startProcess("C"); });

        btnLayout->addWidget(startABtn);
        btnLayout->addWidget(startBBtn);
        btnLayout->addWidget(startCBtn);
        layout->addLayout(btnLayout);

        // ===== 自动重启复选框 =====
        m_autoRestart = new QCheckBox("自动重启死亡的进程");
        m_autoRestart->setChecked(true);  // 默认启用
        layout->addWidget(m_autoRestart);

        /**
         * processDied 信号处理
         *
         * 当检测到进程死亡时：
         *   1. 输出死亡日志
         *   2. 如果启用自动重启，延迟2秒后重启进程
         */
        connect(this, &ProcessMonitor::processDied, this, [this](const QString& proc) {
            logMessage(QString("进程 %1 已死亡!").arg(proc));
            if (m_autoRestart->isChecked()) {
                // 延迟2秒后执行重启
                QTimer::singleShot(2000, this, [this, proc]() {
                    logMessage(QString("正在自动重启 %1...").arg(proc));
                    startProcess(proc);
                });
            }
        });
    }

    /**
     * setupServer - 启动本地服务器
     *
     * 工作流程：
     *   1. 构造服务器名称 (ipc_demo_进程名)
     *   2. 移除可能存在的旧服务器
     *   3. 创建 QLocalServer 并监听
     *   4. 连接 newConnection 信号处理客户端连接
     *
     * 服务器名称冲突说明：
     *   QLocalServer::removeServer() 用于清理之前的服务器
     *   否则可能导致 "Server already running" 错误
     *
     * newConnection 信号：
     *   当有客户端连接时触发
     *   调用 nextPendingConnection() 获取客户端socket
     */
    void setupServer()
    {
        // 服务器名称：ipc_demo_A, ipc_demo_B, ipc_demo_C
        QString serverName = QString("ipc_demo_%1").arg(m_processName);

        // 如果同名服务器已存在，先移除
        // 这在进程异常退出时很重要
        QLocalServer::removeServer(serverName);

        // 创建服务器
        m_server = new QLocalServer(this);

        /**
         * newConnection 信号 - 新客户端连接
         *
         * 触发时机：其他进程尝试连接本进程
         * 执行操作：
         *   1. 获取挂起的连接 (nextPendingConnection)
         *   2. 添加到客户端列表
         *   3. 设置数据接收和断开处理
         */
        connect(m_server, &QLocalServer::newConnection, this, [this]() {
            // 获取客户端socket
            QLocalSocket* socket = m_server->nextPendingConnection();
            if (socket) {
                logMessage("收到新的连接请求");
                m_clientSockets.append(socket);

                // 数据到达信号
                connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
                    QByteArray data = socket->readAll();
                    QString msg = QString::fromUtf8(data);
                    logMessage(QString("收到消息: %1").arg(msg));
                });

                // 连接断开信号
                connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
                    m_clientSockets.removeAll(socket);
                    socket->deleteLater();
                });
            }
        });

        // 开始监听
        if (m_server->listen(serverName)) {
            logMessage(QString("服务器启动成功，监听: %1").arg(serverName));
        } else {
            logMessage(QString("服务器启动失败: %1").arg(m_server->errorString()));
        }
    }

    /**
     * startHeartbeatTimer - 启动心跳定时器
     *
     * 定时器设置：
     *   1. checkTimer: 每2秒检查其他进程状态
     *   2. heartbeatTimer: 每1秒发送心跳
     *   3. 延迟3秒后开始连接到其他进程
     *
     * 为什么延迟连接？
     *   确保每个进程先启动服务器，再尝试连接
     *   否则可能出现连接失败的情况
     */
    void startHeartbeatTimer()
    {
        // ===== 进程存活检查定时器 =====
        // 每2秒检查一次
        QTimer* checkTimer = new QTimer(this);
        connect(checkTimer, &QTimer::timeout, this, &ProcessMonitor::checkOtherProcesses);
        checkTimer->start(2000);  // 2000ms = 2秒

        // ===== 心跳发送定时器 =====
        // 每1秒发送一次心跳
        QTimer* heartbeatTimer = new QTimer(this);
        connect(heartbeatTimer, &QTimer::timeout, this, &ProcessMonitor::sendHeartbeats);
        heartbeatTimer->start(1000);  // 1000ms = 1秒

        // ===== 延迟连接 =====
        // 延迟3秒后连接到其他进程
        // 等待服务器完全启动
        QTimer::singleShot(3000, this, [this]() {
            logMessage("开始连接到其他进程...");
            connectToProcess("A");
            connectToProcess("B");
            connectToProcess("C");
        });
    }

    /**
     * logMessage - 输出日志消息
     *
     * @param msg 日志内容
     *
     * 功能：
     *   1. 在UI文本框显示 (带时间戳)
     *   2. 在控制台输出 (用于调试)
     */
    void logMessage(const QString& msg)
    {
        QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
        m_logEdit->append(QString("[%1] %2").arg(time).arg(msg));
        qDebug() << QString("[%1] %2").arg(m_processName).arg(msg);
    }

private:
    QString m_processName;              // 当前进程名称 (A/B/C)
    QLocalServer* m_server;            // 本地服务器
    QList<QLocalSocket*> m_clientSockets;  // 连接到本进程的客户端列表
    QMap<QString, QLocalSocket*> m_connections;  // 到其他进程的连接
    QMap<QString, QDateTime> m_lastHeartbeat;    // 每进程最后心跳时间
    QMap<QString, bool> m_lastAlive;  // 每进程存活状态
    QTextEdit* m_logEdit;              // 日志显示区
    QCheckBox* m_autoRestart;          // 自动重启复选框
};

// ============================================================================
// 主函数入口
// ============================================================================
//
// 使用方式：
//   ./ipc_demo        # 默认启动进程 A
//   ./ipc_demo A     # 启动进程 A
//   ./ipc_demo B     # 启动进程 B
//   ./ipc_demo C     # 启动进程 C
//
// argc: 命令行参数个数 (包括程序名)
// argv: 命令行参数数组
//   argv[0] = 程序路径
//   argv[1] = 进程名称 (可选)
//
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 从命令行参数获取进程名称
    // 默认使用 "A"
    QString processName = "A";
    if (argc > 1) {
        // 如果提供了参数，使用第一个参数作为进程名
        processName = QString(argv[1]);
    }

    // 创建并显示主窗口
    ProcessMonitor monitor(processName);
    monitor.show();

    // 进入Qt事件循环
    return app.exec();
}

// ============================================================================
// Qt 元对象编译器 (MOC) 必需
// ============================================================================
//
// Q_OBJECT 宏：
//   - 启用元对象系统 (信号槽、动态属性)
//   - 必须添加 #include "main.moc"
//   - qmake 会自动生成 main.moc 文件
//
// 编译过程：
//   1. qmake 生成 Makefile
//   2. moc 解析 Q_OBJECT 生成 main.moc
//   3. 编译器编译 main.moc
//   4. 链接器链接生成的代码
//
// ============================================================================
#include "main.moc"
