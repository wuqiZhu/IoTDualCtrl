#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include "dht11_thread.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    QLabel *GetHumiLabel();
    QLabel *GetTempLabel();

private slots:
    void on_pushButton_led_clicked();
    void on_pushButton_fan_clicked();
    void on_pushButton_led_lamp_clicked();
    void on_pushButton_camera_clicked();
    void on_pushButton_exit_clicked();
    void refreshSensors();
    void refreshSmoke();
    void refreshRelayStates();
    void updateConnectionStatus(bool connected);

private:
    Ui::MainWindow *ui;
    DHT11Thread *thread;
    QTimer *sensorTimer;
    QTimer *relayTimer;
    QTimer *smokeTimer;

    void setRelayStateLabel(QLabel *label, int state);
};

#endif // MAINWINDOW_H
