#ifndef DHT11_THREAD_H
#define DHT11_THREAD_H

#include <QThread>
#include <QLabel>
#include <atomic>

class DHT11Thread : public QThread
{
    Q_OBJECT
public:
    explicit DHT11Thread(QObject *parent = nullptr);
    void run() override;
    void stop();

signals:
    void updateHumidity(QString value);
    void updateTemperature(QString value);

private:
    std::atomic<bool> m_running{true};
};

#endif // DHT11_THREAD_H