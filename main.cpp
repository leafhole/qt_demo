#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsSceneMouseEvent>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QTimer>
#include <QList>
#include <QDebug>

/* ============================================================
 * 舞台剧比喻：
 *   QGraphicsScene  = 舞台本身（承载一切的无形空间）
 *   QGraphicsItem   = 演员和道具（放在舞台上的东西）
 *   QGraphicsView   = 观众的眼睛/摄像机（从某个角度看舞台）
 * ============================================================ */


/* -------- 1. 自定义"演员"：一个可移动的 QGraphicsItem --------
 * 它重写了 paint / boundingRect，在舞台上画出一张笑脸。
 * 这就是一个"演员"——它不知道舞台有多大、也不知道观众从哪看，
 * 它只关心："我自己长什么样、我现在在哪个坐标"。
 */
class SmileyActor : public QGraphicsItem
{
public:
    SmileyActor(const QString &name, QGraphicsItem *parent = nullptr)
        : QGraphicsItem(parent), m_name(name)
    {
        // 允许被选中、可以用鼠标拖动
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setFlag(QGraphicsItem::ItemIsMovable, true);
        setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    }

    // 必须实现：告诉 Qt "这个 item 占多大一块矩形区域"
    QRectF boundingRect() const override
    {
        return QRectF(-50, -50, 100, 100); // 以 (0,0) 为中心，半径 50
    }

    // 必须实现：真正"画自己"的地方
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget = nullptr) override
    {
        Q_UNUSED(option);
        Q_UNUSED(widget);

        painter->setRenderHint(QPainter::Antialiasing);

        // 黄色圆脸
        painter->setPen(QPen(Qt::black, 2));
        painter->setBrush(QBrush(QColor(255, 220, 50)));
        painter->drawEllipse(-40, -40, 80, 80);

        // 两只眼睛
        painter->setBrush(Qt::black);
        painter->drawEllipse(-18, -15, 10, 10);
        painter->drawEllipse(8, -15, 10, 10);

        // 微笑
        painter->setPen(QPen(Qt::black, 3));
        painter->drawArc(QRectF(-20, -5, 40, 30),
                         200 * 16, 140 * 16); // Qt 用 1/16 度做单位

        // 名字标签
        painter->setPen(Qt::black);
        painter->setFont(QFont("Arial", 10));
        painter->drawText(QRectF(-50, 45, 100, 20),
                          Qt::AlignCenter, m_name);
    }

protected:
    // 当这个演员被移动时，我们打印一下位置，让你看到"舞台坐标系"
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override
    {
        if (change == QGraphicsItem::ItemPositionChange && scene()) {
            QPointF newPos = value.toPointF();
            qDebug() << "[舞台] 演员" << m_name << "移动到" << newPos;
        }
        return QGraphicsItem::itemChange(change, value);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        qDebug() << "[舞台] 演员" << m_name << "被观众点到了！";
        update(); // 让 Qt 重绘（这里做一下视觉反馈）
        QGraphicsItem::mousePressEvent(event);
    }

private:
    QString m_name;
};


/* -------- 2. 自定义"道具"：一个旋转的红色方块 --------
 * 演示 QGraphicsObject：它继承自 QObject + QGraphicsItem，
 * 因此可以用信号槽。我们用 QTimer 让它一直在舞台上旋转。
 */
class SpinningProp : public QGraphicsObject
{
    Q_OBJECT
public:
    SpinningProp(QGraphicsObject *parent = nullptr)
        : QGraphicsObject(parent), m_angle(0)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, true);

        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            m_angle += 3;
            setRotation(m_angle);   // QGraphicsItem 自带旋转/缩放等变换
        });
        m_timer->start(50);        // 每 50ms 转一次
    }

    QRectF boundingRect() const override
    {
        return QRectF(-30, -30, 60, 60);
    }

    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget = nullptr) override
    {
        Q_UNUSED(option);
        Q_UNUSED(widget);

        painter->setPen(QPen(Qt::darkRed, 3));
        painter->setBrush(QBrush(QColor(220, 60, 60, 200)));
        painter->drawRect(-25, -25, 50, 50);

        painter->setPen(Qt::white);
        painter->setFont(QFont("Arial", 12, QFont::Bold));
        painter->drawText(boundingRect(), Qt::AlignCenter, "道具");
    }

private:
    QTimer *m_timer;
    int m_angle;
};


/* ============================================================
 * main()  —— 搭舞台、放演员、给观众一台摄像机 (QGraphicsView)
 * ============================================================ */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // ---------- ① QGraphicsScene —— 舞台本身 ----------
    // 舞台是一个 800x500 的虚拟坐标空间，
    // 它本身不可见，但管理所有演员/道具的生命周期与碰撞检测。
    QGraphicsScene stage;
    stage.setSceneRect(0, 0, 800, 500);

    // 给舞台加一个"背景幕布"——这就是一个普通 QGraphicsRectItem
    QGraphicsRectItem *curtain = new QGraphicsRectItem(0, 0, 800, 500);
    curtain->setBrush(QBrush(QColor(40, 20, 60)));  // 深紫色幕布
    curtain->setPen(Qt::NoPen);
    curtain->setZValue(-10);                        // 放在最底层
    stage.addItem(curtain);

    // 舞台地板装饰——用 Qt 内置 shape item
    QGraphicsRectItem *floor = new QGraphicsRectItem(0, 380, 800, 120);
    floor->setBrush(QBrush(QColor(80, 50, 30)));
    floor->setPen(Qt::NoPen);
    floor->setZValue(-5);
    stage.addItem(floor);

    // 舞台标题——文本 item
    QGraphicsTextItem *title = new QGraphicsTextItem("Qt 舞台剧 Demo");
    title->setDefaultTextColor(QColor(255, 230, 120));
    title->setFont(QFont("Microsoft YaHei", 20, QFont::Bold));
    title->setPos(280, 20);
    stage.addItem(title);


    // ---------- ② QGraphicsItem —— 演员与道具 ----------
    // 放三个笑脸"演员"到舞台上。每个演员自己管理自己的外观与行为。
    SmileyActor *actorA = new SmileyActor("Alice");
    actorA->setPos(200, 250);
    stage.addItem(actorA);

    SmileyActor *actorB = new SmileyActor("Bob");
    actorB->setPos(400, 250);
    stage.addItem(actorB);

    SmileyActor *actorC = new SmileyActor("Charlie");
    actorC->setPos(600, 250);
    stage.addItem(actorC);

    // 一个"会旋转的道具"——演示 QGraphicsObject + 信号槽
    SpinningProp *prop = new SpinningProp();
    prop->setPos(100, 150);
    stage.addItem(prop);

    // 再加一个简单椭圆——演示 Qt 预定义 item
    QGraphicsEllipseItem *sun = new QGraphicsEllipseItem(0, 0, 60, 60);
    sun->setBrush(QBrush(QColor(255, 180, 50)));
    sun->setPen(QPen(Qt::yellow, 3));
    sun->setPos(700, 40);
    stage.addItem(sun);


    // ---------- ③ QGraphicsView —— 观众的眼睛/摄像机 ----------
    // View 不拥有数据，它只是一个"摄像机"，
    // 把同一份 scene 里的内容渲染到屏幕上。
    QGraphicsView view(&stage);
    view.setRenderHint(QPainter::Antialiasing);
    view.setBackgroundBrush(QBrush(Qt::black));
    view.setWindowTitle("QGraphicsView = 观众的眼睛");
    view.resize(850, 550);
    view.show();

    // 再开一个"摄像机"——同一个舞台，从另一视角观看（略缩小）
    // 这直观展示了"一个 scene 可以被多个 view 观察"的核心设计。
    QGraphicsView view2(&stage);
    view2.setRenderHint(QPainter::Antialiasing);
    view2.scale(0.6, 0.6);                 // 这个摄像机"拉远了"，画面更小
    view2.rotate(5);                        // 还稍微歪了 5 度
    view2.setBackgroundBrush(QBrush(QColor(20, 20, 40)));
    view2.setWindowTitle("QGraphicsView #2 = 另一台摄像机（缩小+倾斜）");
    view2.resize(550, 400);
    view2.move(900, 100);
    view2.show();

    // 一些提示：
    qDebug() << "=== 舞台与演员 Demo 启动 ===";
    qDebug() << "· 舞台大小:" << stage.sceneRect();
    qDebug() << "· 舞台上的 item 总数:" << stage.items().count();
    qDebug() << "· 试试用鼠标拖动笑脸演员 —— 他们只是舞台上的 QGraphicsItem";
    qDebug() << "· 两个窗口显示的是同一个 scene —— 一个演员动，两个窗口都能看到";

    return app.exec();
}

#include "main.moc"
