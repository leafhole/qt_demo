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

inline qreal randReal(qreal min, qreal max) {
    std::uniform_real_distribution<qreal> dist(min, max);
    return dist(rng);
}

// 动态粒子类 - 用于展示大量元素的渲染
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

        // 边界反弹
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

// 帧率显示类
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

    double getFps() const { return m_fps; }

private:
    int m_frameCount;
    qint64 m_lastTime;
    double m_fps = 0;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 初始化随机数生成器
    rng.seed(std::random_device{}());

    // 设置 OpenGL 配置
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    const int SCENE_WIDTH = 800;
    const int SCENE_HEIGHT = 600;
    const int PARTICLE_COUNT = 500;  // 大量粒子，压力测试

    // 创建舞台（Scene）
    QGraphicsScene scene;
    scene.setSceneRect(0, 0, SCENE_WIDTH, SCENE_HEIGHT);

    // 背景
    QGraphicsRectItem* bg = new QGraphicsRectItem(0, 0, SCENE_WIDTH, SCENE_HEIGHT);
    bg->setBrush(QBrush(QColor(20, 20, 40)));
    bg->setPen(Qt::NoPen);
    scene.addItem(bg);

    // 添加大量动态粒子
    QList<ParticleItem*> particles;
    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        qreal x = randInt(0, SCENE_WIDTH - 1);
        qreal y = randInt(0, SCENE_HEIGHT - 1);
        qreal size = 5 + randInt(0, 15);
        QColor color = QColor::fromHsl(randInt(0, 359), 200, 128);
        ParticleItem* p = new ParticleItem(x, y, size, color);
        particles.append(p);
        scene.addItem(p);
    }

    // ========== 窗口1：使用 OpenGL 加速 ==========
    QGraphicsView glView(&scene);
    QOpenGLWidget* glWidget = new QOpenGLWidget();
    glView.setViewport(glWidget);  // 使用 OpenGL viewport
    glView.setRenderHint(QPainter::Antialiasing);
    glView.setWindowTitle("[OpenGL 加速] 500个粒子 - 对比演示");
    glView.resize(SCENE_WIDTH + 50, SCENE_HEIGHT + 50);
    glView.show();

    FpsCounter* glFps = new FpsCounter();
    glFps->setPos(10, 10);
    scene.addItem(glFps);

    // ========== 窗口2：不使用 OpenGL（软件渲染） ==========
    QGraphicsView noGlView(&scene);
    // 不调用 setViewport()，使用默认的 QWidget（软件渲染）
    noGlView.setRenderHint(QPainter::Antialiasing);
    noGlView.setWindowTitle("[软件渲染] 500个粒子 - 对比演示");
    noGlView.resize(SCENE_WIDTH + 50, SCENE_HEIGHT + 50);
    noGlView.move(SCENE_WIDTH + 60, 0);
    noGlView.show();

    FpsCounter* noGlFps = new FpsCounter();
    noGlFps->setPos(10, SCENE_HEIGHT - 30);
    scene.addItem(noGlFps);

    // 更新定时器 - 60 FPS
    QTimer updateTimer;
    QObject::connect(&updateTimer, &QTimer::timeout, [&]() {
        // 更新所有粒子位置
        for (ParticleItem* p : particles) {
            p->updatePosition(SCENE_WIDTH, SCENE_HEIGHT);
        }
        // 更新帧率
        glFps->tick();
        noGlFps->tick();
    });
    updateTimer.start(16);  // ~60 FPS

    qDebug() << "=== OpenGL vs 软件渲染对比演示 ===";
    qDebug() << "· 场景包含:" << PARTICLE_COUNT << "个动态粒子";
    qDebug() << "· 左侧窗口: OpenGL 硬件加速";
    qDebug() << "· 右侧窗口: 软件渲染 (CPU)";
    qDebug() << "· 观察两个窗口的 FPS 差异";

    return app.exec();
}