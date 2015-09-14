#include "dbuscontroller.h"

#include "dbusinterface/monitormanager_interface.h"
#include "dbusinterface/clipboard_interface.h"
#include "dbusinterface/watcherinstance_interface.h"
#include "dbusinterface/filemonitorInstance_interface.h"
#include "dbusinterface/fileInfo_interface.h"
#include "dbusinterface/desktopdaemon_interface.h"
#include "dbusinterface/fileoperations_interface.h"
#include "dbusinterface/createdirjob_interface.h"
#include "dbusinterface/createfilejob_interface.h"
#include "dbusinterface/createfilefromtemplatejob_interface.h"
#include "dbusinterface/dbusdocksetting.h"

#include "views/global.h"
#include "views/signalmanager.h"
#include "filemonitor/filemonitor.h"
#include "widgets/util.h"

DBusController::DBusController(QObject *parent) : QObject(parent)
{

}

void DBusController::init(){
    QDBusConnection bus = QDBusConnection::sessionBus();
    m_fileInfoInterface = new FileInfoInterface(FileInfo_service, FileInfo_path, bus, this);
    m_desktopDaemonInterface = new DesktopDaemonInterface(DesktopDaemon_service, DesktopDaemon_path, bus, this);
    m_fileOperationsInterface = new FileOperationsInterface(FileMonitor_service, FileOperations_path, bus, this);
    m_clipboardInterface = new ClipboardInterface(FileMonitor_service, Clipboard_path, bus, this);
    m_dockSettingInterface = new DBusDockSetting(this);

    requestDesktopItems();
    requestIconByUrl(ComputerUrl, 48);
    requestIconByUrl(TrashUrl, 48);

    m_fileMonitor = new FileMonitor(this);
    initConnect();

    m_thumbnailTimer = new QTimer;
    m_thumbnailTimer->setInterval(500);
    connect(m_thumbnailTimer, SIGNAL(timeout()), this, SLOT(delayGetThumbnail()));
}

void DBusController::initConnect(){
    connect(m_desktopDaemonInterface, SIGNAL(RequestOpen(QStringList,IntList)),
            this, SLOT(openFiles(QStringList, IntList)));
    connect(signalManager, SIGNAL(openFiles(DesktopItemInfo,QList<DesktopItemInfo>)),
            this, SLOT(openFiles(DesktopItemInfo,QList<DesktopItemInfo>)));
    connect(signalManager, SIGNAL(openFiles(DesktopItemInfo,QStringList)),
            this, SLOT(openFiles(DesktopItemInfo,QStringList)));
    connect(signalManager, SIGNAL(openFile(DesktopItemInfo)), this, SLOT(openFile(DesktopItemInfo)));
    connect(m_desktopDaemonInterface, SIGNAL(RequestCreateDirectory()), this, SLOT(createDirectory()));
    connect(m_desktopDaemonInterface, SIGNAL(RequestCreateFile()), this, SLOT(createFile()));
    connect(m_desktopDaemonInterface, SIGNAL(RequestCreateFileFromTemplate(QString)),
            this, SLOT(createFileFromTemplate(QString)));
    connect(m_desktopDaemonInterface, SIGNAL(RequestSort(QString)),this, SLOT(sortByKey(QString)));

    connect(m_desktopDaemonInterface, SIGNAL(AppGroupCreated(QString,QStringList)),
            this, SLOT(createAppGroup(QString,QStringList)));

    connect(m_desktopDaemonInterface, SIGNAL(ItemCut(QStringList)),
            signalManager, SIGNAL(filesCuted(QStringList)));
    connect(m_desktopDaemonInterface, SIGNAL(RequestRename(QString)), signalManager, SIGNAL(requestRenamed(QString)));
    connect(m_desktopDaemonInterface, SIGNAL(RequestDelete(QStringList)),
            signalManager, SIGNAL(trashingAboutToExcute(QStringList)));

    connect(m_desktopDaemonInterface, SIGNAL(RequestEmptyTrash()), signalManager, SIGNAL(requestEmptyTrash()));

    connect(m_clipboardInterface, SIGNAL(RequestPaste(QString,QStringList,QString)),
            this, SLOT(pasteFiles(QString,QStringList,QString)));



    connect(signalManager, SIGNAL(requestCreatingAppGroup(QStringList)),
            this, SLOT(requestCreatingAppGroup(QStringList)));
    connect(signalManager, SIGNAL(requestMergeIntoAppGroup(QStringList,QString)),
            this, SLOT(mergeIntoAppGroup(QStringList,QString)));

    connect(m_fileMonitor, SIGNAL(fileCreated(QString)), this, SLOT(handleFileCreated(QString)));
    connect(m_fileMonitor, SIGNAL(fileDeleted(QString)), this, SLOT(handleFileDeleted(QString)));
    connect(m_fileMonitor, SIGNAL(fileMovedIn(QString)), this, SLOT(handleFileMovedIn(QString)));
    connect(m_fileMonitor, SIGNAL(fileMovedOut(QString)), this, SLOT(handleFileMovedOut(QString)));
    connect(m_fileMonitor, SIGNAL(fileRenamed(QString,QString)), this, SLOT(handleFileRenamed(QString,QString)));

    connect(m_dockSettingInterface, SIGNAL(DisplayModeChanged(int)), signalManager, SIGNAL(dockModeChanged(int)));
}

DesktopDaemonInterface* DBusController::getDesktopDaemonInterface(){
    return m_desktopDaemonInterface;
}

void DBusController::requestDesktopItems(){
    QDBusPendingReply<DesktopItemInfoMap> reply = m_desktopDaemonInterface->GetDesktopItems();
    reply.waitForFinished();
    if (!reply.isError()){
        DesktopItemInfoMap desktopItems = qdbus_cast<DesktopItemInfoMap>(reply.argumentAt(0));
        /*ToDo desktop daemon settings judge*/
        for(int i=0; i < desktopItems.count(); i++){
            QString key = desktopItems.keys().at(i);
            DesktopItemInfo info = desktopItems.values().at(i);
            qDebug() << info.BaseName << info.Icon << info.thumbnail;
            if (info.thumbnail.length() > 0){
                info.Icon = info.thumbnail;
            }
            desktopItems.insert(key, info);
        }
        emit signalManager->desktopItemsChanged(desktopItems);

        m_desktopItemInfoMap = desktopItems;

        foreach (QString url, desktopItems.keys()) {
            if (isAppGroup(decodeUrl(url))){
                getAppGroupItemsByUrl(url);
            }
        }

    }else{
        LOG_ERROR() << reply.error().message();
    }
}

void DBusController::requestIconByUrl(QString scheme, uint size){
    QDBusPendingReply<QString> reply = m_fileInfoInterface->GetThemeIcon(scheme, size);
    reply.waitForFinished();
    if (!reply.isError()){
        QString iconUrl = reply.argumentAt(0).toString();
        emit signalManager->desktoItemIconUpdated(scheme, iconUrl, size);
    }else{
        LOG_ERROR() << reply.error().message();
    }
}

void DBusController::delayGetThumbnail(){
    foreach(QString url, m_thumbnails){
        requestThumbnail(url, 48);
    }
    if (m_thumbnails.count() == 0){
        m_thumbnailTimer->stop();
    }
}

void DBusController::requestThumbnail(QString scheme, uint size){
    QDBusPendingReply<QString> reply = m_fileInfoInterface->GetThumbnail(scheme, size);
    reply.waitForFinished();
    if (!reply.isError()){
        QString iconUrl = reply.argumentAt(0).toString();
        qDebug() << iconUrl;
        if (m_thumbnails.contains(scheme)){
            m_thumbnails.removeOne(scheme);
        }
        emit signalManager->desktoItemIconUpdated(scheme, iconUrl, size);
    }else{
        LOG_ERROR() << reply.error().message();
    }
}

QMap<QString, DesktopItemInfoMap> DBusController::getAppGroups(){
    return m_appGroups;
}

FileOperationsInterface* DBusController::getFileOperationsInterface(){
    return m_fileOperationsInterface;
}

FileInfoInterface* DBusController::getFileInfoInterface(){
    return m_fileInfoInterface;
}

void DBusController::getAppGroupItemsByUrl(QString group_url){
    QString _group_url = group_url;
    QString group_dir = decodeUrl(_group_url.replace(FilePrefix, ""));
    QDir groupDir(group_dir);
    QFileInfoList fileInfoList  = groupDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files);
    if (groupDir.exists()){
        if (fileInfoList.count() == 0){
            bool flag = groupDir.removeRecursively();
            LOG_INFO() << decodeUrl(group_url) << "delete removeRecursively" << flag;
        }else if (fileInfoList.count() == 1){
            LOG_INFO() << fileInfoList.at(0).filePath() << "only one .desktop file in app group";
            QFile f(fileInfoList.at(0).filePath());
            QString newFileName = desktopLocation + QString(QDir::separator()) + fileInfoList.at(0).fileName();
            f.rename(newFileName);
            QDir(fileInfoList.at(0).absoluteDir()).removeRecursively();
            emit signalManager->appGounpDetailClosed();
        }else{
             LOG_INFO() << "update app group icon==============1";
            QDBusPendingReply<DesktopItemInfoMap> reply = m_desktopDaemonInterface->GetAppGroupItems(group_url);
            reply.waitForFinished();
            if (!reply.isError()){
                DesktopItemInfoMap desktopItemInfos = qdbus_cast<DesktopItemInfoMap>(reply.argumentAt(0));

                if (desktopItemInfos.count() > 1){
                    LOG_INFO() << "update app group icon==============2" << desktopItemInfos.keys();
                    emit signalManager->appGounpItemsChanged(group_url, desktopItemInfos);
                    m_appGroups.insert(group_url, desktopItemInfos);
                }else{
                    m_appGroups.remove(group_url);
                }
            }else{
                LOG_ERROR() << reply.error().message();
            }
        }
    }else{
        LOG_ERROR() << "App Group Dir isn't exists" << groupDir;
    }
}

DesktopItemInfoMap DBusController::getDesktopItemInfoMap(){
    return m_desktopItemInfoMap;
}


void DBusController::asyncRenameDesktopItemByUrl(QString url){
    QDBusPendingReply<DesktopItemInfo> reply = m_desktopDaemonInterface->GetItemInfo(url);

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
                        this, SLOT(asyncRenameDesktopItemByUrlFinished(QDBusPendingCallWatcher*)));
}

void DBusController::asyncRenameDesktopItemByUrlFinished(QDBusPendingCallWatcher *call){
    QDBusPendingReply<DesktopItemInfo> reply = *call;
    if (!reply.isError()) {
        DesktopItemInfo desktopItemInfo = qdbus_cast<DesktopItemInfo>(reply.argumentAt(0));
        emit signalManager->itemMoved(desktopItemInfo);
        updateDesktopItemInfoMap(desktopItemInfo);
    } else {
        LOG_ERROR() << reply.error().message();
    }
    call->deleteLater();
}


void DBusController::asyncCreateDesktopItemByUrl(QString url){
    QDBusPendingReply<DesktopItemInfo> reply = m_desktopDaemonInterface->GetItemInfo(url);

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
                        this, SLOT(asyncCreateDesktopItemByUrlFinished(QDBusPendingCallWatcher*)));
}


void DBusController::asyncCreateDesktopItemByUrlFinished(QDBusPendingCallWatcher *call){
    QDBusPendingReply<DesktopItemInfo> reply = *call;
    if (!reply.isError()) {
        DesktopItemInfo desktopItemInfo = qdbus_cast<DesktopItemInfo>(reply.argumentAt(0));
        qDebug() << desktopItemInfo.Icon << desktopItemInfo.thumbnail;
        /*ToDo desktop daemon settings judge*/
        if (desktopItemInfo.thumbnail.length() > 0){
            desktopItemInfo.Icon = desktopItemInfo.thumbnail;
        }
        m_thumbnails.append(desktopItemInfo.URI);
        m_thumbnailTimer->start();
        emit signalManager->itemCreated(desktopItemInfo);
        LOG_INFO() << "asyncCreateDesktopItemByUrlFinished" << desktopItemInfo.thumbnail <<"11111111111";
        updateDesktopItemInfoMap(desktopItemInfo);

        if (isAppGroup(desktopItemInfo.URI)){
            getAppGroupItemsByUrl(desktopItemInfo.URI);
        }

    } else {
        LOG_ERROR() << reply.error().message();
    }
    call->deleteLater();
}


void DBusController::handleFileCreated(const QString &path){
    LOG_INFO() << "handleFileCreated" << path;
    QFileInfo f(path);
    if (isDesktop(f.path())){
        LOG_INFO() << "create file in desktop" << path;
        asyncCreateDesktopItemByUrl(path);
    }else{
        if(isApp(path)){
            LOG_INFO() << "create file in app group" << path;
            getAppGroupItemsByUrl(f.path());
        }else{
            LOG_INFO() << "*******" << "created invalid file(not .desktop file)"<< "*********";
        }
    }
}

void DBusController::handleFileDeleted(const QString &path){
    LOG_INFO() << "handleFileDeleted" << path;
    QFileInfo f(path);
    if (isDesktop(f.path())){
        LOG_INFO() << "delete desktop file:" << path;
        removeDesktopItemInfoByUrl(path);
        emit signalManager->itemDeleted(path);
        if (isAppGroup(path)){
            getAppGroupItemsByUrl(path);
            emit signalManager->appGounpDetailClosed();
        }
    }else if (isAppGroup(path)){
        removeDesktopItemInfoByUrl(path);
        emit signalManager->itemDeleted(path);
        LOG_INFO() << "delete desktop app group folder:" << path;
        getAppGroupItemsByUrl(f.path());
    }
}


void DBusController::handleFileMovedIn(const QString &path){
    LOG_INFO() << "handleFileMovedIn";
    QFileInfo f(path);
    if (isDesktop(f.path())){
        LOG_INFO() << "move file in desktop" << path;
        asyncCreateDesktopItemByUrl(path);
    }else if (isAppGroup(f.path())){
        LOG_INFO() << "move file in App Group" << path;
       getAppGroupItemsByUrl(f.path());
   }
}


void DBusController::handleFileMovedOut(const QString &path){
    LOG_INFO() << "handleFileMovedOut";
    QFileInfo f(path);
    if (isDesktop(f.path())){
        LOG_INFO() << "move file out desktop" << path;
        removeDesktopItemInfoByUrl(path);
        emit signalManager->itemDeleted(path);
    }else if (isAppGroup(f.path())){
         LOG_INFO() << "move file out App Group" << path;
        getAppGroupItemsByUrl(f.path());
    }
}


void DBusController::handleFileRenamed(const QString &oldPath, const QString &newPath){
    QFileInfo oldFileInfo(oldPath);
    QFileInfo newFileInfo(newPath);
    if (isDesktop(oldFileInfo.path()) && isDesktop(newFileInfo.path())){
        LOG_INFO() << "desktop file renamed";
        m_itemShoudBeMoved = oldPath;
        emit signalManager->itemShoudBeMoved(oldPath);
        asyncRenameDesktopItemByUrl(newPath);
    }else if (isAppGroup(oldFileInfo.path()) && isAppGroup(newFileInfo.path())){
        LOG_INFO() << "renamed file move in app group";
    }
}

void DBusController::updateDesktopItemInfoMap(DesktopItemInfo desktopItemInfo){
    m_desktopItemInfoMap.insert(desktopItemInfo.URI, desktopItemInfo);
}


void DBusController::updateDesktopItemInfoMap_moved(DesktopItemInfo desktopItemInfo){
    QString oldKey = m_itemShoudBeMoved;
    QString newKey = desktopItemInfo.URI;

    DesktopItemInfoMap::Iterator iterator = m_desktopItemInfoMap.find(oldKey);
    if (iterator!=m_desktopItemInfoMap.end()){
        m_desktopItemInfoMap.insert(iterator, newKey, desktopItemInfo);
    }
}

void DBusController::removeDesktopItemInfoByUrl(QString url){
    if (m_desktopItemInfoMap.contains(url)){
        m_desktopItemInfoMap.remove(url);
    }
}

void DBusController::openFiles(QStringList files, IntList intFlags){
    LOG_INFO() << files << intFlags;

    foreach (QString file, files) {
        int index = files.indexOf(file);
        if (intFlags.at(index) == 0){ //RequestOpenPolicyOpen = 0
            QString key = QString(QUrl(file.toLocal8Bit()).toEncoded());
            if (m_desktopItemInfoMap.contains(key)){
                DesktopItemInfo desktopItemInfo = m_desktopItemInfoMap.value(key);
                LOG_INFO() << desktopItemInfo.URI << "open";
                QDBusPendingReply<> reply = m_desktopDaemonInterface->ActivateFile(desktopItemInfo.URI, QStringList(), desktopItemInfo.CanExecute, 0);
                reply.waitForFinished();
                if (!reply.isError()){

                }else{
                    LOG_ERROR() << reply.error().message();
                }
            }
        }else{ //RequestOpenPolicyOpen = 1

        }
    }
}

void DBusController::openFiles(DesktopItemInfo destinationDesktopItemInfo, QList<DesktopItemInfo> desktopItemInfos){
    QStringList urls;
    foreach (DesktopItemInfo info, desktopItemInfos){
        urls.append(info.URI);
    }
    openFiles(destinationDesktopItemInfo, urls);
}


void DBusController::openFiles(DesktopItemInfo destinationDesktopItemInfo, QStringList urls){
    QDBusPendingReply<> reply = m_desktopDaemonInterface->ActivateFile(destinationDesktopItemInfo.URI, urls, destinationDesktopItemInfo.CanExecute, 0);
    reply.waitForFinished();
    if (!reply.isError()){

    }else{
        LOG_ERROR() << reply.error().message();
    }
}

void DBusController::openFile(DesktopItemInfo desktopItemInfo){
    //TODO query RequestOpenPolicyOpen or RequestOpenPolicyOpen
    QDBusPendingReply<> reply = m_desktopDaemonInterface->ActivateFile(desktopItemInfo.URI, QStringList(), desktopItemInfo.CanExecute, 0);
    reply.waitForFinished();
    if (!reply.isError()){

    }else{
        LOG_ERROR() << reply.error().message();
    }
    emit signalManager->appGounpDetailClosed();
}


void DBusController::createDirectory(){
    QDBusPendingReply<QString, QDBusObjectPath, QString> reply = m_fileOperationsInterface->NewCreateDirectoryJob(desktopLocation, "", "", "", "");
    reply.waitForFinished();
    if (!reply.isError()){
        QString service = reply.argumentAt(0).toString();
        QString path = qdbus_cast<QDBusObjectPath>(reply.argumentAt(1)).path();
        m_createDirJobInterface = new CreateDirJobInterface(service, path, QDBusConnection::sessionBus(), this);
        connect(m_createDirJobInterface, SIGNAL(Done(QString)), this, SLOT(createDirectoryFinished(QString)));
        m_createDirJobInterface->Execute();
    }else{
        LOG_ERROR() << reply.error().message();
    }
}


void DBusController::createDirectoryFinished(QString dirName){
    Q_UNUSED(dirName)
    disconnect(m_createDirJobInterface, SIGNAL(Done(QString)), this, SLOT(createDirectoryFinished(QString)));
    m_createDirJobInterface = NULL;
}

void DBusController::createFile(){
    QDBusPendingReply<QString, QDBusObjectPath, QString> reply = m_fileOperationsInterface->NewCreateFileJob(desktopLocation, "", "", "", "", "");
    reply.waitForFinished();
    if (!reply.isError()){
        QString service = reply.argumentAt(0).toString();
        QString path = qdbus_cast<QDBusObjectPath>(reply.argumentAt(1)).path();
        m_createFileJobInterface = new CreateFileJobInterface(service, path, QDBusConnection::sessionBus(), this);
        connect(m_createFileJobInterface, SIGNAL(Done(QString)), this, SLOT(createFileFinished(QString)));
        m_createFileJobInterface->Execute();
    }else{
        LOG_ERROR() << reply.error().message();
    }
}

void DBusController::createFileFinished(QString filename){
    Q_UNUSED(filename)
    disconnect(m_createFileJobInterface, SIGNAL(Done(QString)), this, SLOT(createFileFinished(QString)));
    m_createFileJobInterface = NULL;
}


void DBusController::createFileFromTemplate(QString templatefile){
    QDBusPendingReply<QString, QDBusObjectPath, QString> reply = m_fileOperationsInterface->NewCreateFileFromTemplateJob(desktopLocation, templatefile, "", "", "");
    reply.waitForFinished();
    if (!reply.isError()){
        QString service = reply.argumentAt(0).toString();
        QString path = qdbus_cast<QDBusObjectPath>(reply.argumentAt(1)).path();
        m_createFileFromTemplateJobInterface = new CreateFileFromTemplateJobInterface(service, path, QDBusConnection::sessionBus(), this);
        connect(m_createFileFromTemplateJobInterface, SIGNAL(Done(QString)), this, SLOT(createFileFromTemplateFinished(QString)));
        m_createFileFromTemplateJobInterface->Execute();
    }else{
        LOG_ERROR() << reply.error().message();
    }
}


void DBusController::createFileFromTemplateFinished(QString filename){
    Q_UNUSED(filename)
    disconnect(m_createFileFromTemplateJobInterface, SIGNAL(Done(QString)), this, SLOT(createFileFromTemplateFinished(QString)));
    m_createFileFromTemplateJobInterface = NULL;
}


void DBusController::sortByKey(QString key){
    LOG_INFO() << key;
    emit signalManager->sortByKey(key);
}


void DBusController::requestCreatingAppGroup(QStringList urls){
    QDBusPendingReply<> reply = m_desktopDaemonInterface->RequestCreatingAppGroup(urls);
    reply.waitForFinished();
    if (!reply.isError()){

    }else{
        LOG_ERROR() << reply.error().message();
    }
}

void DBusController::createAppGroup(QString group_url, QStringList urls){
//    LOG_INFO() << group_url << urls;
//    if (urls.count() >= 2){
//        emit signalManager->appGounpCreated(group_url);
//    }
}

void DBusController::mergeIntoAppGroup(QStringList urls, QString group_url){
    LOG_INFO() << urls << "merge into" << group_url;
    QDBusPendingReply<> reply = m_desktopDaemonInterface->RequestMergeIntoAppGroup(urls, group_url);
    reply.waitForFinished();
    if (!reply.isError()){
        getAppGroupItemsByUrl(group_url);
    }else{
        LOG_ERROR() << reply.error().message();
    }
}

void DBusController::pasteFiles(QString action, QStringList files, QString destination){
    if (action == "cut"){
        bool isFilesFromDesktop = true;
        foreach (QString fpath, files) {
            QString url = fpath.replace("file://", "");
            QFileInfo f(url);
            bool flag = (f.absolutePath() == destination);
            isFilesFromDesktop = isFilesFromDesktop && flag;
        }
        if (isFilesFromDesktop){
            emit signalManager->cancelFilesCuted(files);
        }else{
            emit signalManager->moveFilesExcuted(files, destination);
        }
        qApp->clipboard()->clear();
    }else if (action == "copy"){
        emit signalManager->copyFilesExcuted(files, destination);
    }
}


DBusController::~DBusController()
{

}
