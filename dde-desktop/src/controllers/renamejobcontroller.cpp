#include "renamejobcontroller.h"
#include "dbusinterface/renamejob_interface.h"
#include "dbusinterface/fileoperations_interface.h"
#include "views/global.h"

RenameJobController::RenameJobController(QObject *parent) : QObject(parent)
{
    initConnect();
}

void RenameJobController::initConnect(){
    connect(signalManager, SIGNAL(renameJobCreated(QString,QString)), this, SLOT(rename(QString,QString)));
}

void RenameJobController::rename(QString url, QString newName){
    LOG_INFO() << url << newName;
    QDBusPendingReply<QString, QDBusObjectPath, QString> reply = dbusController->getFileOperationsInterface()->NewRenameJob(url, newName);
    reply.waitForFinished();
    if (!reply.isError()){
        QString service = reply.argumentAt(0).toString();
        QString path = qdbus_cast<QDBusObjectPath>(reply.argumentAt(1)).path();
        m_renameJobInterface = new RenameJobInterface(service, path, QDBusConnection::sessionBus(), this);
        connect(m_renameJobInterface, SIGNAL(Done(QString)), this, SLOT(renameJobExcuteFinished(QString)));
        m_renameJobInterface->Execute();
    }else{
        LOG_ERROR() << reply.error().message();
    }
}

void RenameJobController::renameJobExcuteFinished(QString name){
    disconnect(m_renameJobInterface, SIGNAL(Done(QString)), this, SLOT(renameJobExcuteFinished(QString)));
    m_renameJobInterface = NULL;
    LOG_INFO() << "rename job finished" << name;
}

RenameJobController::~RenameJobController()
{

}
