#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QTimer>
#include <QDebug>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QDateTime>
#include <random>

std::mt19937 rng;

inline int randInt(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

class ParticleItem : public QGraphicsEllipseItem
{
public:
    ParticleItem(qreal x, qreal y, qreal size, const QColor& color)
        : QGraphicsEllipseItem(-size/2, -size/2, size, size),
          m_vx((randInt(-100, 100)) / 50.0),
          m_vy((randInt(-100, 100)) / 50.0),
          m_size(size)
    {
        setPos(x, y);
        setBrush(QBrush(color));
        setPen(Qt::NoPen);
        setZValue(1);
    }

    void updatePosition(qreal width, qreal height)
    {
        QPointF pos = this->pos();
        pos.setX(pos.x() + m_vx);
        pos.setY(pos.y() + m_vy);
        if (pos.x() < m_size || pos.x() > width - m_size) {
            m_vx = -m_vx;
            pos.setX(qMax(m_size, qMin(width - m_size, pos.x())));
        }
        if (pos.y() < m_size || pos.y() > height - m_size) {
            m_vy = -m_vy;
            pos.setY(qMax(m_size, qMin(height - m_size, pos.y())));
        }
        setPos(pos);
    }

private:
    qreal m_vx, m_vy;
    qreal m_size;
};

class FpsCounter : public QGraphicsTextItem
{
public:
    FpsCounter(QGraphicsItem* parent = nullptr) : QGraphicsTextItem(parent)
    {
        setDefaultTextColor(QColor(255, 255, 255));
        setFont(QFont("Arial", 14, QFont::Bold));
        setZValue(100);
        m_frameCount = 0;
        m_lastTime = QDateTime::currentMSecsSinceEpoch();
    }

    void tick()
    {
        m_frameCount++;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 delta = now - m_lastTime;
        if (delta >= 1000) {
            m_fps = m_frameCount * 1000.0 / delta;
            setPlainText(QString("FPS: %1").arg(m_fps, 0, 'f', 1));
            m_frameCount = 0;
            m_lastTime = now;
        }
    }

private:
    int m_frameCount;
    qint64 m_lastTime;
    double m_fps = 0;
};

// 创建场景的辅助函数
QGraphicsScene* createScene(int width, int height, int particleCount, FpsCounter*& fpsCounter)
{
    QGraphicsScene* scene = new QGraphicsScene();
    scene->setSceneRect(0, 0, width, height);

    QGraphicsRectItem* bg = new QGraphicsRectItem(0, 0, width, height);
    bg->setBrush(QBrush(QColor(20, 20, 40)));
    bg->setPen(Qt::NoPen);
    scene->addItem(bg);

    for (int i = 0; i < particleCount; ++i) {
        qreal x = randInt(0, width - 1);
        qreal y = randInt(0, height - 1);
        qreal size = 5 + randInt(0, 15);
        QColor color = QColor::fromHsl(randInt(0, 359), 200, 128);
        ParticleItem* p = new ParticleItem(x, y, size, color);
        scene->addItem(p);
    }

    fpsCounter = new FpsCounter();
    fpsCounter->setPos(10, 10);
    scene->addItem(fpsCounter);

    return scene;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    rng.seed(std::random_device{}());

    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    const int WIDTH = 800;
    const int HEIGHT = 600;
    const int PARTICLE_COUNT = 2000;  // 大幅增加粒子数量

    // ========== 场景1：OpenGL 加速窗口 ==========
    FpsCounter* glFps = nullptr;
    QGraphicsScene* glScene = createScene(WIDTH, HEIGHT, PARTICLE_COUNT, glFps);
    QList<QGraphicsItem*> glParticles = glScene->items();

    QGraphicsView glView(glScene);
    QOpenGLWidget* glWidget = new QOpenGLWidget();
    glView.setViewport(glWidget);
    glView.setRenderHint(QPainter::Antialiasing);
    glView.setWindowTitle(QString("[OpenGL 加速] %1个粒子").arg(PARTICLE_COUNT));
    glView.resize(WIDTH + 50, HEIGHT + 50);
    glView.show();

    // ========== 场景2：软件渲染窗口 ==========
    // 使用独立场景，确保公平对比
    FpsCounter* noGlFps = nullptr;
    QGraphicsScene* noGlScene = createScene(WIDTH, HEIGHT, PARTICLE_COUNT, noGlFps);
    QList<QGraphicsItem*> noGlParticles = noGlScene->items();

    QGraphicsView noGlView(noGlScene);
    // 不设置 viewport，使用默认软件渲染
    noGlView.setRenderHint(QPainter::Antialiasing);
    noGlView.setWindowTitle(QString("[软件渲染] %1个粒子").arg(PARTICLE_COUNT));
    noGlView.resize(WIDTH + 50, HEIGHT + 50);
    noGlView.move(WIDTH + 60, 0);
    noGlView.show();

    // 更新定时器
    QTimer updateTimer;
    QObject::connect(&updateTimer, &QTimer::timeout, [&]() {
        for (QGraphicsItem* item : glParticles) {
            ParticleItem* p = dynamic_cast<ParticleItem*>(item);
            if (p) p->updatePosition(WIDTH, HEIGHT);
        }
        glFps->tick();

        for (QGraphicsItem* item : noGlParticles) {
            ParticleItem* p = dynamic_cast<ParticleItem*>(item);
            if (p) p->updatePosition(WIDTH, HEIGHT);
        }
        noGlFps->tick();
    });
    updateTimer.start(16);

    qDebug() << "=== OpenGL vs 软件渲染对比演示 ===";
    qDebug() << "· 粒子数量:" << PARTICLE_COUNT;
    qDebug() << "· 左侧: OpenGL 硬件加速 (GPU)";
    qDebug() << "· 右侧: 软件渲染 (CPU)";
    qDebug() << "· 预计: OpenGL 帧率显著高于软件渲染";

    return app.exec();
}