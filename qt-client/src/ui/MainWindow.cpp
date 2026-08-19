#include "MainWindow.h"
#include "widgets/ChatInputBar.h"
#include "widgets/ChatBubble.h"
#include "widgets/ChatMessageDelegate.h"
#include "widgets/InfoPanel.h"
#include "widgets/FunctionBar.h"
#include "dialogs/AddFriendDialog.h"
#include "dialogs/FriendRequestDialog.h"
#include "dialogs/CreateGroupDialog.h"
#include "dialogs/JoinGroupDialog.h"
#include "dialogs/EditProfileDialog.h"
#include <QMenuBar>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QInputDialog>
#include <QFileDialog>
#include <QScrollBar>
#include <QRandomGenerator>
#include <QDialog>
#include <QListWidget>
#include <QMap>
#include <QPushButton>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupConnections();
}

void MainWindow::setupUi() {
    setWindowTitle("ChatRoom");
    setMinimumSize(960, 640);
    resize(1100, 720);

    // menu bar
    auto *menuBar = this->menuBar();
    Q_UNUSED(menuBar)

    // central widget
    auto *central = new QWidget();
    setCentralWidget(central);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);

    main_splitter_ = new QSplitter(Qt::Horizontal);
    main_splitter_->setHandleWidth(1);

    // ===== left panel =====
    contact_model_ = new ContactListModel(this);
    contact_list_ = new QListView();
    contact_list_->setModel(contact_model_);
    contact_list_->setMinimumWidth(180);
    contact_list_->setStyleSheet(
        "QListView { background-color: #e6e6e6; border: none; }"
        "QListView::item { padding: 10px; border-bottom: 1px solid #d4d4d4; }"
        "QListView::item:selected { background-color: #c7c7c7; }");
    main_splitter_->addWidget(contact_list_);

    // ===== center panel =====
    auto *centerWidget = new QWidget();
    auto *centerLayout = new QVBoxLayout(centerWidget);
    centerLayout->setContentsMargins(0, 0, 0, 0);

    chat_title_ = new QLabel("选择一个联系人开始聊天");
    chat_title_->setStyleSheet(
        "font-size: 16px; font-weight: bold; padding: 10px; "
        "background-color: #f0f0f0; border-bottom: 1px solid #ddd;");
    chat_title_->setAlignment(Qt::AlignCenter);
    centerLayout->addWidget(chat_title_);

    message_model_ = new ChatMessageModel(this);
    message_list_ = new QListView();
    message_list_->setModel(message_model_);
    message_list_->setSelectionMode(QAbstractItemView::NoSelection);
    message_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    message_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    message_list_->setStyleSheet(
        "QListView { background-color: #f5f5f5; border: none; }");
    message_list_->setItemDelegate(new ChatMessageDelegate(message_list_));
    centerLayout->addWidget(message_list_, 1);

    input_bar_ = new ChatInputBar();
    centerLayout->addWidget(input_bar_);

    main_splitter_->addWidget(centerWidget);

    // ===== right panel: 联系人信息 + 功能按钮（始终可见）=====
    right_panel_ = new QWidget();
    right_panel_->setMinimumWidth(180);
    right_panel_->setMaximumWidth(250);
    auto *rightLayout = new QVBoxLayout(right_panel_);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    info_panel_ = new InfoPanel();
    rightLayout->addWidget(info_panel_);

    function_bar_ = new FunctionBar();
    rightLayout->addWidget(function_bar_, 1);

    main_splitter_->addWidget(right_panel_);

    // set splitter proportions: 20:55:25
    main_splitter_->setStretchFactor(0, 2);
    main_splitter_->setStretchFactor(1, 5);
    main_splitter_->setStretchFactor(2, 2);

    centralLayout->addWidget(main_splitter_);
}

void MainWindow::setupConnections() {
    auto *state = ClientState::instance();

    connect(contact_list_, &QListView::clicked, this, &MainWindow::onContactClicked);

    connect(input_bar_, &ChatInputBar::sendClicked, this, &MainWindow::onSendMessage);
    connect(input_bar_, &ChatInputBar::fileUploadClicked, this, &MainWindow::onFileUpload);

    // 微信式上滑加载历史记录
    connect(message_list_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
        if (value <= 0 && !history_loading_) onHistoryLoadRequested();
    });

    connect(state, &ClientState::contactsUpdated, this, &MainWindow::onContactsUpdated);
    connect(state, &ClientState::messagesUpdated, this, &MainWindow::onMessagesUpdated);
    connect(state, &ClientState::currentChatChanged, this, &MainWindow::onChatChanged);
    connect(state, &ClientState::systemNotification, this, &MainWindow::onSystemNotification);
    connect(state, &ClientState::incomingMessage, this, &MainWindow::onIncomingMessage);
    connect(state, &ClientState::groupMembersReceived, this, &MainWindow::onGroupMembersReceived);
    connect(state, &ClientState::blockedUsersReceived, this, &MainWindow::onBlockedUsersReceived);
    connect(state, &ClientState::fileTransferNotify, this, &MainWindow::onFileTransferNotify);

    // function bar
    connect(function_bar_, &FunctionBar::addFriendClicked, this, &MainWindow::onAddFriend);
    connect(function_bar_, &FunctionBar::deleteFriendClicked, this, &MainWindow::onDeleteFriend);
    connect(function_bar_, &FunctionBar::blockFriendClicked, this, &MainWindow::onBlockFriend);
    connect(function_bar_, &FunctionBar::unblockFriendClicked, this, &MainWindow::onUnblockFriend);
    connect(function_bar_, &FunctionBar::queryBlockedClicked, this, &MainWindow::onQueryBlocked);
    connect(function_bar_, &FunctionBar::friendRequestsClicked, this, &MainWindow::onFriendRequests);
    connect(function_bar_, &FunctionBar::createGroupClicked, this, &MainWindow::onCreateGroup);
    connect(function_bar_, &FunctionBar::joinGroupClicked, this, &MainWindow::onJoinGroup);
    connect(function_bar_, &FunctionBar::groupInvitesClicked, this, &MainWindow::onGroupInvites);
    connect(function_bar_, &FunctionBar::editProfileClicked, this, &MainWindow::onEditProfile);
    connect(function_bar_, &FunctionBar::deleteAccountClicked, this, &MainWindow::onDeleteAccount);
    connect(function_bar_, &FunctionBar::quitGroupClicked, this, &MainWindow::onQuitGroup);
    connect(function_bar_, &FunctionBar::viewMembersClicked, this, &MainWindow::onViewMembers);
    connect(function_bar_, &FunctionBar::dismissGroupClicked, this, &MainWindow::onDismissGroup);
    connect(function_bar_, &FunctionBar::approveJoinClicked, this, &MainWindow::onApproveJoin);
    connect(function_bar_, &FunctionBar::removeMemberClicked, this, &MainWindow::onRemoveMember);
    connect(function_bar_, &FunctionBar::addAdminClicked, this, &MainWindow::onAddAdmin);
    connect(function_bar_, &FunctionBar::removeAdminClicked, this, &MainWindow::onRemoveAdmin);
}

void MainWindow::onLoginSuccess() {
    // 右下角显示本人 ID 与昵称
    auto *state = ClientState::instance();
    if (!my_id_label_) {
        my_id_label_ = new QLabel();
        my_id_label_->setStyleSheet("font-size: 11px; color: #888;");
        statusBar()->addPermanentWidget(my_id_label_);
    }
    my_id_label_->setText(QString("我的ID: %1 | %2")
        .arg(state->userId())
        .arg(state->nickname().isEmpty() ? state->username() : state->nickname()));
    show();
}

// ===== left panel =====

void MainWindow::onContactClicked(const QModelIndex &index) {
    auto contacts = ClientState::instance()->contacts();
    if (index.row() < 0 || index.row() >= contacts.size()) return;

    const auto &c = contacts[index.row()];
    history_limit_ = 50;
    history_loading_ = false;
    if (c.type == ContactItem::Friend) {
        ClientState::instance()->setCurrentChat(ChatType::Private, c.id);
        chat_title_->setText(c.name + (c.is_online ? " [在线]" : " [离线]"));
        function_bar_->setGroupMode(false, "");
        updateRightPanel();
    } else {
        ClientState::instance()->setCurrentChat(ChatType::Group, 0, c.group_id);
        chat_title_->setText(c.name + " (群组)");
        function_bar_->setGroupMode(true, c.role);
        updateRightPanel();
    }
}

void MainWindow::onContactsUpdated() {
    auto *state = ClientState::instance();
    contact_model_->setContacts(state->contacts());
    if (state->currentChatType() == ChatType::Private) {
        const auto &c = state->contactById(state->currentTargetId(), ContactItem::Friend);
        if (c.id != 0)
            chat_title_->setText(c.name + (c.is_online ? " [在线]" : " [离线]"));
    }
}

void MainWindow::saveScrollState() {
    auto *bar = message_list_->verticalScrollBar();
    at_bottom_ = (bar->value() + bar->pageStep() >= bar->maximum() - 20);
    top_visible_index_ = message_list_->indexAt(QPoint(2, 2));
}

void MainWindow::restoreScrollState() {
    if (at_bottom_) {
        message_list_->scrollToBottom();
    } else if (top_visible_index_.isValid()) {
        message_list_->scrollTo(top_visible_index_, QAbstractItemView::PositionAtTop);
    }
}

void MainWindow::onMessagesUpdated() {
    auto *state = ClientState::instance();
    const auto &msgs = state->currentChatMessages();

    saveScrollState();

    // 增量追加，避免全量 reset 造成滚动抖动，保证流畅体验
    if (msgs.size() >= message_model_->rowCount()) {
        for (int i = message_model_->rowCount(); i < msgs.size(); ++i)
            message_model_->appendMessage(msgs[i]);
    } else {
        message_model_->setMessages(msgs);
    }

    history_loading_ = false;
    restoreScrollState();
}

void MainWindow::onChatChanged() {
    onMessagesUpdated();
}

void MainWindow::onSystemNotification(const QString &text) {
    // show in status bar or as subtle notification
    statusBar()->showMessage(text, 5000);

    if (text.contains("上线了") || text.contains("下线了") || text.contains("离线")) {
        ClientState::instance()->queryFriends();
    }
}

void MainWindow::onIncomingMessage(uint64_t fromId, const QString &senderName, const QString &content) {
    auto *state = ClientState::instance();
    if (state->currentChatType() == ChatType::Private &&
        state->currentTargetId() != fromId) {
        statusBar()->showMessage(senderName + ": " + content.left(50), 3000);
    }
}

void MainWindow::updateRightPanel() {
    auto *state = ClientState::instance();
    auto contacts = state->contacts();
    for (const auto &c : contacts) {
        if (c.type == ContactItem::Friend && c.id == state->currentTargetId()) {
            info_panel_->showContactInfo(c);
            function_bar_->setGroupMode(false, "");
            return;
        }
        if (c.type == ContactItem::Group && c.group_id == state->currentGroupId()) {
            info_panel_->showContactInfo(c);
            function_bar_->setGroupMode(true, c.role);
            return;
        }
    }
}

// ===== chat =====

void MainWindow::onSendMessage(const QString &text) {
    auto *state = ClientState::instance();
    if (state->currentChatType() == ChatType::Private) {
        state->sendPrivateChat(state->currentTargetId(), text);
    } else {
        state->sendGroupChat(state->currentGroupId(), text);
    }
}

void MainWindow::onFileUpload() {
    auto *state = ClientState::instance();
    if (state->currentChatType() != ChatType::Private) {
        QMessageBox::information(this, "提示", "文件发送仅支持私聊");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(this, "选择文件");
    if (filePath.isEmpty()) return;

    state->sendFileRequest(state->currentTargetId(), filePath);
}

void MainWindow::onHistoryLoadRequested() {
    auto *state = ClientState::instance();
    if (message_model_->rowCount() == 0) return;

    history_loading_ = true;
    history_limit_ += 30;
    if (state->currentChatType() == ChatType::Private) {
        state->getHistory(state->currentTargetId(), 0, history_limit_);
    } else {
        state->getHistory(0, state->currentGroupId(), history_limit_);
    }
}

void MainWindow::onFileTransferNotify(uint64_t transferId, uint64_t senderId,
                                      const QString &senderName, const QString &fileName,
                                      uint64_t fileSize) {
    Q_UNUSED(senderId)
    QString sizeText;
    if (fileSize >= 1024 * 1024)
        sizeText = QString::number(fileSize / 1024.0 / 1024.0, 'f', 1) + " MB";
    else
        sizeText = QString::number(fileSize / 1024.0, 'f', 1) + " KB";

    auto ret = QMessageBox::question(this, "文件传输",
        QString("%1 向你发送文件:\n%2 (%3)\n\n是否接收？")
            .arg(senderName, fileName, sizeText));
    if (ret != QMessageBox::Yes) {
        ClientState::instance()->acceptFileTransfer(transferId, false);
        return;
    }

    ClientState::instance()->acceptFileTransfer(transferId, true);
    QString dir = QFileDialog::getExistingDirectory(this, "选择保存目录");
    if (dir.isEmpty()) return;
    ClientState::instance()->receiveFileChunks(transferId, dir);
}

// ===== function bar actions =====

void MainWindow::onAddFriend() {
    auto *dlg = new AddFriendDialog(this);
    if (dlg->exec() == QDialog::Accepted) {
        uint64_t uid = dlg->userId();
        QString email = dlg->email();
        if (uid == 0 && email.isEmpty()) {
            QMessageBox::warning(this, "提示", "请填写用户ID或邮箱");
            return;
        }
        auto *state = ClientState::instance();
        auto *conn = new QMetaObject::Connection();
        *conn = connect(state, &ClientState::operationResult, this,
            [this, conn, state](bool ok, const QString &err) {
                disconnect(*conn);
                delete conn;
                if (ok) {
                    state->queryFriends();  // 刷新好友列表
                    QMessageBox::information(this, "提示", "添加好友成功");
                } else {
                    QMessageBox::warning(this, "提示", "添加好友失败: " + err);
                }
            });
        state->addFriend(uid, email);
    }
    dlg->deleteLater();
}

void MainWindow::onDeleteFriend() {
    auto *state = ClientState::instance();
    if (state->currentChatType() != ChatType::Private) return;

    auto ret = QMessageBox::question(this, "确认", "确定要删除该好友吗？将同时删除聊天记录。");
    if (ret == QMessageBox::Yes) {
        state->deleteFriend(state->currentTargetId());
    }
}

void MainWindow::onBlockFriend() {
    auto *state = ClientState::instance();
    if (state->currentChatType() != ChatType::Private) {
        QMessageBox::information(this, "提示", "请先选择一个好友聊天，再执行拉黑操作");
        return;
    }

    auto ret = QMessageBox::question(this, "确认", "确定要拉黑该好友吗？");
    if (ret == QMessageBox::Yes) {
        state->blockFriend(state->currentTargetId());
    }
}

void MainWindow::onUnblockFriend() {
    bool ok;
    uint64_t uid = QInputDialog::getText(this, "解除拉黑",
        "输入要解除拉黑的好友ID:").toULongLong(&ok);
    if (!ok || uid == 0) return;

    auto *state = ClientState::instance();
    auto *conn = new QMetaObject::Connection();
    *conn = connect(state, &ClientState::operationResult, this,
        [this, conn, state](bool success, const QString &err) {
            disconnect(*conn);
            delete conn;
            if (success) {
                state->queryFriends();  // 刷新好友列表
                QMessageBox::information(this, "提示", "已解除拉黑");
            } else {
                QMessageBox::warning(this, "提示", "解除拉黑失败: " + err);
            }
        });
    state->unblockFriend(uid);
}

void MainWindow::onQueryBlocked() {
    ClientState::instance()->queryBlockedUsers();
}

void MainWindow::onBlockedUsersReceived(const QVector<ContactItem> &users) {
    if (users.isEmpty()) {
        QMessageBox::information(this, "黑名单", "当前黑名单为空");
        return;
    }

    auto *dlg = new QDialog(this);
    dlg->setWindowTitle("黑名单列表");
    dlg->setMinimumWidth(320);

    auto *layout = new QVBoxLayout(dlg);
    auto *list = new QListWidget(dlg);
    QMap<QString, uint64_t> idByText;
    for (const auto &u : users) {
        QString text = QString("%1 (ID: %2)%3")
            .arg(u.name).arg(u.id).arg(u.is_online ? " [在线]" : " [离线]");
        idByText.insert(text, u.id);
        list->addItem(text);
    }
    layout->addWidget(list);

    auto *unblockBtn = new QPushButton("解除选中好友的拉黑", dlg);
    layout->addWidget(unblockBtn);

    connect(unblockBtn, &QPushButton::clicked, this, [this, dlg, list, idByText]() {
        auto *item = list->currentItem();
        if (!item) {
            QMessageBox::information(dlg, "提示", "请先选择一个好友");
            return;
        }
        uint64_t uid = idByText.value(item->text());
        auto *state = ClientState::instance();
        auto *conn = new QMetaObject::Connection();
        *conn = connect(state, &ClientState::operationResult, this,
            [this, dlg, conn, state](bool success, const QString &err) {
                disconnect(*conn);
                delete conn;
                if (success) {
                    state->queryFriends();
                    dlg->accept();
                    QMessageBox::information(this, "提示", "已解除拉黑");
                } else {
                    QMessageBox::warning(this, "提示", "解除拉黑失败: " + err);
                }
            });
        state->unblockFriend(uid);
    });

    dlg->exec();
    dlg->deleteLater();
}

void MainWindow::onFriendRequests() {
    auto *dlg = new FriendRequestDialog(this);
    // TODO: server needs to support fetching pending requests
    // For now show empty dialog, populate from system notifications
    connect(dlg, &FriendRequestDialog::approved, this, [](uint64_t userId) {
        ClientState::instance()->addFriend(userId);
    });
    dlg->exec();
    dlg->deleteLater();
}

void MainWindow::onCreateGroup() {
    auto *dlg = new CreateGroupDialog(this);
    if (dlg->exec() == QDialog::Accepted) {
        ClientState::instance()->createGroup(
            dlg->groupName(), dlg->description(), dlg->isPublic());
    }
    dlg->deleteLater();
}

void MainWindow::onJoinGroup() {
    auto *dlg = new JoinGroupDialog(this);
    if (dlg->exec() == QDialog::Accepted && dlg->groupId() > 0) {
        ClientState::instance()->joinGroup(dlg->groupId());
    }
    dlg->deleteLater();
}

void MainWindow::onGroupInvites() {
    auto *dlg = new JoinGroupDialog(this);
    // TODO: load invites from server state
    connect(dlg, &JoinGroupDialog::acceptInvite, this, [](uint64_t groupId) {
        ClientState::instance()->joinGroup(groupId);
    });
    dlg->exec();
    dlg->deleteLater();
}

void MainWindow::onEditProfile() {
    auto *dlg = new EditProfileDialog(this);
    if (dlg->exec() == QDialog::Accepted) {
        QString newNick = dlg->newNickname();
        if (!newNick.isEmpty()) {
            auto *state = ClientState::instance();
            auto *conn = new QMetaObject::Connection();
            *conn = connect(state, &ClientState::operationResult, this,
                [this, conn](bool ok, const QString &err) {
                    disconnect(*conn);
                    delete conn;
                    if (ok) QMessageBox::information(this, "提示", "昵称修改成功");
                    else QMessageBox::warning(this, "提示", "昵称修改失败: " + err);
                });
            state->updateNickname(newNick);
        }
    }
    dlg->deleteLater();
}

void MainWindow::onDeleteAccount() {
    bool ok;
    QString password = QInputDialog::getText(this, "注销账号",
        "请输入密码确认注销:", QLineEdit::Password, "", &ok);
    if (ok && !password.isEmpty()) {
        ClientState::instance()->deleteAccount(password);
    }
}

void MainWindow::onQuitGroup() {
    auto *state = ClientState::instance();
    state->quitGroup(state->currentGroupId());
}

void MainWindow::onViewMembers() {
    auto *state = ClientState::instance();
    state->queryGroupMembers(state->currentGroupId());
}

void MainWindow::onGroupMembersReceived(uint64_t groupId, const QStringList &members) {
    Q_UNUSED(groupId)
    info_panel_->showGroupMembers(members);
}

void MainWindow::onDismissGroup() {
    auto ret = QMessageBox::question(this, "确认", "确定要解散该群组吗？所有聊天记录将被删除。");
    if (ret == QMessageBox::Yes) {
        ClientState::instance()->dismissGroup(ClientState::instance()->currentGroupId());
    }
}

void MainWindow::onApproveJoin() {
    bool ok;
    uint64_t uid = QInputDialog::getText(this, "批准加入", "输入用户ID:").toULongLong(&ok);
    if (ok && uid > 0) {
        ClientState::instance()->approveJoinGroup(
            ClientState::instance()->currentGroupId(), uid);
    }
}

void MainWindow::onRemoveMember() {
    bool ok;
    uint64_t uid = QInputDialog::getText(this, "移除成员", "输入用户ID:").toULongLong(&ok);
    if (ok && uid > 0) {
        ClientState::instance()->removeGroupMember(
            ClientState::instance()->currentGroupId(), uid);
    }
}

void MainWindow::onAddAdmin() {
    bool ok;
    uint64_t uid = QInputDialog::getText(this, "添加管理员", "输入用户ID:").toULongLong(&ok);
    if (ok && uid > 0) {
        ClientState::instance()->addGroupAdmin(
            ClientState::instance()->currentGroupId(), uid);
    }
}

void MainWindow::onRemoveAdmin() {
    bool ok;
    uint64_t uid = QInputDialog::getText(this, "移除管理员", "输入用户ID:").toULongLong(&ok);
    if (ok && uid > 0) {
        ClientState::instance()->removeGroupAdmin(
            ClientState::instance()->currentGroupId(), uid);
    }
}