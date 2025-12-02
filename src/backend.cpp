#include "backend.h"

#include "../net/tcp_server.h"
#include "../steam/steam_networking_manager.h"
#include "../steam/steam_room_manager.h"
#include "../steam/steam_utils.h"

#include <QCoreApplication>
#include <QQmlEngine>
#include <QtDebug>
#include <QVariantMap>
#include <algorithm>
#include <mutex>

Backend::Backend(QObject *parent)
    : QObject(parent), steamReady_(false), localPort_(25565), lastTcpClients_(0)
{
    // Set a default app id so Steam can bootstrap in development environments
    qputenv("SteamAppId", QByteArray("480"));
    qputenv("SteamGameId", QByteArray("480"));

    steamReady_ = SteamAPI_Init();
    if (!steamReady_)
    {
        status_ = tr("无法初始化 Steam API，请确认客户端已登录。");
        return;
    }

    steamManager_ = std::make_unique<SteamNetworkingManager>();
    if (!steamManager_->initialize())
    {
        status_ = tr("Steam 网络初始化失败。");
        steamReady_ = false;
        return;
    }

    roomManager_ = std::make_unique<SteamRoomManager>(steamManager_.get());

    workGuard_ = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(boost::asio::make_work_guard(ioContext_));
    ioThread_ = std::thread([this]() { ioContext_.run(); });

    steamManager_->setMessageHandlerDependencies(ioContext_, server_, localPort_);
    steamManager_->startMessageHandler();

    connect(&callbackTimer_, &QTimer::timeout, this, &Backend::tick);
    callbackTimer_.start(16);

    connect(&slowTimer_, &QTimer::timeout, this, &Backend::refreshFriends);
    slowTimer_.start(5000);

    refreshFriends();
    updateMembersList();
    updateStatus();
}

Backend::~Backend()
{
    callbackTimer_.stop();
    slowTimer_.stop();

    if (steamManager_)
    {
        steamManager_->stopMessageHandler();
    }

    if (server_)
    {
        server_->stop();
        server_.reset();
    }

    if (workGuard_)
    {
        workGuard_->reset();
    }

    ioContext_.stop();
    if (ioThread_.joinable())
    {
        ioThread_.join();
    }

    // Let the Steam manager handle shutdown/SteamAPI cleanup in its destructor
    steamManager_.reset();
    roomManager_.reset();
}

bool Backend::isHost() const
{
    return steamReady_ && steamManager_ && steamManager_->isHost();
}

bool Backend::isConnected() const
{
    return steamReady_ && steamManager_ && steamManager_->isConnected();
}

QString Backend::lobbyId() const
{
    if (!roomManager_)
    {
        return {};
    }
    CSteamID lobby = roomManager_->getCurrentLobby();
    return lobby.IsValid() ? QString::number(lobby.ConvertToUint64()) : QString();
}

int Backend::tcpClients() const
{
    return server_ ? server_->getClientCount() : 0;
}

void Backend::setJoinTarget(const QString &id)
{
    if (joinTarget_ == id)
    {
        return;
    }
    joinTarget_ = id;
    emit joinTargetChanged();
}

void Backend::setLocalPort(int port)
{
    port = std::max(0, port);
    if (localPort_ == port)
    {
        return;
    }
    localPort_ = port;
    emit localPortChanged();
}

bool Backend::ensureSteamReady(const QString &actionLabel)
{
    if (steamReady_)
    {
        return true;
    }
    emit errorMessage(tr("无法%1：Steam 未初始化。").arg(actionLabel));
    return false;
}

void Backend::startHosting()
{
    if (!ensureSteamReady(tr("主持房间")))
    {
        return;
    }
    if (isHost())
    {
        emit errorMessage(tr("已经在主持房间中。"));
        return;
    }
    if (isConnected())
    {
        emit errorMessage(tr("当前已连接到房间，请先断开。"));
        return;
    }

    if (roomManager_ && roomManager_->startHosting())
    {
        steamManager_->setHostSteamID(SteamUser()->GetSteamID());
        updateStatus();
    }
    else
    {
        emit errorMessage(tr("创建房间失败，请检查 Steam 状态。"));
    }
}

void Backend::ensureServerRunning()
{
    if (server_)
    {
        return;
    }
    server_ = std::make_unique<TCPServer>(8888, steamManager_.get());
    if (!server_->start())
    {
        emit errorMessage(tr("启动本地 TCP 服务器失败。"));
        server_.reset();
    }
    emit serverChanged();
}

void Backend::joinHost()
{
    if (!ensureSteamReady(tr("加入房间")))
    {
        return;
    }
    if (isConnected())
    {
        emit errorMessage(tr("已经连接到房间，请先断开。"));
        return;
    }
    const QString trimmedTarget = joinTarget_.trimmed();
    if (trimmedTarget.isEmpty())
    {
        startHosting();
        return;
    }
    bool ok = false;
    uint64 hostID = trimmedTarget.toULongLong(&ok);
    if (!ok)
    {
        emit errorMessage(tr("请输入有效的房间/Steam ID。"));
        return;
    }

    if (steamManager_->joinHost(hostID))
    {
        ensureServerRunning();
        updateStatus();
    }
    else
    {
        emit errorMessage(tr("无法连接到房主。"));
    }
}

void Backend::disconnect()
{
    if (roomManager_)
    {
        roomManager_->leaveLobby();
    }
    if (steamManager_)
    {
        steamManager_->disconnect();
    }
    if (server_)
    {
        server_->stop();
        server_.reset();
        emit serverChanged();
    }
    updateMembersList();
    updateStatus();
}

void Backend::refreshFriends()
{
    if (!steamReady_ || !steamManager_)
    {
        qDebug() << "[Friends] skip refresh - steam not ready";
        return;
    }
    QVariantList updated;
    std::vector<FriendsModel::Entry> modelData;
    int idx = 0;
    for (const auto &friendInfo : SteamUtils::getFriendsList())
    {
        const QString steamId = QString::number(friendInfo.id.ConvertToUint64());
        const QString displayName = QString::fromStdString(friendInfo.name);
        const QString avatar = QString::fromStdString(friendInfo.avatarDataUrl);

        QVariantMap entry;
        entry.insert(QStringLiteral("id"), steamId);
        entry.insert(QStringLiteral("name"), displayName);
        if (!avatar.isEmpty())
        {
            entry.insert(QStringLiteral("avatar"), avatar);
        }
        updated.push_back(entry);
        modelData.push_back({steamId, displayName, avatar});
        if (idx < 3)
        {
            qDebug() << "[Friends] sample" << idx << entry;
        }
        ++idx;
    }
    friendsModel_.setFriends(std::move(modelData));
    if (updated != friends_)
    {
        friends_ = updated;
        emit friendsChanged();
    }
    qDebug() << "[Friends] fetched" << updated.size() << "entries";
}

void Backend::setFriendFilter(const QString &text)
{
    if (friendFilter_ == text)
    {
        return;
    }
    friendFilter_ = text;
    friendsModel_.setFilter(friendFilter_);
    emit friendFilterChanged();
}

void Backend::refreshMembers()
{
    updateMembersList();
}

void Backend::inviteFriend(const QString &steamId)
{
    if (!ensureSteamReady(tr("邀请好友")))
    {
        return;
    }
    bool ok = false;
    uint64 friendId = steamId.toULongLong(&ok);
    if (!ok)
    {
        emit errorMessage(tr("无效的好友 ID。"));
        return;
    }
    if (roomManager_ && roomManager_->getCurrentLobby().IsValid())
    {
        SteamMatchmaking()->InviteUserToLobby(roomManager_->getCurrentLobby(), CSteamID(friendId));
    }
    else
    {
        emit errorMessage(tr("当前未在房间中，无法邀请。"));
    }
}

void Backend::tick()
{
    if (!steamReady_ || !steamManager_)
    {
        return;
    }

    SteamAPI_RunCallbacks();
    steamManager_->update();

    updateMembersList();
    updateStatus();
}

void Backend::updateStatus()
{
    QString next;
    if (!steamReady_)
    {
        next = tr("Steam 未就绪。");
    }
    else if (isHost())
    {
        const QString id = lobbyId();
        next = id.isEmpty() ? tr("主持房间中…") : tr("主持房间 %1").arg(id);
    }
    else if (isConnected())
    {
        const QString id = lobbyId();
        next = id.isEmpty() ? tr("已连接到房间") : tr("已连接到房主 %1").arg(id);
    }
    else
    {
        next = tr("空闲，等待创建或加入房间。");
    }

    const int clientCount = tcpClients();
    if (server_)
    {
        next += tr(" · 本地 TCP 客户端 %1").arg(clientCount);
    }

    if (clientCount != lastTcpClients_)
    {
        lastTcpClients_ = clientCount;
        emit serverChanged();
    }

    if (next != status_)
    {
        status_ = next;
        emit stateChanged();
    }
}

void Backend::updateMembersList()
{
    if (!steamReady_ || !steamManager_ || !roomManager_)
    {
        if (!members_.isEmpty())
        {
            members_.clear();
            emit membersChanged();
        }
        return;
    }

    QVariantList updated;
    if ((isHost() || isConnected()) && roomManager_->getCurrentLobby().IsValid())
    {
        std::vector<CSteamID> lobbyMembers = roomManager_->getLobbyMembers();
        CSteamID myId = SteamUser()->GetSteamID();
        CSteamID hostId = steamManager_->getHostSteamID();

        for (const auto &memberId : lobbyMembers)
        {
            QVariantMap entry;
            entry.insert(QStringLiteral("id"), QString::number(memberId.ConvertToUint64()));
            entry.insert(QStringLiteral("name"), QString::fromUtf8(SteamFriends()->GetFriendPersonaName(memberId)));

            if (memberId == myId)
            {
                entry.insert(QStringLiteral("ping"), QVariant());
                entry.insert(QStringLiteral("relay"), tr("本机"));
            }
            else
            {
                int ping = 0;
                std::string relayInfo = "-";

                std::lock_guard<std::mutex> lock(steamManager_->getConnectionsMutex());
                if (isHost())
                {
                    for (const auto &conn : steamManager_->getConnections())
                    {
                        SteamNetConnectionInfo_t info;
                        if (steamManager_->getInterface()->GetConnectionInfo(conn, &info) &&
                            info.m_identityRemote.GetSteamID() == memberId)
                        {
                            ping = steamManager_->getConnectionPing(conn);
                            relayInfo = steamManager_->getConnectionRelayInfo(conn);
                            break;
                        }
                    }
                }
                else if (memberId == hostId)
                {
                    ping = steamManager_->getHostPing();
                    if (steamManager_->getConnection() != k_HSteamNetConnection_Invalid)
                    {
                        relayInfo = steamManager_->getConnectionRelayInfo(steamManager_->getConnection());
                    }
                }

                entry.insert(QStringLiteral("ping"), ping > 0 ? ping : QVariant());
                entry.insert(QStringLiteral("relay"), QString::fromStdString(relayInfo));
            }

            updated.push_back(entry);
        }
    }

    if (updated != members_)
    {
        members_ = updated;
        emit membersChanged();
    }
}
