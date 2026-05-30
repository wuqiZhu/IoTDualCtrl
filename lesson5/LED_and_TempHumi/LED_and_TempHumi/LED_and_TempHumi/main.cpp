#include "mainwindow.h"
#include "rpc_client.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    if (RPC_Client_Init() < 0) {
            printf("RPC_Client_Init failed\n");
            return -1;
        }
        printf("RPC_Client_Init success\n");  // 添加打印

    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
