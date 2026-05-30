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
    void on_pushButton_clicked();
    void on_pushButton_relay_clicked();
    void on_pushButton_relay2_clicked();
    void refreshSensors();
    void refreshSmoke();

private:
    Ui::MainWindow *ui;
    QLabel *labelHumi;
    QLabel *labelTemp;
    QTimer *refreshTimer;
    QTimer *smokeTimer;
    DHT11Thread *thread;
};

#endif // MAINWINDOW_H