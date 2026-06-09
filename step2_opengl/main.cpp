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
#include <QPainter>
#include <QWidget>
#include <random>

std::mt19937 rng;

inline int randInt(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

// 粒子数据结构
struct Particle {
    qreal x, y;
    qreal vx, vy;
    qreal size;
    QColor color;
    int alpha;       // 透明度
    int rotation;    // 旋转角度
};

// 纯软件渲染窗口 - 不使用任何GPU加速
class SoftwareRenderWidget : public QWidget
{
    Q_OBJECT
public:
    SoftwareRenderWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedSize(800, 600);
        m_particles.resize(10000);  // 大幅增加粒子数量
        for (int i = 0; i < m_particles.size(); ++i) {
            m_particles[i].x = randInt(0, 799);
            m_particles[i].y = randInt(0, 599);
            m_particles[i].vx = (randInt(-100, 100)) / 50.0;
            m_particles[i].vy = (randInt(-100, 100)) / 50.0;
            m_particles[i].size = 3 + randInt(0, 10);
            m_particles[i].color = QColor::fromHsl(randInt(0, 359), 200, 128);
            m_particles[i].alpha = 128 + randInt(0, 127);  // 透明度
            m_particles[i].rotation = randInt(0, 359);
        }
        m_frameCount = 0;
        m_lastTime = QDateTime::currentMSecsSinceEpoch();
        m_fps = 0;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        // 纯软件绘制 - 使用QImage作为缓冲区
        QImage buffer(width(), height(), QImage::Format_ARGB32);
        buffer.fill(QColor(20, 20, 40));
        
        QPainter painter(&buffer);
        painter.setRenderHint(QPainter::Antialiasing);
        
        // 绘制所有粒子（带透明度和旋转，增加渲染复杂度）
        for (const Particle& p : m_particles) {
            painter.save();
            painter.translate(p.x, p.y);
            painter.rotate(p.rotation);
            painter.setBrush(QBrush(QColor(p.color.red(), p.color.green(), p.color.blue(), p.alpha)));
            painter.setPen(QPen(QColor(255, 255, 255, 128), 1));
            painter.drawEllipse(QPointF(0, 0), p.size/2, p.size/2);
            painter.restore();
        }
        
        // 绘制FPS
        painter.setPen(QColor(255, 255, 255));
        painter.setFont(QFont("Arial", 14, QFont::Bold));
        painter.drawText(10, 25, QString("FPS: %1").arg(m_fps, 0, 'f', 1));
        
        // 显示缓冲区
        QPainter screenPainter(this);
        screenPainter.drawImage(0, 0, buffer);
    }

public slots:
    void updateParticles()
    {
        for (Particle& p : m_particles) {
            p.x += p.vx;
            p.y += p.vy;
            p.rotation += 2;  // 旋转
            if (p.x < p.size || p.x > width() - p.size) {
                p.vx = -p.vx;
                p.x = qMax(p.size, qMin((qreal)(width() - p.size), p.x));
            }
            if (p.y < p.size || p.y > height() - p.size) {
                p.vy = -p.vy;
                p.y = qMax(p.size, qMin((qreal)(height() - p.size), p.y));
            }
        }
        
        m_frameCount++;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 delta = now - m_lastTime;
        if (delta >= 1000) {
            m_fps = m_frameCount * 1000.0 / delta;
            m_frameCount = 0;
            m_lastTime = now;
        }
        
        update();
    }

private:
    QList<Particle> m_particles;
    int m_frameCount;
    qint64 m_lastTime;
    double m_fps;
};

// OpenGL加速窗口
class OpenGLRenderWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    OpenGLRenderWidget(QWidget* parent = nullptr) : QOpenGLWidget(parent)
    {
        setFixedSize(800, 600);
        m_particles.resize(10000);  // 同样使用10000个粒子
        for (int i = 0; i < m_particles.size(); ++i) {
            m_particles[i].x = randInt(0, 799);
            m_particles[i].y = randInt(0, 599);
            m_particles[i].vx = (randInt(-100, 100)) / 50.0;
            m_particles[i].vy = (randInt(-100, 100)) / 50.0;
            m_particles[i].size = 3 + randInt(0, 10);
            m_particles[i].color = QColor::fromHsl(randInt(0, 359), 200, 128);
            m_particles[i].alpha = 128 + randInt(0, 127);
            m_particles[i].rotation = randInt(0, 359);
        }
        m_frameCount = 0;
        m_lastTime = QDateTime::currentMSecsSinceEpoch();
        m_fps = 0;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        // 使用QPainter + OpenGL加速
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(20, 20, 40));
        
        // 绘制所有粒子（带透明度和旋转）
        for (const Particle& p : m_particles) {
            painter.save();
            painter.translate(p.x, p.y);
            painter.rotate(p.rotation);
            painter.setBrush(QBrush(QColor(p.color.red(), p.color.green(), p.color.blue(), p.alpha)));
            painter.setPen(QPen(QColor(255, 255, 255, 128), 1));
            painter.drawEllipse(QPointF(0, 0), p.size/2, p.size/2);
            painter.restore();
        }
        
        // 绘制FPS
        painter.setPen(QColor(255, 255, 255));
        painter.setFont(QFont("Arial", 14, QFont::Bold));
        painter.drawText(10, 25, QString("FPS: %1").arg(m_fps, 0, 'f', 1));
    }

public slots:
    void updateParticles()
    {
        for (Particle& p : m_particles) {
            p.x += p.vx;
            p.y += p.vy;
            p.rotation += 2;  // 旋转
            if (p.x < p.size || p.x > width() - p.size) {
                p.vx = -p.vx;
                p.x = qMax(p.size, qMin((qreal)(width() - p.size), p.x));
            }
            if (p.y < p.size || p.y > height() - p.size) {
                p.vy = -p.vy;
                p.y = qMax(p.size, qMin((qreal)(height() - p.size), p.y));
            }
        }
        
        m_frameCount++;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 delta = now - m_lastTime;
        if (delta >= 1000) {
            m_fps = m_frameCount * 1000.0 / delta;
            m_frameCount = 0;
            m_lastTime = now;
        }
        
        update();
    }

private:
    QList<Particle> m_particles;
    int m_frameCount;
    qint64 m_lastTime;
    double m_fps;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    rng.seed(std::random_device{}());

    // 设置OpenGL配置
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    // ========== 窗口1：OpenGL加速 ==========
    OpenGLRenderWidget* glWidget = new OpenGLRenderWidget();
    glWidget->setWindowTitle("[OpenGL 加速] 10000个粒子 - GPU渲染");
    glWidget->show();

    // ========== 窗口2：纯软件渲染 ==========
    SoftwareRenderWidget* swWidget = new SoftwareRenderWidget();
    swWidget->setWindowTitle("[纯软件渲染] 10000个粒子 - CPU渲染");
    swWidget->move(820, 0);
    swWidget->show();

    // 更新定时器
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, glWidget, &OpenGLRenderWidget::updateParticles);
    QObject::connect(&timer, &QTimer::timeout, swWidget, &SoftwareRenderWidget::updateParticles);
    timer.start(16);

    qDebug() << "=== OpenGL vs 纯软件渲染对比演示 ===";
    qDebug() << "· 粒子数量: 10000";
    qDebug() << "· 左侧: OpenGL加速 (GPU)";
    qDebug() << "· 右侧: 纯软件渲染 (CPU)";
    qDebug() << "· 每个粒子包含：位置、速度、大小、颜色、透明度、旋转";

    return app.exec();
}

#include "main.moc"