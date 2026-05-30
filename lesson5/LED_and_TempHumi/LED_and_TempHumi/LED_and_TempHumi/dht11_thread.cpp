#include "dht11_thread.h"
#include "rpc_client.h"
#include <QDebug>

DHT11Thread::DHT11Thread(QObject *parent) : QThread(parent)
{
}

void DHT11Thread::stop()
{
    m_running = false;
    wait();
}

void DHT11Thread::run()
{
    int humi = -1, temp = -1;
    char raw_humi, raw_temp;

    while (m_running) {
        if (0 == rpc_dht11_read(&raw_humi, &raw_temp)) {
            humi = (int)raw_humi;
            temp = (int)raw_temp;

            if (humi >= 0 && temp >= 0) {
                QString humiStr = QString("湿度: %1%").arg(humi);
                QString tempStr = QString("温度: %1°C").arg(temp);
                emit updateHumidity(humiStr);
                emit updateTemperature(tempStr);
            }
        } else {
            qDebug() << "DHT11 read failed, retrying...";
        }
        msleep(2000);
    }
}