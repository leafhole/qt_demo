QT += core gui widgets

# 三个进程共享同一个项目文件，通过命令行参数区分
greaterThan(QT_MAJOR_VERSION, 4) {
    QT += network
} else {
    QT += network
}

TARGET = ipc_demo
TEMPLATE = app

SOURCES += main.cpp
