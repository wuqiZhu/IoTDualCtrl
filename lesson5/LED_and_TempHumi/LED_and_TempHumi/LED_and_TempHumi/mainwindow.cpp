#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "rpc_client.h"
#include "dht11_thread.h"
#include <QDebug>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("智能家居监控系统");

    labelHumi = ui->label;
    labelTemp = ui->label_2;

    thread = new DHT11Thread(this);
    connect(thread, &DHT11Thread::updateHumidity, this, [this](QString s) {
        labelHumi->setText(s);
    });
    connect(thread, &DHT11Thread::updateTemperature, this, [this](QString s) {
        labelTemp->setText(s);
    });
    thread->start();

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &MainWindow::refreshSensors);
    refreshTimer->start(1000);

    smokeTimer = new QTimer(this);
    connect(smokeTimer, &QTimer::timeout, this, &MainWindow::refreshSmoke);
    smokeTimer->start(2000);
}

MainWindow::~MainWindow()
{
    if (thread) {
        thread->stop();
    }
    delete ui;
}

QLabel *MainWindow::GetHumiLabel()
{
    return labelHumi;
}

QLabel *MainWindow::GetTempLabel()
{
    return labelTemp;
}

void MainWindow::on_pushButton_clicked()
{
    static int ledState = 0;
    ledState = !ledState;
    int ret = rpc_led_control(ledState);
    if (ret == 0) {
        ui->pushButton->setText(ledState ? "LED: ON" : "LED: OFF");
    } else {
        qDebug() << "LED control failed";
        ledState = !ledState;
    }
}

void MainWindow::on_pushButton_relay_clicked()
{
    static int relayState = 0;
    relayState = !relayState;
    int ret = rpc_relay_control(relayState);
    if (ret == 0) {
        ui->pushButton_relay->setText(relayState ? "风扇: ON" : "风扇: OFF");
    } else {
        qDebug() << "Fan control failed";
        relayState = !relayState;
    }
}

void MainWindow::on_pushButton_relay2_clicked()
{
    static int state = 0;
    state = !state;
    int ret = rpc_relay2_control(state);
    if (ret == 0) {
        ui->pushButton_relay2->setText(state ? "LED灯: ON" : "LED灯: OFF");
    } else {
        qDebug() << "LED lamp control failed";
        state = !state;
    }
}

void MainWindow::refreshSensors()
{
    int pir, light;
    if (rpc_pir_read(&pir) == 0) {
        ui->label_pir->setText(pir ? "有人" : "无人");
    }
    if (rpc_light_read(&light) == 0) {
        ui->label_light->setText(light ? "暗" : "亮");
    }
}

void MainWindow::refreshSmoke()
{
    int smokeDigital = 0;
    if (rpc_smoke_digital_read(&smokeDigital) == 0) {
        ui->label_smoke->setText(smokeDigital ? "烟雾: 正常" : "烟雾: 报警!");
    }
}
