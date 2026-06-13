#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "rpc_client.h"
#include "dht11_thread.h"
#include <QDebug>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QCoreApplication>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("智能家居监控系统 v3.0");

    /* 初始化RPC连接 */
    bool connected = (RPC_Client_Init() == 0);
    updateConnectionStatus(connected);

    /* ===== DHT11 温湿度（独立线程） ===== */
    thread = new DHT11Thread(this);
    connect(thread, &DHT11Thread::updateHumidity, this, [this](QString s) {
        ui->value_humi->setText(s);
    });
    connect(thread, &DHT11Thread::updateTemperature, this, [this](QString s) {
        ui->value_temp->setText(s);
    });
    thread->start();

    /* ===== 传感器刷新定时器（1秒） ===== */
    sensorTimer = new QTimer(this);
    connect(sensorTimer, &QTimer::timeout, this, &MainWindow::refreshSensors);
    sensorTimer->start(1000);

    /* ===== 烟雾检测定时器（2秒） ===== */
    smokeTimer = new QTimer(this);
    connect(smokeTimer, &QTimer::timeout, this, &MainWindow::refreshSmoke);
    smokeTimer->start(2000);

    /* ===== 继电器状态刷新定时器（3秒） ===== */
    relayTimer = new QTimer(this);
    connect(relayTimer, &QTimer::timeout, this, &MainWindow::refreshRelayStates);
    relayTimer->start(3000);

    /* 初始读一次继电器状态 */
    refreshRelayStates();
}

MainWindow::~MainWindow()
{
    if (thread) {
        thread->stop();
        thread->wait(2000);
    }
    delete ui;
}

QLabel *MainWindow::GetHumiLabel()
{
    return ui->value_humi;
}

QLabel *MainWindow::GetTempLabel()
{
    return ui->value_temp;
}

/* ===== LED 控制（板载LED） ===== */
void MainWindow::on_pushButton_led_clicked()
{
    /* 先读当前状态 */
    int current = 0;
    int ret = rpc_led_control(1);  /* JSON-RPC是toggle语义，此处用set */
    /* 实际上 led_control 是 set 操作 */
    /* 不需要本地翻转，等 relayTimer 回读真实状态 */
    if (ret == 0) {
        statusBar()->showMessage("LED 控制成功", 2000);
    } else {
        statusBar()->showMessage("LED 控制失败", 2000);
    }
}

/* ===== 风扇控制（继电器1） ===== */
void MainWindow::on_pushButton_fan_clicked()
{
    int current = 0;
    if (rpc_relay_read(&current) == 0) {
        int newState = current ? 0 : 1;
        if (rpc_relay_control(newState) == 0) {
            statusBar()->showMessage(
                QString("风扇 %1").arg(newState ? "已开启" : "已关闭"), 2000);
            /* 立即刷新状态 */
            refreshRelayStates();
        } else {
            statusBar()->showMessage("风扇控制失败", 2000);
        }
    } else {
        statusBar()->showMessage("读取风扇状态失败", 2000);
    }
}

/* ===== LED灯控制（继电器2） ===== */
void MainWindow::on_pushButton_led_lamp_clicked()
{
    int current = 0;
    if (rpc_relay2_read(&current) == 0) {
        int newState = current ? 0 : 1;
        if (rpc_relay2_control(newState) == 0) {
            statusBar()->showMessage(
                QString("LED灯 %1").arg(newState ? "已开启" : "已关闭"), 2000);
            refreshRelayStates();
        } else {
            statusBar()->showMessage("LED灯控制失败", 2000);
        }
    } else {
        statusBar()->showMessage("读取LED灯状态失败", 2000);
    }
}

/* ===== 摄像头抓拍 ===== */
void MainWindow::on_pushButton_camera_clicked()
{
    ui->value_camera_result->setText("正在抓拍...");
    ui->value_camera_result->setStyleSheet("color: #2196F3;");
    QCoreApplication::processEvents();

    char path[64];
    snprintf(path, sizeof(path), "/tmp/qt_capture_%ld.jpg", time(NULL));
    int ret = rpc_camera_capture_jpeg(path);
    if (ret == 0) {
        ui->value_camera_result->setText("✅ 抓拍成功");
        ui->value_camera_result->setStyleSheet("color: #4CAF50;");
        statusBar()->showMessage(QString("抓拍已保存: %1").arg(path), 3000);
    } else {
        ui->value_camera_result->setText("❌ 抓拍失败");
        ui->value_camera_result->setStyleSheet("color: #F44336;");
        statusBar()->showMessage("摄像头抓拍失败，请检查设备", 3000);
    }
}

/* ===== 退出 ===== */
void MainWindow::on_pushButton_exit_clicked()
{
    close();
}

/* ===== 刷新传感器（PIR + 光照） ===== */
void MainWindow::refreshSensors()
{
    int pir, light;
    if (rpc_pir_read(&pir) == 0) {
        ui->value_pir->setText(pir ? "🚶 有人" : "💤 无人");
        ui->value_pir->setStyleSheet(pir ? "color: #4CAF50;" : "color: #9E9E9E;");
    }
    if (rpc_light_read(&light) == 0) {
        ui->value_light->setText(light ? "🌙 黑暗" : "☀️ 明亮");
        ui->value_light->setStyleSheet(light ? "color: #FF9800;" : "color: #FFC107;");
    }
}

/* ===== 刷新烟雾状态 ===== */
void MainWindow::refreshSmoke()
{
    int smoke = 0;
    if (rpc_smoke_digital_read(&smoke) == 0) {
        if (smoke == 0) {
            ui->value_smoke->setText("🚨 烟雾报警!");
            ui->value_smoke->setStyleSheet("color: #F44336; font-weight: bold;");
        } else {
            ui->value_smoke->setText("✅ 正常");
            ui->value_smoke->setStyleSheet("color: #4CAF50;");
        }
    }
}

/* ===== 刷新继电器状态（从设备真实读取） ===== */
void MainWindow::refreshRelayStates()
{
    int val;

    if (rpc_relay_read(&val) == 0) {
        setRelayStateLabel(ui->value_fan_state, val);
    }

    if (rpc_relay2_read(&val) == 0) {
        setRelayStateLabel(ui->value_led_lamp_state, val);
    }
}

/* ===== 辅助：设置继电器状态标签 ===== */
void MainWindow::setRelayStateLabel(QLabel *label, int state)
{
    if (state) {
        label->setText("🟢 已开启");
        label->setStyleSheet("color: #4CAF50; font-weight: bold;");
    } else {
        label->setText("🔴 已关闭");
        label->setStyleSheet("color: #9E9E9E;");
    }
}

/* ===== 更新连接状态 ===== */
void MainWindow::updateConnectionStatus(bool connected)
{
    if (connected) {
        statusBar()->showMessage("✅ RPC服务器已连接  |  127.0.0.1:1234");
    } else {
        statusBar()->showMessage("❌ RPC服务器连接失败  |  请检查 rpc_server 是否运行");
    }
}
